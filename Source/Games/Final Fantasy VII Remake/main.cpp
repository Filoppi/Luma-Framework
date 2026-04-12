#define GAME_FF7_REMAKE 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1
#ifdef NDEBUG
#define ALLOW_SHADERS_DUMPING 1
#endif

#include <chrono>
#include <random>
#include <d3d11TokenizedProgramFormat.hpp>
#include "includes\settings.hpp"
#include "..\..\Core\core.hpp"

namespace
{
   float2 projection_jitters = {0, 0};
   float vert_fov = 60.f * (M_PI / 180.f);
   float near_plane = 0.1f;
   std::unique_ptr<float4[]> downsample_buffer_data;
   std::unique_ptr<float4[]> upsample_buffer_data;

   // GTAO Constants
   constexpr size_t XE_GTAO_DEPTH_MIP_LEVELS = 5;
   constexpr UINT XE_GTAO_NUMTHREADS_X = 8;
   constexpr UINT XE_GTAO_NUMTHREADS_Y = 8;
   float g_xegtao_enable = 0.f;  // Changed from bool to float

   // GTAO Shader Hashes
   ShaderHashesList shader_hashes_AO_Temporal;
   ShaderHashesList shader_hashes_AO_Denoise1;
   ShaderHashesList shader_hashes_AO_Denoise2;

   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_Title;
   ShaderHashesList shader_hashes_MotionVectors;
   ShaderHashesList shader_hashes_DOF;
   ShaderHashesList shader_hashes_Motion_Blur;
   ShaderHashesList shader_hashes_Downsample_Bloom;
   ShaderHashesList shader_hashes_Bloom;
   ShaderHashesList shader_hashes_MenuSlowdown;
   ShaderHashesList shader_hashes_Tonemap;
   ShaderHashesList shader_hashes_Velocity_Flatten;
   ShaderHashesList shader_hashes_Velocity_Gather;
   ShaderHashesList shader_hashes_Output_HDR;
   ShaderHashesList shader_hashes_Output_SDR;

#if ENABLE_FRAMEGEN
   // Map from game PS hash to native shader key for Output HDR UI composition pass
   std::unordered_map<uint32_t, size_t> output_hdr_ui_shader_keys;
   // Map from game PS hash to native shader key for Output HDR tonemap pass
   std::unordered_map<uint32_t, size_t> output_hdr_tonemap_shader_keys;
   // Map from game PS hash to native shader key for Output SDR UI composition pass
   std::unordered_map<uint32_t, size_t> output_sdr_ui_shader_keys;
   // Map from game PS hash to native shader key for Output SDR tonemap pass
   std::unordered_map<uint32_t, size_t> output_sdr_tonemap_shader_keys;
#endif // ENABLE_FRAMEGEN

   const uint32_t CBPerViewGlobal_buffer_size = 4096;
   std::atomic<bool> can_sharpen = true;
   float enabled_dithering_fix = 1.f;
   float sr_custom_pre_exposure = 0.f; // Ignored at 0
   float ignore_warnings = 0.f;

   Luma::Settings::Settings settings = {
      new Luma::Settings::Section{
         .label = "Post Processing",
         .settings = {
            new Luma::Settings::Setting{
               .key = "EnableXEGTAO",
               .binding = &g_xegtao_enable,
               .type = Luma::Settings::SettingValueType::BOOLEAN,
               .default_value = 0.f,
               .can_reset = true,
               .label = "Enable GTAO",
               .tooltip = "Enable or disable GTAO ambient occlusion (Experimental). Default is Off."
            },
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
               .key = "FXVignette",
               .binding = &cb_luma_global_settings.GameSettings.custom_vignette,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Vignette",
               .tooltip = "Vignette strength multiplier. Default is 50.",
               .min = 0.f,
               .max = 100.f,
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "FXFilmGrain",
               .binding = &cb_luma_global_settings.GameSettings.custom_film_grain_strength,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Film Grain",
               .tooltip = "Film grain strength multiplier. Default is 50, for Vanilla look set to 0.",
               .min = 0.f,
               .max = 100.f,
               //.is_visible = []() { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; },
               .parse = [](float value)
               { return value * 0.02f; }
            },
            new Luma::Settings::Setting{
               .key = "FXRCAS",
               .binding = &cb_luma_global_settings.GameSettings.custom_sharpness_strength,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 50.f,
               .can_reset = true,
               .label = "Sharpness",
               .tooltip = "RCAS strength multiplier. Default is 50, for Vanilla look set to 0.",
               .min = 0.f,
               .max = 100.f,
               .is_enabled = []()
               { return (sr_user_type != SR::UserType::None) && can_sharpen; },
               .is_visible = []()
               { return sr_user_type != SR::UserType::None; },
               .parse = [](float value)
               { return value * 0.01f; }
            },

            new Luma::Settings::Setting{
               .key = "FXHDRVideos",
               .binding = &cb_luma_global_settings.GameSettings.custom_hdr_videos,
               .type = Luma::Settings::SettingValueType::BOOLEAN,
               .default_value = 1,
               .can_reset = true,
               .label = "HDR Videos",
               .tooltip = "Enable or disable HDR video playback. Default is On.",
               .min = 0,
               .max = 1,
               .is_visible = []()
               { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; }
            }
         }
      },
      new Luma::Settings::Section{
         .label = "Color Grading",
         .is_visible = []()
         { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; },
         .settings = {
            new Luma::Settings::Setting{
               .key = "TonemapType",
               .binding = &cb_luma_global_settings.GameSettings.tonemap_type,
               .type = Luma::Settings::SettingValueType::INTEGER,
               .default_value = 1.f,
               .can_reset = true,
               .label = "Tone Map Type",
               .tooltip = "Tone mapping algorithm to use",
               .labels = {"Vanilla","Neutwo"},
               .min = 0.f,
               .max = 1.f,
               .is_enabled = []()
               { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; },
               .is_visible = []()
               { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; }
            },
            new Luma::Settings::Setting{
               .key = "CustomLUTStrength",
               .binding = &cb_luma_global_settings.GameSettings.custom_lut_strength,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 92.f,
               .can_reset = true,
               .label = "LUT Strength",
               .tooltip = "LUT strength multiplier.",
               .min = 0.f,
               .max = 100.f,
               .is_visible = []()
               { return cb_luma_global_settings.DisplayMode == DisplayModeType::HDR; },
               .parse = [](float value)
               { return value * 0.01f; }
            },
            new Luma::Settings::Setting{
               .key = "ColorGradeHueCorrection",
               .binding = &cb_luma_global_settings.GameSettings.hue_correction_strength,
               .type = Luma::Settings::SettingValueType::FLOAT,
               .default_value = 0.3f,
               .can_reset = true,
               .label = "Hue Shift",
               .min = 0.f,
               .max = 1.f,
               .format = "%.2f",
            },
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
               .default_value = 20.f,
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
         .label = "Advanced Settings", 
         .settings = {
            new Luma::Settings::Setting{
               .key = "IgnoreWarnings", 
               .binding = &ignore_warnings, 
               .type = Luma::Settings::SettingValueType::BOOLEAN, 
               .can_reset = false, .label = "Ignore Warnings", 
               .tooltip = "Ignore warning messages. Default is Off."
            }, 
            new Luma::Settings::Setting{
               .key = "EnableDitheringFix", 
               .binding = &enabled_dithering_fix, 
               .type = Luma::Settings::SettingValueType::BOOLEAN, 
               .default_value = 1.f, 
               .can_reset = true, 
               .label = "Enable Dithering Fix (Experimental)", 
               .tooltip = "Enables a fix for dithering that can cause checkered patterns when using Super Resolution. Default is On, requires restart to take effect.",
            }
         }
      }
   };

   static inline bool NearZero(float v, float eps)
   {
      return std::fabs(v) <= eps;
   }

   static inline float ComputeNearPlane(const Math::Matrix44F& view_to_clip)
   {
      return (view_to_clip.m33 - view_to_clip.m32) / (view_to_clip.m22 - view_to_clip.m23);
   }

   static inline float ComputeFovY(const Math::Matrix44F& view_to_clip)
   {
      float eps = 1e-3f;
      if (!NearZero(view_to_clip.m11, eps))
      {
         return atan(1.f / view_to_clip.m11) * 2.0;
      }
      return 0.0f;
   }
} // namespace

struct GameDeviceDataFF7Remake final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   com_ptr<ID3D11Texture2D> sr_motion_vectors;
   com_ptr<ID3D11ShaderResourceView> sr_motion_vectors_srv; // NEW: SRV for Motion Vectors
   com_ptr<ID3D11Resource> sr_source_color;
   com_ptr<ID3D11Resource> depth_buffer;
   com_ptr<ID3D11RenderTargetView> sr_motion_vectors_rtv;
   std::unique_ptr<SR::SettingsData> sr_settings_data;
   std::unique_ptr<SR::SuperResolutionImpl::DrawData> sr_draw_data;
#endif // ENABLE_SR

   // NEW: GTAO Resources
   com_ptr<ID3D11Texture2D> gtao_working_depth;
   std::array<com_ptr<ID3D11UnorderedAccessView>, XE_GTAO_DEPTH_MIP_LEVELS> gtao_working_depth_uavs;
   com_ptr<ID3D11ShaderResourceView> gtao_working_depth_srv;
   
   com_ptr<ID3D11Texture2D> gtao_ao_edges;
   com_ptr<ID3D11UnorderedAccessView> gtao_ao_edges_uav;
   com_ptr<ID3D11ShaderResourceView> gtao_ao_edges_srv;
   
   UINT gtao_width = 0;
   UINT gtao_height = 0;

   void CleanGTAOResources()
   {
      gtao_working_depth = nullptr;
      for (auto& uav : gtao_working_depth_uavs) uav = nullptr;
      gtao_working_depth_srv = nullptr;
      gtao_ao_edges = nullptr;
      gtao_ao_edges_uav = nullptr;
      gtao_ao_edges_srv = nullptr;
      gtao_width = 0;
      gtao_height = 0;
   }

   // NEW: Bloom History Resources
   com_ptr<ID3D11Texture2D> prev_bloom_texture;
   com_ptr<ID3D11ShaderResourceView> prev_bloom_srv;
   bool first_bloom_frame = true;
   std::atomic<bool> camera_cut = false;
   std::atomic<bool> has_drawn_title = false;
   std::atomic<bool> has_drawn_taa = false;
   std::atomic<bool> has_drawn_upscaling = false;
   std::atomic<bool> has_drawn_gtao = false;
   std::atomic<bool> found_per_view_globals = false;
   std::atomic<bool> drs_active = false;
   std::atomic<uint32_t> jitterless_frames_count = 0;
   std::atomic<bool> is_in_menu = true; // Start in menu
   float2 upscaled_render_resolution = {1, 1};
   float resolution_scale = 1.0f;
   uint4 viewport_rect = {0, 0, 1, 1};

   // Output HDR two-pass split: intermediate hudless render target (R10G10B10A2_UNORM, PQ-encoded)
#if ENABLE_FRAMEGEN
   com_ptr<ID3D11Texture2D> output_hdr_hudless_texture;
   com_ptr<ID3D11ShaderResourceView> output_hdr_hudless_srv;
   com_ptr<ID3D11RenderTargetView> output_hdr_hudless_rtv;
   UINT output_hdr_hudless_width = 0;
   UINT output_hdr_hudless_height = 0;

   void CleanOutputHDRHudlessResources()
   {
      output_hdr_hudless_texture = nullptr;
      output_hdr_hudless_srv = nullptr;
      output_hdr_hudless_rtv = nullptr;
      output_hdr_hudless_width = 0;
      output_hdr_hudless_height = 0;
   }

   // Output SDR two-pass split: intermediate hudless render target (R10G10B10A2_UNORM, linear)
   com_ptr<ID3D11Texture2D> output_sdr_hudless_texture;
   com_ptr<ID3D11ShaderResourceView> output_sdr_hudless_srv;
   com_ptr<ID3D11RenderTargetView> output_sdr_hudless_rtv;
   UINT output_sdr_hudless_width = 0;
   UINT output_sdr_hudless_height = 0;

   void CleanOutputSDRHudlessResources()
   {
      output_sdr_hudless_texture = nullptr;
      output_sdr_hudless_srv = nullptr;
      output_sdr_hudless_rtv = nullptr;
      output_sdr_hudless_width = 0;
      output_sdr_hudless_height = 0;
   }
