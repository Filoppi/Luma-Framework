#define GAME_QUANTUM_BREAK 1

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../../../Shaders/Quantum Break/Includes/GameCBuffers.hlsl"
#include "../../Core/core.hpp"

namespace
{
   namespace Settings
   {
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
      }

      void Load(reshade::api::effect_runtime* runtime)
      {
         for (const Descriptor& setting : k_descriptors)
         {
            float& value = cb_luma_global_settings.GameSettings.*(setting.member);
            reshade::get_config_value(runtime, NAME, setting.label, value);
         }
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

class QuantumBreakGame final : public Game
{
public:
   void OnInit(bool async) override
   {
      (void)async;

      // Game constant buffer indices for Luma settings/data.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      Settings::Initialize();
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      Settings::Load(runtime);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      (void)device_data;

      ImGui::NewLine();

      Settings::DrawAll(runtime);
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
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
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
                  "\nNeutwo and Film Grain (from RenoDX) - Copyright (c) 2026 Carlos Lopez Jr. Licensed under MIT."
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
      Globals::SetGlobals(PROJECT_NAME, "Quantum Break Luma mod", "https://ko-fi.com/musaqh");
      Globals::VERSION = 1;

      RuntimeConfig::ConfigureSwapchainAndFormatUpgrades();

      game = new QuantumBreakGame();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
