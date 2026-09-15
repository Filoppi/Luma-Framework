#define GAME_METAL_GEAR_SOLID_4 1

#define ENABLE_SMAA 1

#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

// TODO: make a generic system for this.
// Shared with the UI shaders, it defines how the blend state we send them through "LumaData" is packed
#include "..\..\..\Shaders\Includes\HardwareBlendsEmulation.hlsl"

#include "gtao.h"

namespace
{
   const ShaderHashesList<ShaderHashesCount::Multiple, ShaderHashesStages::Graphics> shader_hashes_FXAA = { .pixel_shaders = { 0xFAB5AE7C } };
   const ShaderHashesList<ShaderHashesCount::Multiple, ShaderHashesStages::Graphics> shader_hashes_SwapchainCopy = { .pixel_shaders = { 0xD4BDA6C0 } };
   const ShaderHashesList<ShaderHashesCount::Multiple, ShaderHashesStages::Graphics> shader_hashes_MainUI = { .pixel_shaders = { 0xAC6CABC7 } };
   const ShaderHashesList<ShaderHashesCount::Multiple, ShaderHashesStages::Graphics> shader_hashes_ResampleWithTintAndDiscard = { .pixel_shaders = { 0xE4857A56 } };
   const ShaderHashesList<ShaderHashesCount::Multiple, ShaderHashesStages::Graphics> shader_hashes_ComposeTranslucency = { .pixel_shaders = { 0xF76C639C } };
   const ShaderHashesList<ShaderHashesCount::Multiple, ShaderHashesStages::Graphics> shader_hashes_LinearizeDepth = { .pixel_shaders = { 0xE8A7387A } };

   // User settings:
   bool enable_smaa = true;
   bool enable_gtao = false; // TODO1: disable by default?
   uint fix_bloom_scaling_type = 1;
   uint shadow_maps_resolution_multiplier = 2;

   constexpr bool disable_resolution_scaling = true;

   // This cannot be disabled. We have a UAV fallback and the whole UI geometry is apparently 2D with no overlapping layers,
   // however UAVs don't guarantee any sync between draw calls, so they will flicker.
   constexpr bool allow_rov_for_ui = false; // TODO1: fix UAV tooltips
}

struct GameDeviceDataMetalGearSolid4 final : public GameDeviceData
{
   MGS4GTAO::Data gtao_data;

   // The resolution "DrawSMAA()" last created its (Luma managed) intermediate render targets at
   uint32_t smaa_width = 0;
   uint32_t smaa_height = 0;

   bool drawn_fxaa = false;

   CustomPixelShaderPassData correct_subtractive_blends_data;

   // Assume it's supported to begin with if we use it. Matches the "ENABLE_ROV_UI" shader define.
   bool rov_supported = allow_rov_for_ui;

   // The Rasterizer Ordered View (UAV) the UI shaders compose themselves through, and the texture it was created for.
   // Keeping a reference on that texture guarantees its pointer can't be recycled by another one while we cache a view of it.
   ComPtr<ID3D11Resource> ui_uav_resource;
   ComPtr<ID3D11UnorderedAccessView> ui_uav;

   // The texture needs the unordered access bind flag (see the swapchain copy pass, which replaces it with one that has it), otherwise this returns null
   ID3D11UnorderedAccessView* GetOrCreateUIUAV(ID3D11Device* native_device, ID3D11Resource* resource)
   {
      if (ui_uav_resource.get() != resource)
      {
         ui_uav.reset();
         ui_uav_resource = resource; // Cache it even if the creation below failed, so we don't retry on every single draw call
         // A null desc makes a view of the whole resource with the format it was created with, which is fine given Luma upgraded it to R16G16B16A16_FLOAT (UAVs can't be typeless nor sRGB)
         if (FAILED(native_device->CreateUnorderedAccessView(resource, nullptr, ui_uav.put())))
         {
            ASSERT_ONCE_MSG(false, "Failed to create the UI UAV");
            ui_uav.reset();
         }
      }
      return ui_uav.get();
   }
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
      MGS4GTAO::RegisterShaders();

