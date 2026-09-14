#define GAME_METAL_GEAR_SOLID_4 1

#define ENABLE_SMAA 1

#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

namespace
{
   const ShaderHashesList shader_hashes_FXAA = { .pixel_shaders = { 0xFAB5AE7C } };
   const ShaderHashesList shader_hashes_SwapchainCopy = { .pixel_shaders = { 0xD4BDA6C0 } };

   // User settings:
   bool smaa_enable = true; // TODO: test!
}

struct GameDeviceDataMetalGearSolid4 final : public GameDeviceData
{
   // The resolution "DrawSMAA()" last created its (Luma managed) intermediate render targets at
   uint32_t smaa_width = 0;
   uint32_t smaa_height = 0;

   CustomPixelShaderPassData correct_subtractive_blends_data;
};

class GameMetalGearSolid4 final : public Game
{
public:
   static GameDeviceDataMetalGearSolid4& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMetalGearSolid4*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      // No gamma mismatch baked in the textures as the game never applied gamma, it was gamma from the beginning to the end.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_LUMA", '1', true, false, "Allows disabling some of the mod's improvements (e.g. unclamping dynamic range and tonemapping)", 1},
         {"ENABLE_IMPROVED_COLOR_GRADING", '1', true, false, "Adds modernized and improved color grading that doesn't crush shadow as much but still retains contrast", 1},
         {"ENABLE_COLOR_GRADING", '1', true, false, "Allows disabling the game's color grading (not adviced)", 1},
         {"ENABLE_FILM_GRAIN", '1', true, false, "Allows disabling the game's film grain effect (it's not always present)", 1},
         {"ENABLE_VIGNETTE", '1', true, false, "Allows disabling the game's vignette effect (not always used) (best left at default)", 1},
         //{"ALLOW_AA", '1', true, false, "The game uses FXAA at the end", 1},
         {"ENABLE_AUTO_HDR", '1', true, false, "Enables an SDR to HDR conversion for videos", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMetalGearSolid4;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "SMAAEnable", smaa_enable);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      if (ImGui::Checkbox("SMAA Enable", &smaa_enable))
         reshade::set_config_value(runtime, NAME, "SMAAEnable", smaa_enable);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA, which is sharper and more temporally stable.");
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      // Replace FXAA with SMAA. They both run on the gamma space post process buffer, from one texture onto another one, so it's a drop in replacement.
      if (smaa_enable && original_shader_hashes.Contains(shader_hashes_FXAA))
      {
         // "DrawSMAA()" retrieves its shaders without checking whether they are valid, and they might still be compiling on boot (or be reloading in development builds), in that case we simply keep the original FXAA.
         auto IsShaderReady = [](const auto& native_shaders, uint32_t shader_name_hash)
         {
            const auto it = native_shaders.find(shader_name_hash);
            return it != native_shaders.end() && it->second.get() != nullptr;
         };
         if (!IsShaderReady(device_data.native_vertex_shaders, "SMAA Edge Detection VS"_h)
            || !IsShaderReady(device_data.native_pixel_shaders, "SMAA Edge Detection PS"_h)
            || !IsShaderReady(device_data.native_vertex_shaders, "SMAA Blending Weight Calculation VS"_h)
            || !IsShaderReady(device_data.native_pixel_shaders, "SMAA Blending Weight Calculation PS"_h)
            || !IsShaderReady(device_data.native_vertex_shaders, "SMAA Neighborhood Blending VS"_h)
            || !IsShaderReady(device_data.native_pixel_shaders, "SMAA Neighborhood Blending PS"_h))
         {
            return DrawOrDispatchOverrideType::None;
         }

         // SRV 0 is the scene, and FXAA resolves it onto RT 0.
         ComPtr<ID3D11ShaderResourceView> srv_scene;
         native_device_context->PSGetShaderResources(0, 1, srv_scene.put());
         ComPtr<ID3D11RenderTargetView> rtv_scene;
         native_device_context->OMGetRenderTargets(1, rtv_scene.put(), nullptr);
         if (!srv_scene || !rtv_scene) return DrawOrDispatchOverrideType::None;

         ComPtr<ID3D11Resource> srv_resource;
         srv_scene->GetResource(srv_resource.put());
         ComPtr<ID3D11Resource> rtv_resource;
         rtv_scene->GetResource(rtv_resource.put());
         // Neither AA technique can read and write the same texture at the same time, so if that ever happened, fall back on the original pass.
         ASSERT_ONCE(srv_resource && rtv_resource && srv_resource.get() != rtv_resource.get());
         if (!srv_resource || !rtv_resource || srv_resource.get() == rtv_resource.get()) return DrawOrDispatchOverrideType::None;

         ComPtr<ID3D11Texture2D> rtv_texture;
         rtv_resource->QueryInterface(rtv_texture.put());
         if (!rtv_texture) return DrawOrDispatchOverrideType::None;
         D3D11_TEXTURE2D_DESC rtv_texture_desc;
         rtv_texture->GetDesc(&rtv_texture_desc);

         auto& game_device_data = GetGameDeviceData(device_data);
         auto& managed_resources = device_data.managed_resources;

         // "DrawSMAA()" only re-creates its resolution dependent resources when the swapchain is re-initialized, so do it ourselves in case the game ever changed its post processing resolution on its own.
         // Note: the game resolution cannot change anyway!
         if (game_device_data.smaa_width != rtv_texture_desc.Width || game_device_data.smaa_height != rtv_texture_desc.Height)
         {
            managed_resources.depth_stencil_views["smaa_dsv"_h].reset();
            managed_resources.render_target_views["smaa_edge_detection"_h].reset();
            managed_resources.render_target_views["smaa_blending_weight_calculation"_h].reset();
            game_device_data.smaa_width = rtv_texture_desc.Width;
            game_device_data.smaa_height = rtv_texture_desc.Height;
         }

         // Push the render target resolution ("SMAA_RT_METRICS"). This game has no Luma game settings cbuffer, and the post process buffers aren't guaranteed to match the swapchain resolution, so we send our own (see "Luma_SMAA_impl.hlsl").
         auto& cb_smaa_metrics = managed_resources.buffers["smaa_metrics_cb"_h];
         [[unlikely]] if (!cb_smaa_metrics)
         {
            D3D11_BUFFER_DESC buffer_desc = {};
            buffer_desc.ByteWidth = sizeof(float) * 4;
            buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
            buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ensure(native_device->CreateBuffer(&buffer_desc, nullptr, cb_smaa_metrics.put()), >= 0);
            if (!cb_smaa_metrics) return DrawOrDispatchOverrideType::None;
         }
         const float smaa_rt_metrics[4] = { 1.f / float(rtv_texture_desc.Width), 1.f / float(rtv_texture_desc.Height), float(rtv_texture_desc.Width), float(rtv_texture_desc.Height) };
         if (D3D11_MAPPED_SUBRESOURCE mapped_buffer; SUCCEEDED(native_device_context->Map(cb_smaa_metrics.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
         {
            std::memcpy(mapped_buffer.pData, smaa_rt_metrics, sizeof(smaa_rt_metrics));
            native_device_context->Unmap(cb_smaa_metrics.get(), 0);
         }
         else
         {
            return DrawOrDispatchOverrideType::None;
         }

         // "DrawSMAA()" restores all the states it changes but the cbuffers, so back these up ourselves.
         ComPtr<ID3D11Buffer> vs_cb1_original;
         native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_original.put());
         ComPtr<ID3D11Buffer> ps_cb1_original;
         native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_original.put());
         ID3D11Buffer* const cb_smaa_metrics_ptr = cb_smaa_metrics.get();
         native_device_context->VSSetConstantBuffers(1, 1, &cb_smaa_metrics_ptr);
         native_device_context->PSSetConstantBuffers(1, 1, &cb_smaa_metrics_ptr);

         // Colors are in gamma space at this point ("POST_PROCESS_SPACE_TYPE" is 0), which is what SMAA's color edge detection expects,
         // so the same texture serves as both the edge detection and the neighborhood blending input (no linearization pass is needed).
         DrawSMAA(native_device, native_device_context, device_data, rtv_scene.get(), srv_scene.get(), srv_scene.get());

         ID3D11Buffer* const vs_cb1_original_ptr = vs_cb1_original.get();
         native_device_context->VSSetConstantBuffers(1, 1, &vs_cb1_original_ptr);
         ID3D11Buffer* const ps_cb1_original_ptr = ps_cb1_original.get();
         native_device_context->PSSetConstantBuffers(1, 1, &ps_cb1_original_ptr);
         
#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context;
            trace_draw_call_data.custom_name = "SMAA";
            // Re-use the RTV data for simplicity
            GetResourceInfo(rtv_scene.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
#endif

         return DrawOrDispatchOverrideType::Replaced;
      }
      // Clear the swapchain in case our display aspect ratio isn't 16:9 and we don't have an UW mod.
      // The game doesn't and as such if you have reshade open, imgui ends up trailing behind
      else if (original_shader_hashes.Contains(shader_hashes_SwapchainCopy))
      {
         // SRV 0 is the scene, and FXAA resolves it onto RT 0.
         ComPtr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, srv.put());
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);

         if (srv && rtv)
         {
            ComPtr<ID3D11Resource> srv_resource;
            srv->GetResource(srv_resource.put());
            ComPtr<ID3D11Resource> rtv_resource;
            rtv->GetResource(rtv_resource.put());

            if (srv_resource && rtv_resource)
            {
               ComPtr<ID3D11Texture2D> rtv_texture_2d;
               rtv_resource->QueryInterface(rtv_texture_2d.put());
               D3D11_TEXTURE2D_DESC rtv_texture_2d_desc = {};
               if (rtv_texture_2d)
                  rtv_texture_2d->GetDesc(&rtv_texture_2d_desc);

               ComPtr<ID3D11Texture2D> srv_texture_2d;
               srv_resource->QueryInterface(srv_texture_2d.put());
               D3D11_TEXTURE2D_DESC srv_texture_2d_desc = {};
               if (srv_texture_2d)
                  srv_texture_2d->GetDesc(&srv_texture_2d_desc);

               if (rtv_texture_2d_desc.Width != srv_texture_2d_desc.Width || rtv_texture_2d_desc.Height != srv_texture_2d_desc.Height)
               {
                  constexpr FLOAT clear_color[4] = { 0.f, 0.f, 0.f, 1.f };
                  native_device_context->ClearRenderTargetView(rtv.get(), clear_color);
               }
            }
         }
      }
      // TODO: delete once we add UI separate composition, this would work out of the box there? Also renove "ENABLE_POST_DRAW_DISPATCH_CALLBACK".
      else if (original_shader_hashes.Contains(shader_hashes_UI))
      {
         ComPtr<ID3D11BlendState> blend_state;
         native_device_context->OMGetBlendState(blend_state.put(), nullptr, nullptr);
         D3D11_BLEND_DESC blend_desc = {};
         if (blend_state)
         {
            blend_state->GetDesc(&blend_desc);
         }

         // This pass subtracts the source from the target so we need to clamp it after draw in R16G16B16A16_FLOAT otherwise values can go negative
         if (IsBlendInverted(blend_desc, 1, false, 0))
         {
            if (original_draw_dispatch_func && *original_draw_dispatch_func)
            {
               (*original_draw_dispatch_func)();
            }
            
            com_ptr<ID3D11RenderTargetView> rtv;
            com_ptr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargets(1, &rtv, &dsv);
            
            if (rtv.get() && test_index != 14)
            {
               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
            
               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv->GetDesc(&rtv_desc);
               const bool ms = rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS;
               ASSERT_ONCE(rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS || rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D);
            
               // Clip all negative values, like vanilla (I tried to clamp to the closest valid luminance instead, but it created weird colors)
               auto& game_device_data = GetGameDeviceData(device_data);
               DrawCustomPixelShaderPass(native_device, native_device_context, rtv.get(), device_data, ms ? Math::CompileTimeStringHash("Copy RGB Max 0 A Sat MS") : Math::CompileTimeStringHash("Copy RGB Max 0 A Sat"), game_device_data.correct_subtractive_blends_data);
            
               draw_state_stack.Restore(native_device_context);
            
#if DEVELOPMENT
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  TraceDrawCallData trace_draw_call_data;
                  trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                  trace_draw_call_data.command_list = native_device_context;
                  trace_draw_call_data.custom_name = "Sanitize Subtractive Blends";
                  // Re-use the RTV data for simplicity
                  GetResourceInfo(rtv.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                  cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
               }
#endif
            }

            return DrawOrDispatchOverrideType::Replaced;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Metal Gear Solid 4: Guns of the Patriots Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Metal Gear Solid 4: Guns of the Patriots Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
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

            // Overkill what whatever
            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,

            reshade::api::format::r11g11b10_float,
      };

      texture_format_upgrades_2d_size_filters = 0
         // The game creates textures before properly sizing the swapchain on boot, so we need to set the display resolution as upgrade filter too (assuming the game is being played in fullscreen size).
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::DisplayResolution
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio
         // Needed in case players play at 16:9 within a UW monitor, the game adds black bars by default
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomSize;
      // 4k videos, for direct upgrading with an HDR boost
      texture_format_upgrades_2d_custom_sizes = {{3840, 2160}};

      enable_indirect_texture_format_upgrades = true;
      // Probably not needed but won't hurt
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
      
      // Game floors are blurry without this // TODO: test. Not actually sure it helps
      enable_samplers_upgrade = true;

      shader_hashes_UI.pixel_shaders = {
         std::stoul("AC6CABC7", nullptr, 16),
         std::stoul("E7786E92", nullptr, 16),
         std::stoul("B7EB5F39", nullptr, 16),
         std::stoul("1625064C", nullptr, 16),
      };
      
#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("8EBC590F", nullptr, 16), "Wind");
      forced_shader_names.emplace(std::stoul("38875909", nullptr, 16), "Wind");
      forced_shader_names.emplace(std::stoul("FAB5AE7C", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("E7786E92", nullptr, 16), "UI");
      forced_shader_names.emplace(std::stoul("B7EB5F39", nullptr, 16), "UI");
      forced_shader_names.emplace(std::stoul("1625064C", nullptr, 16), "UI Font");
#endif

      game = new GameMetalGearSolid4();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}