#endif // ENABLE_FRAMEGEN
};

class FF7Remake final : public Game
{
   static GameDeviceDataFF7Remake& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataFF7Remake*>(device_data.game);
   }

public:
   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(FF7Remake::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(FF7Remake::OnUnmapBufferRegion);
         // reshade::register_event<reshade::addon_event::update_buffer_region>(FF7Remake::OnUpdateBufferRegion);
         reshade::register_event<reshade::addon_event::create_device>(FF7Remake::OnCreateDevice);
      }
   }

   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('1');

      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs"), ShaderDefinition{"Luma_MotionVec_UE4_Decode", reshade::api::pipeline_subobject_type::pixel_shader});

      // Register GTAO compute shaders - defines come from Settings.hlsl, recompiled when changed
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R XeGTAO Prefilter Depths"), 
         ShaderDefinition("Luma_FF7R_XeGTAO_impl", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs"));
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R XeGTAO Main Pass"), 
         ShaderDefinition("Luma_FF7R_XeGTAO_impl", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs"));
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R XeGTAO Denoise Pass"), 
         ShaderDefinition("Luma_FF7R_XeGTAO_impl", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs"));

#if ENABLE_FRAMEGEN
      // Register Output HDR UI composition pixel shaders (one per permutation)
      // Each is compiled from the wrapper file with UI_COMPOSITION_PASS=1 and the hash-specific define
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI 922A71D1"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_922A71D1", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI A8EB118F"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_A8EB118F", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI 3A4D858E"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_3A4D858E", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI D950DA01"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_D950DA01", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI 5CD12E67"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_5CD12E67", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI 3B489929"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_3B489929", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI 8D04181D"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_8D04181D", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR UI 6846FF90"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_6846FF90", "1"}}});

      // Register Output HDR tonemap pixel shaders (one per permutation)
      // Each is compiled from the tonemap wrapper file with TONEMAP_PASS=1 and the hash-specific define
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM 922A71D1"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_922A71D1", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM A8EB118F"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_A8EB118F", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM 3A4D858E"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_3A4D858E", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM D950DA01"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_D950DA01", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM 5CD12E67"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_5CD12E67", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM 3B489929"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_3B489929", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM 8D04181D"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_8D04181D", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output HDR TM 6846FF90"),
         ShaderDefinition{"Luma_FF7R_Output_HDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_6846FF90", "1"}}});

      // Register Output SDR UI composition pixel shaders (one per permutation)
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI 506D5998"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_506D5998", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI F68D39B5"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_F68D39B5", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI BBB9CE42"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_BBB9CE42", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI 51E2B894"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_51E2B894", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI 803889E8"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_803889E8", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI D96EF76D"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_D96EF76D", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI 5C2D3A71"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_5C2D3A71", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR UI 66162229"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_UI_Composition", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"UI_COMPOSITION_PASS", "1"}, {"_66162229", "1"}}});

      // Register Output SDR tonemap pixel shaders (one per permutation)
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM 506D5998"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_506D5998", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM F68D39B5"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_F68D39B5", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM BBB9CE42"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_BBB9CE42", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM 51E2B894"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_51E2B894", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM 803889E8"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_803889E8", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM D96EF76D"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_D96EF76D", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM 5C2D3A71"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_5C2D3A71", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("FF7R Output SDR TM 66162229"),
         ShaderDefinition{"Luma_FF7R_Output_SDR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr,
            {{"TONEMAP_PASS", "1"}, {"_66162229", "1"}}});
