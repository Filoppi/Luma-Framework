#define GAME_QUANTUM_BREAK 1

#include "..\..\Core\core.hpp"

class QuantumBreakGame final : public Game
{
public:
   void OnInit(bool async) override
   {
      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Quantum Break\" is developed by Musa and is open source and free.\nIf you enjoy it, consider donating.");

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

      ImGui::NewLine();
      ImGui::Text("Build Date:");
      ImGui::Text(__DATE__);
      ImGui::Text(__TIME__);
      ImGui::NewLine();

      ImGui::Text("Credits:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nShortFuse (Neutwo tonemapper)"
                  "");
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
