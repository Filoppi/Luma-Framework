#define GAME_NIOH 1

// The game seemengly tries to create a DX9 device on boot
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

// The game seems to occasionally cache what last shader was set and restore it later, which can cause unexpected behaviours with Luma unless we protect against this with it
#define ENABLE_GAME_PIPELINE_STATE_READBACK 1

#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList shader_hashes_Copy;
   ShaderHashesList shader_hashes_SwapchainCopy;
   ShaderHashesList shader_hashes_PostProcessEncode;
   ShaderHashesList shader_hashes_Sky;
   ShaderHashesList shader_hashes_BeginMaterialsDrawing;
   ShaderHashesList shader_hashes_EndMaterialsDrawing;

   bool first_frame_draw_call = true;
   bool playing_video = false;
   bool has_drawn_post_process = false;
   int final_post_process_copy_draws = 0;
   constexpr size_t max_final_post_process_copy_draws = 3; // Probably wouldn't need more than 2 ever

   com_ptr<ID3D11DepthStencilView> main_dsv;
   com_ptr<ID3D11RenderTargetView> post_process_rtvs[max_final_post_process_copy_draws];
   com_ptr<ID3D11RenderTargetView> upgraded_post_process_rtvs[max_final_post_process_copy_draws];
   com_ptr<ID3D11ShaderResourceView> upgraded_post_process_srvs[max_final_post_process_copy_draws];
   com_ptr<ID3D11Texture2D> upgraded_post_process_textures_2d[max_final_post_process_copy_draws];

   bool upgrade_materials_samplers = true;
}