#endif // ENABLE_FRAMEGEN

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      // Start from here, we then update it later in case the game rendered with black bars due to forcing a different aspect ratio from the swapchain buffer
      game_device_data.upscaled_render_resolution = device_data.output_resolution;
   }

   static bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version)
   {
      if (api != reshade::api::device_api::d3d11)
      {
         if (ignore_warnings == 0.f)
            MessageBoxA(game_window, "This mod only supports Direct3D 11.\nSet -dx11 in the launch options or uninstall the mod.\nIf you are sure the launch option is set correctly, set ignore warning in advanced settings.", NAME, MB_SETFOREGROUND);
      }
      return false;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataFF7Remake;
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size,
      reshade::api::pipeline_subobject_type type,
      uint64_t shader_hash = -1,
      const std::byte* shader_object = nullptr,
      size_t shader_object_size = 0) override
   {
      if (enabled_dithering_fix == 0.f)
         return nullptr;
      if (type != reshade::api::pipeline_subobject_type::pixel_shader && type != reshade::api::pipeline_subobject_type::compute_shader)
         return nullptr;

      // Pattern: cb1[139].z + literal l(3) used around the ishl sequence
      const std::vector<std::byte> dithering_anchor = {
         std::byte{0x2A}, std::byte{0x80}, std::byte{0x20}, std::byte{0x00}, // CONST_BUFFER, 4-comp SELECT_1 (.z)
         std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // cb index = 1
         std::byte{0x8B}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, // element = 139
         std::byte{0x01}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00}, // IMMEDIATE32 (1 component)
         std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}  // literal value = 3
      };

      // Scan for our dithering anchor
      std::vector<std::byte*> anchors = System::ScanMemoryForPattern(code, size, dithering_anchor);
      if (anchors.empty())
         return nullptr;

      // Confirm we are inside an 'ishl' (cb-pattern is +12 bytes from instruction start)
      const std::byte* cb_match = anchors[0];
      if (cb_match < code + 12)
         return nullptr;
      const std::byte* ishl_start = cb_match - 12;

      // Verify ishl opcode token = 0x08000029 -> bytes LE: 29 00 00 08
      if (ishl_start[0] != std::byte{0x29} || ishl_start[1] != std::byte{0x00} ||
          ishl_start[2] != std::byte{0x00} || ishl_start[3] != std::byte{0x08})
         return nullptr;

      constexpr size_t ishl_instruction_size = 32; // keep existing behavior
      constexpr size_t iadd_instruction_size = 32;

      const size_t injection_point_offset = static_cast<size_t>(ishl_start - code) + ishl_instruction_size;

      // check if has SampleGrad/sample_d_indexable instruction - if so, skip dithering fix
      bool has_samplegrad = false;
      {
         const std::vector<std::byte> samplegrad_pattern = {std::byte{uint8_t(D3D10_SB_OPCODE_SAMPLE_D)}, std::byte{0x00}};
         auto samplegrad_hits = System::ScanMemoryForPattern(code, size, samplegrad_pattern, true);
         if (!samplegrad_hits.empty())
         {
            // confirm it's start of instruction
            const size_t offset = static_cast<size_t>(samplegrad_hits[0] - code);
            if (!((offset & 0x3) != 0 || offset + 4 > size))
            {
               has_samplegrad = true;
            }
         }
      }

      // Additional check: Only inject if a DISCARD instruction exists after the ISHL.
      bool discard_found = false;
      if (injection_point_offset + sizeof(uint32_t) <= size)
      {
         const size_t window_begin = injection_point_offset;
         const size_t window_len = size - window_begin;

         const std::vector<std::byte> discard_pat = {std::byte{uint8_t(D3D10_SB_OPCODE_DISCARD)}, std::byte{0x00}};
         auto disc_hits = System::ScanMemoryForPattern(code + window_begin, window_len, discard_pat);

         for (const std::byte* pbyte : disc_hits)
         {
            const size_t offset = static_cast<size_t>(pbyte - code);
            if ((offset & 0x3) != 0 || offset + 4 > size)
               continue;

            const uint32_t opcode_tok0 = *reinterpret_cast<const uint32_t*>(code + offset);
            if (DECODE_D3D10_SB_OPCODE_TYPE(opcode_tok0) != D3D10_SB_OPCODE_DISCARD)
               continue;

            const uint32_t len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_tok0);
            if (len >= 1 && len <= MAX_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH)
            {
               discard_found = true;
               break;
            }
         }
      }

      if (discard_found || has_samplegrad)
      {
         const uint32_t dest_token = *reinterpret_cast<const uint32_t*>(ishl_start + 4);
         const uint32_t dest_index = *reinterpret_cast<const uint32_t*>(ishl_start + 8);

         auto dest_to_src_select1 = [](uint32_t token) -> uint32_t
         {
            const uint32_t mask = (token >> 4) & 0xF; // xyzw bits
            uint32_t comp = (mask & 0x1) ? 0u : (mask & 0x2) ? 1u
                                             : (mask & 0x4)  ? 2u
                                                             : 3u;
            token &= ~(0x3u << 2);
            token |= (2u << 2); // SELECT_1
            token &= ~(0x30u);
            token |= ((comp & 3u) << 4);
            return token;
         };

         const uint32_t src0_token = dest_to_src_select1(dest_token);
         const uint32_t src0_index = dest_index;

         const uint32_t cb_token_z = *reinterpret_cast<const uint32_t*>(cb_match + 0);
         const uint32_t cb_index = *reinterpret_cast<const uint32_t*>(cb_match + 4);
         const uint32_t cb_elem = *reinterpret_cast<const uint32_t*>(cb_match + 8);

         auto set_select1_comp = [](uint32_t token, uint32_t comp) -> uint32_t
         {
            token &= ~(0x3u << 2);
            token |= (2u << 2);          // SELECT_1
            token &= ~(0x30u);           // clear selected component (bits 5:4)
            token |= ((comp & 3u) << 4); // set to X/Y/Z/W = 0/1/2/3
            return token;
         };
         const uint32_t cb_token_w = set_select1_comp(cb_token_z, 3u);

         const size_t new_size = size + iadd_instruction_size;
         auto new_code = std::make_unique<std::byte[]>(new_size);

         std::memcpy(new_code.get(), code, injection_point_offset);

         uint32_t iadd[8] = {};
         iadd[0] = ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(8u) | D3D10_SB_OPCODE_IADD;
         iadd[1] = dest_token;
         iadd[2] = dest_index;
         iadd[3] = src0_token;
         iadd[4] = src0_index;
         iadd[5] = cb_token_w;
         iadd[6] = cb_index;
         iadd[7] = cb_elem;

         std::memcpy(new_code.get() + injection_point_offset, iadd, sizeof(iadd));
         std::memcpy(new_code.get() + injection_point_offset + sizeof(iadd), code + injection_point_offset, size - injection_point_offset);

         size += sizeof(iadd);
         return new_code;
      }

      // No DISCARD => remove dithering: find the LD reading t0 near ISHL and replace with MOV dest, l(1)
      {
         // ld[_aoffimmi(u,v,w)] dest[.mask], srcAddress[.swizzle], srcResource[.swizzle]
         const size_t window_begin = injection_point_offset;
         const size_t window_len = size - window_begin;

         const std::vector<std::byte> ld_pat = {std::byte{uint8_t(D3D10_SB_OPCODE_LD)}, std::byte{0x00}};
         auto ld_hits = System::ScanMemoryForPattern(code + window_begin, window_len, ld_pat);

         for (const std::byte* pbyte : ld_hits)
         {
            size_t offset_to_ld = static_cast<size_t>(pbyte - code);
            if ((offset_to_ld & 0x3) != 0 || offset_to_ld + 4 > size)
               continue;

            const uint32_t* ld_start = reinterpret_cast<const uint32_t*>(code + offset_to_ld);
            const uint32_t opcode_tok0 = ld_start[0];
            if (DECODE_D3D10_SB_OPCODE_TYPE(opcode_tok0) != D3D10_SB_OPCODE_LD)
               continue;

            const uint32_t old_len = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_tok0);
            if (old_len < 3 || offset_to_ld + old_len * 4 > size)
               continue;

            // check if extensions
            uint32_t is_ext = 0;
            while (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(ld_start[is_ext]) != 0)
            {
               is_ext++;
            }

            // Operand 0: dest (TEMP). If this is not TEMP we likely misaligned; skip this hit.
            const uint32_t* dest_operand_start = ld_start + 1 + is_ext;
            uint32_t index_represntation = DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(DECODE_D3D10_SB_RESOURCE_DIMENSION(dest_operand_start[0]), dest_operand_start[0]);
            if (index_represntation != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
               continue;
            is_ext = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(dest_operand_start[0]);
            ASSERT_ONCE(is_ext == 0); // should not be extended for LD
            if (DECODE_D3D10_SB_OPERAND_TYPE(dest_operand_start[0]) != D3D10_SB_OPERAND_TYPE_TEMP)
               continue;

            // Operand 1: uv source. Must be Temp
            const uint32_t* src_operand_start = dest_operand_start + 2 + is_ext;
            index_represntation = DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(DECODE_D3D10_SB_RESOURCE_DIMENSION(src_operand_start[0]), src_operand_start[0]);
            if (index_represntation != D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
               continue;
            is_ext = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(src_operand_start[0]);
            ASSERT_ONCE(is_ext == 0); // should not be extended for LD
            if (DECODE_D3D10_SB_OPERAND_TYPE(src_operand_start[0]) != D3D10_SB_OPERAND_TYPE_TEMP)
               continue;

            // Operand 2: resource. Must be Resource
            const uint32_t* res_operand_start = src_operand_start + 2 + is_ext;
            is_ext = DECODE_IS_D3D10_SB_OPERAND_EXTENDED(res_operand_start[0]);
            ASSERT_ONCE(is_ext == 0); // should not be extended for LD
            if (DECODE_D3D10_SB_OPERAND_TYPE(res_operand_start[0]) != D3D10_SB_OPERAND_TYPE_RESOURCE)
               continue;
            if (DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(res_operand_start[0]) != D3D10_SB_OPERAND_INDEX_1D)
               continue;
            if (DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(D3D10_SB_OPERAND_INDEX_1D, res_operand_start[0]) !=
                D3D10_SB_OPERAND_INDEX_IMMEDIATE32)
               continue;

            const uint32_t res_index = res_operand_start[1 + is_ext];
            if (res_index != 0)
               continue; // must be resource 0

            // Replace LD with MOV dest, l(1) and pad with NOPs
            auto new_code = std::make_unique<std::byte[]>(size);
            std::memcpy(new_code.get(), code, size);

            std::vector<uint32_t> repl;
            repl.reserve(old_len);

            // placeholder for MOV token0
            repl.push_back(0u);

            // copy dest operand
            repl.push_back(dest_operand_start[0]);
            repl.push_back(dest_operand_start[1]); // index
                                                   // repl.push_back(op_dest[i]);

            // Immediate32 operand: match dest component count (1 or 4)
            const uint32_t dest_tok0 = dest_operand_start[0];
            const auto dest_numc = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(dest_tok0);

            if (dest_numc == D3D10_SB_OPERAND_1_COMPONENT)
            {
               const uint32_t imm_tok =
                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                  ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_1_COMPONENT);
               repl.push_back(imm_tok);
               repl.push_back(0x3F000000u); // 1.0f
            }
            else
            {
               const uint32_t imm_tok =
                  ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32) |
                  ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                  ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE) |
                  D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE;
               repl.push_back(imm_tok);
               repl.push_back(0x3F000000u);
               repl.push_back(0x3F000000u);
               repl.push_back(0x3F000000u);
               repl.push_back(0x3F000000u);
            }

            // finalize MOV token0
            const uint32_t mov_len = static_cast<uint32_t>(repl.size());
            repl[0] = ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(mov_len) | D3D10_SB_OPCODE_MOV;

            // pad with NOPs to preserve original instruction size
            while (repl.size() < old_len)
               repl.push_back(ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1u) | D3D10_SB_OPCODE_NOP);

            std::memcpy(reinterpret_cast<uint8_t*>(new_code.get()) + offset_to_ld, repl.data(), old_len * sizeof(uint32_t));
            return new_code;
         }
      }

      // No LD t0 near ISHL found; do nothing for now.
      return nullptr;
   }

   void PrepareDrawForEarlyUpscaling(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      auto& game_device_data = FF7Remake::GetGameDeviceData(device_data);

      // check if the render target texture is of output resolution size and store if true in a flag
      bool is_upscaled_render_resolution = false;
      com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
      com_ptr<ID3D11DepthStencilView> depth_stencil_view;
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
      // Just a guess... always only check the first render target
      if (render_target_views[0].get() != nullptr)
      {
         com_ptr<ID3D11Resource> render_target_resource;
         render_target_views[0]->GetResource(&render_target_resource);

         com_ptr<ID3D11Texture2D> render_target_texture;
         HRESULT hr = render_target_resource->QueryInterface(&render_target_texture);
         ASSERT_ONCE(SUCCEEDED(hr));
         if (render_target_texture.get() == nullptr)
         {
            // If the render target is not a texture 2D, it's not what we are looking for, and we have no reason to continue
            return;
         }

         D3D11_TEXTURE2D_DESC render_target_desc;
         render_target_texture->GetDesc(&render_target_desc);
         if (std::lrintf(game_device_data.upscaled_render_resolution.x) == render_target_desc.Width && std::lrintf(game_device_data.upscaled_render_resolution.y) == render_target_desc.Height)
         {
            is_upscaled_render_resolution = true;
         }
      }

      // set scissor and viewport to the output resolution (scissor are probably not necessary, but they might have been set already).
      // For bloom/exposure mips passes, we don't need to change the viewport as their size is independent from the current res scale.
      if (is_upscaled_render_resolution)
      {
         D3D11_VIEWPORT viewport;
         viewport.Width = std::lrintf(game_device_data.upscaled_render_resolution.x);
         viewport.Height = std::lrintf(game_device_data.upscaled_render_resolution.y);
         viewport.MinDepth = 0.0f;
         viewport.MaxDepth = 1.0f;
         viewport.TopLeftX = 0.0f;
         viewport.TopLeftY = 0.0f;
         native_device_context->RSSetViewports(1, &viewport);
         D3D11_RECT scissor_rect;
         scissor_rect.left = 0;
         scissor_rect.top = 0;
         scissor_rect.right = std::lrintf(game_device_data.upscaled_render_resolution.x);
         scissor_rect.bottom = std::lrintf(game_device_data.upscaled_render_resolution.y);
         native_device_context->RSSetScissorRects(1, &scissor_rect);
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // ============================================================================
      // GTAO Implementation
      // ============================================================================
      bool gtao_enabled = g_xegtao_enable != 0.f;
      // Replace the temporal AO pass with GTAO compute shaders
      if (gtao_enabled && original_shader_hashes.Contains(shader_hashes_AO_Temporal))
      {
         game_device_data.has_drawn_gtao = true;
         // Check if compute shaders are available
         auto it_prefilter = device_data.native_compute_shaders.find(CompileTimeStringHash("FF7R XeGTAO Prefilter Depths"));
         auto it_main = device_data.native_compute_shaders.find(CompileTimeStringHash("FF7R XeGTAO Main Pass"));
         
         if (it_prefilter == device_data.native_compute_shaders.end() || 
             it_main == device_data.native_compute_shaders.end() ||
             !it_prefilter->second || !it_main->second)
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get the original SRVs bound to the pixel shader
         com_ptr<ID3D11ShaderResourceView> ps_srvs[6];
         native_device_context->PSGetShaderResources(0, 6, &ps_srvs[0]);
         
         // t1 = Normals, t2 = Depth (based on original shader resource layout)
         com_ptr<ID3D11ShaderResourceView> srv_normals = ps_srvs[1];
         com_ptr<ID3D11ShaderResourceView> srv_depth = ps_srvs[2];
         
         if (!srv_depth || !srv_normals)
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get render target to determine resolution and for debug output
         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
         
         if (!rtvs[0])
         {
            return DrawOrDispatchOverrideType::None;
         }
         
         com_ptr<ID3D11Resource> rtv_resource;
         rtvs[0]->GetResource(&rtv_resource);
         com_ptr<ID3D11Texture2D> rtv_texture;
         rtv_resource->QueryInterface(&rtv_texture);
         
         D3D11_TEXTURE2D_DESC rtv_desc;
         rtv_texture->GetDesc(&rtv_desc);
         
         // Use render resolution from global buffer (updated in OnUnmapBufferRegion from cb1[122])
         // This is set early in the frame before any draw calls
         UINT width = static_cast<UINT>(device_data.render_resolution.x);
         UINT height = static_cast<UINT>(device_data.render_resolution.y);
         
         // Fallback to render target size if render_resolution not set
         if (width == 0 || height == 0)
         {
            width = rtv_desc.Width;
            height = rtv_desc.Height;
         }
         
         // Get constant buffers that the shader needs
         com_ptr<ID3D11Buffer> ps_cbs[2];
         native_device_context->PSGetConstantBuffers(0, 2, &ps_cbs[0]);
         
         // Create GTAO resources if needed
         if (!CreateGTAOResources(native_device, game_device_data, width, height))
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get sampler (should be point sampler)
         com_ptr<ID3D11SamplerState> ps_samplers[1];
         native_device_context->PSGetSamplers(0, 1, &ps_samplers[0]);

         // Set Luma constant buffers before caching state (like UE4 does)
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Cache pipeline state
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Unbind RTVs since we're doing compute
         ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
         native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);

         // ========================================
         // Pass 1: Prefilter Depths (16x16 blocks)
         // ========================================
         {
            native_device_context->CSSetShader(it_prefilter->second.get(), nullptr, 0);
            
            // Bind depth as SRV (t0)
            ID3D11ShaderResourceView* srvs[] = { srv_depth.get() };
            native_device_context->CSSetShaderResources(0, 1, srvs);
            
            // Bind working depth UAVs (u0-u4)
            ID3D11UnorderedAccessView* uavs[XE_GTAO_DEPTH_MIP_LEVELS];
            for (UINT i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i)
               uavs[i] = game_device_data.gtao_working_depth_uavs[i].get();
            native_device_context->CSSetUnorderedAccessViews(0, XE_GTAO_DEPTH_MIP_LEVELS, uavs, nullptr);
            
            // Set sampler
            ID3D11SamplerState* samplers[] = { ps_samplers[0].get() };
            native_device_context->CSSetSamplers(0, 1, samplers);
            
            // Set constant buffers (cb0, cb1 for depth linearization)
            ID3D11Buffer* cbs[] = { ps_cbs[0].get(), ps_cbs[1].get() };
            native_device_context->CSSetConstantBuffers(0, 2, cbs);
            
            // Dispatch: each thread processes 2x2, 8x8 threads per group = 16x16 pixels per group
            UINT dispatch_x = (width + 15) / 16;
            UINT dispatch_y = (height + 15) / 16;
            native_device_context->Dispatch(dispatch_x, dispatch_y, 1);
         }

         // Unbind prefilter UAVs before main pass (resource hazard)
         {
            ID3D11UnorderedAccessView* null_uavs[XE_GTAO_DEPTH_MIP_LEVELS] = {};
            native_device_context->CSSetUnorderedAccessViews(0, XE_GTAO_DEPTH_MIP_LEVELS, null_uavs, nullptr);
         }

         // ========================================
         // Pass 2: Main GTAO Pass
         // ========================================
         {
            native_device_context->CSSetShader(it_main->second.get(), nullptr, 0);
            
            // Bind working depth SRV (t0) and normals SRV (t1)
            ID3D11ShaderResourceView* srvs[] = { game_device_data.gtao_working_depth_srv.get(), srv_normals.get() };
            native_device_context->CSSetShaderResources(0, 2, srvs);
            
            // Bind AO+edges UAV (u0)
            ID3D11UnorderedAccessView* uavs[] = { game_device_data.gtao_ao_edges_uav.get() };
            native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            
            // Dispatch: 8x8 threads per group, 1 pixel per thread
            UINT dispatch_x = (width + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X;
            UINT dispatch_y = (height + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y;
            native_device_context->Dispatch(dispatch_x, dispatch_y, 1);
         }

         // Unbind main pass UAV before restore
         {
            ID3D11UnorderedAccessView* null_uavs[] = { nullptr };
            native_device_context->CSSetUnorderedAccessViews(0, 1, null_uavs, nullptr);
         }

         // Restore state - this handles all unbinding automatically
         draw_state_stack.Restore(native_device_context, device_data.uav_max_count);
         compute_state_stack.Restore(native_device_context, device_data.uav_max_count);
         
#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context;
            trace_draw_call_data.custom_name = "GTAO Prefilter + Main Pass";
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
#endif

         return DrawOrDispatchOverrideType::Replaced;
      }

      // Skip first denoiser pass when GTAO is enabled
      if (gtao_enabled && original_shader_hashes.Contains(shader_hashes_AO_Denoise1))
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      // Replace second (final) denoiser pass with GTAO denoise
      if (gtao_enabled && original_shader_hashes.Contains(shader_hashes_AO_Denoise2))
      {
         if (!game_device_data.gtao_ao_edges_srv)
         {
            return DrawOrDispatchOverrideType::None;
         }

         auto it_denoise = device_data.native_compute_shaders.find(CompileTimeStringHash("FF7R XeGTAO Denoise Pass"));
         
         if (it_denoise == device_data.native_compute_shaders.end() || !it_denoise->second)
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get the original render target
         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
         
         if (!rtvs[0])
         {
            return DrawOrDispatchOverrideType::None;
         }

         com_ptr<ID3D11Resource> rtv_resource;
         rtvs[0]->GetResource(&rtv_resource);
         com_ptr<ID3D11Texture2D> rtv_texture;
         rtv_resource->QueryInterface(&rtv_texture);
         
         D3D11_TEXTURE2D_DESC rtv_desc;
         rtv_texture->GetDesc(&rtv_desc);

         // Use the same resolution as the GTAO resources that were created in the main pass
         // This ensures consistency between passes
         UINT width = game_device_data.gtao_width;
         UINT height = game_device_data.gtao_height;
         
         if (width == 0 || height == 0)
         {
            width = rtv_desc.Width;
            height = rtv_desc.Height;
         }

         // Get sampler
         com_ptr<ID3D11SamplerState> ps_samplers[1];
         native_device_context->PSGetSamplers(0, 1, &ps_samplers[0]);
         
         // Get constant buffers that the shader needs
         com_ptr<ID3D11Buffer> ps_cbs[2];
         native_device_context->PSGetConstantBuffers(0, 2, &ps_cbs[0]);

         // Check if the original render target supports UAV
         bool rtv_supports_uav = (rtv_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
         
         com_ptr<ID3D11Texture2D> output_texture;
         com_ptr<ID3D11UnorderedAccessView> output_uav;
         bool need_copy = false;

         if (rtv_supports_uav)
         {
            output_texture = rtv_texture;
            
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.Format = rtv_desc.Format;
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Texture2D.MipSlice = 0;
            
            HRESULT hr = native_device->CreateUnorderedAccessView(output_texture.get(), &uav_desc, &output_uav);
            if (FAILED(hr))
            {
               return DrawOrDispatchOverrideType::None;
            }
         }
         else
         {
            D3D11_TEXTURE2D_DESC temp_desc = rtv_desc;
            temp_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            
            HRESULT hr = native_device->CreateTexture2D(&temp_desc, nullptr, &output_texture);
            if (FAILED(hr))
            {
               return DrawOrDispatchOverrideType::None;
            }
            
            hr = native_device->CreateUnorderedAccessView(output_texture.get(), nullptr, &output_uav);
            if (FAILED(hr))
            {
               return DrawOrDispatchOverrideType::None;
            }
            
            need_copy = true;
         }

         // Set Luma constant buffers
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Cache state
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Unbind RTVs
         ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
         native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);

         // Run denoise compute shader
         native_device_context->CSSetShader(it_denoise->second.get(), nullptr, 0);
         
         ID3D11ShaderResourceView* srvs[] = { game_device_data.gtao_ao_edges_srv.get() };
         native_device_context->CSSetShaderResources(0, 1, srvs);
         
         ID3D11UnorderedAccessView* uavs[] = { output_uav.get() };
         native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
         
         ID3D11SamplerState* samplers[] = { ps_samplers[0].get() };
         native_device_context->CSSetSamplers(0, 1, samplers);
         
         // Set constant buffers (cb0, cb1 for shader parameters)
         ID3D11Buffer* cbs[] = { ps_cbs[0].get(), ps_cbs[1].get() };
         native_device_context->CSSetConstantBuffers(0, 2, cbs);
         
         // Dispatch: 8x8 threads, each thread processes 2 horizontal pixels (when not in debug mode)
         // In debug mode, each thread processes 1 pixel
         UINT dispatch_x = (width + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X;
         UINT dispatch_y = (height + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y;
         native_device_context->Dispatch(dispatch_x, dispatch_y, 1);

         // Restore state
         draw_state_stack.Restore(native_device_context, device_data.uav_max_count);
         compute_state_stack.Restore(native_device_context, device_data.uav_max_count);
         
         // Copy result back if needed
         if (need_copy)
         {
            native_device_context->CopyResource(rtv_texture.get(), output_texture.get());
         }

#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context;
            trace_draw_call_data.custom_name = "GTAO Denoise Pass";
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
#endif

         return DrawOrDispatchOverrideType::Replaced;
      }

      // ============================================================================
      // END GTAO Implementation
      // ============================================================================

      // Nothing more to do after tonemapping
      if (device_data.has_drawn_main_post_processing)
      {
         return DrawOrDispatchOverrideType::None;
      }

      const bool is_taa = original_shader_hashes.Contains(shader_hashes_TAA);
      if (is_taa)
      {
         game_device_data.has_drawn_taa = true;
         device_data.taa_detected = true;
      }

      // Nothing to do if TAA isn't enabled
      if (!game_device_data.has_drawn_taa)
      {
         return DrawOrDispatchOverrideType::None;
      }

      bool is_tonemapping_sdr = !is_taa && original_shader_hashes.Contains(shader_hashes_Output_SDR);
      bool is_tonemapping_hdr = !is_taa && original_shader_hashes.Contains(shader_hashes_Output_HDR);
      const bool is_tonemapping = is_tonemapping_sdr || is_tonemapping_hdr;
      if (is_tonemapping)
      {
         DisplayModeType current_display_mode = cb_luma_global_settings.DisplayMode;
         bool display_mode_changed = (is_tonemapping_sdr && current_display_mode == DisplayModeType::HDR) || (is_tonemapping_hdr && current_display_mode == DisplayModeType::SDR);
         bool enable_hdr = is_tonemapping_hdr && current_display_mode != DisplayModeType::HDR;
         if (display_mode_changed)
         {
            if (enable_hdr)
            {
               Display::SetHDREnabled(game_window);
               bool dummy_bool;
               Display::IsHDRSupportedAndEnabled(game_window, dummy_bool, hdr_enabled_display, nullptr);
               if (!reshade::get_config_value(nullptr, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite) || cb_luma_global_settings.ScenePeakWhite <= 0.f)
               {
                  cb_luma_global_settings.ScenePeakWhite = device_data.default_user_peak_white;
               }
               if (!reshade::get_config_value(nullptr, NAME, "ScenePaperWhite", cb_luma_global_settings.ScenePaperWhite))
               {
                  cb_luma_global_settings.ScenePaperWhite = default_paper_white;
               }
               if (!reshade::get_config_value(nullptr, NAME, "UIPaperWhite", cb_luma_global_settings.UIPaperWhite))
               {
                  cb_luma_global_settings.UIPaperWhite = default_paper_white;
               }
            }
            else
            {
               cb_luma_global_settings.ScenePeakWhite = srgb_white_level;
               cb_luma_global_settings.ScenePaperWhite = srgb_white_level;
               cb_luma_global_settings.UIPaperWhite = srgb_white_level;
            }

            cb_luma_global_settings.DisplayMode = is_tonemapping_hdr ? DisplayModeType::HDR : DisplayModeType::SDR;
            device_data.cb_luma_global_settings_dirty = true;
         }
         game_device_data.has_drawn_upscaling = true;       // This pass will make upscaling happen if DLSS didn't already do it before
         device_data.has_drawn_main_post_processing = true; // Post processing is finished, nothing more to fix
      }

      // ============================================================================
      // Output two-pass split: tonemap to intermediate, then composite UI
      // ============================================================================
#if ENABLE_FRAMEGEN
      if (is_tonemapping_hdr)
      {
         // Look up UI composition native PS for the current permutation
         uint32_t current_ps_hash = static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]);
         auto ui_key_it = output_hdr_ui_shader_keys.find(current_ps_hash);
         auto tm_key_it = output_hdr_tonemap_shader_keys.find(current_ps_hash);
         if (ui_key_it == output_hdr_ui_shader_keys.end() || tm_key_it == output_hdr_tonemap_shader_keys.end())
         {
            // Unknown permutation — fall through to single-pass
            return DrawOrDispatchOverrideType::None;
         }

         auto ui_ps_it = device_data.native_pixel_shaders.find(ui_key_it->second);
         auto tm_ps_it = device_data.native_pixel_shaders.find(tm_key_it->second);
         auto copy_vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
         if (ui_ps_it == device_data.native_pixel_shaders.end() || !ui_ps_it->second.get()
             || tm_ps_it == device_data.native_pixel_shaders.end() || !tm_ps_it->second.get()
             || copy_vs_it == device_data.native_vertex_shaders.end() || !copy_vs_it->second.get())
         {
            // Native shaders not compiled yet — fall through to single-pass
            return DrawOrDispatchOverrideType::None;
         }

         // Get the game's current RTV to determine dimensions and to restore later
         com_ptr<ID3D11RenderTargetView> original_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> original_dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &original_rtvs[0], &original_dsv);

         if (!original_rtvs[0].get())
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get render target dimensions
         com_ptr<ID3D11Resource> rtv_resource;
         original_rtvs[0]->GetResource(&rtv_resource);
         com_ptr<ID3D11Texture2D> rtv_texture;
         rtv_resource->QueryInterface(&rtv_texture);
         D3D11_TEXTURE2D_DESC rtv_desc;
         rtv_texture->GetDesc(&rtv_desc);

         // Create/recreate intermediate hudless RT if dimensions changed
         if (!game_device_data.output_hdr_hudless_texture.get()
             || game_device_data.output_hdr_hudless_width != rtv_desc.Width
             || game_device_data.output_hdr_hudless_height != rtv_desc.Height)
         {
            game_device_data.CleanOutputHDRHudlessResources();

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = rtv_desc.Width;
            desc.Height = rtv_desc.Height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            HRESULT hr = native_device->CreateTexture2D(&desc, nullptr, &game_device_data.output_hdr_hudless_texture);
            ASSERT_ONCE(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
               hr = native_device->CreateRenderTargetView(game_device_data.output_hdr_hudless_texture.get(), nullptr, &game_device_data.output_hdr_hudless_rtv);
               ASSERT_ONCE(SUCCEEDED(hr));
               hr = native_device->CreateShaderResourceView(game_device_data.output_hdr_hudless_texture.get(), nullptr, &game_device_data.output_hdr_hudless_srv);
               ASSERT_ONCE(SUCCEEDED(hr));
            }

            if (FAILED(hr))
            {
               game_device_data.CleanOutputHDRHudlessResources();
               return DrawOrDispatchOverrideType::None;
            }

            game_device_data.output_hdr_hudless_width = rtv_desc.Width;
            game_device_data.output_hdr_hudless_height = rtv_desc.Height;
         }

         // Cache GPU state
         com_ptr<ID3D11VertexShader> prev_vs;
         com_ptr<ID3D11PixelShader> prev_ps;
         native_device_context->VSGetShader(&prev_vs, nullptr, nullptr);
         native_device_context->PSGetShader(&prev_ps, nullptr, nullptr);
         D3D11_PRIMITIVE_TOPOLOGY prev_topology;
         native_device_context->IAGetPrimitiveTopology(&prev_topology);

         // ===== Pass 1: Tonemap to intermediate (hudless) =====
         // Redirect output to intermediate RT and set the native tonemap PS (with TONEMAP_PASS=1).
         // All other state (VS, SRVs, CBs, samplers) stays as-is from the game.
         ID3D11RenderTargetView* const hudless_rtv = game_device_data.output_hdr_hudless_rtv.get();
         native_device_context->OMSetRenderTargets(1, &hudless_rtv, nullptr);
         native_device_context->PSSetShader(tm_ps_it->second.get(), nullptr, 0);

         // Ensure Luma CBs are bound for the PS stage
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Issue the game's original draw call with our tonemap PS
         if (original_draw_dispatch_func)
         {
            (*original_draw_dispatch_func)();
         }
         else
         {
            native_device_context->DrawIndexed(3, 6, 0);
         }

         // ===== Pass 2: UI Composition to final RT =====
         // Restore original RTV
         ID3D11RenderTargetView* const* restored_rtvs = (ID3D11RenderTargetView**)std::addressof(original_rtvs[0]);
         native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restored_rtvs, original_dsv.get());

         // Rebind t1 (colorTex register) to the intermediate hudless SRV
         ID3D11ShaderResourceView* const hudless_srv = game_device_data.output_hdr_hudless_srv.get();
         native_device_context->PSSetShaderResources(1, 1, &hudless_srv);

         // Set UI composition PS and Copy VS
         native_device_context->PSSetShader(ui_ps_it->second.get(), nullptr, 0);
         native_device_context->VSSetShader(copy_vs_it->second.get(), nullptr, 0);
         native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

         // Ensure Luma CBs are bound for the PS stage
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Draw fullscreen quad
         native_device_context->Draw(4, 0);

         // Restore VS, PS, topology
         native_device_context->VSSetShader(prev_vs.get(), nullptr, 0);
         native_device_context->PSSetShader(prev_ps.get(), nullptr, 0);
         native_device_context->IASetPrimitiveTopology(prev_topology);

         // Restore original SRV on t1 (so we don't leave the intermediate bound)
         com_ptr<ID3D11ShaderResourceView> original_color_srv;
         // We don't have the original t1 cached, but it's no longer needed since post-processing is done.
         // Just unbind to be safe.
         ID3D11ShaderResourceView* null_srv = nullptr;
         native_device_context->PSSetShaderResources(1, 1, &null_srv);

#if DEVELOPMENT
         {
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = "Output HDR Two-Pass (Tonemap + UI Composition)";
               cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
            }
         }
#endif

         return DrawOrDispatchOverrideType::Replaced;
      }

      // ============================================================================
      // Output SDR two-pass split: tonemap to intermediate, then composite UI
      // ============================================================================
      if (is_tonemapping_sdr)
      {
         // Look up UI composition and tonemap native PS for the current SDR permutation
         uint32_t current_ps_hash = static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]);
         auto ui_key_it = output_sdr_ui_shader_keys.find(current_ps_hash);
         auto tm_key_it = output_sdr_tonemap_shader_keys.find(current_ps_hash);
         if (ui_key_it == output_sdr_ui_shader_keys.end() || tm_key_it == output_sdr_tonemap_shader_keys.end())
         {
            // Unknown permutation — fall through to single-pass
            return DrawOrDispatchOverrideType::None;
         }

         auto ui_ps_it = device_data.native_pixel_shaders.find(ui_key_it->second);
         auto tm_ps_it = device_data.native_pixel_shaders.find(tm_key_it->second);
         auto copy_vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
         if (ui_ps_it == device_data.native_pixel_shaders.end() || !ui_ps_it->second.get()
             || tm_ps_it == device_data.native_pixel_shaders.end() || !tm_ps_it->second.get()
             || copy_vs_it == device_data.native_vertex_shaders.end() || !copy_vs_it->second.get())
         {
            // Native shaders not compiled yet — fall through to single-pass
            return DrawOrDispatchOverrideType::None;
         }

         // Get the game's current RTV to determine dimensions and to restore later
         com_ptr<ID3D11RenderTargetView> original_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> original_dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &original_rtvs[0], &original_dsv);

         if (!original_rtvs[0].get())
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get render target dimensions
         com_ptr<ID3D11Resource> rtv_resource;
         original_rtvs[0]->GetResource(&rtv_resource);
         com_ptr<ID3D11Texture2D> rtv_texture;
         rtv_resource->QueryInterface(&rtv_texture);
         D3D11_TEXTURE2D_DESC rtv_desc;
         rtv_texture->GetDesc(&rtv_desc);

         // Create/recreate intermediate hudless RT if dimensions changed
         if (!game_device_data.output_sdr_hudless_texture.get()
             || game_device_data.output_sdr_hudless_width != rtv_desc.Width
             || game_device_data.output_sdr_hudless_height != rtv_desc.Height)
         {
            game_device_data.CleanOutputSDRHudlessResources();

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = rtv_desc.Width;
            desc.Height = rtv_desc.Height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            HRESULT hr = native_device->CreateTexture2D(&desc, nullptr, &game_device_data.output_sdr_hudless_texture);
            ASSERT_ONCE(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
               hr = native_device->CreateRenderTargetView(game_device_data.output_sdr_hudless_texture.get(), nullptr, &game_device_data.output_sdr_hudless_rtv);
               ASSERT_ONCE(SUCCEEDED(hr));
               hr = native_device->CreateShaderResourceView(game_device_data.output_sdr_hudless_texture.get(), nullptr, &game_device_data.output_sdr_hudless_srv);
               ASSERT_ONCE(SUCCEEDED(hr));
            }

            if (FAILED(hr))
            {
               game_device_data.CleanOutputSDRHudlessResources();
               return DrawOrDispatchOverrideType::None;
            }

            game_device_data.output_sdr_hudless_width = rtv_desc.Width;
            game_device_data.output_sdr_hudless_height = rtv_desc.Height;
         }

         // Cache GPU state
         com_ptr<ID3D11VertexShader> prev_vs;
         com_ptr<ID3D11PixelShader> prev_ps;
         native_device_context->VSGetShader(&prev_vs, nullptr, nullptr);
         native_device_context->PSGetShader(&prev_ps, nullptr, nullptr);
         D3D11_PRIMITIVE_TOPOLOGY prev_topology;
         native_device_context->IAGetPrimitiveTopology(&prev_topology);

         // ===== Pass 1: Tonemap to intermediate (hudless) =====
         ID3D11RenderTargetView* const hudless_rtv = game_device_data.output_sdr_hudless_rtv.get();
         native_device_context->OMSetRenderTargets(1, &hudless_rtv, nullptr);
         native_device_context->PSSetShader(tm_ps_it->second.get(), nullptr, 0);

         // Ensure Luma CBs are bound for the PS stage
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Issue the game's original draw call with our tonemap PS
         if (original_draw_dispatch_func)
         {
            (*original_draw_dispatch_func)();
         }
         else
         {
            native_device_context->DrawIndexed(3, 6, 0);
         }

         // ===== Pass 2: UI Composition to final RT =====
         ID3D11RenderTargetView* const* restored_rtvs = (ID3D11RenderTargetView**)std::addressof(original_rtvs[0]);
         native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restored_rtvs, original_dsv.get());

         // Rebind t1 (colorTex register) to the intermediate hudless SRV
         ID3D11ShaderResourceView* const hudless_srv = game_device_data.output_sdr_hudless_srv.get();
         native_device_context->PSSetShaderResources(1, 1, &hudless_srv);

         // Set UI composition PS and Copy VS
         native_device_context->PSSetShader(ui_ps_it->second.get(), nullptr, 0);
         native_device_context->VSSetShader(copy_vs_it->second.get(), nullptr, 0);
         native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

         // Ensure Luma CBs are bound for the PS stage
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Draw fullscreen quad
         native_device_context->Draw(4, 0);

         // Restore VS, PS, topology
         native_device_context->VSSetShader(prev_vs.get(), nullptr, 0);
         native_device_context->PSSetShader(prev_ps.get(), nullptr, 0);
         native_device_context->IASetPrimitiveTopology(prev_topology);

         // Unbind intermediate from t1
         ID3D11ShaderResourceView* null_srv = nullptr;
         native_device_context->PSSetShaderResources(1, 1, &null_srv);

