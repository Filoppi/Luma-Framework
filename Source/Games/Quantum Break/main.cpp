#include "shared.h"
#include "Upscaling.hpp"

namespace
{
   namespace Settings
   {
      // Slider descriptors keep defaults, config keys, UI labels, and cbuffer members in one table.
      enum class Kind : uint8_t
      {
         Float,
         Integer
      };

      struct Descriptor
      {
         Kind kind = Kind::Float;
         const char* label = "";
         float CB::LumaGameSettings::* member = nullptr;
         float default_value = 1.f;
         float min_value = 0.f;
         float max_value = 2.f;
         const char* format = "%.2f";
         const char* tooltip = nullptr;
         std::vector<std::string> labels = {"Off", "On"};
         bool (*is_enabled)(const CB::LumaGameSettings&) = nullptr;
      };

      const Descriptor k_descriptors[] = {
         {
            .kind = Kind::Float,
            .label = "Highlights",
            .member = &CB::LumaGameSettings::Highlights,
         },
         {
            .kind = Kind::Float,
            .label = "Shadows",
            .member = &CB::LumaGameSettings::Shadows,
         },
         {
            .kind = Kind::Float,
            .label = "Contrast",
            .member = &CB::LumaGameSettings::Contrast,
         },
         {
            .kind = Kind::Float,
            .label = "Saturation",
            .member = &CB::LumaGameSettings::Saturation,
         },
         {
            .kind = Kind::Float,
            .label = "Highlight Saturation",
            .member = &CB::LumaGameSettings::HighlightSaturation,
         },
         {
            .kind = Kind::Float,
            .label = "Dechroma",
            .member = &CB::LumaGameSettings::Dechroma,
            .default_value = 0.f,
            .max_value = 1.f,
            .tooltip = "Controls highlight desaturation due to overexposure.",
         },
         {
            .kind = Kind::Float,
            .label = "Flare",
            .member = &CB::LumaGameSettings::Flare,
            .default_value = 0.f,
            .max_value = 1.f,
            .tooltip = "Flare/Glare Compensation",
         },
         {
            .kind = Kind::Float,
            .label = "LUT Strength",
            .member = &CB::LumaGameSettings::LUTStrength,
            .max_value = 1.f,
         },
         {
            .kind = Kind::Float,
            .label = "LUT Scaling",
            .member = &CB::LumaGameSettings::LUTScaling,
            .max_value = 1.f,
            .tooltip = "Scales the color grade LUT to full range when size is clamped.",
         },
         {
            .kind = Kind::Integer,
            .label = "Grain Type",
            .member = &CB::LumaGameSettings::GrainType,
            .labels = {"Vanilla", "Perceptual"},
         },
         {
            .kind = Kind::Float,
            .label = "Grain Strength",
            .member = &CB::LumaGameSettings::GrainStrength,
            .max_value = 1.f,
         },
      };

      int IntegerSliderMin(const Descriptor& setting)
      {
         return setting.labels.empty()
                   ? static_cast<int>(setting.min_value)
                   : 0;
      }

      int IntegerSliderMax(const Descriptor& setting)
      {
         return setting.labels.empty()
                   ? static_cast<int>(setting.max_value)
                   : static_cast<int>(setting.labels.size() - 1);
      }

      void SaveSettingValue(reshade::api::effect_runtime* runtime, const Descriptor& setting, float value)
      {
         reshade::set_config_value(runtime, NAME, setting.label, value);
      }

      void Initialize()
      {
         for (const Descriptor& setting : k_descriptors)
         {
            default_luma_global_game_settings.*(setting.member) = setting.default_value;
            cb_luma_global_settings.GameSettings.*(setting.member) = setting.default_value;
         }

         QuantumBreakUpscaling::Settings::Initialize();
      }

      void Load(reshade::api::effect_runtime* runtime)
      {
         for (const Descriptor& setting : k_descriptors)
         {
            float& value = cb_luma_global_settings.GameSettings.*(setting.member);
            reshade::get_config_value(runtime, NAME, setting.label, value);
         }

         QuantumBreakUpscaling::Settings::Load(runtime);
      }

      void DrawIntegerSetting(const Descriptor& setting, float& value, reshade::api::effect_runtime* runtime)
      {
         const int min_value_i = IntegerSliderMin(setting);
         const int max_value_i = IntegerSliderMax(setting);
         int slider_value = std::clamp(static_cast<int>(std::lround(value)), min_value_i, max_value_i);

         const char* slider_format = "%d";
         if (!setting.labels.empty())
         {
            slider_format = setting.labels[static_cast<size_t>(slider_value - min_value_i)].c_str();
         }

         if (ImGui::SliderInt(setting.label, &slider_value, min_value_i, max_value_i, slider_format))
         {
            value = static_cast<float>(slider_value);
            SaveSettingValue(runtime, setting, value);
         }
      }

