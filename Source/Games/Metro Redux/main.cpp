#define GAME_METRO_REDUX 1

#define DISABLE_AUTO_DEBUGGER 1

#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#define AVOID_INPUT_LOSS 1

// Needed because the game exclusively set CBuffers once on boot, and uses all of them!
#define ENABLE_AUTO_CBUFFER_RESTORATION 1

#include <chrono>
#include <random>
#include "includes\settings.hpp"
#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList<> shader_hashes_Tonemapper;

   Luma::Settings::Settings settings = {
      new Luma::Settings::Section{
         .label = "Color Grading",
         .settings = {
            // new Luma::Settings::Setting{
            //    .key = "TonemapType",
            //    .binding = &cb_luma_global_settings.GameSettings.tonemap_type,
            //    .type = Luma::Settings::SettingValueType::INTEGER,
            //    .default_value = 1.f,
            //    .can_reset = true,
            //    .label = "Tone Map Type",
            //    .tooltip = "Tone mapping algorithm to use",
            //    .labels = {"Vanilla","Neutwo"},
            //    .min = 0.f,
            //    .max = 1.f,
            //    .is_enabled = []()
            //    { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; },
            //    .is_visible = []()
            //    { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; }
            // },
            // new Luma::Settings::Setting{
            //    .key = "ColorGradeHueCorrection",
            //    .binding = &cb_luma_global_settings.GameSettings.hue_correction_strength,
            //    .type = Luma::Settings::SettingValueType::FLOAT,
            //    .default_value = 0.3f,
            //    .can_reset = true,
            //    .label = "Hue Shift",
            //    .min = 0.f,
            //    .max = 1.f,
            //    .format = "%.2f",
            // },
            new Luma::Settings::Setting{
               .key = "ColorGradeExposure",
               .binding = &cb_luma_global_settings.GameSettings.exposure,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 1.f,
               .can_reset = true,
               .label = "Exposure",
               .min = 0.f,
               .max = 2.f,
               .format = "%.2f",
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeHighlights",
               .binding = &cb_luma_global_settings.GameSettings.highlights,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Highlights",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f;}
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeShadows",
               .binding = &cb_luma_global_settings.GameSettings.shadows,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Shadows",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeContrast",
               .binding = &cb_luma_global_settings.GameSettings.contrast,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Contrast",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeSaturation",
               .binding = &cb_luma_global_settings.GameSettings.saturation,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Saturation",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeHighlightSaturation",
               .binding = &cb_luma_global_settings.GameSettings.highlight_saturation,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Highlight Saturation",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeBlowout",
               .binding = &cb_luma_global_settings.GameSettings.blowout,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 0.f,
               .can_reset = true,
               .label = "Blowout",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeFlare",
               .binding = &cb_luma_global_settings.GameSettings.flare,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 0.f,
               .can_reset = true,
               .label = "Flare",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            }
         }
      },
            new Luma::Settings::Section{
         .label = "Post Processing",
         .settings = {
            new Luma::Settings::Setting{
               .key = "FXBloom",
               .binding = &cb_luma_global_settings.GameSettings.custom_bloom,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Bloom",
               .tooltip = "Bloom strength multiplier. Default is 50.",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; } // Scale down to 0.0-2.0 for the shader
            },
            new Luma::Settings::Setting{
               .key = "FXLensDirt",
               .binding = &cb_luma_global_settings.GameSettings.custom_lens_dirt,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Lens Dirt",
               .tooltip = "Lens dirt strength multiplier. Default is 50.",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; } // Scale down to 0.0-2.0 for the shader
            },
            new Luma::Settings::Setting{
               .key = "FXFilmGrain",
               .binding = &cb_luma_global_settings.GameSettings.custom_film_grain_strength,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 0.f,
               .can_reset = true,
               .label = "Film Grain",
               .tooltip = "Film grain strength multiplier. Original behavior is 0.",
               .min = 0.f,
               .max = 100.f,
               //.is_visible = []() { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; },
               .parse = [](float value)
               { return value * 0.01f; }
            },
            // new Luma::Settings::Setting{
            //    .key = "FXRCAS",
            //    .binding = &cb_luma_global_settings.GameSettings.custom_sharpness_strength,
            //    .type = Luma::Settings::SettingValueType::FLOAT,
            //    .default_value = 50.f,
            //    .can_reset = true,
            //    .label = "Sharpness",
            //    .tooltip = "RCAS strength multiplier. Default is 50, for Vanilla look set to 0.",
            //    .min = 0.f,
            //    .max = 100.f,
            //    .is_enabled = []()
            //    { return (sr_user_type != SR::UserType::None) && can_sharpen; },
            //    .is_visible = []()
            //    { return sr_user_type != SR::UserType::None; },
            //    .parse = [](float value)
            //    { return value * 0.01f; }
            // },

            new Luma::Settings::Setting{
               .key = "FXHDRVideos",
               .binding = &cb_luma_global_settings.GameSettings.custom_hdr_videos,
               .type = Luma::Settings::SettingValueType::INTEGER,
               .default_value = 1,
               .can_reset = true,
               .label = "HDR Videos",
               .tooltip = "Enable or disable HDR video playback.",
               .labels = {"Off", "Subtle", "Strong"},
               .min = 0,
               .max = 2,
               .is_visible = []()
               { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; }
            }
         }
      },
   };
} // namespace