#if DEVELOPMENT
         {
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = "Output SDR Two-Pass (Tonemap + UI Composition)";
               cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
            }
         }
#endif

         return DrawOrDispatchOverrideType::Replaced;
      }
#endif // ENABLE_FRAMEGEN

#if ENABLE_SR
      if (is_taa && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
      {
         if (device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs")].get() == nullptr)
         {
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }
         // 1 depth
         // 2 current color source
         // 3 previous color source (previous frame)
         // 4 motion vectors
         com_ptr<ID3D11ShaderResourceView> ps_shader_resources[5];
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), &ps_shader_resources[0]);

         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1 or 2
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         const bool dlss_inputs_valid = ps_shader_resources[0].get() != nullptr && ps_shader_resources[1].get() != nullptr && ps_shader_resources[2].get() != nullptr && ps_shader_resources[4].get() != nullptr && render_target_views[0].get() != nullptr;
         ASSERT_ONCE(dlss_inputs_valid);

         if (dlss_inputs_valid)
         {
            auto* sr_instance_data = device_data.GetSRInstanceData();
            ASSERT_ONCE(sr_instance_data);

            com_ptr<ID3D11Resource> output_color_resource;
            render_target_views[0]->GetResource(&output_color_resource);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT hr = output_color_resource->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));

            D3D11_TEXTURE2D_DESC taa_output_texture_desc;
            output_color->GetDesc(&taa_output_texture_desc);

            D3D11_VIEWPORT viewport;
            uint32_t num_viewports = 1;
            native_device_context->RSGetViewports(&num_viewports, &viewport);
            device_data.render_resolution = {viewport.Width, viewport.Height};
            game_device_data.upscaled_render_resolution = {(float)taa_output_texture_desc.Width, (float)taa_output_texture_desc.Height};
            game_device_data.resolution_scale = (float)device_data.render_resolution.x / (float)game_device_data.upscaled_render_resolution.x;
            game_device_data.viewport_rect = {static_cast<uint32_t>(viewport.TopLeftX), static_cast<uint32_t>(viewport.TopLeftY), static_cast<uint32_t>(game_device_data.upscaled_render_resolution.x), static_cast<uint32_t>(game_device_data.upscaled_render_resolution.y)};
            // Once DRS has engaged once, we can't really detect if it's been turned off ever again, anyway it's always active by default in this game (unless one has mods to disable it, or fix a scaled render resolution)
            if (!game_device_data.drs_active && game_device_data.resolution_scale == 1.0f)
            {
               device_data.sr_render_resolution_scale = 1.0f;
            }
            else if (game_device_data.resolution_scale < 0.5f - FLT_EPSILON)
            {
               device_data.sr_render_resolution_scale = game_device_data.resolution_scale;
               game_device_data.drs_active = true;
            }
            else
            {
               // This should pick quality or balanced mode, with a range from 100% to 50% resolution scale
               device_data.sr_render_resolution_scale = 1.f / 1.5f;
               game_device_data.drs_active = true;
            }

            // The TAA input and output textures were guaranteed to be of the same size, so we pass in the output one as render res,
            // scaled by the DLSS render resolution scaling factor (which is a fixed multiplication to enabled dynamic res scaling in DLSS, it doesn't change every frame, as long as the res doesn't drop below 50%).
            double target_aspect_ratio = (double)game_device_data.upscaled_render_resolution.x / (double)game_device_data.upscaled_render_resolution.y;
            std::array<uint32_t, 2> dlss_input_resolution = FindClosestIntegerResolutionForAspectRatio((double)taa_output_texture_desc.Width * device_data.sr_render_resolution_scale, (double)taa_output_texture_desc.Height * device_data.sr_render_resolution_scale, target_aspect_ratio);

            if (dlss_input_resolution[0] > game_device_data.upscaled_render_resolution.x || dlss_input_resolution[1] > game_device_data.upscaled_render_resolution.y)
            {
               device_data.force_reset_sr = true;
               return DrawOrDispatchOverrideType::None;
            }

            SR::SettingsData settings_data;
            settings_data.output_width = game_device_data.upscaled_render_resolution.x;
            settings_data.output_height = game_device_data.upscaled_render_resolution.y;
            settings_data.render_width = dlss_input_resolution[0];
            settings_data.render_height = dlss_input_resolution[1];
            settings_data.dynamic_resolution = game_device_data.drs_active;
            settings_data.hdr = true; // Unreal Engine does DLSS before tonemapping, in HDR linear space
            settings_data.inverted_depth = true;
            settings_data.mvs_jittered = false;
            settings_data.auto_exposure = false;
            settings_data.mvs_x_scale = 1.0f;
            settings_data.mvs_y_scale = 1.0f;
            settings_data.render_preset = dlss_render_preset;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

            bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
            bool dlss_output_changed = false;

            constexpr bool dlss_use_native_uav = true;
            bool dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
            // Create a copy that supports Unordered Access if it wasn't already supported
            if (!dlss_output_supports_uav)
            {
               D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
               dlss_output_texture_desc.Width = std::lrintf(game_device_data.upscaled_render_resolution.x);
               dlss_output_texture_desc.Height = std::lrintf(game_device_data.upscaled_render_resolution.y);
               dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

               if (device_data.sr_output_color.get())
               {
                  D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                  device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                  dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
               }
               if (!device_data.sr_output_color.get() || dlss_output_changed)
               {
                  device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                  hr = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
               // Texture creation failed, we can't proceed with DLSS
               if (!device_data.sr_output_color.get())
               {
                  skip_dlss = true;
               }
            }
            else
            {
               ASSERT_ONCE(device_data.sr_output_color == nullptr);
               device_data.sr_output_color = output_color;
            }

            if (!skip_dlss)
            {
               game_device_data.sr_source_color = nullptr;
               ps_shader_resources[2]->GetResource(&game_device_data.sr_source_color);
               game_device_data.depth_buffer = nullptr;
               ps_shader_resources[1]->GetResource(&game_device_data.depth_buffer);
               com_ptr<ID3D11Resource> object_velocity;
               ps_shader_resources[4]->GetResource(&object_velocity);

               // TODO: add exposure texture support (it's possibly calculated just earlier in the auto exposure steps, but they could be after DLSS too, depends on UE), either way auto exposure is ok
               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               // We don't actually replace the shaders with the classic luma shader swapping feature, so we need to set the CBs manually
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
               // Decode the motion vector from pixel shader
               {
                  if (!AreResourcesEqual(object_velocity.get(), game_device_data.sr_motion_vectors.get(), false /*check_format*/))
                  {
                     com_ptr<ID3D11Texture2D> object_velocity_texture;
                     hr = object_velocity->QueryInterface(&object_velocity_texture);
                     ASSERT_ONCE(SUCCEEDED(hr));
                     D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                     object_velocity_texture->GetDesc(&object_velocity_texture_desc);
                     ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);
#if 0 // Use the higher quality for MVs, the game's one were R16G16F. This has a ~1% cost on performance but helps with reducing shimmering on fine lines (stright lines looking segmented, like Bart's hair or Shark's teeth) when the camera is moving in a linear fashion.
                     object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
#else // Note: for FF7, 16bit might be enough, to be tried and compared, but the extra precision won't hurt
                     object_velocity_texture_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
#endif

                     game_device_data.sr_motion_vectors = nullptr; // Make sure we discard the previous one

                     // FIX: Add SHADER_RESOURCE bind flag so we can read MVs in Bloom pass
                     object_velocity_texture_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

                     hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &game_device_data.sr_motion_vectors);
                     ASSERT_ONCE(SUCCEEDED(hr));

                     game_device_data.sr_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                     game_device_data.sr_motion_vectors_srv = nullptr; // Reset SRV
                     if (SUCCEEDED(hr))
                     {
                        hr = native_device->CreateRenderTargetView(game_device_data.sr_motion_vectors.get(), nullptr, &game_device_data.sr_motion_vectors_rtv);
                        ASSERT_ONCE(SUCCEEDED(hr));

                        // Create SRV for MVs
                        hr = native_device->CreateShaderResourceView(game_device_data.sr_motion_vectors.get(), nullptr, &game_device_data.sr_motion_vectors_srv);
                        ASSERT_ONCE(SUCCEEDED(hr));
                     }
                  }

                  com_ptr<ID3D11VertexShader> prev_shader_vx;
                  com_ptr<ID3D11PixelShader> prev_shader_px;
                  native_device_context->VSGetShader(&prev_shader_vx, nullptr, nullptr);
                  native_device_context->PSGetShader(&prev_shader_px, nullptr, nullptr);
                  D3D11_PRIMITIVE_TOPOLOGY primitive_topology;
                  native_device_context->IAGetPrimitiveTopology(&primitive_topology);

                  // Set up for motion vector shader
                  ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.sr_motion_vectors_rtv.get();
                  native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, nullptr);

                  // We only need to swap the pixel/vertex shaders, depth and blend were already in the right state
                  native_device_context->VSSetShader(device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), nullptr, 0);
                  native_device_context->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs")].get(), nullptr, 0);

                  // We could probably keep the original vertex shader too, but whatever
                  native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                  // native_device_context->IASetInputLayout(nullptr); // Seemengly not needed
                  // native_device_context->RSSetState(nullptr); // Seemengly not needed

                  // Finally draw:
                  native_device_context->Draw(4, 0);
                  // native_device_context->DrawIndexed(3, 6, 0); // Original call would have been this, but we swap the pixel and vertex shaders