      void DrawFloatSetting(const Descriptor& setting, float& value, reshade::api::effect_runtime* runtime)
      {
         if (ImGui::SliderFloat(setting.label, &value, setting.min_value, setting.max_value, setting.format))
         {
            SaveSettingValue(runtime, setting, value);
         }
      }

      void DrawOne(const Descriptor& setting, reshade::api::effect_runtime* runtime)
      {
         float& value = cb_luma_global_settings.GameSettings.*(setting.member);
         const float default_value = default_luma_global_game_settings.*(setting.member);
         const bool is_enabled = setting.is_enabled == nullptr || setting.is_enabled(cb_luma_global_settings.GameSettings);

         if (!is_enabled)
         {
            ImGui::BeginDisabled();
         }

         if (setting.kind == Kind::Integer)
         {
            DrawIntegerSetting(setting, value, runtime);
         }
         else
         {
            DrawFloatSetting(setting, value, runtime);
         }

         if (setting.tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("%s", setting.tooltip);
         }

         DrawResetButton(value, default_value, setting.label, runtime);

         if (!is_enabled)
         {
            ImGui::EndDisabled();
         }
      }

      void DrawAll(reshade::api::effect_runtime* runtime)
      {
         for (const Descriptor& setting : k_descriptors)
         {
            DrawOne(setting, runtime);
         }
      }

   } // namespace Settings

   namespace RuntimeConfig
   {
      void ApplyUltrawidePatches()
      {
         HMODULE module_handle = GetModuleHandle(nullptr);
         if (!module_handle)
         {
            assert(false);
            return;
         }

         auto* dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
         if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
         {
            assert(false);
            return;
         }

         auto* nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(module_handle) + dos_header->e_lfanew);
         if (nt_headers->Signature != IMAGE_NT_SIGNATURE)
         {
            assert(false);
            return;
         }

         std::byte* base = reinterpret_cast<std::byte*>(module_handle);
         const std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;

         // Ultrawide patch: QB hardcodes its borderless/fullscreen resolution list and clamps aspect ratio.
         // Replace the highest built-in ultrawide option with the current primary monitor resolution.
         const int hardcoded_res_width = 3440;
         const int hardcoded_res_height = 1440;
         const char hardcoded_res_str[] = "3440 x 1440";

         std::vector<std::byte*> hardcoded_res_width_addresses = System::ScanMemoryForPattern(base, section_size, reinterpret_cast<const std::byte*>(&hardcoded_res_width), sizeof(hardcoded_res_width));
         assert(!hardcoded_res_width_addresses.empty());
         for (std::byte* hardcoded_res_width_address : hardcoded_res_width_addresses)
         {
            // Scan the next three 32-bit values to find the matching height. There is usually one 32-bit gap.
            std::vector<std::byte*> hardcoded_res_height_addresses = System::ScanMemoryForPattern(hardcoded_res_width_address, sizeof(hardcoded_res_height) * 3u, reinterpret_cast<const std::byte*>(&hardcoded_res_height), sizeof(hardcoded_res_height));
            if (!hardcoded_res_height_addresses.empty())
            {
               const int screen_width = GetSystemMetrics(SM_CXSCREEN);
               const int screen_height = GetSystemMetrics(SM_CYSCREEN);

               System::PatchMemory(hardcoded_res_width_address, &screen_width, sizeof(screen_width), System::PatchMemoryType::Code);
               System::PatchMemory(hardcoded_res_height_addresses[0], &screen_height, sizeof(screen_height), System::PatchMemoryType::Code);

               // Patch the menu label as well. QB stores these resolution strings in 16-byte slots.
               const std::vector<std::byte> hardcoded_res_str_pattern(reinterpret_cast<const std::byte*>(hardcoded_res_str), reinterpret_cast<const std::byte*>(hardcoded_res_str) + std::strlen(hardcoded_res_str) + 1u);
               std::vector<std::byte*> hardcoded_res_str_addresses = System::ScanMemoryForPattern(base, section_size, hardcoded_res_str_pattern, true);
               if (!hardcoded_res_str_addresses.empty())
               {
                  char screen_res_str[0x10] = {};
                  const int written = std::snprintf(screen_res_str, sizeof(screen_res_str), "%i x %i", screen_width, screen_height);
                  if (written > 0 && written < static_cast<int>(sizeof(screen_res_str)))
                  {
                     System::PatchMemory(hardcoded_res_str_addresses[0], screen_res_str, sizeof(screen_res_str), System::PatchMemoryType::Data);
                  }
               }

               break;
            }
         }

