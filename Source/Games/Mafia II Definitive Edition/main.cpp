#define GAME_MAFIA_II_DE 1

// TODO: game crashes without this???
#define DISABLE_AUTO_DEBUGGER 1

// We get a warning on boot without this?
#define ENABLE_GAME_PIPELINE_STATE_READBACK 1

// To access "last_draw_dispatch_data"
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList shader_hashes_DecodeVideo;
   ShaderHashesList shader_hashes_UI_Video;
   ShaderHashesList shader_hashes_UI_Boot;
   ShaderHashesList shader_hashes_Tonemap;
   ShaderHashesList shader_hashes_BlackBars;

   // User settings
   bool remove_black_bars = true;
}

struct GameDeviceDataMafiaIIDE final : public GameDeviceData
{
   com_ptr<ID3D11Resource> video;

   int pending_draws_to_black_bars = 0;

   // For now we don't have a proper way to tell if a video is playing back in the main menu or upfront a game chapter,
   // so do the extra main menu video fix the first time we get to the menu after the game boots, and never again if the user comes back to the menu (one way to tell could be the amount of UI draws, but it's whatever!)
   // TODO: fix that (not a big deal)
   bool is_in_main_menu = true;

   // Device is only ever created one so this is fine
   bool has_booted = false;

   bool has_drawn_anything = false;
};