#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = "DLSS Decode Motion Vectors";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(game_device_data.sr_motion_vectors.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  // Restore the state
                  native_device_context->VSSetShader(prev_shader_vx.get(), nullptr, 0);
                  native_device_context->PSSetShader(prev_shader_px.get(), nullptr, 0);

                  native_device_context->IASetPrimitiveTopology(primitive_topology);
                  ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
                  native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());
               }

               bool reset_dlss = device_data.force_reset_sr || dlss_output_changed || game_device_data.camera_cut;
               device_data.force_reset_sr = false;
               game_device_data.camera_cut = false;
               // Render resolution doesn't necessarily match with the source texture size, DRS draws on the top left of textures
               uint32_t render_width_dlss = 0;
               uint32_t render_height_dlss = 0;
               if (game_device_data.found_per_view_globals)
               {
                  render_width_dlss = std::lrintf(device_data.render_resolution.x);
                  render_height_dlss = std::lrintf(device_data.render_resolution.y);
               }
               else // Shouldn't happen!
               {
                  render_width_dlss = taa_output_texture_desc.Width;
                  render_height_dlss = taa_output_texture_desc.Height;
               }

               float dlss_pre_exposure = 0.f;

               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = game_device_data.sr_source_color.get();
               draw_data.output_color = device_data.sr_output_color.get();
               draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth_buffer.get();
               draw_data.pre_exposure = sr_custom_pre_exposure;
               draw_data.jitter_x = projection_jitters.x * device_data.render_resolution.x * 0.5f;
               draw_data.jitter_y = projection_jitters.y * device_data.render_resolution.y * -0.5f;
               draw_data.user_sharpness = false;
               draw_data.far_plane = FLT_MAX;
               draw_data.near_plane = near_plane;
               draw_data.vert_fov = vert_fov;
               draw_data.reset = reset_dlss;
               draw_data.render_width = render_width_dlss;
               draw_data.render_height = render_height_dlss;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
               if (dlss_succeeded)
               {
                  device_data.has_drawn_sr = true;
               }
               game_device_data.camera_cut = false;
               game_device_data.sr_source_color = nullptr;
               game_device_data.depth_buffer = nullptr;
               draw_state_stack.Restore(native_device_context, device_data.uav_max_count);
               compute_state_stack.Restore(native_device_context, device_data.uav_max_count);

               if (device_data.has_drawn_sr)
               {
#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = "DLSS";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  // Upscaling happened later (during tonemapping) natively but we anticipate it with DLSS
                  game_device_data.has_drawn_upscaling = true;

                  if (!dlss_output_supports_uav)
                  {
                     native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                  }
                  else
                  {
                     device_data.sr_output_color = nullptr;
                  }

                  return DrawOrDispatchOverrideType::Replaced;
               }
               else
               {
                  // ASSERT_ONCE(false);
                  // cb_luma_global_settings.SRType = 0;
                  // device_data.cb_luma_global_settings_dirty = true;
                  // device_data.sr_suppressed = true;
                  device_data.force_reset_sr = true;
               }
            }
            if (dlss_output_supports_uav)
            {
               device_data.sr_output_color = nullptr;
            }
         }
      }

      // Always upgrade the viewport to the full upscaled resolution if we anticipated upscaling to happen before tonemapping
      // Prevent this from happening on compute shaders as it's never needed (also they don't use viewport)
      if (device_data.has_drawn_sr && game_device_data.drs_active && game_device_data.found_per_view_globals && (stages & reshade::api::shader_stage::all_compute) == 0)
      {
         PrepareDrawForEarlyUpscaling(native_device, native_device_context, device_data);
      }

      // add linear sampler to motionblur and dof shaders to upscale input textures to new resolution if needed
      if (original_shader_hashes.Contains(shader_hashes_Motion_Blur) ||
          original_shader_hashes.Contains(shader_hashes_DOF))
      {
         ID3D11SamplerState* const linear_sampler = device_data.sampler_state_linear.get();
         native_device_context->PSSetSamplers(0, 1, &linear_sampler);
      }