      // No gamma mismatch baked in the textures as the game never applied gamma, it was gamma from the beginning to the end.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_LUMA", '1', true, false, "Allows disabling some of the mod's improvements (e.g. unclamping dynamic range and tonemapping)", 1},
         {"ENABLE_IMPROVED_BLUR", '1', true, false, "Improves all blurring post processes (resampling) to be more stable and higher quality.\nThis fixes the flickering in bloom, and rings in DoF, for a performance cost (test for it before using it at 8k)", 1},
         {"ENABLE_HIGH_QUALITY_MOTION_BLUR", '1', true, false, "Makes motion blur smoother, fixing most of the trailing effect it causes.\nHas a performance cost", 1},
         {"ENABLE_HIGH_QUALITY_AUTO_EXPOSURE", '1', true, false, "Can help make auto exposure more stable (less flickering) and more accurate in case there's shadow and light on screen.\nHas a performance cost", 1},
         {"ENABLE_HIGH_QUALITY_NIGHT_VISION", '1', true, false, "Improves the night vision pixellization effect.\nHas a performance cost", 1},
         {"ENABLE_IMPROVED_COLOR_GRADING", '1', true, false, "Adds modernized and improved color grading that doesn't crush shadow as much but still retains contrast", 1},
         {"IMPROVED_COLOR_GRADING_TYPE", '1', true, false, "The higher the value, the more we stray from the original look, towards a more modern one (both technically and artistically)", 2},
         {"ENABLE_COLOR_GRADING", '1', true, false, "Allows disabling the game's color grading (not adviced)", 1},
         {"ENABLE_COLOR_TINTING", '1', true, false, "Allows disabling the tint part of color grading, which can occasionally contribute to the so called \"piss filter\"", 1},
         {"ENABLE_FILM_GRAIN", '1', true, false, "Allows disabling the game's film grain effect (it's not always present)", 1},
         {"ENABLE_VIGNETTE", '1', true, false, "Allows disabling the game's vignette effect (not always used) (best left at default)", 1},
         {"ENABLE_MOTION_BLUR", '1', true, false, "Allows disabling the game's additive motion blur effect", 1},
         {"ENABLE_BLOOM", '1', true, false, "Allows disabling the game's bloom (best left at default)", 1},
         {"ENABLE_HDR_BOOST", '1', true, false, "Enable a faint HDR boosting effect (applies to videos too)", 1},
         {"ENABLE_VANILLA_UI", '0', true, false, "Clamp the UI to SDR to preserve the original look (note: this can be slow)", 1},
         {"ENABLE_UI_TONEMAP", '0', true, false, "Avoid the UI going beyond the peak brightness, generally unnecessary and slow", 1},
         {"ENABLE_ROV_UI", allow_rov_for_ui ? '1' : '0' /*GameDeviceDataMetalGearSolid4::rov_supported*/, true, true, "Automatically set if ROV is supported by the GPU (and enabled in the mod). Allows for faster+safe HDR UI blends", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      // Disable dynamic resolution (pattern and byte patch by Lyall).
      // Luma's post processing might assume the scene is rendered at full resolution (maybe),
      // and their vanilla code doesn't properly clamp UVs at the bottom right so black or previous garbage leaks into the edges of the image,
      // and either way this does not help with performance at all if not on potato PCs from 2015, especially because the final scaling is bilinear...
      if (disable_resolution_scaling)
      {
         HMODULE module_handle = GetModuleHandle(nullptr);
         auto base = reinterpret_cast<std::byte*>(module_handle);
         auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
         auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos_header->e_lfanew);

         const std::vector<System::BytePattern> dynamic_resolution_pattern = {
            0xE8, System::ANY, System::ANY, System::ANY, System::ANY,
            0x0F, 0xB6, 0xC8,
            0xE8, System::ANY, System::ANY, System::ANY, System::ANY,
            0xE8, System::ANY, System::ANY, System::ANY, System::ANY,
            0x84, System::ANY, 0x0F, 0x84, System::ANY, System::ANY, System::ANY, System::ANY,
            0x41, System::ANY, 0x3C, 0x00, 0x00, 0x00,
         };
         const auto matches = System::ScanMemoryForPattern(base, nt_headers->OptionalHeader.SizeOfImage, dynamic_resolution_pattern);
         // Skip missing/ambiguous matches, including code already patched or hooked by another mod.
         if (matches.size() == 1)
         {
            std::byte* patch_address = matches[0] + 0x5;
            constexpr uint8_t original_bytes[] = { 0x0F, 0xB6, 0xC8 }; // movzx ecx, al
            constexpr uint8_t patched_bytes[] = { 0xB1, 0x00, 0x90 }; // mov cl, 0; nop
            // Recheck immediately before writing. This patch is intentionally never restored on exit.
            if (std::memcmp(patch_address, original_bytes, sizeof(original_bytes)) == 0)
               System::PatchMemory(patch_address, patched_bytes, sizeof(patched_bytes), System::PatchMemoryType::Code);
         }
      }

      draw_callbacks_by_shader_hashes = {
         { shader_hashes_FXAA, &OnDraw_FXAA },
         { shader_hashes_ComposeTranslucency, &OnDraw_ComposeTranslucency },
         { shader_hashes_LinearizeDepth, &OnDraw_LinearizeDepth },
         { shader_hashes_SwapchainCopy, &OnDraw_SwapchainCopy },
         { shader_hashes_ResampleWithTintAndDiscard, &OnDraw_ResampleWithTintAndDiscard },
         { shader_hashes_UI, &OnDraw_UI },
      };
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMetalGearSolid4;
      auto& game_device_data = GetGameDeviceData(device_data);