         // Patch the aspect ratio limit from 21:9-ish to 48:9.
         const std::vector<uint8_t> pattern_aspect_ratio_limit = {0x26, 0xB4, 0x17, 0x40}; // 2.37037038803101 (2560x1080)
         std::vector<std::byte*> aspect_ratio_limit_addresses = System::ScanMemoryForPattern(base, section_size, pattern_aspect_ratio_limit);
         assert(aspect_ratio_limit_addresses.size() == 1); // Only one of these in the Steam build as of early 2026.
         if (aspect_ratio_limit_addresses.size() == 1)
         {
            const float new_aspect_ratio_limit = 48.f / 9.f;
            System::PatchMemory(aspect_ratio_limit_addresses[0], &new_aspect_ratio_limit, sizeof(new_aspect_ratio_limit), System::PatchMemoryType::Data);
         }
      }

      void ConfigureSwapchainAndFormatUpgrades()
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
         texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

         texture_upgrade_formats = {
            reshade::api::format::r11g11b10_float,
         };
         texture_format_upgrades_2d_size_filters =
            0 | static_cast<uint32_t>(TextureFormatUpgrades2DSizeFilters::SwapchainResolution) | static_cast<uint32_t>(TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio);
      }
   } // namespace RuntimeConfig
} // namespace

struct GameDeviceDataQuantumBreak final : public GameDeviceData
{
   QuantumBreakUpscaling::Data upscaling;

   // Frame markers are latched on present so pause/menu frames can be distinguished from scene frames.
   bool had_scene_temporal_resolve_last_frame = false;
   uint32_t ui_only_frame_hold_counter = 0u;
   bool debug_prev_saw_history_reprojection_pass = false;
   bool debug_prev_saw_temporal_resolve_pass = false;
   bool saw_history_reprojection_pass = false;
   bool saw_temporal_resolve_pass = false;
};