#endif // ENABLE_SR

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Clean GTAO resources if they exist but weren't used this frame (Prey pattern)
      if (!game_device_data.has_drawn_gtao && game_device_data.gtao_working_depth.get())
      {
         game_device_data.CleanGTAOResources();
      }

      // if (game_device_data.has_drawn_title)
      //{
      //    ASSERT_ONCE(game_device_data.found_per_view_globals);
      // }

      game_device_data.camera_cut = (!game_device_data.has_drawn_taa && !device_data.has_drawn_sr && !device_data.force_reset_sr) || game_device_data.camera_cut;
      device_data.has_drawn_main_post_processing = false;
      game_device_data.has_drawn_upscaling = false;
      if (!game_device_data.has_drawn_taa)
      {
         device_data.sr_suppressed = false;
      }
      game_device_data.has_drawn_taa = false;
      device_data.has_drawn_sr = false;
      game_device_data.has_drawn_gtao = false;
      game_device_data.found_per_view_globals = false;
      device_data.cb_luma_global_settings_dirty = true;
      static std::mt19937 random_generator(std::chrono::system_clock::now().time_since_epoch().count());
      static auto random_range = static_cast<float>((std::mt19937::max)() - (std::mt19937::min)());
      cb_luma_global_settings.GameSettings.custom_random = static_cast<float>(random_generator() + (std::mt19937::min)()) / random_range;
      cb_luma_global_settings.SRType = static_cast<uint32_t>(device_data.sr_type);
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.sr_motion_vectors_srv = nullptr;
      game_device_data.sr_motion_vectors_rtv = nullptr;
      game_device_data.sr_source_color = nullptr;
      game_device_data.depth_buffer = nullptr;
      game_device_data.sr_settings_data = nullptr;
      game_device_data.sr_draw_data = nullptr;
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::Text("Luma for \"Final Fantasy VII Remake\" is developed by Izueh and Pumbo and is open source and free.\n"
                  "It adds DLSS and improved HDR tonemapping.\n"
                  "Additional thanks to ShortFuse and Musa from the RenoDX team and their HDR mods for Remake and Rebirth which served as reference.\n",
         "If you enjoy it, consider donating to any of the contributors.", "");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 136, 124, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(17, 149, 134, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(57, 133, 111, 255));
      static const std::string donation_link_izueh = std::string("Buy Izueh a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_izueh.c_str()))
      {
         system("start https://ko-fi.com/izueh");
      }
      ImGui::PopStyleColor(3);
      ImGui::NewLine();

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
                  "\nIzueh"
                  "\nPumbo"

                  "\n\nAcknowledgments:"
                  "\nShortFuse"
                  "\nMusa"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nRenoDX"
                  "\n3Dmigoto"
                  "\nOklab"
                  "\nDICE (HDR tonemapper)",
         "");
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      data.GameData.RenderResolution = {device_data.render_resolution.x, device_data.render_resolution.y, 1.0f / device_data.render_resolution.x, 1.0f / device_data.render_resolution.y};
      data.GameData.OutputResolution = {game_device_data.upscaled_render_resolution.x, game_device_data.upscaled_render_resolution.y, 1.0f / game_device_data.upscaled_render_resolution.x, 1.0f / game_device_data.upscaled_render_resolution.y};
      data.GameData.ResolutionScale = {game_device_data.resolution_scale, 1.0f / game_device_data.resolution_scale};
      data.GameData.DrewUpscaling = device_data.has_drawn_sr ? 1 : 0;
      data.GameData.ViewportRect = game_device_data.viewport_rect;
      // Populate GTAO Data
      data.GameData.GTAO.Near = near_plane;
      data.GameData.GTAO.Far = 10000.0f; // Approximate far plane
      data.GameData.GTAO.FOV = vert_fov;

      can_sharpen = device_data.output_resolution.x == game_device_data.upscaled_render_resolution.x && device_data.output_resolution.y == game_device_data.upscaled_render_resolution.y;
      cb_luma_global_settings.GameSettings.can_sharpen = can_sharpen ? 1.f : 0.f;
   }

   static void UpdateLODBias(reshade::api::device* device)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);

         const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            // Reset to default (our mip offset is additive, so this is neutral)
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
         const auto new_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;

         bool texture_mip_lod_bias_offset_changed = prev_texture_mip_lod_bias_offset != new_texture_mip_lod_bias_offset;
         // Re-create all samplers immediately here instead of doing it at the end of the frame.
         // This allows us to avoid possible (but very unlikely) hitches that could happen if we re-created a new sampler for a new resolution later on when samplers descriptors are set.
         // It also allows us to use the right samplers for this frame's resolution.
         if (texture_mip_lod_bias_offset_changed)
         {
            ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
            for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
            {
               if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset))
                  continue; // Skip "resolutions" that already got their custom samplers created
               ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
               shared_lock_samplers.unlock(); // This is fine!
               {
                  D3D11_SAMPLER_DESC native_desc;
                  native_sampler->GetDesc(&native_desc);
                  com_ptr<ID3D11SamplerState> custom_sampler = CreateCustomSampler(device_data, native_device, native_desc);
                  const std::unique_lock unique_lock_samplers(s_mutex_samplers);
                  samplers_handle.second[new_texture_mip_lod_bias_offset] = custom_sampler;
               }
               shared_lock_samplers.lock();
            }
         }
      }
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      // The frames until this draw have a defaulted 1920x1080 resolution (and slightly after too)
      if (game_device_data.found_per_view_globals)
      {
         return;
      }

      // No need to convert to native DX11 flags
      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size)
         {
            device_data.cb_per_view_global_buffer = buffer;
            ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
            device_data.cb_per_view_global_buffer_map_data = *data;
         }
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      if (game_device_data.found_per_view_globals)
      {
         return;
      }
      bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;
      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);
      if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr)
      {
         float4(&float_data)[CBPerViewGlobal_buffer_size / sizeof(float4)] = *((float4(*)[CBPerViewGlobal_buffer_size / sizeof(float4)]) device_data.cb_per_view_global_buffer_map_data);

         // TAA cbuffer, called once (?) per frame, possibly at the beginning of the frame
         // 122 target texture res
         // 125 render res
         // 126 upscaled render res (doesn't necessarily match the display/swapchain resolution, there might be black bars)
         // 118 jitter
         bool is_valid_cbuffer = true && float_data[126].z == 1.0f / float_data[126].x && float_data[126].w == 1.0f / float_data[126].y && float_data[125].z == 1.0f / float_data[125].x && float_data[125].w == 1.0f / float_data[125].y;
         //&& float_data[122].x == float_data[125].x && float_data[122].y == float_data[125].y && float_data[122].z == float_data[125].z && float_data[122].w == float_data[125].w;

         // Make absolutely sure the jitters aren't both 0, which should never happen if they used proper jitter generation math, but we don't know,
         // though this happens in menus or when TAA is disabed (through mods)
         bool jitters_valid = std::abs(float_data[118].x) <= 1.f && std::abs(float_data[118].y) <= 1.f; // TODO: the jitters range is probably 0.5/render_res or so, hence we could restrict the check to that range to make it safer?
         jitters_valid &= (std::abs(float_data[118].x) > 0.f || std::abs(float_data[118].y) > 0.f);
         is_valid_cbuffer &= jitters_valid;

         if (is_valid_cbuffer)
         {
            size_t thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
            ASSERT_ONCE(!game_device_data.found_per_view_globals); // We found this twice? Shouldn't happen, we should probably reject one of the two
            bool has_jitters = float_data[118].x != 0.f || float_data[118].y != 0.f;
            if (has_jitters)
            {
               game_device_data.jitterless_frames_count = 0;
               game_device_data.is_in_menu = false;
            }
            // Give it a two frames tolerance just to make 100% sure that the jitters random generation didn't actually pick 0 for both in one frame (it might be possible depending on the random pattern generation they used, but probably impossible for two frames, see "Halton")
            else
            {
               // Note: for now we don't disable DLSS even if jitters are off
               game_device_data.jitterless_frames_count++;
               if (game_device_data.jitterless_frames_count >= 2)
               {
                  if (!game_device_data.is_in_menu)
                  {
                     device_data.force_reset_sr = true; // TODO: make sure this doesn't happen when pausing the game and the scene in the background remains the same, it'd mean we get a couple blurry frames when we go back to the game

                     game_device_data.is_in_menu = true;
                  }
               }
            }

            game_device_data.found_per_view_globals = true;
            // Extract jitter from constant buffer 1
            projection_jitters.x = float_data[118].x;
            projection_jitters.y = float_data[118].y;
            
            // Update render resolution from cb1[122] early so GTAO can use it before TAA runs
            // cb1[122].xy = actual render size (the rendered region within the texture)
            if (float_data[122].x > 0 && float_data[122].y > 0)
            {
               device_data.render_resolution = {float_data[122].x, float_data[122].y};
            }

            Math::Matrix44F projection_matrix;
            std::memcpy(&projection_matrix, &float_data[24], sizeof(Math::Matrix44F));
            near_plane = ComputeNearPlane(projection_matrix) / 100.0f; // Game uses cm
            vert_fov = ComputeFovY(projection_matrix);

            // check valid near plane and fov
            if (near_plane < 0.01f || near_plane > 100.f)
               near_plane = 0.1f;
            if (vert_fov < (10.f * (M_PI / 180.f)) || vert_fov > (170.f * (M_PI / 180.f)))
               vert_fov = 60.f * (M_PI / 180.f);

            game_device_data.camera_cut = float_data[140].y != 0.f;
         }
        
         device_data.cb_per_view_global_buffer_map_data = nullptr;
         device_data.cb_per_view_global_buffer = nullptr;
         UpdateLODBias(device);
      }
   }

   static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (device_data.has_drawn_sr && size == 512)
      {
         // It's not very nice to const cast, but we know for a fact this is dynamic memory, so it's probably fine to edit it (ReShade doesn't offer an interface for replacing it easily, and doesn't pass in the command list)
         float4* mutable_float_data = reinterpret_cast<float4*>(const_cast<void*>(data));
         const float4* float_data = reinterpret_cast<const float4*>(data);
         if (float_data[20].z == device_data.render_resolution.x && float_data[20].w == device_data.render_resolution.y && float_data[21].x == game_device_data.upscaled_render_resolution.x / 2.0f && float_data[21].y == game_device_data.upscaled_render_resolution.y / 2.0f)
         {
            mutable_float_data[20].z = game_device_data.upscaled_render_resolution.x;
            mutable_float_data[20].w = game_device_data.upscaled_render_resolution.y;
         }
      }
      else if (device_data.has_drawn_sr && size == 1024)
      {
         float4* mutable_float_data = reinterpret_cast<float4*>(const_cast<void*>(data));
         const float4* float_data = reinterpret_cast<const float4*>(data);
         // float_data[30].zw and [31].xy have render_res
         if (float_data[30].z == device_data.render_resolution.x && float_data[30].w == device_data.render_resolution.y && float_data[31].x == device_data.render_resolution.x && float_data[31].y == device_data.render_resolution.y)
         {
            mutable_float_data[30].z = game_device_data.upscaled_render_resolution.x;
            mutable_float_data[30].w = game_device_data.upscaled_render_resolution.y;
            mutable_float_data[31].x = game_device_data.upscaled_render_resolution.x;
            mutable_float_data[31].y = game_device_data.upscaled_render_resolution.y;
         }
      }
      return false;
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
#if ENABLE_SR
      ImGui::NewLine();
      // ImGui::SliderFloat("SR Custom Exposure", &sr_custom_exposure, 0.0, 10.0);
      ImGui::SliderFloat("SR Custom Pre-Exposure", &sr_custom_pre_exposure, 0.0, 10.0);
