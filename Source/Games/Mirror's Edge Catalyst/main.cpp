// ### Rename this ###
#define GAME_MIRRORS_EDGE_CATALYST 1

#include "..\..\Core\core.hpp"

class MirrorsEdgeCatalyst final : public Game // ### Rename this to your game's name ###
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
      ImGui::Text("Mirror's Edge Catalyst Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Mirror's Edge Catalyst - Luma mod"); // ### Rename this ###
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      enable_indirect_texture_format_upgrades = true;
      enable_automatic_indirect_texture_format_upgrades = true;

      // ### Check which of these are needed and remove the rest ###
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         // reshade::api::format::r8g8b8a8_unorm_srgb,
         // reshade::api::format::r8g8b8a8_typeless,
         // reshade::api::format::r8g8b8x8_unorm,
         // reshade::api::format::r8g8b8x8_unorm_srgb,
         // reshade::api::format::b8g8r8a8_unorm,
         // reshade::api::format::b8g8r8a8_unorm_srgb,
         // reshade::api::format::b8g8r8a8_typeless,
         // reshade::api::format::b8g8r8x8_unorm,
         // reshade::api::format::b8g8r8x8_unorm_srgb,
         // reshade::api::format::b8g8r8x8_typeless,

         reshade::api::format::r11g11b10_float,
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      game = new MirrorsEdgeCatalyst();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}