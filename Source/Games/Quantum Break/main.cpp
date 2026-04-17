#define GAME_QUANTUM_BREAK 1

#include "../../../Shaders/Quantum Break/Includes/GameCBuffers.hlsl"
#include "..\..\Core\core.hpp"

class QuantumBreakGame final : public Game
{
public:
   void OnInit(bool async) override
   {
      (void)async;

      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      default_luma_global_game_settings.Highlights = cb_luma_global_settings.GameSettings.Highlights = 1.f;
      default_luma_global_game_settings.Shadows = cb_luma_global_settings.GameSettings.Shadows = 1.f;
      default_luma_global_game_settings.Contrast = cb_luma_global_settings.GameSettings.Contrast = 1.f;
      default_luma_global_game_settings.Saturation = cb_luma_global_settings.GameSettings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightSaturation = cb_luma_global_settings.GameSettings.HighlightSaturation = 1.f;
      default_luma_global_game_settings.Dechroma = cb_luma_global_settings.GameSettings.Dechroma = 0.f;
      default_luma_global_game_settings.Flare = cb_luma_global_settings.GameSettings.Flare = 0.f;
      default_luma_global_game_settings.LUTStrength = cb_luma_global_settings.GameSettings.LUTStrength = 1.f;
      default_luma_global_game_settings.LUTScaling = cb_luma_global_settings.GameSettings.LUTScaling = 1.f;
      default_luma_global_game_settings.GrainType = cb_luma_global_settings.GameSettings.GrainType = 1.f;
      default_luma_global_game_settings.GrainStrength = cb_luma_global_settings.GameSettings.GrainStrength = 1.f;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      reshade::get_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      reshade::get_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      reshade::get_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      reshade::get_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
      reshade::get_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
      reshade::get_config_value(runtime, NAME, "LUTStrength", cb_luma_global_settings.GameSettings.LUTStrength);
      reshade::get_config_value(runtime, NAME, "LUTScaling", cb_luma_global_settings.GameSettings.LUTScaling);
      reshade::get_config_value(runtime, NAME, "GrainType", cb_luma_global_settings.GameSettings.GrainType);
      reshade::get_config_value(runtime, NAME, "GrainStrength", cb_luma_global_settings.GameSettings.GrainStrength);

      cb_luma_global_settings.GameSettings.GrainType = cb_luma_global_settings.GameSettings.GrainType >= 0.5f ? 1.f : 0.f;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      (void)device_data;

      ImGui::NewLine();

      auto DrawFloatSlider = [&](const char* label, const char* config_key, float& value, float default_value,
                                 const char* tooltip = nullptr, float min_value = 0.f, float max_value = 2.f, const char* format = "%.2f")
      {
         if (ImGui::SliderFloat(label, &value, min_value, max_value, format))
         {
            reshade::set_config_value(runtime, NAME, config_key, value);
         }
         if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("%s", tooltip);
         }
         DrawResetButton(value, default_value, config_key, runtime);
      };

      auto DrawBoolSlider = [&](const char* label, const char* config_key, float& value, float default_value,
                                const char* tooltip = nullptr, const char* false_label = "Off", const char* true_label = "On")
      {
         int slider_value = value >= 0.5f ? 1 : 0;
         if (ImGui::SliderInt(label, &slider_value, 0, 1, slider_value == 0 ? false_label : true_label))
         {
            value = static_cast<float>(slider_value);
            reshade::set_config_value(runtime, NAME, config_key, value);
         }
         if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("%s", tooltip);
         }
         DrawResetButton(value, default_value, config_key, runtime);
      };

      DrawFloatSlider("Highlights", "Highlights",
                      cb_luma_global_settings.GameSettings.Highlights, default_luma_global_game_settings.Highlights);
      DrawFloatSlider("Shadows", "Shadows",
                      cb_luma_global_settings.GameSettings.Shadows, default_luma_global_game_settings.Shadows);
      DrawFloatSlider("Contrast", "Contrast",
                      cb_luma_global_settings.GameSettings.Contrast, default_luma_global_game_settings.Contrast);
      DrawFloatSlider("Saturation", "Saturation",
                      cb_luma_global_settings.GameSettings.Saturation, default_luma_global_game_settings.Saturation);
      DrawFloatSlider("Highlight Saturation", "HighlightSaturation",
                      cb_luma_global_settings.GameSettings.HighlightSaturation, default_luma_global_game_settings.HighlightSaturation);
      DrawFloatSlider("Dechroma", "Dechroma",
                      cb_luma_global_settings.GameSettings.Dechroma, default_luma_global_game_settings.Dechroma,
                      "Controls highlight desaturation due to overexposure.", 0.f, 1.f);
      DrawFloatSlider("Flare", "Flare",
                      cb_luma_global_settings.GameSettings.Flare, default_luma_global_game_settings.Flare,
                      "Flare/Glare Compensation", 0.f, 1.f);
      DrawFloatSlider("LUT Strength", "LUTStrength",
                      cb_luma_global_settings.GameSettings.LUTStrength, default_luma_global_game_settings.LUTStrength, nullptr, 0.f, 1.f);
      DrawFloatSlider("LUT Scaling", "LUTScaling",
                      cb_luma_global_settings.GameSettings.LUTScaling, default_luma_global_game_settings.LUTScaling,
                      "Scales the color grade LUT to full range when size is clamped.", 0.f, 1.f);
      DrawBoolSlider("Grain Type", "GrainType",
                     cb_luma_global_settings.GameSettings.GrainType, default_luma_global_game_settings.GrainType,
                     nullptr, "Vanilla", "Perceptual");
      DrawFloatSlider("Grain Strength", "GrainStrength",
                      cb_luma_global_settings.GameSettings.GrainStrength, default_luma_global_game_settings.GrainStrength, nullptr, 0.f, 1.f);
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

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // ### Check which of these are needed and remove the rest ###
      texture_upgrade_formats = {
         reshade::api::format::r11g11b10_float,
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | static_cast<uint32_t>(TextureFormatUpgrades2DSizeFilters::SwapchainResolution) | static_cast<uint32_t>(TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio);

      game = new QuantumBreakGame();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