class QuantumBreakGame final : public Game
{
   static GameDeviceDataQuantumBreak& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataQuantumBreak*>(device_data.game);
   }

   static const GameDeviceDataQuantumBreak& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataQuantumBreak*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      (void)async;

      RuntimeConfig::ApplyUltrawidePatches();

      // QB custom shaders reserve these slots for Luma settings/data during the temporal resolve replacement.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      QuantumBreakUpscaling::OnInit();

      Settings::Initialize();
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      Settings::Load(runtime);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      (void)native_device;
      device_data.game = new GameDeviceDataQuantumBreak;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (QuantumBreakUpscaling::IsHistoryReprojectionPass(original_shader_hashes))
      {
         game_device_data.saw_history_reprojection_pass = true;
         device_data.taa_detected = true;
         QuantumBreakUpscaling::CaptureMotionVectors(native_device_context, game_device_data.upscaling);

         return DrawOrDispatchOverrideType::None;
      }

      if (!QuantumBreakUpscaling::IsTemporalResolvePass(original_shader_hashes))
      {
         return DrawOrDispatchOverrideType::None;
      }

      // Temporal resolve is the SR insertion point: it sees color, depth, post-stack constants, and final output.
      game_device_data.saw_temporal_resolve_pass = true;
      device_data.has_drawn_main_post_processing = true;

      const auto sr_result = QuantumBreakUpscaling::RunTemporalResolve(native_device, native_device_context, device_data, game_device_data.upscaling);
      if (sr_result.stop_processing)
      {
         return DrawOrDispatchOverrideType::None;
      }

      QuantumBreakUpscaling::SetSRTypeForTemporalResolve(device_data, sr_result.succeeded);
      QuantumBreakUpscaling::ForceResetIfRequestedAndFailed(device_data, sr_result);

      if (original_draw_dispatch_func && *original_draw_dispatch_func)
      {
         // Post-draw callback path lets us bind the SR result and then execute QB's original resolve draw once.
         com_ptr<ID3D11ShaderResourceView> original_ps_srv_2;
         native_device_context->PSGetShaderResources(2, 1, &original_ps_srv_2);

         com_ptr<ID3D11Buffer> original_luma_settings_cb;
         com_ptr<ID3D11Buffer> original_luma_data_cb;
         if (is_custom_pass)
         {
            native_device_context->PSGetConstantBuffers(luma_settings_cbuffer_index, 1, &original_luma_settings_cb);
            native_device_context->PSGetConstantBuffers(luma_data_cbuffer_index, 1, &original_luma_data_cb);

            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         QuantumBreakUpscaling::BindOutputToTemporalResolve(native_device_context, game_device_data.upscaling, sr_result.succeeded);

         (*original_draw_dispatch_func)();

         ID3D11ShaderResourceView* original_ps_srv_2_ptr = original_ps_srv_2.get();
         native_device_context->PSSetShaderResources(2, 1, &original_ps_srv_2_ptr);

         if (is_custom_pass)
         {
            ID3D11Buffer* original_luma_settings_cb_ptr = original_luma_settings_cb.get();
            ID3D11Buffer* original_luma_data_cb_ptr = original_luma_data_cb.get();
            native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &original_luma_settings_cb_ptr);
            native_device_context->PSSetConstantBuffers(luma_data_cbuffer_index, 1, &original_luma_data_cb_ptr);
         }

         return DrawOrDispatchOverrideType::Replaced;
      }

      if (is_custom_pass)
      {
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
         updated_cbuffers = true;
      }

      QuantumBreakUpscaling::BindOutputToTemporalResolve(native_device_context, game_device_data.upscaling, sr_result.succeeded);

      return DrawOrDispatchOverrideType::None;
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      QuantumBreakUpscaling::CleanResources(device_data, game_device_data.upscaling);
   }

   bool IsGamePaused(const DeviceData& device_data) const override
   {
      // If scene temporal resolve stops briefly after being active, treat it as pause/menu rather than gameplay.
      const auto& game_device_data = GetGameDeviceData(device_data);
      return !game_device_data.saw_temporal_resolve_pass && (game_device_data.had_scene_temporal_resolve_last_frame || game_device_data.ui_only_frame_hold_counter > 0u);
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      (void)native_device;
      auto& game_device_data = GetGameDeviceData(device_data);

      QuantumBreakUpscaling::OnPresent(device_data, game_device_data.upscaling);

      game_device_data.debug_prev_saw_history_reprojection_pass = game_device_data.saw_history_reprojection_pass;
      game_device_data.debug_prev_saw_temporal_resolve_pass = game_device_data.saw_temporal_resolve_pass;

      device_data.taa_detected = game_device_data.saw_history_reprojection_pass;
      device_data.has_drawn_sr = false;
      device_data.has_drawn_main_post_processing = false;

      const uint32_t back_buffer_count = (std::max)(2u, static_cast<uint32_t>(device_data.back_buffers.size()));
      // Hold the paused state across the swapchain queue so UI-only frames do not look like gameplay frames.
      if (game_device_data.saw_temporal_resolve_pass)
      {
         game_device_data.had_scene_temporal_resolve_last_frame = true;
         game_device_data.ui_only_frame_hold_counter = 0u;
      }
      else
      {
         if (game_device_data.had_scene_temporal_resolve_last_frame)
         {
            game_device_data.ui_only_frame_hold_counter = back_buffer_count > 0u ? (back_buffer_count - 1u) : 0u;
         }
         else if (game_device_data.ui_only_frame_hold_counter > 0u)
         {
            --game_device_data.ui_only_frame_hold_counter;
         }

         game_device_data.had_scene_temporal_resolve_last_frame = false;
      }

      game_device_data.saw_history_reprojection_pass = false;
      game_device_data.saw_temporal_resolve_pass = false;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      // Keep user-facing grading/effects controls above the SR tuning/debug block.
      Settings::DrawAll(runtime);
      QuantumBreakUpscaling::Settings::Draw(device_data, runtime);

      auto& game_device_data = GetGameDeviceData(device_data);
      QuantumBreakUpscaling::DrawDebug(
         game_device_data.upscaling,
         game_device_data.debug_prev_saw_history_reprojection_pass,
         game_device_data.debug_prev_saw_temporal_resolve_pass,
         game_device_data.had_scene_temporal_resolve_last_frame,
         game_device_data.ui_only_frame_hold_counter);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Quantum Break\" is developed by Musa and is open source and free.\nIf you enjoy it, consider donating.\n");

      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_musa = std::string("Buy Musa a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_musa.c_str()))
      {
         system("start https://ko-fi.com/musaqh");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);
      ImGui::NewLine();

      ImGui::Text("Credits:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nNeutwo, LUT Scaling, and Film Grain (from RenoDX) - Copyright (c) 2026 Carlos Lopez Jr. Licensed under MIT."
                  "");
      static const std::string neutwo_license_link = std::string("RenoDX MIT License ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(neutwo_license_link.c_str()))
      {
         system("start https://github.com/clshortfuse/renodx/blob/main/LICENSE");
      }
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // Register QB before CoreMain initializes hooks, shader hashes, and runtime-wide format upgrades.
      Globals::SetGlobals(PROJECT_NAME, "Quantum Break Luma mod", "https://ko-fi.com/musaqh");
      Globals::VERSION = 1;

      QuantumBreakUpscaling::RegisterShaderHashes();

      RuntimeConfig::ConfigureSwapchainAndFormatUpgrades();

      game = new QuantumBreakGame();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