class MetroRedux final : public Game
{
public:
   void OnInit(bool async) override
   {

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_AREA_SAMPLING", '1', true, false, "Use area sampling instead of vanilla bilinear sampling when downscaling SSAA", 1},
         {"ENABLE_FXAA", '1', true, false, "Enable Luma's improved FXAA implementation", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // Game is not linear all the way
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('1'); 
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0'); // Game uses a 2.0 encode normally
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // Gamma correction looks correct. TODO: Experiment with different LUT sampling options instead.
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0'); // TODO: Figure out what went wrong with encoding and then implement UI_DRAW_TYPE 2
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // Contains out of gamut colors, pushing into invalid territory
      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1'); // The game just clipped, so HDR is an extension of SDR (except for some shaders that we adjust)

      // ### Update these (find the right values) ###
      // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.has_drawn_main_post_processing = false;

      static std::mt19937 random_generator(std::chrono::system_clock::now().time_since_epoch().count());
      static auto random_range = static_cast<float>((std::mt19937::max)() - (std::mt19937::min)());
      cb_luma_global_settings.GameSettings.custom_random = static_cast<float>(random_generator() + (std::mt19937::min)()) / random_range;
      
   }

   void LoadConfigs() override
   {
      Luma::Settings::LoadSettings();
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      Luma::Settings::DrawSettings();
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
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
      texture_upgrade_formats = {
      //    // reshade::api::format::r8g8b8a8_unorm,
          //reshade::api::format::r8g8b8a8_unorm_srgb,
      //    // reshade::api::format::r8g8b8a8_typeless,
      //    // reshade::api::format::r8g8b8x8_unorm,
      //   //  reshade::api::format::r8g8b8x8_unorm_srgb,
      //    /*reshade::api::format::b8g8r8a8_unorm,
      //    reshade::api::format::b8g8r8a8_unorm_srgb,
      //    reshade::api::format::b8g8r8a8_typeless,
      //    reshade::api::format::b8g8r8x8_unorm,
      //    reshade::api::format::b8g8r8x8_unorm_srgb,
      //    reshade::api::format::b8g8r8x8_typeless,

      //    reshade::api::format::r11g11b10_float,*/
      };
      
         auto_texture_format_upgrade_shader_hashes[std::stoul("5C867D7E", nullptr, 16)] = { {0}, {} }; // Tonemap
         auto_texture_format_upgrade_shader_hashes[std::stoul("F398A1ED", nullptr, 16)] = { {0}, {} }; // Tonemap
         auto_texture_format_upgrade_shader_hashes[std::stoul("A453ADB1", nullptr, 16)] = { {0}, {} }; // Tonemap
         auto_texture_format_upgrade_shader_hashes[std::stoul("FA7FE535", nullptr, 16)] = { {0}, {} }; // Tonemap
         auto_texture_format_upgrade_shader_hashes[std::stoul("386FCE67", nullptr, 16)] = { {0}, {} }; // AA
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      force_borderless = true; // These games have very bad window managment, we might need to force windowed mode and then reroute it as borderless
      force_ignore_dpi = true; // Not sure whether it makes a difference, but shouldn't hurt

      Luma::Settings::Initialize(&settings);

      game = new MetroRedux();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}