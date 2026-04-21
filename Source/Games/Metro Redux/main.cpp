#define GAME_METRO_REDUX 1

#define DISABLE_AUTO_DEBUGGER 1

#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#define AVOID_INPUT_LOSS 1

// Needed because the game exclusively set CBuffers once on boot, and uses all of them!
#define ENABLE_AUTO_CBUFFER_RESTORATION 1

#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList<> shader_hashes_Tonemapper;

   // Slider-only placeholders. Wiring to shader/constants is intentionally left for later.
   float slider_bloom_strength = 1.0f;
   float slider_lens_dirt_strength = 1.0f;
   float slider_color_grading_strength = 1.0f;

   float slider_filmic_strength = 1.0f;
} // namespace

class MetroRedux final : public Game
{
public:
   void OnInit(bool async) override
   {

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // Game is not linear all the way
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('1'); 
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); // Game uses a 2.0 encode normally
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // Gamma correction looks correct. TODO: Experiment with different LUT sampling options instead.
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0'); // TODO: Figure out what went wrong with encoding and then implement UI_DRAW_TYPE 2
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // Contains out of gamut colors, pushing into invalid territory

      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.has_drawn_main_post_processing = false;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Metro 2033 Redux and Metro Last Light Redux Luma mod - about and credits section", ""); // ### Rename this ###
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Luma for Metro 2033 Redux and Metro Last Light Redux");
      Globals::VERSION = 1;

      shader_hashes_Tonemapper.pixel_shaders = {
         0x5C867D7E,
         0xF398A1ED,
         0xA453ADB1,
         0xFA7FE535,
      };

      redirected_shader_hashes["Tonemap"] =
      {
         "5C867D7E",
         "F398A1ED",
         "A453ADB1",
         "FA7FE535",
      };

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
         // reshade::api::format::r8g8b8a8_unorm,
         reshade::api::format::r8g8b8a8_unorm_srgb,
         // reshade::api::format::r8g8b8a8_typeless,
         // reshade::api::format::r8g8b8x8_unorm,
        //  reshade::api::format::r8g8b8x8_unorm_srgb,
         /*reshade::api::format::b8g8r8a8_unorm,
         reshade::api::format::b8g8r8a8_unorm_srgb,
         reshade::api::format::b8g8r8a8_typeless,
         reshade::api::format::b8g8r8x8_unorm,
         reshade::api::format::b8g8r8x8_unorm_srgb,
         reshade::api::format::b8g8r8x8_typeless,

         reshade::api::format::r11g11b10_float,*/
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      force_borderless = true; // These games have very bad window managment, we might need to force windowed mode and then reroute it as borderless
      force_ignore_dpi = true; // Not sure whether it makes a difference, but shouldn't hurt

      game = new MetroRedux();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}