class MafiaIIDE final : public Game
{
public:
   static const GameDeviceDataMafiaIIDE& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataMafiaIIDE*>(device_data.game);
   }
   static GameDeviceDataMafiaIIDE& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMafiaIIDE*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_IMPROVED_BLOOM", '1', true, false, "Fixes bloom downscaling the image in a cheap way, causing it to flicker (be unstable).", 1},
         {"ENABLE_HDR_BOOST", '1', true, false, "Enable a \"Fake\" HDR boosting effect (applies to videos too).", 1},
         {"IMPROVED_COLOR_GRADING_TYPE", '2', true, false, "0 - Vanilla\n1 - Modernizes the color grading logic (e.g. running in linear space, avoids quality loss from math etc)\n2 - Same as 1, but also expands gamut (working in BT.2020)\n3 - Lowers the color grading strengths and expands the dynamic range (less raised blacks, though they were used to simulate ambient lighting so it might not always look right)\n4 - Fully expanded dynamic range (similar to 3 but stronger)", 4},
         {"ENABLE_BLOOM", '1', true, false, "Allows disabling the game's bloom effects.", 1},
         {"ENABLE_DEPTH_OF_FIELD", '1', true, false, "Allows disabling the game's depth of effect.", 1},
         {"ENABLE_MOTION_BLUR", '1', true, false, "Allows disabling the game's motion blur effect.", 1},
         {"ENABLE_COLOR_GRADING", '1', true, false, "Allows disabling the game's color grading effects.", 1},
         {"DISABLE_BLACK_BARS", '1', true, false, "Disable black bars around 16:9 during cutscenes (useful for ultrawide).", 1},
         {"FORCE_BT709_VIDEOS", '1', true, false, "Corrects videos color space, decoding them as BT.709 instead of BT.601. This is mostly noticeable in reds, producing better skin tones.\nIt's not 100% guaranteed the fix is correct so it's optional.", 1},
      };

      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      // The game is all SDR (UNORM) gamma space
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // Gamma 2.2 in and out
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

      // Only cb0 is ever used (dx9 port)
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMafiaIIDE;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Fixes the boot screen accumulating garbage because the swapchain wasn't cleared, so we forcefully clear the swapchain, which wasn't done (possibly due to luma texture upgrades?)
      if (((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)) // Game only has pixel shaders (no compute)
      {
#if 0 // Doesn't work, there's likely other shaders
         if (!original_shader_hashes.Contains(shader_hashes_UI_Boot))
         {
            game_device_data.has_booted = true;
         }
         else
#endif
         if (!game_device_data.has_booted && !game_device_data.has_drawn_anything)
         {
            SwapchainData& swapchain_data = *(*device_data.swapchains.begin())->get_private_data<SwapchainData>(); // Game only ever has 1 swapchain, so this is safe
            com_ptr<ID3D11RenderTargetView> display_composition_rtv;
            {
               const std::shared_lock lock_swapchain(swapchain_data.mutex);
               display_composition_rtv = swapchain_data.display_composition_rtvs.empty() ? nullptr : swapchain_data.display_composition_rtvs[0]; // Always 1 swapchain buffer in DX11, this will never change as it'd break the design of games that rely on it
            }

            ID3D11RenderTargetView* display_composition_rtv_const = display_composition_rtv.get();
            constexpr FLOAT clear_color[4] = { 0.f, 0.f, 0.f, 1.f };
            native_device_context->ClearRenderTargetView(display_composition_rtv.get(), clear_color);

            Trace::Marker(native_device_context, "Clear Swapchain", display_composition_rtv.get());
         }
         game_device_data.has_drawn_anything = true;
      }

      // This only runs some frames
      if (original_shader_hashes.Contains(shader_hashes_DecodeVideo))
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);

         uint4 size;
         DXGI_FORMAT format;
         GetResourceInfo(rtv.get(), size, format);

         // If the video decode shader is drawing to a swapchain sized resource, we know it's drawing a 2D fullscreen video
         if (rtv.get() && (size.x == uint(device_data.output_resolution.x + 0.5f) || size.y == uint(device_data.output_resolution.y + 0.5f)))
         {
            game_device_data.video = nullptr;
            rtv->GetResource(&game_device_data.video);
         
            if (is_custom_pass)
            {
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, game_device_data.is_in_main_menu ? 2 : 1); // Set custom data to 1 to highlight it's a 2D fullscreen video. 2 to tell it's also in the main menu!
               updated_cbuffers = true;
            }
         }
      }
      // If the UI shader is playing back our video, to fix the stretching or cropping that happens in the main menu and during other videos playback (e.g. game chapters intro) (they draw in a different way, the menu was fine),
      // we simply skip the draw call and copy the fullscreen (swapchain sized) video on it, or inform the shader it's a fullscreen video.
      else if (game_device_data.video && original_shader_hashes.Contains(shader_hashes_UI_Video) && test_index != 13)
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(8, 1, &srv);
         if (srv.get())
         {
            com_ptr<ID3D11Resource> sr;
            srv->GetResource(&sr);

            if (sr == game_device_data.video)
            {
               com_ptr<ID3D11RenderTargetView> rtv;
               native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
               
               uint4 size;
               DXGI_FORMAT format;
               GetResourceInfo(rtv.get(), size, format);

               if (rtv.get() && (size.x == uint(device_data.output_resolution.x + 0.5f) || size.y == uint(device_data.output_resolution.y + 0.5f)))
               {
                  com_ptr<ID3D11Resource> rt;
                  rtv->GetResource(&rt);

                  // The UI shader had the ability to do some filtering on videos, so make sure we preserve it just in case
                  if (is_custom_pass && test_index != 12)
                  {
                     SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
                     SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, game_device_data.is_in_main_menu ? 2 : 1);
                     updated_cbuffers = true;
                  }
                  else
                  {
                     native_device_context->CopyResource(rt.get(), game_device_data.video.get()); // Both resources should be upgraded
                     
                     return DrawOrDispatchOverrideType::Replaced;
                  }
               }
            }
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_Tonemap))
      {
         game_device_data.video = nullptr; // Clear the video shader, it doesn't (and shouldn't) draw during scene rendering

         device_data.has_drawn_main_post_processing = true; // This shader seemingly always run, at least when the scene is rendering
         game_device_data.pending_draws_to_black_bars = 2; // We expect black bars to draw immediately after (after another copy shader)

         // If we are drawing the scene, we aren't in the main menu
         game_device_data.is_in_main_menu = false;
         game_device_data.has_booted = true;
      }
      // Sometimes there's a single fullscreen black bar. By checking the index count, we only acknowledge double black bars for cutscenes.
      else if (original_shader_hashes.Contains(shader_hashes_BlackBars) && remove_black_bars && game_device_data.pending_draws_to_black_bars == 1 && last_draw_dispatch_data.index_count == 12 && test_index != 14)
      {
         if (is_custom_pass)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 1); // Set custom data to 1 to highlight it should skip black bars (if the shader also detects itself as black bars)
            updated_cbuffers = true;
         }
         else // Less safe (false positives risk)
         {
            return DrawOrDispatchOverrideType::Skip;
         }
         game_device_data.pending_draws_to_black_bars--;
      }
      else
      {
         game_device_data.pending_draws_to_black_bars--; // Let it go <0, it doesn't matter
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      device_data.has_drawn_main_post_processing = false;
      game_device_data.pending_draws_to_black_bars = 0; // Unnecessary, but clean

      game_device_data.has_drawn_anything = false;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Mafia II: Definitive Edition\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

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
                  "\nImGui"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Mafia II Definitive Edition Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
#if 1 // Most are unused, game is all R8G8B8A8_UNORM
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
            // Most post process textures are R10G10B10A2
            reshade::api::format::r10g10b10a2_typeless,
            reshade::api::format::r10g10b10a2_unorm,
#endif
#if 0 // Used by bloom mips, it's seemingly overkill but it also has an extremely small scale so maybe it's necessary to be 32bit
            reshade::api::format::r32g32b32a32_float,
#endif
            // Unused
            reshade::api::format::r11g11b10_float,
      };
      // Depth in Mafia II was 24 bits but exhibited some z-fighting, this partially fixes it
      texture_depth_upgrade_formats = {
            reshade::api::format(DXGI_FORMAT_D24_UNORM_S8_UINT),
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
#if 1 // Seemingly not necessary
      enable_indirect_texture_format_upgrades = true;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
#endif
      prevent_fullscreen_state = true;
      // Game uses DPI scale when reading the resolutions from Windows (meaning DPI affects them), the only way to play in the native borderless res is windowed mode, or start the game with DPI 1.0. Actually FSE doesn't seem affected by the issue, at least not with this flag on.
      force_borderless = true;
      force_ignore_dpi = true;

      // Needed to force clear the swapchain on boot, otherwise alpha goes beyond 0-1 as it draws in an additive way
      force_create_swapchain_rtvs = true;

      // TODO: these are all the permutations that have a color matrix, but there's ~200 other that don't. Whether they are used or not is unclear, probably not. Use "43758.5469" (nah, false positives) or "s050_PostProcessSrcTexture" as search patterns to find them? We could automatically collect these in c++ and warn users.
      // If necessary, we could simply add a check to make sure any known color grading shader has run, otherwise send a warning.
      redirected_shader_hashes["ColorGrading"] =
         {
            "06FFF98A",
            "0806297C",
            "0FFE0AED",
            "1BCED85C",
            "211FB55F",
            "27E3BE86",
            "435E4F7A",
            "44880094",
            "49644172",
            "49D7718D",
            "49F6EFCE",
            "4DA41D5E",
            "4E137FCB",
            "4F61099C",
            "6A62D2F7",
            "7579CEAD",
            "7AFD9825",
            "7C04055C",
            "80524805",
            "8321F836",
            "8CE1E3FA",
            "939824ED",
            "97EE9E05",
            "9DE64DC9",
            "A393BEBF",
            "A78AC640",
            "B8D26E59",
            "CA2E65D6",
            "CB9FA7F0",
            "D259AA98",
            "DB3F0961",
            "EA9C5691",
            "288047AA",
            "2F104EE3",
            "33B467FB",
            "5109AB83",
            "6EA8AD80",
            "7758DF4C",
            "86F8F7C9",
            "A0B3FA64",
            "A857DBC1",
            "B3BE6B94",
            "B756FB04",
            "DD1033D8",
            "E0B922A1",
            "E79BD563",
            "E87A3D68",
            "F345BA5D",
         };

      shader_hashes_DecodeVideo.pixel_shaders = {0xBFAEA516};
      shader_hashes_UI_Video.vertex_shaders = {0x661B34E8};
      shader_hashes_UI_Video.pixel_shaders = {0xDC056E98};
      shader_hashes_UI_Boot.pixel_shaders = {0xF48758E4};
      shader_hashes_Tonemap.pixel_shaders = {0xD80171EC};
      shader_hashes_BlackBars.vertex_shaders = {0x2D4034E6};
      shader_hashes_BlackBars.pixel_shaders = {0xDC9CC19C};

#if DEVELOPMENT
      forced_shader_names.emplace(0x24755D18, "FXAA");

      forced_shader_names.emplace(0x334DB2D1, "SMAA EdgeDetection 2");
      
      forced_shader_names.emplace(0xCC956B68, "DoF Blur");
      forced_shader_names.emplace(0xE5A8562D, "DoF Blur");
      forced_shader_names.emplace(0x9C0C7512, "Bloom Blur");
#endif

      game = new MafiaIIDE();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}