      D3D11_FEATURE_DATA_D3D11_OPTIONS2 options_2 = {};
      HRESULT hr = native_device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options_2,  sizeof(options_2));
      // Pretend it's not supported if it's globally disabled
      if (!allow_rov_for_ui)
         options_2.ROVsSupported = FALSE;
      // The GPU and driver support Rasterizer Ordered Views (it might actually always be if the game booted, but we don't know).
      if (SUCCEEDED(hr) && game_device_data.rov_supported != (bool)options_2.ROVsSupported)
      {
         game_device_data.rov_supported = (bool)options_2.ROVsSupported;

         // This is needed as well, given the UI shaders read the background back through the ROV, and DX11 only guarantees typed UAV loads on 32 bit single channel formats without it.
         ASSERT(options_2.TypedUAVLoadAdditionalFormats);

         // Recompile shaders without ROV support. It will require a manual recompilation or restart to actually take effect.
         const std::shared_lock lock(s_mutex_shader_defines);
         if (GetShaderDefineCompiledNumericalValue(char_ptr_crc32("ENABLE_ROV_UI")) != int(game_device_data.rov_supported))
         {
            GetShaderDefineData(char_ptr_crc32("ENABLE_ROV_UI")).SetDefaultValue(game_device_data.rov_supported ? '1' : '0');
            GetShaderDefineData(char_ptr_crc32("ENABLE_ROV_UI")).SetValue(game_device_data.rov_supported ? '1' : '0');
            defines_need_recompilation = true;
            ShaderDefineData::Save(shader_defines_data, NAME_ADVANCED_SETTINGS);
         }
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "EnableSMAA", enable_smaa);
      reshade::get_config_value(runtime, NAME, "EnableGTAO", enable_gtao);
      // TODO: add an "init config" function (or a configs struct) to pre-create these for users, so they'd learn about them
      reshade::get_config_value(runtime, NAME, "ShadowMapsResolutionMultiplier", shadow_maps_resolution_multiplier);
      reshade::get_config_value(runtime, NAME, "FixBloomScalingType", fix_bloom_scaling_type);

      // TODO: the target resolution is still guessed at this point, it will only fully work in fullscreen and unless we are at 16:9, with the UW fixes enabled.
      // The game creates all textures when it has already decided it's resolution (which cannot be changed after boot) but before sizing,
      // so we'd need to change "texture_custom_dimensions_upgrades" after the swapchain resizes, and after the first scene render happens, so we can know if there's UW mods
      // to unlock the limited 16:9 rendering. We could add a feature to "re-upgrade" current resources with the new upgrade settings!
      const float aspect_ratio = (float)GetSystemMetrics(SM_CXSCREEN) / (float)GetSystemMetrics(SM_CYSCREEN);
      // Round to make sure mip like downscaling of bloom (4 tap samples) work properly,
      // otherwise they wouldn't be divisors and shaders would require more advanced resampling.
      // The downside is that the quality of bloom might snap depending on the aspect ratio, but that's ok.
      const float bloom_rounded_aspect_ratio = max(std::round(aspect_ratio), 1.f);
      // Used when the horizontal resolution was already wider, accounting for 16:9 as baseline.
      // We use this as the behaviour at 16:9 was already fine.
      const float bloom_rounded_relative_aspect_ratio = max(std::round(aspect_ratio / (16.f / 9.f)), 1.f);

      float bloom_horizontal_scale = 1.f;
      if (fix_bloom_scaling_type == 1)
      {
         bloom_horizontal_scale = bloom_rounded_relative_aspect_ratio;
      }
      // Improves 16:9 too
      else if (fix_bloom_scaling_type == 2)
      {
         bloom_horizontal_scale = bloom_rounded_aspect_ratio;
      }
      // Risky
      else if (fix_bloom_scaling_type >= 3)
      {
         bloom_horizontal_scale = aspect_ratio;
      }

      // Editing "texture_custom_dimensions_upgrades" is still safe here, even without mutexes
      if (shadow_maps_resolution_multiplier != 1)
      {
         // TODO: the opaque geometry pixel shadows automatically draw shadow, however, they have a cb with the current cascade map resolution. If we changed that too to the upscaled resolution,
         // we'd end up softening the edges less, so ultimately this is better, even if the sampling logic in the shaders doesn't really acknowledge the wider res and thus might leave some texels in between out.
         texture_custom_dimensions_upgrades.insert(texture_custom_dimensions_upgrades.end(), {
         // Shadow maps (they always seem to be of these sizes)
#if 1 // These will have been caused by other mods scaling the size before luma, bring them back to base and override the scaling with ours
      // Other Mod Extreme quality
            {uint4(4096, 4096, 1, 1), reshade::api::format::r32_typeless, uint4(2048 * shadow_maps_resolution_multiplier, 2048 * shadow_maps_resolution_multiplier, 0, 0)},
            {uint4(4096, 4096 * 2, 1, 1), reshade::api::format::r32_typeless, uint4(2048 * shadow_maps_resolution_multiplier, 2048 * 2 * shadow_maps_resolution_multiplier, 0, 0)},
#endif
            // High quality
            {uint4(2048, 2048, 1, 1), reshade::api::format::r32_typeless, uint4(2048 * shadow_maps_resolution_multiplier, 2048 * shadow_maps_resolution_multiplier, 0, 0)},
            {uint4(2048, 2048 * 2, 1, 1), reshade::api::format::r32_typeless, uint4(2048 * shadow_maps_resolution_multiplier, 2048 * 2 * shadow_maps_resolution_multiplier, 0, 0)},
#if 0 // Leave lower qualities unchanged, for potato PCs, no reason to upgrade them
      // Medium
            {uint4(1024, 1024, 1, 1), reshade::api::format::r32_typeless, uint4(1024 * shadow_maps_resolution_multiplier, 1024 * shadow_maps_resolution_multiplier, 0, 0)},
            {uint4(1024, 1024 * 2, 1, 1), reshade::api::format::r32_typeless, uint4(1024 * shadow_maps_resolution_multiplier, 1024 * 2 * shadow_maps_resolution_multiplier, 0, 0)},
            // Low quality
            {uint4(512, 512, 1, 1), reshade::api::format::r32_typeless, uint4(512 * shadow_maps_resolution_multiplier, 512 * shadow_maps_resolution_multiplier, 0, 0)},
            {uint4(512, 512 * 2, 1, 1), reshade::api::format::r32_typeless, uint4(512 * shadow_maps_resolution_multiplier, 512 * 2 * shadow_maps_resolution_multiplier, 0, 0)},
#endif
         });
      }
      if (bloom_horizontal_scale != 1.f)
      {
         texture_custom_dimensions_upgrades.insert(texture_custom_dimensions_upgrades.end(), {
            // Bloom/Exposure (they shouldn't have MS enabled but we allow it in the filter anyway)
            // TODO: do these scale further at 8k vertical resolution? If so, add more values.
            {uint4(4096, 2048, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(4096 * bloom_horizontal_scale), 2048, 0, 0)},
            {uint4(2048, 2048, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(2048 * bloom_horizontal_scale), 2048, 0, 0)},
            {uint4(1024, 1024, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(1024 * bloom_horizontal_scale), 1024, 0, 0)},
            {uint4(512, 512, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(512 * bloom_horizontal_scale), 512, 0, 0)},
            {uint4(256, 512, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(256 * bloom_horizontal_scale), 512, 0, 0)},
            {uint4(256, 256, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(256 * bloom_horizontal_scale), 256, 0, 0)},
            {uint4(128, 256, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(128 * bloom_horizontal_scale), 256, 0, 0)},
            {uint4(128, 128, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(128 * bloom_horizontal_scale), 128, 0, 0)},
            // Also color grading LUT texture which for some reason is marked as render target (even if it's never written/changed),
            // it uses initial data so we can skip the size change based on that.
            {uint4(64, 64, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(64 * bloom_horizontal_scale), 64, 0, 0)},
            // Also exposure texture size so this can cause problems as it's read back by the CPU (but that one is R16G16B16A16_FLOAT!).
            {uint4(32, 32, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(32 * bloom_horizontal_scale), 32, 0, 0)},
            {uint4(16, 16, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(16 * bloom_horizontal_scale), 16, 0, 0)},
            {uint4(8, 8, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(8 * bloom_horizontal_scale), 8, 0, 0)},
            {uint4(4, 4, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(4 * bloom_horizontal_scale), 4, 0, 0)},
            {uint4(2, 2, 1, 0), reshade::api::format::r8g8b8a8_unorm, uint4(std::lround(2 * bloom_horizontal_scale), 2, 0, 0)},
         });
      }
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      auto& game_device_data = GetGameDeviceData(device_data);
      ImGui::BeginDisabled(!game_device_data.drawn_fxaa);
      if (ImGui::Checkbox("Enable SMAA", &enable_smaa))
         reshade::set_config_value(runtime, NAME, "EnableSMAA", enable_smaa);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA, which is sharper and more temporally stable.");
      ImGui::EndDisabled();

      if (ImGui::Checkbox("Enable GTAO", &enable_gtao))
      {
         // Reset all data
         if (!enable_gtao)
         {
            MGS4GTAO::Clear(game_device_data.gtao_data);
         }
         reshade::set_config_value(runtime, NAME, "EnableGTAO", enable_gtao);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Adds ambient occlusion.");

#if DEVELOPMENT // TODO1: test with night vision etc. This is probably not safe enough to propose to the final player atm as night vision draws through it.
      if (!shader_hashes_UI.Empty())
         ImGui::Checkbox("Hide UI", &hide_ui);
#endif
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      if (enable_gtao)
      {
         ImGui::NewLine();

         auto& gtao_data = GetGameDeviceData(device_data).gtao_data;

         constexpr float game_scale_to_m = 1.f / 1000.f; // Game was in mm

         float effect_radius = gtao_data.gtao_constants.effect_radius * game_scale_to_m;
         if (ImGui::SliderFloat("GTAO Radius", &effect_radius, 0.01f, 5.f, "%.01f", ImGuiSliderFlags_Logarithmic))
         {
            gtao_data.gtao_constants.effect_radius = effect_radius / game_scale_to_m;
         }
         ImGui::SliderFloat("GTAO Intensity", &gtao_data.gtao_constants.final_value_power, 0.1f, 5.f, "%.2f", ImGuiSliderFlags_Logarithmic);

         static bool gtao_radius_distance_scaling = gtao_data.gtao_constants.radius_scaling_multiplier != 1.f;
         static float gtao_radius_scaling_multiplier = gtao_data.gtao_constants.radius_scaling_multiplier;
         ImGui::Checkbox("GTAO Radius Distance Scaling", &gtao_radius_distance_scaling);
         if (gtao_radius_distance_scaling)
         {
            float radius_scaling_min_depth = gtao_data.gtao_constants.radius_scaling_min_depth * game_scale_to_m;
            if (ImGui::SliderFloat("GTAO Radius Scaling Min Depth", &radius_scaling_min_depth, 0.1f, 100.f, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
               gtao_data.gtao_constants.radius_scaling_min_depth = radius_scaling_min_depth / game_scale_to_m;
            }
            float radius_scaling_max_depth = gtao_data.gtao_constants.radius_scaling_max_depth * game_scale_to_m;
            if (ImGui::SliderFloat("GTAO Radius Scaling Max Depth", &radius_scaling_max_depth, 10.f, 2000.f, "%.1f", ImGuiSliderFlags_Logarithmic))
            {
               gtao_data.gtao_constants.radius_scaling_max_depth = radius_scaling_max_depth / game_scale_to_m;
            }

            ImGui::SliderFloat("GTAO Radius Scaling Multiplier", &gtao_radius_scaling_multiplier, 1.f, 100.f, "%.1f", ImGuiSliderFlags_Logarithmic);
         }
         gtao_data.gtao_constants.radius_scaling_multiplier = gtao_radius_distance_scaling ? gtao_radius_scaling_multiplier : 1.f;

         ImGui::NewLine();

         ImGui::Text("GTAO Camera Constant Buffer: %s", gtao_data.proj_mat_constant_buffer ? "Found" : "Not Found");
         ImGui::Text("GTAO Fog Constant Buffer: %s", gtao_data.fog_constant_buffer ? "Found" : "Not Found");
      }
   }
#endif

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      // The first vertex+pixel that draws on the scene color texture with depth writes should have the projection matrix for GTAO, cache it!
      if (enable_gtao &&
         (!game_device_data.gtao_data.found_proj_mat_constant_buffer || !game_device_data.gtao_data.found_fog_constant_buffer) &&
         game_device_data.gtao_data.scene_resource &&
         game_device_data.gtao_data.scene_frame != cb_luma_global_settings.FrameIndex &&
         (stages & (reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel)) == (reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel))
      {
         // TODO: move to GTAO file!

         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);

         com_ptr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(&depth_stencil_state, nullptr);

         if (depth_stencil_state && rtv)
         {
            ComPtr<ID3D11Resource> rtv_resource;
            D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
            depth_stencil_state->GetDesc(&depth_stencil_desc);

            rtv->GetResource(rtv_resource.put());

            // The first few writes are the sky and they don't use the right cb layout. They don't write depth.
            if (game_device_data.gtao_data.scene_resource == rtv_resource && depth_stencil_desc.DepthEnable && depth_stencil_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO)
            {
               // Wait until we have a mesh that draws with shadow maps to make sure we are a proper mesh, otherwise the fog data isn't in the CB
               com_ptr<ID3D11ShaderResourceView> ps_srvs[7];
               native_device_context->PSGetShaderResources(0, static_cast<UINT>(std::size(ps_srvs)), &ps_srvs[0]);
               for (int i = 0; i < static_cast<int>(std::size(ps_srvs)); ++i)
               {
                  if (!ps_srvs[i])
                     continue;

                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  ps_srvs[i]->GetDesc(&srv_desc);
                  if (srv_desc.Format == DXGI_FORMAT_R32_FLOAT)
                  {
                     ComPtr<ID3D11Buffer> fog_cb;
                     native_device_context->PSGetConstantBuffers(0, 1, fog_cb.put());
                     if (fog_cb && !game_device_data.gtao_data.found_fog_constant_buffer)
                     {
                        game_device_data.gtao_data.fog_constant_buffer = fog_cb;
                        game_device_data.gtao_data.found_fog_constant_buffer = true;
                     }
                     break;
                  }
               }

               ComPtr<ID3D11Buffer> pj_cb;
               native_device_context->VSGetConstantBuffers(0, 1, pj_cb.put());
               if (pj_cb && !game_device_data.gtao_data.found_proj_mat_constant_buffer)
               {
                  game_device_data.gtao_data.proj_mat_constant_buffer = pj_cb;
                  game_device_data.gtao_data.found_proj_mat_constant_buffer = true;
               }
            }
         }
      }
      return DrawOrDispatchOverrideType::None;
   }

   static DrawOrDispatchOverrideType OnDraw_FXAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.drawn_fxaa = true;

      // Replace FXAA with SMAA. They both run on the gamma space post process buffer, from one texture onto another one, so it's a drop in replacement.
      if (!enable_smaa) return DrawOrDispatchOverrideType::None;

      // "DrawSMAA()" retrieves its shaders without checking whether they are valid, and they might still be compiling on boot (or be reloading in development builds), in that case we simply keep the original FXAA.
      // NOTE: this is likely unnecessary.
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

      // Only run SMAA on the main rendering. Mirror views also run FXAA so we can just leave that.
      if (rtv_texture_desc.Width != (UINT)device_data.render_resolution.x || rtv_texture_desc.Height != (UINT)device_data.render_resolution.y)
         return DrawOrDispatchOverrideType::None;

      auto& managed_resources = device_data.managed_resources;

      // "DrawSMAA()" only re-creates its resolution dependent resources when the swapchain is re-initialized, so do it ourselves in case the game ever changed its post processing resolution on its own.
      // Note: the game resolution cannot change after boot anyway!
      if (game_device_data.smaa_width != rtv_texture_desc.Width || game_device_data.smaa_height != rtv_texture_desc.Height)
      {
         managed_resources.depth_stencil_views["smaa_dsv"_h].reset();
         managed_resources.render_target_views["smaa_edge_detection"_h].reset();
         managed_resources.render_target_views["smaa_blending_weight_calculation"_h].reset();
         game_device_data.smaa_width = rtv_texture_desc.Width;
         game_device_data.smaa_height = rtv_texture_desc.Height;
      }

      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
      updated_cbuffers = true;

      // Colors are in gamma space at this point ("POST_PROCESS_SPACE_TYPE" is 0), which is what SMAA's color edge detection expects,
      // so the same texture serves as both the edge detection and the neighborhood blending input (no linearization pass is needed).
      DrawSMAA(native_device, native_device_context, device_data, rtv_scene.get(), srv_scene.get(), srv_scene.get());

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

   static DrawOrDispatchOverrideType OnDraw_ComposeTranslucency(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
   {
      // The first composition of a frame composes hair+decals onto the opaque scene, AO is applied on its output
      if (enable_gtao)
      {
         MGS4GTAO::OnComposeTranslucency(native_device_context, device_data, GetGameDeviceData(device_data).gtao_data);
      }

      return DrawOrDispatchOverrideType::None;
   }

   static DrawOrDispatchOverrideType OnDraw_LinearizeDepth(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
   {
      // Add AO on the composed scene (after hair and decals), right before the depth linearization that follows the first translucency composition of the frame:

      // Device depth (PS t0 of this draw) is linearized by GTAO's prefilter pass.
      // The camera comes from the game's constant buffer found in "OnDrawOrDispatch()", GTAO doesn't draw until then.
      // Interestingly, GTAO scene darkening applies as a multiplicative RGB blend on top of the scene, which is HDR encoded into a UNROM, however, the result is mathematically identical to doing it in linear.
      auto& game_device_data = GetGameDeviceData(device_data);
      if (enable_gtao && MGS4GTAO::Draw(native_device, native_device_context, device_data, game_device_data.gtao_data))
      {
         // TODO: skip the game's depth linearization draw and redirect the game linearized depth texture to the GTAO one (which should be identical?, but has mips).
         // Alternatively just redirect it as a resource copy to their resource, which should be faster than running a linearization draw call (not by much).
#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context;
            trace_draw_call_data.custom_name = "GTAO";
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
#endif
      }

      return DrawOrDispatchOverrideType::None;
   }

   static DrawOrDispatchOverrideType OnDraw_SwapchainCopy(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      //game_device_data.drawn_fxaa = false; // TODO1: delay to next frame's start, or anyway, after imgui

      // SRV 0 is the scene, and FXAA resolves it onto RT 0.
      ComPtr<ID3D11ShaderResourceView> srv;
      native_device_context->PSGetShaderResources(0, 1, srv.put());
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);

      // Clear the swapchain in case our display aspect ratio isn't 16:9 and we don't have an UW mod.
      // The game doesn't and as such if you have reshade open, imgui ends up trailing behind
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

            // TODO: if ever needed, we could assing this earlier in the frame too, by checking "shader_hashes_ComposeTranslucency", given it should always (?) run.
            // It'd only help with dynamic resolution scaling though (would it even? It probably doesn't ever change the texture size, just renders on the top left).
            device_data.render_resolution = float2(srv_texture_2d_desc.Width, srv_texture_2d_desc.Height);
            cb_luma_global_settings.RenderSize = device_data.render_resolution;
            cb_luma_global_settings.RenderInvSize = float2(1.f / cb_luma_global_settings.RenderSize.x, 1.f / cb_luma_global_settings.RenderSize.y);
            device_data.cb_luma_global_settings_dirty = true;
  
            // Only clear if the draw isn't fullscreen
            if (rtv_texture_2d_desc.Width != srv_texture_2d_desc.Width || rtv_texture_2d_desc.Height != srv_texture_2d_desc.Height)
            {
               constexpr FLOAT clear_color[4] = { 0.f, 0.f, 0.f, 1.f };
               native_device_context->ClearRenderTargetView(rtv.get(), clear_color);
            }

            // On the first frame of the game, replace the source texture (the one where the final post process and UI write),
            // and make it UAV compatible, so we can use ROV with it.
            // By the time a draw call reaches us, Luma has already redirected this SRV to the (indirect) upgraded mirror it keeps for the game's texture,
            // so "srv_resource" is that mirror, and all we do is swapping it for an identical texture that also has the unordered access bind flag.
            // There's no point in doing this if we can't use ROV, the UI shaders would have been compiled without the code that writes to the UAV (see "ENABLE_ROV_UI").
            if (srv_texture_2d
               && (srv_texture_2d_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) == 0
               && srv_texture_2d_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
               && srv_texture_2d_desc.SampleDesc.Count == 1) // MSAA textures can't have UAVs
            {
               const uint64_t srv_resource_handle = (uint64_t)srv_resource.get();
               uint64_t original_srv_resource_handle = srv_resource_handle;
               reshade::api::device* reshade_device = device_data.reshade_device;

               std::unique_lock lock_device_write(device_data.mutex);

               // Inverted map search to find the original resource from the upgraded one (assuming there was one)
               for (const auto& original_resource_to_mirrored_upgraded_resource : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
               {
                  if (original_resource_to_mirrored_upgraded_resource.second.mirror_handle == srv_resource_handle)
                  {
                     original_srv_resource_handle = original_resource_to_mirrored_upgraded_resource.first;

                     // TODO1: do a "remove indirect upgraded resource" func instead! This is already copied from it. Below too. see "device_data.resource_upgrades.ReUpgradeResource()";"
                     auto original_resource_to_mirrored_upgraded_resource = device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources.find(original_srv_resource_handle);
                     if (original_resource_to_mirrored_upgraded_resource != device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources.end())
                     {
                        const auto mirrored_upgraded_resource = original_resource_to_mirrored_upgraded_resource->second.mirror_handle;
                        device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources.erase(original_resource_to_mirrored_upgraded_resource);

                        // Invalidate stale view mappings for this mirror while the lock is held.
                        std::vector<uint64_t> unlinked_mirror_views;
                        if (auto mirror_views_it = device_data.resource_upgrades.mirror_views_by_mirror_resource.find(mirrored_upgraded_resource); mirror_views_it != device_data.resource_upgrades.mirror_views_by_mirror_resource.end())
                        {
                           const auto& mirror_views = mirror_views_it->second;
                           for (auto view_map_it = device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views.begin(); view_map_it != device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views.end();)
                           {
                              if (mirror_views.contains(view_map_it->second))
                              {
                                 unlinked_mirror_views.push_back(view_map_it->second);
                                 device_data.resource_upgrades.mirror_views_to_mirror_resources.erase(view_map_it->second);
                                 view_map_it = device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views.erase(view_map_it);
                              }
                              else
                              {
                                 ++view_map_it;
                              }
                           }
                           device_data.resource_upgrades.mirror_views_by_mirror_resource.erase(mirror_views_it);
                        }

                        constexpr bool delayed_destruction = true;
                        // Defer freeing to present: the mirror may still be in flight in hooks or bound on recorded lists.
                        if (delayed_destruction)
                        {
                           for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
                           {
                              device_data.resource_upgrades.pending_mirror_view_destructions.push_back({ unlinked_mirror_view });
                           }
                           device_data.resource_upgrades.pending_mirror_resource_destructions.push_back({ mirrored_upgraded_resource });
                        }
                        else
                        {
                           lock_device_write.unlock();
                           for (const uint64_t unlinked_mirror_view : unlinked_mirror_views)
                           {
                              reshade_device->destroy_resource_view({ unlinked_mirror_view });
                           }
                           reshade_device->destroy_resource({ mirrored_upgraded_resource });
                           lock_device_write.lock();
                        }
                     }

                     break;
                  }
               }

               lock_device_write.unlock();
               std::shared_lock lock_device_read(device_data.mutex);

               uint64_t new_resource;
               if (FindOrCreateIndirectUpgradedResource(reshade_device, 0, original_srv_resource_handle, new_resource, device_data, true, reshade::api::resource_usage::shader_resource_pixel, lock_device_read, false, false, false, reshade::api::resource_usage::unordered_access))
               {
                  // Carry the current content over, just in case it was used for multi frame motion blur or something (very unlikely)
                  native_device_context->CopyResource((ID3D11Resource*)new_resource, srv_resource.get());
               }
            }
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   static DrawOrDispatchOverrideType OnDraw_ResampleWithTintAndDiscard(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
   {
      D3D11_BLEND_DESC blend_desc = {};
      FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
      UINT sample_mask = 0xFFFFFFFF; // Unused, we don't do MSAA here
      ComPtr<ID3D11BlendState> blend_state;
      native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
      if (blend_state)
      {
         blend_state->GetDesc(&blend_desc);
      }

      // Inform the shader of inverted blends, to it can pre-clamp to the best of its abilities.
      const uint32_t custom_data_1 = IsBlendInverted(blend_desc, 1, false, 0) ? 1 : 0;
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1);
      updated_cbuffers = true;

      return DrawOrDispatchOverrideType::None;
   }
   
   static DrawOrDispatchOverrideType OnDraw_UI(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
   {
      D3D11_BLEND_DESC blend_desc = {};
      FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
      UINT sample_mask = 0xFFFFFFFF; // Unused, we don't do MSAA here
      ComPtr<ID3D11BlendState> blend_state;
      native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
      if (blend_state)
      {
         blend_state->GetDesc(&blend_desc);
      }

      // Note: this shader clips output to 1, we need our version to unclip it (we could save a few "Luma_*" versions of an identical modified shader),
      // but unless we use rov in the UI shaders, which prevents having conditions and forces a read/write for every pixel,
      // we can just draw the new one with a branch to not do the UAV write.
      auto DrawWithOriginalPixelShader = [&](bool force_custom_shader = false) -> void
         {
            // If we don't use ROV, we can just use the custom shader anyway as it branches out of the UAV writes, not adding a relevant cost against the original shader (which might have saturates that clamp HDR output)
            if (!allow_rov_for_ui || force_custom_shader)
            {
               if (original_draw_dispatch_func && *original_draw_dispatch_func)
               {
                  (*original_draw_dispatch_func)();
               }
               return;
            }

            if (cmd_list_data.pipeline_state_original_pixel_shader.handle == 0 || !is_custom_pass)
            {
               if (original_draw_dispatch_func && *original_draw_dispatch_func)
               {
                  (*original_draw_dispatch_func)();
               }
               return;
            }

            ComPtr<ID3D11PixelShader> previous_shader;
            native_device_context->PSGetShader(previous_shader.put(), nullptr, nullptr);

            native_device_context->PSSetShader(reinterpret_cast<ID3D11PixelShader*>(cmd_list_data.pipeline_state_original_pixel_shader.handle), nullptr, 0);

            if (original_draw_dispatch_func && *original_draw_dispatch_func)
            {
               (*original_draw_dispatch_func)();
            }

            native_device_context->PSSetShader(previous_shader.get(), nullptr, 0);
         };

      bool force_hw_blends_emulation = GetShaderDefineCompiledNumericalValue(char_ptr_crc32("ENABLE_VANILLA_UI")) || GetShaderDefineCompiledNumericalValue(char_ptr_crc32("ENABLE_UI_TONEMAP"));
      const bool enable_ui_fix = test_index != 14;
      const bool force_use_legacy_ui_fix = test_index == 15;
      const bool is_blend_inverted = IsBlendInverted(blend_desc, 1, false, 0);
      bool is_render_target_float = true; // Basic assumption
      // This pass subtracts the source from the target so we need to clamp it after draw in R16G16B16A16_FLOAT otherwise values can go negative
      if (force_hw_blends_emulation || (enable_ui_fix && is_blend_inverted))
      {
         auto& game_device_data = GetGameDeviceData(device_data);

         com_ptr<ID3D11RenderTargetView> rtv;
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(1, &rtv, &dsv);

         // The ROV (UAV) view of the render target the UI draws on. Without it we fall back on the legacy fix.
         ID3D11UnorderedAccessView* ui_uav = nullptr;
         ComPtr<ID3D11Resource> rtv_resource;
         if (rtv.get())
         {
            rtv->GetResource(rtv_resource.put());
            if (rtv_resource)
            {
               ComPtr<ID3D11Texture2D> rtv_texture_2d;
               rtv_resource->QueryInterface(rtv_texture_2d.put());
               D3D11_TEXTURE2D_DESC rtv_texture_2d_desc = {};
               if (rtv_texture_2d)
                  rtv_texture_2d->GetDesc(&rtv_texture_2d_desc);

               // No UAV flag, can't do ROV. We probably just need to wait for the next frame to handle this
               // (the swapchain copy pass replaces this texture with an unordered access compatible one).
               // Alternatively we could create it on the spot and copy the content from the previous one!
               // Note that the texture only ever gets that flag if ROV is supported, so this covers the case of the UI shaders having been compiled without the ROV code too.
               // Only the main UI shader has the ROV/UAV code for now, the other ones would draw nothing at all if we replaced their render target with a ROV.
               //
               // Note: this also has another positive consequence, which is to avoid these shader replacements from accidentally running
               // on a "UI" shader that actually draw some other stuff that wasn't UI, because the game didn't ever use UAV, so the added flag
               // could only come from our modifications.
               if ((rtv_texture_2d_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0)
               {
                  ui_uav = game_device_data.GetOrCreateUIUAV(native_device, rtv_resource.get());
               }
               is_render_target_float = rtv_texture_2d_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            }
         }

         bool can_use_ui_uav_fix = ui_uav != nullptr;

         can_use_ui_uav_fix &= is_custom_pass; // Continue only if changed, otherwise we wouldn't have the needed shader code
         can_use_ui_uav_fix &= is_render_target_float; // No need to do anything on UNORM RTs

         // Wait for recompilation, otherwise the shader is in an uxpected state and it could crash if executed. If no ROV, we fall back on UAV.
         can_use_ui_uav_fix &= game_device_data.rov_supported == (bool)GetShaderDefineData(char_ptr_crc32("ENABLE_ROV_UI")).GetCompiledNumericalValue();
#if 0 // If there were overlapping issues, we can just disable this behaviour in case ROV is not supported. Not needed until then. // TODO: re-enable this and expose a setting to disable UI fixes to users. UAV actually produces garbage, ROV is also needed to coordinate between multiple consecutive draw calls.
         can_use_ui_uav_fix &= game_device_data.rov_supported;
#endif

         bool is_main_ui_shader = original_shader_hashes.Contains(shader_hashes_MainUI);

         if (rtv.get() && can_use_ui_uav_fix && (!force_use_legacy_ui_fix || force_hw_blends_emulation))
         {
#if TEST || DEVELOPMENT
            if (!force_hw_blends_emulation)
            {
               // The main blend mode we expect for inverted blends
               ASSERT_ONCE(blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA
                           && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE
                           && blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT
                           && blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE
                           && blend_desc.RenderTarget[0].DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO
                           && blend_desc.RenderTarget[0].BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD);
            }
            else
            {
               // Write mask is disabled in the shader for optimization, hence it's not supported.
               ASSERT_ONCE(blend_desc.RenderTarget[0].RenderTargetWriteMask == D3D11_COLOR_WRITE_ENABLE_ALL);
            }

            // Make sure depth writes are not set, otherwise we could screw them up by writing on output anyway (ROV writes cannot be conditional).
            {
               com_ptr<ID3D11DepthStencilState> depth_stencil_state;
               native_device_context->OMGetDepthStencilState(&depth_stencil_state, nullptr);

               D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
               depth_stencil_state->GetDesc(&depth_stencil_desc);

               if (dsv.get() && depth_stencil_desc.DepthEnable && depth_stencil_desc.DepthWriteMask != D3D11_DEPTH_WRITE_MASK_ZERO)
               {
                  ASSERT_ONCE(false);
               }
            }
            // Make sure there's only one RTV
            {
               // We only check blend for the first RT, so make sure that's all that is used!
               com_ptr<ID3D11RenderTargetView> rtvs[2];
               native_device_context->OMGetRenderTargets(2, &rtvs[0], nullptr);
               ASSERT_ONCE(rtvs[1] == nullptr);
            }
#endif

            // Inform the UI shader we want to use ROV/UAV
            // whichever the best one that is supported, UAV can have issues with sorting unless the UI is all 2D and has no overlapping triangles,
            // which is likely the case anyway, so chances are it'd be faster and look right nonetheless.
            // Nulling the render target below also takes the fixed function output merger out of the equation, so the shader has to run the blend equation itself:
            // send it the whole blend state (of render target 1, the only one these passes ever draw to) and the constant blend factor (see "BlendState.hlsl").
            // The packing outputs 0 if blending is disabled.
            const uint32_t custom_data_1 = force_hw_blends_emulation ? PackRenderTargetBlendState(blend_desc.RenderTarget[0]) : 1;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, PackBlendFactorRG(blend_factor), PackBlendFactorBA(blend_factor));
            updated_cbuffers = true;

            // Swap the render target for a ROV (UAV) of the same texture: the shader now reads the background and writes the composed (e.g. subtractive) result itself, so it can clamp it.
            // Render targets and UAVs share the same slots in DX11, so the UAV has to start after.
            constexpr UINT uav_initial_count = -1;
            native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, dsv.get(), 1, 1, &ui_uav, &uav_initial_count);

            if (original_draw_dispatch_func && *original_draw_dispatch_func)
            {
               (*original_draw_dispatch_func)();
            }

            // Set back the original RTV for the following UI draw calls.
            ID3D11RenderTargetView* const original_rtv = rtv.get();
            native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(1, &original_rtv, dsv.get(), 1, 0, nullptr, nullptr);
         }
         // Extremely slow as it does a fullscreen copy, and also clips any BT.2020 color we might have had
         else if (rtv.get() && is_render_target_float && is_blend_inverted)
         {
            DrawWithOriginalPixelShader(is_main_ui_shader);

            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
            draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
            rtv->GetDesc(&rtv_desc);
            const bool ms = rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS;
            ASSERT_ONCE(rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS || rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D);
         
            // Clip all negative values, like vanilla (I tried to clamp to the closest valid luminance instead, but it created weird colors)
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
      }
      // In any other case, disable the custom UI shaders that have ROV read/writes, given they cannot be conditional (based on runtime branches), and they are slow.
      else
      {
         const uint32_t custom_data_1 = 0; // Make sure we disable ROV/UAV in the shader
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1);
         updated_cbuffers = true;
      
         DrawWithOriginalPixelShader(false);
      }
      
      return DrawOrDispatchOverrideType::Replaced;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Metal Gear Solid 4: Guns of the Patriots\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

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
         "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Metal Gear Solid 4: Guns of the Patriots Luma mod");
      Globals::VERSION = 2;

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

         // Overkill but whatever
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
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomSize
         | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;
      texture_format_upgrades_2d_custom_sizes = {
         // 2160p/1080p videos, for direct upgrading with an HDR boost
         {3840, 2160}, {1920, 1080},
         // Bloom (optionally handled through the "auto_texture_format_upgrade_shader_hashes" defines below instead)
         {4096, 2048}, {2048, 2048}, {1024, 1024}, {512, 512}, {256, 512}, {256, 256}, {128, 256}, {128, 128}, {64, 64}, {32, 32}, {16, 16}, {8, 8}, {4, 4}, {2, 2}
      };

#if 0 // All (?) bloom shaders. Not needed for now.
      auto_texture_format_upgrade_shader_hashes[std::stoul("E4857A56", nullptr, 16)] = { {0}, {} };
      auto_texture_format_upgrade_shader_hashes[std::stoul("9AD4FC56", nullptr, 16)] = { {0}, {} };
      auto_texture_format_upgrade_shader_hashes[std::stoul("68FD4145", nullptr, 16)] = { {0}, {} };
      auto_texture_format_upgrade_shader_hashes[std::stoul("D04AD135", nullptr, 16)] = { {0}, {} };
      auto_texture_format_upgrade_shader_hashes[std::stoul("8D575FD1", nullptr, 16)] = { {0}, {} };
#endif

#if 1 // Might not be needed until proven otherwise (there's no z fighting and upgrading to FP32 doesn't usually fully fix it anyway)
      // The game actually creates depth resources as typeless
      texture_depth_upgrade_formats = {
#if 0 // Already have the best format
         // For shadow maps
         reshade::api::format(DXGI_FORMAT_D32_FLOAT),
         reshade::api::format(DXGI_FORMAT_R32_FLOAT),
         reshade::api::format(DXGI_FORMAT_R32_TYPELESS),
#endif
         // For main rendering. MGS4 uses inverse depth but on UNORM D24 depth, which makes no sense so it'd actually benefit greatly from upgrading depth.
         reshade::api::format(DXGI_FORMAT_D24_UNORM_S8_UINT),
         reshade::api::format(DXGI_FORMAT_R24G8_TYPELESS),
      };
#endif

      enable_indirect_texture_format_upgrades = true;
      // Probably not needed but won't hurt
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

#if DEVELOPMENT && 0 // Disabled outside of dev as they don't do anything, all 3D samplers are already 16x AF
      enable_samplers_upgrade = true;
#endif

      // Note: for now 0x641204D3 is not included as it's for 3D objects. Nor "0xD5C01639" as it's for camera views.
      shader_hashes_UI.pixel_shaders = {
         std::stoul("AC6CABC7", nullptr, 16),
         std::stoul("E7786E92", nullptr, 16),
         std::stoul("B7EB5F39", nullptr, 16),
         std::stoul("1625064C", nullptr, 16),
         std::stoul("9A2AEFBE", nullptr, 16),
      };

#if !DEVELOPMENT 
      old_shader_file_names.emplace("DepthOfFieldCompose_0xC62F6252.ps_5_0.hlsl"); // Renamed
#endif

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("83AC2129", nullptr, 16), "Shadow Map");
      forced_shader_names.emplace(std::stoul("5A82035B", nullptr, 16), "Sky");
      forced_shader_names.emplace(std::stoul("F3EC0381", nullptr, 16), "Sky"); // There's probably many more
      forced_shader_names.emplace(std::stoul("8EBC590F", nullptr, 16), "Wind");
      forced_shader_names.emplace(std::stoul("38875909", nullptr, 16), "Wind");
      forced_shader_names.emplace(std::stoul("F6D774C4", nullptr, 16), "Frost Particles");
      forced_shader_names.emplace(std::stoul("FAB5AE7C", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("9D2CEAA1", nullptr, 16), "UI TV Static");
#endif

      game = new GameMetalGearSolid4();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}