#endif // ENABLE_SR
   }
#endif // DEVELOPMENT

   void LoadConfigs() override
   {
      Luma::Settings::LoadSettings();
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      Luma::Settings::DrawSettings();
   }

   static bool CreateGTAOResources(ID3D11Device* native_device, GameDeviceDataFF7Remake& game_device_data, UINT width, UINT height)
   {
      // Return early if resources already exist with correct dimensions
      if (game_device_data.gtao_working_depth.get() && game_device_data.gtao_width == width && game_device_data.gtao_height == height)
         return true;
      
      // Clean old resources if dimensions changed
      if (game_device_data.gtao_width != width || game_device_data.gtao_height != height)
      {
         game_device_data.CleanGTAOResources();
      }
      
      game_device_data.gtao_width = width;
      game_device_data.gtao_height = height;
      
      HRESULT hr;
      
      D3D11_TEXTURE2D_DESC depth_desc = {};
      depth_desc.Width = width;
      depth_desc.Height = height;
      depth_desc.MipLevels = XE_GTAO_DEPTH_MIP_LEVELS;
      depth_desc.ArraySize = 1;
      depth_desc.Format = DXGI_FORMAT_R32_FLOAT;
      depth_desc.SampleDesc.Count = 1;
      depth_desc.Usage = D3D11_USAGE_DEFAULT;
      depth_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      
      hr = native_device->CreateTexture2D(&depth_desc, nullptr, &game_device_data.gtao_working_depth);
      if (FAILED(hr)) return false;
      
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      for (UINT i = 0; i < XE_GTAO_DEPTH_MIP_LEVELS; ++i)
      {
         uav_desc.Texture2D.MipSlice = i;
         hr = native_device->CreateUnorderedAccessView(game_device_data.gtao_working_depth.get(), &uav_desc, &game_device_data.gtao_working_depth_uavs[i]);
         if (FAILED(hr)) return false;
      }
      
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = XE_GTAO_DEPTH_MIP_LEVELS;
      hr = native_device->CreateShaderResourceView(game_device_data.gtao_working_depth.get(), &srv_desc, &game_device_data.gtao_working_depth_srv);
      if (FAILED(hr)) return false;
      
      D3D11_TEXTURE2D_DESC ao_desc = {};
      ao_desc.Width = width;
      ao_desc.Height = height;
      ao_desc.MipLevels = 1;
      ao_desc.ArraySize = 1;
      ao_desc.Format = DXGI_FORMAT_R8G8_UNORM;
      ao_desc.SampleDesc.Count = 1;
      ao_desc.Usage = D3D11_USAGE_DEFAULT;
      ao_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      
      hr = native_device->CreateTexture2D(&ao_desc, nullptr, &game_device_data.gtao_ao_edges);
      if (FAILED(hr)) return false;
      
      hr = native_device->CreateUnorderedAccessView(game_device_data.gtao_ao_edges.get(), nullptr, &game_device_data.gtao_ao_edges_uav);
      if (FAILED(hr)) return false;
      
      hr = native_device->CreateShaderResourceView(game_device_data.gtao_ao_edges.get(), nullptr, &game_device_data.gtao_ao_edges_srv);
      if (FAILED(hr)) return false;
      
      return true;
   }
};
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      default_paper_white = 250.f;
      Globals::SetGlobals(PROJECT_NAME, "Final Fantasy VII Remake Luma mod");
      Globals::VERSION = 2;

      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("4729683B", nullptr, 16));
      shader_hashes_Title.pixel_shaders.emplace(std::stoul("5FEE74F9", nullptr, 16));
      shader_hashes_DOF.pixel_shaders.emplace(std::stoul("B400FAF6", nullptr, 16));
      shader_hashes_Motion_Blur.pixel_shaders.emplace(std::stoul("B0F56393", nullptr, 16));
      shader_hashes_Downsample_Bloom.pixel_shaders.emplace(std::stoul("2174B927", nullptr, 16));
      shader_hashes_Bloom.pixel_shaders.emplace(std::stoul("4D6F937E", nullptr, 16));
      shader_hashes_MenuSlowdown.pixel_shaders.emplace(std::stoul("968B821F", nullptr, 16));
      shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("F68D39B5", nullptr, 16));
      shader_hashes_Velocity_Flatten.compute_shaders.emplace(std::stoul("4EB2EA5B", nullptr, 16));
      shader_hashes_Velocity_Gather.compute_shaders.emplace(std::stoul("FEE03685", nullptr, 16));
      shader_hashes_AO_Temporal.pixel_shaders.emplace(std::stoul("04BFE575", nullptr, 16));
      shader_hashes_AO_Denoise1.pixel_shaders.emplace(std::stoul("8BD60486", nullptr, 16));
      shader_hashes_AO_Denoise2.pixel_shaders.emplace(std::stoul("E6A8D4FB", nullptr, 16));

      std::vector<ShaderDefineData> game_shader_defines_data = {
         // GTAO defines
         {"XE_GTAO_QUALITY", '2', true, false, "GTAO Quality Level\n0 - Low\n1 - Medium\n2 - High (default)\n3 - Very High\n4 - Ultra\nHigher values use more samples for better quality but lower performance"}
      };
      shader_defines_data.append_range(game_shader_defines_data);