class Nioh final : public Game
{
public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"IMPROVED_TONEMAPPING_TYPE", '1', true, false, "Enable modern tonemapping code combinations, some have more pop, other looks more realistic.", 3},
         {"IMPROVED_COLOR_GRADING_TYPE", '0', true, false, "Improves the original grading in multiple ways, though it usually does next to nothing while being expensive.", 3},
         {"IMPROVED_BLOOM", '1', true, false, "Reduces the overly strong bloom effect.", 1},
         {"ENABLE_HDR_COLOR_GRADING", '1', true, false, "Enables the color grading that happens early in post processing, in HDR space (it's not advised to change this).", 1},
         {"ENABLE_SDR_COLOR_GRADING", '1', true, false, "Enables the color grading that happens after the vanilla SDR tonemapping, which mostly increased contrast.", 1},
         {"ENABLE_HDR_BOOST", '1', true, false, "Enable a \"Fake\" HDR boosting effect (applies to videos too).", 1},
         {"ENABLE_VIGNETTE", '1', true, false, "Allows disabling the game's strong vignette (+dither) effect.", 1},
         {"ENABLE_FXAA", '1', true, false, "The game lacked of Anti Aliasing, Luma adds FXAA.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      // Game was encoding with sRGB on both PC and PS4/PS5, but looks best decoded with gamma 2.2 (it's obvious by looking at the image, but also implied by it being a console first game).
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');

      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // Fix videos playback being stretched in UW. Also inform the shader that a video is playing, so we can do an HDR extrapolation effect in it.
      if ((stages & reshade::api::shader_stage::pixel) != 0 && first_frame_draw_call && test_index != 14)
      {
         first_frame_draw_call = false;

         // 2D fullscreen videos are always played as draw call (after their texture is copied 
         if (original_shader_hashes.Contains(shader_hashes_Copy))
         {
            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);
            uint4 size;
            DXGI_FORMAT format;
            GetResourceInfo(srv.get(), size, format);
            if (size.x == 1920 && size.y == 1080 && (format == DXGI_FORMAT_B8G8R8X8_UNORM || format == DXGI_FORMAT_B8G8R8X8_TYPELESS)) // All videos are of that resolution and format (so it seems) (except maybe the boot ones, which already play in 16:9 and we don't want an HDR boost on them anyway)
            {
               playing_video = true;

               if (is_custom_pass)
               {
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 1); // Set custom data to 1 to highlight it's a video.
                  updated_cbuffers = true;
               }
            }
         }
      }

      // Exclusively apply samplers upgrades during materials drawing.
      // The game did not use Anisotropic sampling on most materials, and instead used "Linear" sampling, we use "force_upgrade_linear_samplers" to remedy.
      // 
      // Usually samplers in materials would be called like this and have these indexes in the pixel shaders:
      // __smpsStage0 0
      // __smpsSpMap  1
      // __smpsOcc    2
      // __smpsNMap   3
      // __smpsLit    4
      // However, materials vary and the order can change between them.
      // Given that there's seemingly not much that should avoid doing Anisotropic filtering during materials drawing,
      // we upgrade all of them to it. It's possible that this breaks some material effects but I doubt it,
      // given the game materials are simple and have minimal effects, especially in the g-buffer drawing phase.
      // Note that this can increase shimmering (game has no AA by default nor TAA (Luma adds FXAA))
      if (original_shader_hashes.Contains(shader_hashes_BeginMaterialsDrawing) && upgrade_materials_samplers)
      {
         ignore_upgraded_samplers = false; // "shader_hashes_BeginMaterialsDrawing" shaders don't use samplers so they are good to be upgraded already
      }
      else if (!ignore_upgraded_samplers && original_shader_hashes.Contains(shader_hashes_EndMaterialsDrawing))
      {
         ignore_upgraded_samplers = true;

         // Revert currently applied upgrades because "shader_hashes_EndMaterialsDrawing" use samplers that might need to not be anisotropic
         com_ptr<ID3D11SamplerState> samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
         native_device_context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &samplers[0]);

         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         for (uint32_t i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
         {
            for (auto& custom_samplers : device_data.custom_sampler_by_original_sampler)
            {
               const auto it = custom_samplers.second.find(device_data.texture_mip_lod_bias_offset);
               if (it != custom_samplers.second.end())
               {
                  ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(custom_samplers.first);
                  if (it->second != nullptr && it->second == samplers[i]) // Null means the sampler didn't need an upgrade
                  {
                     samplers[i] = native_sampler;
                     break;
                  }
               }
            }
         }

         ID3D11SamplerState* const* samplers_const = (ID3D11SamplerState**)std::addressof(samplers[0]);
         native_device_context->PSSetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers_const);
      }

      // The sky always draws if the scene drew (seems so, excluding during some video playbacks menus etc)
      if (original_shader_hashes.Contains(shader_hashes_Sky))
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         main_dsv = nullptr;
         native_device_context->OMGetRenderTargets(1, &rtv, &main_dsv);
      }

      // In UW, at least at high resolutions, the game wouldn't go beyond 4k 16:9, and thus the final post process would lose half of the quality
      if (original_shader_hashes.Contains(shader_hashes_PostProcessEncode))
      {
         has_drawn_post_process = true;

         com_ptr<ID3D11RenderTargetView> rtv;
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(1, &rtv, &dsv);

         uint4 size;
         DXGI_FORMAT format;
         GetResourceInfo(rtv.get(), size, format);

         if (rtv.get() && rtv.get() != post_process_rtvs[final_post_process_copy_draws] && (size.x != uint(device_data.output_resolution.x + 0.5f) || size.y != uint(device_data.output_resolution.y + 0.5f)))
         {
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            UINT viewports_num = 1;
            native_device_context->RSGetViewports(&viewports_num, nullptr);
            native_device_context->RSGetViewports(&viewports_num, &viewports[0]);
            if (viewports_num == 1)
            {
               com_ptr<ID3D11Resource> post_process_resource;
               rtv->GetResource(&post_process_resource);
               if (post_process_resource)
               {
                  com_ptr<ID3D11Texture2D> post_process_texture_2d;
                  post_process_resource->QueryInterface(&post_process_texture_2d);
                  if (post_process_texture_2d)
                  {
                     // Re-use the original descs for a full match

                     D3D11_TEXTURE2D_DESC texture_2d_desc;
                     post_process_texture_2d->GetDesc(&texture_2d_desc);

                     texture_2d_desc.Width = uint(device_data.output_resolution.x + 0.5f);
                     texture_2d_desc.Height = uint(device_data.output_resolution.y + 0.5f);

                     D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
                     rtv->GetDesc(&rtv_desc);

                     D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                     srv_desc.Format = rtv_desc.Format;
                     srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                     srv_desc.Texture2D.MipLevels = 1;
                     srv_desc.Texture2D.MostDetailedMip = 0;

                     upgraded_post_process_textures_2d[final_post_process_copy_draws] = nullptr;
                     upgraded_post_process_rtvs[final_post_process_copy_draws] = nullptr;
                     upgraded_post_process_srvs[final_post_process_copy_draws] = nullptr;
                     native_device->CreateTexture2D(&texture_2d_desc, nullptr, &upgraded_post_process_textures_2d[final_post_process_copy_draws]); // This is never read back as an SRV so it should be fine
                     native_device->CreateRenderTargetView(upgraded_post_process_textures_2d[final_post_process_copy_draws].get(), &rtv_desc, &upgraded_post_process_rtvs[final_post_process_copy_draws]);
                     native_device->CreateShaderResourceView(upgraded_post_process_textures_2d[final_post_process_copy_draws].get(), &srv_desc, &upgraded_post_process_srvs[final_post_process_copy_draws]);

                     post_process_rtvs[final_post_process_copy_draws] = rtv;
                  }
               }
            }
         }
         if (rtv.get() && rtv.get() == post_process_rtvs[final_post_process_copy_draws] && test_index != 12)
         {
            D3D11_VIEWPORT viewport;
            viewport.TopLeftX = 0.f;
            viewport.TopLeftY = 0.f;
            viewport.MinDepth = 0.f;
            viewport.MaxDepth = 1.f;
            viewport.Width = device_data.output_resolution.x;
            viewport.Height = device_data.output_resolution.y;
            native_device_context->RSSetViewports(1, &viewport);
            native_device_context->RSSetScissorRects(0, nullptr);

            // This keeping its old value would prevent our RTV from being bound, because the size of the depth buffer is different, even if the depth isn't actually used in post processing (not from my limited testing),
            // so we swap it with the DSV actually used by the scene at full rendering resolution.
            // The game always makes one for the scene rendering and one for the UI, both are cleared immediately after this pass (and the scene one again as the next frame starts), but the UI one was hardcoded to 16:9, like the UI passes, so it was smaller in UW.
            dsv = dsv ? main_dsv : nullptr;

            ID3D11RenderTargetView* upgraded_post_process_rtv_const = upgraded_post_process_rtvs[final_post_process_copy_draws].get();
            native_device_context->OMSetRenderTargets(1, &upgraded_post_process_rtv_const, dsv.get());
         }
         else
         {
            post_process_rtvs[final_post_process_copy_draws] = nullptr;
            upgraded_post_process_textures_2d[final_post_process_copy_draws] = nullptr;
            upgraded_post_process_rtvs[final_post_process_copy_draws] = nullptr;
            upgraded_post_process_srvs[final_post_process_copy_draws] = nullptr;
         }

         final_post_process_copy_draws++;
         ASSERT_ONCE(final_post_process_copy_draws <= max_final_post_process_copy_draws); // Unsupported, we need to increase arrays like "post_process_rtvs" to support it! I don't think the game ever does 2 of these passes!
      }
      // Refresh the rtv and viewport.
      // This should exclusively be the UI.
      // The game was literally stretching (shrinking) the backbuffer to a 16:9,
      // drawing UI on it with a fullscreen viewport (which would have been 16:9), but stretched,
      // and then at the end unstretching the image back to your swapchain aspect ratio.
      // The game didn't support ultrawide so this is the behaviour that the "Nioh Resolution" mod induced,
      // it's possibly nobody noticed the resolution would have been halved before.
      // So, above we scale the backbuffer to full resolution, and here we force draw the UI in 16:9.
      // We assume the same behaviour would happen at 4:3 etc, but it's untested.
      // 
      // Note: if there's more than one of these ("final_post_process_copy_draws" > 1), take the last, as that's what seems like the game uses as RT, while the previous one is a copy for UI custom usage.
      // 
      // TODO: some of the UI that is mapped to the world is stretched in UW (it seems like the issues is in the vertices, so we can't fix it by detecting data in the shaders, everything is the same as the other 2D UI shaders)
      else if (has_drawn_post_process && upgraded_post_process_rtvs[final_post_process_copy_draws-1])
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(1, &rtv, &dsv);
         if (rtv && (rtv == post_process_rtvs[final_post_process_copy_draws-1] || rtv == upgraded_post_process_rtvs[final_post_process_copy_draws-1]))
         {
            D3D11_VIEWPORT viewport;
            viewport.MinDepth = 0.f;
            viewport.MaxDepth = 1.f;

            viewport.TopLeftX = 0.f;
            viewport.TopLeftY = 0.f;
            viewport.Width = device_data.output_resolution.x;
            viewport.Height = device_data.output_resolution.y;

            native_device_context->RSSetViewports(1, &viewport);
            // Clear scissors too, given they were set, they are probably not enabled and we'd need to adapt them as well
            native_device_context->RSSetScissorRects(0, nullptr);

            dsv = dsv ? main_dsv : nullptr;

            ID3D11RenderTargetView* upgraded_post_process_rtv_const = upgraded_post_process_rtvs[final_post_process_copy_draws-1].get();
            native_device_context->OMSetRenderTargets(1, &upgraded_post_process_rtv_const, dsv.get());
         }
      }

      bool is_swapchain_copy = original_shader_hashes.Contains(shader_hashes_SwapchainCopy);
      bool is_ui = original_shader_hashes.Contains(shader_hashes_UI);

      if (is_swapchain_copy || is_ui)
      {
         if (is_swapchain_copy && is_custom_pass)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, playing_video ? 1 : 0); // Set custom data to 1 to highlight it's a video and do aspect ratio scaling.
            // Note: it's possible we could just force the viewport to the SRV size, though we aren't guaranteed the swapchain would have been cleared, nor that any other cases would break this behaviour, so for now, scaling is done in the shader
            updated_cbuffers = true;
         }

         int i = 0;
         // If we made two copies before, the UI would read back the first one, while the swapchain copy the second one
         if (final_post_process_copy_draws >= 2)
            i = is_ui ? 0 : (final_post_process_copy_draws-1);
         if (has_drawn_post_process && upgraded_post_process_srvs[i])
         {
            com_ptr<ID3D11ShaderResourceView> ui_srv;
            native_device_context->PSGetShaderResources(0, 1, &ui_srv);

            // The UI re-uses the same shader for a lot of stuff, make sure the texture matches the original one!
            if (!is_ui || AreViewsOfSameResource(ui_srv.get(), post_process_rtvs[i].get()))
            {
               ID3D11ShaderResourceView* const upgraded_post_process_srv_const = upgraded_post_process_srvs[i].get();
               native_device_context->PSSetShaderResources(0, 1, &upgraded_post_process_srv_const);
            }

            if (is_swapchain_copy)
            {
               // The viewport was probably already fullscreen here, but let's force it anyway
               D3D11_VIEWPORT viewport;
               viewport.TopLeftX = 0.f;
               viewport.TopLeftY = 0.f;
               viewport.MinDepth = 0.f;
               viewport.MaxDepth = 1.f;
               viewport.Width = device_data.output_resolution.x;
               viewport.Height = device_data.output_resolution.y;
               native_device_context->RSSetViewports(1, &viewport);
               native_device_context->RSSetScissorRects(0, nullptr);
            }
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      if (!has_drawn_post_process)
      {
         main_dsv = nullptr;
      }

      // Clear all non first post process texture copies,
      // because we might not have a chance to null them after, and we'd keep a reference forever,
      // so it's simply better to re-create them every frame, especially because this only happens outside of gameplay.
      // This shouldn't ever crash.
      while (final_post_process_copy_draws > 1)
      {
         post_process_rtvs[final_post_process_copy_draws-1] = nullptr;
         upgraded_post_process_textures_2d[final_post_process_copy_draws-1] = nullptr;
         upgraded_post_process_rtvs[final_post_process_copy_draws-1] = nullptr;
         upgraded_post_process_srvs[final_post_process_copy_draws-1] = nullptr;
         final_post_process_copy_draws--;
      }

      ASSERT_ONCE(ignore_upgraded_samplers); // This means we missed the end "event" for materials drawing

      first_frame_draw_call = true;
      playing_video = false;
      has_drawn_post_process = false;
      final_post_process_copy_draws = 0;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "UpgradeMaterialsSamplers", upgrade_materials_samplers);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::Checkbox("Force Anisotropic Filtering", &upgrade_materials_samplers))
         reshade::set_config_value(runtime, NAME, "UpgradeMaterialsSamplers", upgrade_materials_samplers);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Nioh\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.\n\nNote that \"NiohResolution\" is still needed to unlock ultrawide or unconventional resolutions.");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Nioh Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      // We actually don't need to upgrade most texture formats as the game did post processing in R11G11B10_FLOAT (bloom etc) but mostly in R16G16B16A16_FLOAT, up until the final swapchain copy (after UI), so it's all HDR friendly
      texture_upgrade_formats = {
#if 0
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_unorm_srgb,
            reshade::api::format::b8g8r8x8_typeless,
#endif

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;
#if 0 // Seemengly not needed
      nable_indirect_texture_format_upgrades = true;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
#endif
      force_ignore_dpi = true;

      enable_samplers_upgrade = true;
      force_upgrade_linear_samplers = true;
      ignore_upgraded_samplers = true; // By default, ignore the upgrades. Once we detect the materials drawing phase, enable the live swapping, and then disable it at the end. It's all good because the game is single threaded on rendering.

      shader_hashes_Copy.pixel_shaders = { 0x5D15CFEE };
      shader_hashes_SwapchainCopy.pixel_shaders = { 0xF0298A93 };
      shader_hashes_PostProcessEncode.pixel_shaders = { 0x2838FB01 };
      shader_hashes_UI.pixel_shaders = { 0xE2A52FDE };
      shader_hashes_Sky.pixel_shaders = { 0x46784DDA };
      shader_hashes_BeginMaterialsDrawing.compute_shaders = { 0xECE033E1, 0x37B447FE };
      shader_hashes_EndMaterialsDrawing.pixel_shaders = { 0xC155D568, 0x8D34F907 };

      game = new Nioh();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}