#if DEVELOPMENT
      // These make things messy in this game, given it renders at lower resolutions and then upscales and adds black bars beyond 16:9
      debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Fullscreen;

      forced_shader_names.emplace(std::stoul("4729683B", nullptr, 16), "TAA");
      forced_shader_names.emplace(std::stoul("B400FAF6", nullptr, 16), "DoF");
      forced_shader_names.emplace(std::stoul("B0F56393", nullptr, 16), "Motion Blur");
      forced_shader_names.emplace(std::stoul("2174B927", nullptr, 16), "Downsample Bloom"); // This is the first bloom downsample pass to run, it does a maximum of 50% downscaling, but if the resolution scale was e.g. 75% of the output one, it will convert from 75% to 50%, so the bloom chain isn't really affected by the render res
      forced_shader_names.emplace(std::stoul("A77F0B56", nullptr, 16), "Downsample Bloom"); // The result of this (4x3 texture or something) is used for bloom, maybe also as a local exposure map or something
      forced_shader_names.emplace(std::stoul("D9E87012", nullptr, 16), "Upscale Bloom");
      forced_shader_names.emplace(std::stoul("46727E9A", nullptr, 16), "Upscale Bloom"); // There's multiple versions of this (maybe one for DRS?)
      forced_shader_names.emplace(std::stoul("CCD7FA05", nullptr, 16), "Blur Bloom");
      forced_shader_names.emplace(std::stoul("69467442", nullptr, 16), "Blur Bloom");
      forced_shader_names.emplace(std::stoul("4D6F937E", nullptr, 16), "Apply Bloom");
      forced_shader_names.emplace(std::stoul("968B821F", nullptr, 16), "Slowdown Menu");
      forced_shader_names.emplace(std::stoul("F68D39B5", nullptr, 16), "Upscale and Tonemap");
      forced_shader_names.emplace(std::stoul("4EB2EA5B", nullptr, 16), "Velocity Flatten");
      forced_shader_names.emplace(std::stoul("FEE03685", nullptr, 16), "Velocity Gather");
      forced_shader_names.emplace(std::stoul("1D610CBA", nullptr, 16), "Ambient Occlusion");
      forced_shader_names.emplace(std::stoul("04BFE575", nullptr, 16), "Ambient Occlusion Temporal (GTAO Replaced)");
      forced_shader_names.emplace(std::stoul("8BD60486", nullptr, 16), "Ambient Occlusion Denoise 1 (Skipped for GTAO)");
      forced_shader_names.emplace(std::stoul("E6A8D4FB", nullptr, 16), "Ambient Occlusion Denoise 2 (GTAO Denoise)");
#endif

#if !DEVELOPMENT
      old_shader_file_names.emplace("Output_HDR_0xA8EB118F_0x922A71D1_0x3A4D858E_0xD950DA01.ps_5_0.hlsl");
      old_shader_file_names.emplace("Output_SDR_0x506D5998_0xF68D39B5_0xBBB9CE42_0x51E2B894.ps_5_0.hlsl");
      old_shader_file_names.emplace("Velocity_Flatten_0x4EB2EA5B.cs_5_0.hlsl");
      old_shader_file_names.emplace("Velocity_Gather_0xFEE03685.cs_5_0.hlsl");
#endif

#if !DEVELOPMENT
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::HDR10;
#else
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
#endif

      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Texture upgrades (8 bit unorm and 11 bit float etc to 16 bit float)
      texture_upgrade_formats = {
#if 0 // Probably not needed
				reshade::api::format::r8g8b8a8_unorm,
				reshade::api::format::r8g8b8a8_unorm_srgb,
				reshade::api::format::r8g8b8a8_typeless,
				reshade::api::format::r8g8b8x8_unorm,
				reshade::api::format::r8g8b8x8_unorm_srgb,
				reshade::api::format::b8g8r8a8_unorm,
				reshade::api::format::b8g8r8a8_unorm_srgb,
				//reshade::api::format::b8g8r8a8_typeless, // currently causes validation issues due to some odd 1x1x1 textures (that's just an assert Luma put for mods devs to double check though). TODO: Figure out what these textures do.
				reshade::api::format::b8g8r8x8_unorm,
				reshade::api::format::b8g8r8x8_unorm_srgb,
				reshade::api::format::b8g8r8x8_typeless,
#endif

         reshade::api::format::r10g10b10a2_unorm,
         reshade::api::format::r10g10b10a2_typeless,

         reshade::api::format::r11g11b10_float,
      };
      // Upgrade all 16:9 render targets too, because the game defaults to that aspect ratio internally unless mods are applied
      texture_format_upgrades_2d_size_filters |= (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios = {16.f / 9.f};
      // LUT is 3D 32x
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;

      enable_samplers_upgrade = true;

      redirected_shader_hashes["Output_HDR"] = {"A8EB118F", "922A71D1", "3A4D858E", "D950DA01", "5CD12E67", "3B489929", "8D04181D", "6846FF90"};
      redirected_shader_hashes["Output_SDR"] = {"506D5998", "F68D39B5", "BBB9CE42", "51E2B894", "803889E8", "D96EF76D", "5C2D3A71", "66162229"};
      for (const auto& output_hdr_shader_hash : redirected_shader_hashes["Output_HDR"])
      {
         shader_hashes_Output_HDR.pixel_shaders.emplace(std::stoul(output_hdr_shader_hash, nullptr, 16));
      }

      // Build mapping from game PS hash to native UI composition shader key
#if ENABLE_FRAMEGEN
      output_hdr_ui_shader_keys[0x922A71D1] = CompileTimeStringHash("FF7R Output HDR UI 922A71D1");
      output_hdr_ui_shader_keys[0xA8EB118F] = CompileTimeStringHash("FF7R Output HDR UI A8EB118F");
      output_hdr_ui_shader_keys[0x3A4D858E] = CompileTimeStringHash("FF7R Output HDR UI 3A4D858E");
      output_hdr_ui_shader_keys[0xD950DA01] = CompileTimeStringHash("FF7R Output HDR UI D950DA01");
      output_hdr_ui_shader_keys[0x5CD12E67] = CompileTimeStringHash("FF7R Output HDR UI 5CD12E67");
      output_hdr_ui_shader_keys[0x3B489929] = CompileTimeStringHash("FF7R Output HDR UI 3B489929");
      output_hdr_ui_shader_keys[0x8D04181D] = CompileTimeStringHash("FF7R Output HDR UI 8D04181D");
      output_hdr_ui_shader_keys[0x6846FF90] = CompileTimeStringHash("FF7R Output HDR UI 6846FF90");

      // Build mapping from game PS hash to native tonemap shader key
      output_hdr_tonemap_shader_keys[0x922A71D1] = CompileTimeStringHash("FF7R Output HDR TM 922A71D1");
      output_hdr_tonemap_shader_keys[0xA8EB118F] = CompileTimeStringHash("FF7R Output HDR TM A8EB118F");
      output_hdr_tonemap_shader_keys[0x3A4D858E] = CompileTimeStringHash("FF7R Output HDR TM 3A4D858E");
      output_hdr_tonemap_shader_keys[0xD950DA01] = CompileTimeStringHash("FF7R Output HDR TM D950DA01");
      output_hdr_tonemap_shader_keys[0x5CD12E67] = CompileTimeStringHash("FF7R Output HDR TM 5CD12E67");
      output_hdr_tonemap_shader_keys[0x3B489929] = CompileTimeStringHash("FF7R Output HDR TM 3B489929");
      output_hdr_tonemap_shader_keys[0x8D04181D] = CompileTimeStringHash("FF7R Output HDR TM 8D04181D");
      output_hdr_tonemap_shader_keys[0x6846FF90] = CompileTimeStringHash("FF7R Output HDR TM 6846FF90");

      // Build mapping from game PS hash to native SDR UI composition shader key
      output_sdr_ui_shader_keys[0x506D5998] = CompileTimeStringHash("FF7R Output SDR UI 506D5998");
      output_sdr_ui_shader_keys[0xF68D39B5] = CompileTimeStringHash("FF7R Output SDR UI F68D39B5");
      output_sdr_ui_shader_keys[0xBBB9CE42] = CompileTimeStringHash("FF7R Output SDR UI BBB9CE42");
      output_sdr_ui_shader_keys[0x51E2B894] = CompileTimeStringHash("FF7R Output SDR UI 51E2B894");
      output_sdr_ui_shader_keys[0x803889E8] = CompileTimeStringHash("FF7R Output SDR UI 803889E8");
      output_sdr_ui_shader_keys[0xD96EF76D] = CompileTimeStringHash("FF7R Output SDR UI D96EF76D");
      output_sdr_ui_shader_keys[0x5C2D3A71] = CompileTimeStringHash("FF7R Output SDR UI 5C2D3A71");
      output_sdr_ui_shader_keys[0x66162229] = CompileTimeStringHash("FF7R Output SDR UI 66162229");

      // Build mapping from game PS hash to native SDR tonemap shader key
      output_sdr_tonemap_shader_keys[0x506D5998] = CompileTimeStringHash("FF7R Output SDR TM 506D5998");
      output_sdr_tonemap_shader_keys[0xF68D39B5] = CompileTimeStringHash("FF7R Output SDR TM F68D39B5");
      output_sdr_tonemap_shader_keys[0xBBB9CE42] = CompileTimeStringHash("FF7R Output SDR TM BBB9CE42");
      output_sdr_tonemap_shader_keys[0x51E2B894] = CompileTimeStringHash("FF7R Output SDR TM 51E2B894");
      output_sdr_tonemap_shader_keys[0x803889E8] = CompileTimeStringHash("FF7R Output SDR TM 803889E8");
      output_sdr_tonemap_shader_keys[0xD96EF76D] = CompileTimeStringHash("FF7R Output SDR TM D96EF76D");
      output_sdr_tonemap_shader_keys[0x5C2D3A71] = CompileTimeStringHash("FF7R Output SDR TM 5C2D3A71");
      output_sdr_tonemap_shader_keys[0x66162229] = CompileTimeStringHash("FF7R Output SDR TM 66162229");
#endif // ENABLE_FRAMEGEN
      for (const auto& output_sdr_shader_hash : redirected_shader_hashes["Output_SDR"])
      {
         shader_hashes_Output_SDR.pixel_shaders.emplace(std::stoul(output_sdr_shader_hash, nullptr, 16));
      }
      // shader_hashes_Output_HDR.pixel_shaders.emplace(redirected_shader_hashes["Output_HDR"]);
      // shader_hashes_Output_SDR.pixel_shaders.emplace(redirected_shader_hashes["Output_SDR"]);

      Luma::Settings::Initialize(&settings);

      game = new FF7Remake();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(FF7Remake::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(FF7Remake::OnUnmapBufferRegion);
      // reshade::unregister_event<reshade::addon_event::update_buffer_region>(FF7Remake::OnUpdateBufferRegion);
      reshade::unregister_event<reshade::addon_event::create_device>(FF7Remake::OnCreateDevice);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}