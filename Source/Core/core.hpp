#pragma once

// "_DEBUG" might already be defined in debug?
// Setting it to 0 causes the compiler to still assume it as defined and that thus we are in debug mode (don't change this manually).
#ifndef NDEBUG
#define _DEBUG 1
#endif // !NDEBUG

// Enable when you are developing shaders or code (not debugging, there's "NDEBUG" for that).
// This brings out the "devkit", allowing you to trace draw calls and a lot more stuff.
#ifndef DEVELOPMENT
#define DEVELOPMENT 0
#endif // DEVELOPMENT
// Enable when you are testing shaders or code (e.g. to dump the shaders, logging warnings, etc etc).
// This is not mutually exclusive with "DEVELOPMENT", but it should be a sub-set of it.
// If neither of these are true, then we are in "shipping" mode, with code meant to be used by the final user.
#ifndef TEST
#define TEST 0
#endif // TEST

#define LOG_VERBOSE ((DEVELOPMENT || TEST) && 0)

// Disables loading the ReShade Addon code (useful to test the mod without any ReShade dependencies (e.g. optionally "Prey"))
#define DISABLE_RESHADE 0

#pragma comment(lib, "Gdi32.lib") // For "SetDeviceGammaRamp"
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "Dxva2.lib")   // For "SetMonitorBrightness"

#define _USE_MATH_DEFINES

#ifdef _WIN32
#define ImTextureID ImU64
#endif

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <HighLevelMonitorConfigurationAPI.h> // For "SetMonitorBrightness"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <shared_mutex>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <semaphore>
#include <utility>
#include <cstdint>
#include <functional>
#include <regex>
#include <ranges>

// DirectX dependencies
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

// ReShade dependencies
#include <deps/imgui/imgui.h>
#include <include/reshade.hpp>
#include <source/com_ptr.hpp>
#include <examples/utils/crc32_hash.hpp>
#if 0 // Not needed atm
#include <source/d3d11/d3d11_impl_type_convert.hpp>
#endif

#ifndef FORCE_KEEP_CUSTOM_SHADERS_LOADED
#define FORCE_KEEP_CUSTOM_SHADERS_LOADED 1
#endif // FORCE_KEEP_CUSTOM_SHADERS_LOADED
#ifndef ALLOW_LOADING_DEV_SHADERS
#define ALLOW_LOADING_DEV_SHADERS 1
#endif // ALLOW_LOADING_DEV_SHADERS
#ifndef GEOMETRY_SHADER_SUPPORT
#define GEOMETRY_SHADER_SUPPORT 0
#endif // GEOMETRY_SHADER_SUPPORT
// Not used by mod engines (e.g. Prey)
#ifndef ENABLE_SHADER_CLASS_INSTANCES
#define ENABLE_SHADER_CLASS_INSTANCES 0
#endif // ENABLE_SHADER_CLASS_INSTANCES
// 64x only
#ifndef ENABLE_NGX
#define ENABLE_NGX 0
#endif // ENABLE_NGX
#ifndef ENABLE_FIDELITY_SK
#define ENABLE_FIDELITY_SK 0
#endif // ENABLE_FIDELITY_SK
// Automatically define "ENABLE_SR" if any SR tech is enabled
#ifdef ENABLE_SR
#undef ENABLE_SR
#endif // ENABLE_SR
#if (defined(ENABLE_NGX) && ENABLE_NGX) || (defined(ENABLE_FIDELITY_SK) && ENABLE_FIDELITY_SK)
#define ENABLE_SR 1
#else
#define ENABLE_SR 0
#endif
#ifndef ENABLE_NVAPI
#define ENABLE_NVAPI 0
#endif // ENABLE_NVAPI
#ifndef PROJECT_NAME
// Matches "Globals::MOD_NAME"
#define PROJECT_NAME "Luma"
#endif // PROJECT_NAME
#ifndef ENABLE_GAME_PIPELINE_STATE_READBACK
#define ENABLE_GAME_PIPELINE_STATE_READBACK 0
#endif // ENABLE_GAME_PIPELINE_STATE_READBACK
#ifndef ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 0
#endif // ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
#ifndef ENABLE_POST_DRAW_CALLBACK
#define ENABLE_POST_DRAW_CALLBACK 0
#endif // ENABLE_POST_DRAW_CALLBACK
#ifndef TEST_DUPLICATE_SHADER_HASH
#define TEST_DUPLICATE_SHADER_HASH 0
#endif // TEST_DUPLICATE_SHADER_HASH
#ifndef CHECK_GRAPHICS_API_COMPATIBILITY
#define CHECK_GRAPHICS_API_COMPATIBILITY 0
#endif // CHECK_GRAPHICS_API_COMPATIBILITY

#if DX12
constexpr bool OneShaderPerPipeline = false;
#else
#define DX11 1
constexpr bool OneShaderPerPipeline = true;
#endif

// This might not disable all shaders dumping related code, but it disables enough to remove any performance cost
#ifndef ALLOW_SHADERS_DUMPING
#define ALLOW_SHADERS_DUMPING (DEVELOPMENT || TEST)
#endif

// Only enable this if needed, given that it adds a lot of unnecessary checks for most games
#if CHECK_GRAPHICS_API_COMPATIBILITY
#define SKIP_UNSUPPORTED_DEVICE_API(api, ...)                              \
   do {                                                                    \
      if (!IsSupportedGraphicsAPI(api)) {                                  \
         if constexpr (sizeof(__VA_ARGS__) > 0)                            \
            return __VA_ARGS__;                                            \
         else                                                              \
            return;                                                        \
      }                                                                    \
   } while (0)
#else
#define SKIP_UNSUPPORTED_DEVICE_API(api, ...) ((void)0)
#endif

// Depends on "DEVELOPMENT"
#define TEST_SR (DEVELOPMENT && 0)

#include "dlss/DLSS.h" // see "ENABLE_NGX"
#include "fsr/FSR.h" // see "ENABLE_FIDELITY_SK"

#include "includes/globals.h"
#include "includes/debug.h"
#include "includes/cbuffers.h"
#include "includes/math.h"
#include "includes/matrix.h"
#include "includes/hash.h"
#include "includes/shader_types.h"
#include "includes/recursive_shared_mutex.h"
#include "includes/shaders.h"
#include "includes/shader_define.h"
#include "includes/instance_data.h"
#include "includes/game.h"

#include "utils/format.hpp"
#include "utils/pipeline.hpp"
#include "utils/shader_compiler.hpp"
#include "utils/display.hpp"
#include "utils/resource.hpp"
#include "utils/draw.hpp"
#include "utils/system.h"

#define ICON_FK_CANCEL reinterpret_cast<const char*>(u8"\uf00d")
#define ICON_FK_OK reinterpret_cast<const char*>(u8"\uf00c")
#define ICON_FK_PLUS reinterpret_cast<const char*>(u8"\uf067")
#define ICON_FK_MINUS reinterpret_cast<const char*>(u8"\uf068")
#define ICON_FK_REFRESH reinterpret_cast<const char*>(u8"\uf021")
#define ICON_FK_UNDO reinterpret_cast<const char*>(u8"\uf0e2")
#define ICON_FK_SEARCH reinterpret_cast<const char*>(u8"\uf002")
#define ICON_FK_WARNING reinterpret_cast<const char*>(u8"\uf071")
#define ICON_FK_FILE_CODE reinterpret_cast<const char*>(u8"\uf1c9")

#ifndef RESHADE_EXTERNS
// These are needed by ReShade
extern "C" __declspec(dllexport) const char* NAME = &Globals::MOD_NAME[0];
extern "C" __declspec(dllexport) const char* DESCRIPTION = &Globals::DESCRIPTION[0];
extern "C" __declspec(dllexport) const char* WEBSITE = &Globals::WEBSITE[0];
#endif

// Make sure we can use com_ptr as c arrays of pointers
static_assert(sizeof(com_ptr<ID3D11Resource>) == sizeof(void*));

using namespace Shader;
using namespace Math;

namespace
{
   constexpr uint32_t HASH_CHARACTERS_LENGTH = 8;
   const std::string NAME_ADVANCED_SETTINGS = std::string(NAME) + " Advanced";

   // A default "template" (empty) game data that we can fall back to in case we didn't specify any
   Game default_game = {};
   // The pointer to the current game implementation (data and code), it can be replaced
   Game* game = &default_game;

   // Mutexes:
   // For "pipeline_cache_by_pipeline_handle", "pipeline_cache_by_pipeline_clone_handle", "pipeline_caches_by_shader_hash" and "cloned_pipeline_count"
   recursive_shared_mutex s_mutex_generic;
   // For "shaders_to_dump", "dumped_shaders", "dumped_shaders_meta_paths", "shader_cache". In general for dumping shaders to disk (this almost always needs to be read and write locked together so there's no need for it to be a shared mutex)
   std::recursive_mutex s_mutex_dumping;
   // For "custom_shaders_cache", "pipelines_to_reload". In general for loading shaders from disk and compiling them
   recursive_shared_mutex s_mutex_loading;
   // Mutex for created shader DX objects (and "created_native_shaders")
   std::shared_mutex s_mutex_shader_objects;
   // Mutex for shader defines ("shader_defines_data", "code_shaders_defines", "shader_defines_data_index")
   std::shared_mutex s_mutex_shader_defines;
   // Mutex to deal with data shared with ReShade, like ini/config saving and loading (including "cb_luma_global_settings")
   std::shared_mutex s_mutex_reshade;
   // For "custom_sampler_by_original_sampler" and "texture_mip_lod_bias_offset"
   std::shared_mutex s_mutex_samplers;
   // For "global_native_devices", "global_devices_data", "game_window"
   recursive_shared_mutex s_mutex_device;
#if DEVELOPMENT
   // for "trace_count" and "trace_scheduled" and "trace_running"
   std::shared_mutex s_mutex_trace;
#endif

   // Dev or User settings:
   bool auto_dump = (bool)ALLOW_SHADERS_DUMPING;
   bool auto_load = true;
#if TEST && !DEVELOPMENT
   bool reshade_effects_toggle_to_display_mode_toggle = true;
#elif DEVELOPMENT
	bool reshade_effects_toggle_to_display_mode_toggle = false;
#endif
#if DEVELOPMENT
   bool compile_clear_all_shaders = false; // If true, even shaders that aren't used by the current mod will be compiled, for a test
   bool trace_show_command_list_info = true;
   bool trace_ignore_vertex_shaders = true;
   bool trace_ignore_buffer_writes = true;
   bool trace_ignore_bindings = true;
   bool trace_ignore_non_bound_shader_referenced_resources = true;
   bool allow_replace_draw_nans = false;
#endif // DEVELOPMENT
   constexpr bool precompile_custom_shaders = true; // Async shader compilation on boot
   constexpr bool block_draw_until_device_custom_shaders_creation = true; // Needs "precompile_custom_shaders". Note that drawing (and "Present()") could be blocked anyway due to other mutexes on boot if custom shaders are still compiling
#if !TEST && !DEVELOPMENT
   constexpr
#endif 
   bool custom_shaders_enabled = true;
#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
   bool strip_original_shaders_debug_data = false;
#endif

#if ENABLE_SR
   SR::UserType sr_user_type = SR::UserType::Auto; // If set to a non "None" value, some SR tech is enabled by the user (but not necessarily supported+initialized correctly, that's by device)
   std::map<SR::Type, std::unique_ptr<SR::SuperResolutionImpl>> sr_implementations; // All the SR implementations class objects (not necessarily initialized, nor fully loaded)

   const char* sr_game_tooltip = "";
#endif // ENABLE_SR

   bool hdr_enabled_display = false;
   bool hdr_supported_display = false;
   constexpr bool prevent_shader_cache_loading = false;
   bool prevent_shader_cache_saving = false;
#if DEVELOPMENT
   int frame_sleep_ms = 0;
   int frame_sleep_interval = 1;
#endif // DEVELOPMENT
#if DEVELOPMENT || TEST
   int test_index = 0; //TODOFT5: remove most of the calls to this once Prey performance is fixed
#else
   constexpr int test_index = 0;
#endif // DEVELOPMENT || TEST

   // Upgrades
   namespace
   {
      enum class DisplayModeType : uint
      {
         SDR = 0,
         // Could be scRGB or HDR10
         HDR = 1,
         SDRInHDR = 2,
      };
      const char* display_mode_preset_strings[3] = {
          "SDR", // SDR (80 nits) on scRGB HDR for SDR (gamma sRGB, because Windows interprets scRGB as sRGB)
          "HDR",
          "SDR on HDR", // (Fake) SDR (baseline to 203 nits) on scRGB HDR for HDR (gamma 2.2) - Dev only, for quick comparisons
      };

      enum class TextureFormatUpgradesType : uint8_t
      {
         None,
         AllowedDisabled,
         AllowedEnabled
      };
      enum class SwapchainUpgradeType : uint8_t
      {
         // keep the original one, SDR or whatnot
         None,
         // scRGB linear HDR (16 bit float)
         scRGB,
         // BT.2020 PQ HDR (10 bit UNORM)
         HDR10
      };

      //
      // Swapchain/Window
      //

      TextureFormatUpgradesType swapchain_format_upgrade_type = TextureFormatUpgradesType::None; // Only swap between allowed enabled/disabled after init
      SwapchainUpgradeType swapchain_upgrade_type = SwapchainUpgradeType::scRGB; // TODO: finish implementing HDR10 output (as input it'd be harder but it might be possible as a "POST_PROCESS_SPACE_TYPE")

      // For now, by default, we prevent fullscreen on boot and later, given that it's pointless.
      // If there were issues, we could exclusively do it when the swapchain resolution matched the monitor resolution.
      bool prevent_fullscreen_state = true;
      // Force borderless instead when the game tried to go to FSE (and when leaving it too)
      bool force_borderless = false;

      //
      // Textures
      //

      // Only needed by "texture_format_upgrades_2d_custom_aspect_ratios" at the moment (if changed after initialization)
      std::shared_mutex s_mutex_texture_upgrades;

      TextureFormatUpgradesType texture_format_upgrades_type = TextureFormatUpgradesType::None; // Only swap between allowed enabled/disabled after init
      // Whether texture upgrades are done directly on the original resource, or on an upgraded mirrored version of it that we keep separately and live replace when the original resource is referenced.
      // Indirect upgrades might be safer, and can be made more selectively, to avoid upgrading random textures, though they also keep the original texture so memory usage might go up.
      // This will fail to replace references to resources if the game had DLSS/Streamline calls.
      // These are sometimes referred to as: indirect, mirrored, proxy, redirected, cloned, ...
      bool enable_indirect_texture_format_upgrades = false;
      // Automatically upgrade all textures
      // Note that indirect texture mirrors might still be automatically created if "texture_format_upgrades_type" is enabled, in case the game tried to copy an upgraded resource into an incompatible one that wasn't upgraded etc.
      bool enable_automatic_indirect_texture_format_upgrades = false;
      // Allows to temporarily ignore indirectly upgraded textures
      // In publishing mode, there's no need to ever forcefully ignore the indirectly upgraded textures,
      // given that the settings can't change live, hence they are not created if they are not enabled in the first place.
      PUBLISHING_CONSTEXPR bool ignore_indirect_upgraded_textures = false; // TODO: test why when this is turned off live in Lego City Undercover, the output breaks
#if DEVELOPMENT && 0 // WIP
      // From 0.5 to 2.0, resolution multiplier. Ideally exactly 0.5 or 2.0 (beside 1.0).
      // It should work until any shader does ".Load()" on a texture, or gets their dimensions from shaders.
      float indirect_upgraded_textures_size_scaling = 1.f;
#endif
	   // List of render targets (and unordered access) textures that we upgrade to R16G16B16A16_FLOAT.
      // Most formats are supported but some might not act well when upgraded.
      std::unordered_set<reshade::api::format> texture_upgrade_formats;
      // Redirect incompatible copies between UNORM and FLOAT textures to a custom pixel shader that would do the same (not globally compatible).
      // This can happen if the game uses a temp texture that isn't either a render target nor is unordered access, so we don't upgrade it.
      bool enable_upgraded_texture_resource_copy_redirection = true; // TODO: delete given that we now have "enable_indirect_texture_format_upgrades"
      // TODO: Add a warning for textures we missed upgrading if the swapchain resolution changed later.
      enum class TextureFormatUpgrades2DSizeFilters : uint32_t
      {
         // If the flags are set to 0, we upgrade all textures independently of their size. This flag can't be added separately!
         All = 0,
         // The output resolution (usually matches the window resolution too).
         SwapchainResolution = 1 << 0,
         // The rendering resolution (e.g. for TAA and other types of upscaling).
         RenderResolution = 1 << 1,
         // The aspect ratio of the swapchain texture.
         // This can be useful for bloom or resolution scaling etc.
         // Ideally we'd also check the rendering resolution, but we can't really reliably determine it until rendering has started and textures have been created.
         SwapchainAspectRatio = 1 << 2,
         RenderAspectRatio = 1 << 3,
         // A custom aspect ratio (defaulted to 16:9, because that's the global standard).
         // It can be useful for games that don't support UltraWide or 4:3 resolutions and internally force 16:9 rendering, while having a fullscreen swapchain with black bars.
         CustomAspectRatio = 1 << 4,
         // All mip chain sizes based starting from the highest resolution between rendering and swapchain resolution (they should generally have the same aspect ratio anyway) to 1.
         // This can be useful for blur passes etc, if they used power of 2 mips, instead of simply halving the base resolution.
         Mips = 1 << 5,
         // Upgrade textures cubes (of all sizes), these are sometimes used by old games to do reflections (e.g. Burnout Revenge cars reflections)
         Cubes = 1 << 6,
         // Checks the swapchain/output resolution width only (e.g. used by games that add horizontal lines, like "Thumper" or "Beyond: Two Souls").
         // These are usually hard to match to an aspect ratio without using the "CustomAspectRatio" with a manually found aspect ratio,
         // and thus mips like bloom might be missing
         SwapchainResolutionWidth = 1 << 7,
         SwapchainResolutionHeight = 1 << 8,
         // Avoid upgrading 1x1 textures
         No1Px = 1 << 9,
      };
      uint32_t texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      std::unordered_set<float> texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f };
      // Most games do resolution scaling properly, with a maximum aspect ratio offset of 1 pixel, though occasionally it goes to 2 pixels of difference.
      // Set to 0 to only accept 100% matching aspect ratio.
      uint32_t texture_format_upgrades_2d_aspect_ratio_pixel_threshold = 1;
      // The size of the LUT we might want to upgrade, whether it's 1D, 2D or 3D.
      // LUTs in most games are 16x or 32x, though in some cases they might be 15x, 31x, 48x, 64x etc.
      uint32_t texture_format_upgrades_lut_size = -1;
      enum class LUTDimensions
      {
         _1D,
         _2D,
         _3D
      };
      LUTDimensions texture_format_upgrades_lut_dimensions = LUTDimensions::_2D;
      // Similar to "texture_upgrade_formats" but allows upgrading depth to R32_FLOAT/D32_FLOAT instead (e.g. useful in old games, especially when they allocated bits for stencil without using them)
      std::unordered_set<reshade::api::format> texture_depth_upgrade_formats;

      // Automatically upgrade the formats of the textures this shader pass draws to. Generally best used on shaders that originally encoded from HDR (native rendering) to SDR. If the source textures were SDR too (UNORM), they'd need to be upgraded through other means.
      // First pair value is the RTVs indexes to upgrade, the second one the UAVs (whether it's a pixel or compute shader).
      // This is meant to be used if "enable_indirect_texture_format_upgrades" is off, or if very specific custom upgrades are needed.
      std::unordered_map<uint32_t, std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> auto_texture_format_upgrade_shader_hashes;

      //
      // UI
      //

      // If enabled, all the UI will be drawn onto a separate (e.g.) UNORM texture and composed back with the game scene at the very end, at least in case the scene is rendering.
      // This has multiple advantages:
      // - It allows the UI scene background to be tonemapped, increasing the UI readibility (this can be important in some games).
      // - The scene rendering can be kept in linear scRGB even after encoding, as it doesn't need to blended with UI in gamma space.
      // - The scene rendering doesn't needs to be scaled by the inverse of the UI brightness to allow UI brightness scaling.
      // - The UI avoids generating NaNs on the float upgraded float backbuffer.
      // - It avoids the UI subtracting colors as it did in SDR with UNORM textures (that were limited to 0), which cause large negative values with upgraded float textures.
      // - It can do AutoHDR on the game scene only given that the UI is in a different layer.
      // - It allows hiding the UI.
      // - It allows to possibly dither the UI separately, or scale parts of the UI in size.
      // - It allows to change the vanilla blending formula in case alpha blends were ugly.
      // Note that drawing pre-multiplied UI on a separate texture can cause a slightly additional loss of quality, but the render target format can be upgraded to make it even better looking than vanilla.
      bool enable_ui_separation = false; // TODO: remove dependency with "UI_DRAW_TYPE" 3
      // Leave unknown to automatically retrieve it from the swapchain, though that's not necessarily the right value,
      // especially if the UI used R8G8B8A8_UNORM instead of R10G10B10A2_UNORM/R16G16B16A16_FLOAT as the swapchain could have been, or in case it flipped sRGB views on or off compared to the swapchain.
      // It's important to pick a format that has the same "encoding" as the original game, to preserve the look of alpha blends, so keep linear space for linear space and gamma space for gamma space.
      // Note: as of now the UI is expected to be gamma space during composition. // TODO: fix that when re-working the display composition and per game gamma settings! We should have an ecoding for draw and one for blending? Maybe using different views?
      // 
      // For high quality SDR use "DXGI_FORMAT_R16G16B16A16_UNORM", 16 bits might reduce banding. This is best for gamma space UI but can also work in linear.
      // For high quality HDR use "DXGI_FORMAT_R16G16B16A16_FLOAT", though this can trigger NaNs or negative luminances (that you might want to preserve). This is best for linear space UI but can also work in gamma. The UI in SDR games might still go into HDR if it's ever additive.
      // For linear space SDR use "DXGI_FORMAT_R8G8B8A8_UNORM_SRGB", especially if there's no need to upgrade the bit depth, gamut and dynamic range of the UI.
      // For gamma space SDR use "DXGI_FORMAT_R8G8B8A8_UNORM", especially if there's no need to upgrade the bit depth, gamut and dynamic range of the UI.
      // For high quality gamma space SDR use "DXGI_FORMAT_R10G10B10A2_UNORM", it can alternatively be used to improve the quality if you are sure the game doesn't read back alpha (you can try with "DXGI_FORMAT_R11G11B10_FLOAT" as a test, and see if any of the UI looks different, given it has no alpha).
      DXGI_FORMAT ui_separation_format = DXGI_FORMAT_UNKNOWN;
      // Optionally add the UI shaders to this list, to make sure they draw to a separate render target for proper HDR composition
      ShaderHashesList shader_hashes_UI;
      // Shaders that might be running after "has_drawn_main_post_processing" has turned true, but that are still not UI (most games don't have a fixed last shader that runs on the scene rendering before UI, e.g. FXAA might add a pass based on user settings etc), so we have to exclude them like this
      ShaderHashesList shader_hashes_UI_excluded;
      // Hides gameplay UI (or well, any UI that draws when the main scene also draws, some games always render the main scene, even behind pause or main menus).
      // Requires "enable_ui_separation"
      bool hide_ui = false;

      //
      // Display
      //

      // Only enable this in games that use the feature, otherwise it's gonna add an unnecessary confusing button.
      bool allow_disabling_gamma_ramp = false;
      // Ignored if <= 0. Can be used to change all behaviours that use gamma 2.2 to 2.35 or 2.4 or whatever other value.
      // This will apply to all decoding and decoding gamma functions.
      float custom_sdr_gamma = 0.f;

      //
      // Samplers
      //

      // Can't be changed after boot
      bool enable_samplers_upgrade = false;
      int samplers_upgrade_mode = 4;
#if DEVELOPMENT
      int samplers_upgrade_mode_2 = 0;
#endif
      PUBLISHING_CONSTEXPR bool custom_texture_mip_lod_bias_offset = false; // Live edit
   }

   // The root path of all the shaders we load and dump
   std::filesystem::path shaders_path;

   // In case this is a generic mod for multiple games, set this to the actual game name (e.g. "GAME_ROCKET_LEAGUE")
   const char* sub_game_shader_define = nullptr;
   // Optionally define an appended name for our current game, it might apply to shader names and dump folders etc
   std::string sub_game_shaders_appendix;

   // In case a single hlsl file needed multiple hashes (e.g. many tonemapper permutations in some games),
   // we can add "0x########" to their name as one of its hashes, and it will then take the actual hashes from this list,
   // that can be filled from any game's code.
   std::unordered_map<std::string, std::unordered_set<std::string>> redirected_shader_hashes;

#if DEVELOPMENT
   // Allows games to hardcode in some shader names by hash, to easily identify them across reboots (we could just store this in the user ini, but that info wouldn't be persistent)
   std::unordered_map<uint32_t, std::string> forced_shader_names;

   // Replace the shaders load and dump directory
   std::filesystem::path custom_shaders_path;
#else
   // Shader files (e.g. hlsl) that we have removed from the mode since the original version, so we can force delete them from the user shaders folder on boot,
   // to avoid the mod loading conflicting with new shaders with the same hash (e.g. renamed file), or simply replacing passes that don't need replacement anymore.
   std::unordered_set<std::string> old_shader_file_names;
#endif

   // Game specific constants (these are not expected to be changed at runtime)
   uint32_t luma_settings_cbuffer_index = 13;
   uint32_t luma_data_cbuffer_index = -1; // Needed, unless "POST_PROCESS_SPACE_TYPE" is 0 and we don't need the final display composition pass
	uint32_t luma_ui_cbuffer_index = -1; // Optional, for "UI_DRAW_TYPE" 1

   // A list of luma native shader definitions, to be later compiled in the device data. The hash can be used globally to identify and retrieve them (they need to be unique!).
   // We define the shader name and the file shader because we can make different permutations out of the same code file.
   // Games can add shaders to this list, though they should generally not remove from it.
   // Do not edit this after init, it's not thread safe.
   // As of now, these need the "Luma_" prefix in the file.
   std::unordered_map<uint32_t, ShaderDefinition> native_shaders_definitions =
   {
      { CompileTimeStringHash("Copy VS"), { "Luma_Copy_VS", reshade::api::pipeline_subobject_type::vertex_shader } },
      { CompileTimeStringHash("Copy PS"), { "Luma_Copy_PS", reshade::api::pipeline_subobject_type::pixel_shader } },

      { CompileTimeStringHash("Display Composition"), { "Luma_DisplayComposition", reshade::api::pipeline_subobject_type::pixel_shader } },
      
      { CompileTimeStringHash("Copy RGB Max 0 A Sat"), { "Luma_Copy_PS", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, { { "RGB_MAX_0", "1" }, { "A_SAT", "1" } } } },
      { CompileTimeStringHash("Copy RGB Max 0 A Sat MS"), { "Luma_Copy_PS", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, { { "MS", "1" }, { "RGB_MAX_0", "1" }, { "A_SAT", "1" } } } },
      { CompileTimeStringHash("Sanitize A"), { "Luma_SanitizeAlpha", reshade::api::pipeline_subobject_type::compute_shader } },
      { CompileTimeStringHash("Sanitize RGBA CS"), { "Luma_SanitizeRGBA", reshade::api::pipeline_subobject_type::compute_shader } },
      { CompileTimeStringHash("Sanitize RGBA PS"), { "Luma_SanitizeRGBA", reshade::api::pipeline_subobject_type::pixel_shader } },
      { CompileTimeStringHash("Sanitize RGBA Mipped"), { "Luma_SanitizeRGBAMipped", reshade::api::pipeline_subobject_type::pixel_shader } },
      { CompileTimeStringHash("Gen Sanitized Mip"), { "Luma_GenerateSanitizedMip_CS", reshade::api::pipeline_subobject_type::compute_shader } },

      { CompileTimeStringHash("Draw Purple PS"), { "Luma_DrawColor_PS", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, { { "COLOR", "float4(1.0, 0.0, 1.0, 1.0)" } } } },
      { CompileTimeStringHash("Draw Purple CS"), { "Luma_DrawColor_CS", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, { { "COLOR", "float4(1.0, 0.0, 1.0, 1.0)" } } } },
      { CompileTimeStringHash("Draw NaN PS"), { "Luma_DrawColor_PS", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, { { "COLOR", "FLT_NAN" } } } },
      { CompileTimeStringHash("Draw NaN CS"), { "Luma_DrawColor_CS", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, { { "COLOR", "FLT_NAN" } } } },

      // TODO: finish these (e.g. use shader defines to make branches!)
      { CompileTimeStringHash("Normalize LUT 3D"), { "Luma_NormalizeLUT3D", reshade::api::pipeline_subobject_type::compute_shader } },
#if 0
      { CompileTimeStringHash("Normalize LUT 2D"), { "Luma_NormalizeLUT2D", reshade::api::pipeline_subobject_type::compute_shader } },
      { CompileTimeStringHash("Normalize LUT 1D"), { "Luma_NormalizeLUT1D", reshade::api::pipeline_subobject_type::compute_shader } },
      { CompileTimeStringHash("Unclip LUT 3D"), { "Luma_UnclipLUT3D", reshade::api::pipeline_subobject_type::compute_shader } },
      { CompileTimeStringHash("Unclip LUT 2D"), { "Luma_UnclipLUT2D", reshade::api::pipeline_subobject_type::compute_shader } },
      { CompileTimeStringHash("Unclip LUT 1D"), { "Luma_UnclipLUT1D", reshade::api::pipeline_subobject_type::compute_shader } },
#endif
   };

   // TODO: make the data in these a unique ptr for easier handling, and the shader binary data contained inside of "CachedShader" too.
   // All the shaders the game ever loaded (including the ones that have been unloaded). Only used by shader dumping (if "ALLOW_SHADERS_DUMPING" is on) or to see their binary code in the ImGUI view. By original shader binary hash.
   // The data it contains is fully its own, so it's not by "Device". These are "immutable" once set.
   // Might be empty in some build configurations.
   std::unordered_map<uint32_t, CachedShader*> shader_cache;
   // All the shaders the user has (and has had) as custom in the shader folders (whether they are game specific or Luma global/native shaders). By the original shader binary hash (unless they are Luma native shaders, in that case it'd be their custom hash).
   // The data it contains is fully its own, so it's not by "Device".
   // The hash here is 64 bit instead of 32 to leave extra room for Luma native shaders, that have customly generated hashes (to not mix with the game ones).
   std::unordered_map<uint64_t, CachedCustomShader*> custom_shaders_cache;

   // Shader dumping
   namespace
   {
      // Newly loaded shaders that still need to be (auto) dumped, by shader hash
      std::unordered_set<uint32_t> shaders_to_dump;
      // All the shaders we have already dumped, by shader hash
      std::unordered_set<uint32_t> dumped_shaders;
      std::unordered_set<uint32_t> meta_stored_shaders;
      std::filesystem::path shaders_dump_path;
#if DEVELOPMENT
      std::unordered_map<uint32_t, std::filesystem::path> dumped_shaders_meta_paths;
      uint32_t shader_cache_count = 0;
#endif
   }

   std::string shaders_compilation_errors; // errors and warning log

   // List of define values read by our settings shaders
   std::unordered_map<std::string, uint8_t> code_shaders_defines;

   // These default should ideally match shaders values (Settings.hlsl), but it's not necessary because whatever the default values they have they will be overridden.
   // For further descriptions, see their shader declarations.
   // TODO: add grey out conditions (another define, by name, whether its value is > 0, or flipped (we can already manually toggle their editability manually)), and "category"
   std::vector<ShaderDefineData> shader_defines_data = {
       {"DEVELOPMENT", DEVELOPMENT ? '1' : '0', true, DEVELOPMENT ? false : true, "Enables some development/debug features that are otherwise not allowed (use a DEVELOPMENT build if you want to use this)", 1},
       {"TEST", (TEST || DEVELOPMENT) ? '1' : '0', true, (TEST || DEVELOPMENT) ? false : true, "Enables some test features to aid with development (use a DEVELOPMENT or TEST build if you want to use this)", 1},
       // Usually if we store in gamma space, we also keep the paper white not multiplied in until we apply it on the final output, while if we store in linear space, we pre-multiply it in (and we might also pre-correct gamma before the final output).
       // NOTE: "POST_PROCESS_SPACE_TYPE" 2 is actually implemented as well but only used by Prey.
       {"POST_PROCESS_SPACE_TYPE", '0', true, DEVELOPMENT ? false : true, "Describes in what \"space\" (encoding) the game post processing color buffers are stored\n0 - SDR Gamma space\n1 - Linear space"},
       {"EARLY_DISPLAY_ENCODING", '0', true, DEVELOPMENT ? false : true, "Whether the main gamma correction and paper white scaling happens early in post processing or in the final display composition pass (only applies if \"POST_PROCESS_SPACE_TYPE\" is set to linear)", 1},
       // Sadly most games encoded with sRGB (or used linear sRGB buffers, similar thing), so that's the default here
       {"VANILLA_ENCODING_TYPE", '0', true, DEVELOPMENT ? false : true, "0 - sRGB\n1 - Gamma 2.2"},
       {"GAMMA_CORRECTION_TYPE", '1', true, false, "(HDR only) Emulates a specific SDR transfer function\nThis is best left to \"1\" (Gamma 2.2) unless you have crushed blacks or overly saturated colors\n0 - sRGB\n1 - Gamma 2.2\n2 - sRGB (color hues) with gamma 2.2 luminance (corrected by channel)\n3 - sRGB (color hues) with gamma 2.2 luminance (corrected by luminance)\n4 - Gamma 2.2 (corrected by luminance) with per channel correction chrominance"},
       {"GAMUT_MAPPING_TYPE", '0', true, DEVELOPMENT ? false : true, "The type of gamut mapping that is needed by the game.\nIf rendering and post processing don't generate any colors beyond the target gamut, there's no need to do gamut mapping.\n0 - None\n1 - Auto (SDR/HDR)\n2 - SDR (BT.709)\n3 - HDR (BT.2020)"},
       {"UI_DRAW_TYPE", '0', true, DEVELOPMENT ? false : true, "Describes how the UI draws in\n0 - Raw (original linear to linear or gamma to gamma draws) (no custom UI paper white control)\n1 - Direct Custom (gamma to linear adapted blends)\n2 - Direct (inverse scene brightness draws)\n3 - Separate (renders the UI on a separate texture, allows tonemapping of UI background)", 3},
#if DEVELOPMENT
       {"TEST_2X_ZOOM", '0', true, false, "Allows you to zoom in into the film image center to better analyze it", 1},
#endif
       {"TEST_SDR_HDR_SPLIT_VIEW_MODE", '0', true, false, "Allows you to clamp to SDR on a portion of the screen, to run quick comparisons between SDR and HDR\n(note that the tonemapper might still run in HDR mode and thus clip further than it would have had in SDR)", 1},
       {"TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL", '0', true, DEVELOPMENT ? false : true, "Tells whether \"TEST_SDR_HDR_SPLIT_VIEW_MODE\" is natively implemented in the game's tonemapper, outputting some SDR and some HDR, or if it's not and we are simply clipping to SDR at the very end", 1},
   };

   // TODO: if at runtime we can't edit "shader_defines_data" (e.g. in non dev modes), then we could directly set these to the index value of their respective "shader_defines_data" and skip the map?
   // TODO: use "CompileTimeStringHash" to do these at runtime? Or swap shader definitions to also use constexpr variables instead of strings.
   constexpr uint32_t DEVELOPMENT_HASH = char_ptr_crc32("DEVELOPMENT");
   constexpr uint32_t POST_PROCESS_SPACE_TYPE_HASH = char_ptr_crc32("POST_PROCESS_SPACE_TYPE"); // TODO: split this into a few variables... UI space, render space, post process space, and then a variable telling at what point we are.
   constexpr uint32_t EARLY_DISPLAY_ENCODING_HASH = char_ptr_crc32("EARLY_DISPLAY_ENCODING");
   constexpr uint32_t VANILLA_ENCODING_TYPE_HASH = char_ptr_crc32("VANILLA_ENCODING_TYPE");
   constexpr uint32_t GAMMA_CORRECTION_TYPE_HASH = char_ptr_crc32("GAMMA_CORRECTION_TYPE");
   constexpr uint32_t GAMUT_MAPPING_TYPE_HASH = char_ptr_crc32("GAMUT_MAPPING_TYPE");
   constexpr uint32_t UI_DRAW_TYPE_HASH = char_ptr_crc32("UI_DRAW_TYPE");
   constexpr uint32_t TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH = char_ptr_crc32("TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL");

   // uint8_t is enough for MAX_SHADER_DEFINES
   std::unordered_map<uint32_t, uint8_t> shader_defines_data_index;

   // Global data (not device dependent really):

   // Directly from cbuffer
   CB::LumaGlobalSettingsPadded cb_luma_global_settings = { }; // Not in device data as this stores some users settings too // Set "cb_luma_global_settings_dirty" when changing within a frame (so it's uploaded again)
   CB::LumaGameSettings default_luma_global_game_settings;

   bool hdr_display_mode_pending_auto_peak_white_calibration = false;

   bool has_init = false;
   bool asi_loaded = true; // Whether we've been loaded from an ASI loader or ReShade Addons system (we assume true until proven otherwise)
   std::thread thread_auto_dumping;
   std::atomic<bool> thread_auto_dumping_running = false;
   std::thread thread_auto_compiling;
   std::atomic<bool> thread_auto_compiling_running = false;
   bool last_pressed_unload = false;
   bool needs_unload_shaders = false;
   bool needs_load_shaders = false; // Load/compile or reload/recompile shaders, no need to default it to true, we have "auto_load" for that

   // There's only one swapchain and one device in most games (e.g. Prey), but the game changes its configuration from different threads.
   // A new device+swapchain can be created if the user resets the settings as the old one is still rendering (or is it?).
   // These are raw pointers that did not add a reference to the counter.
   std::vector<ID3D11Device*> global_native_devices; // Possibly unused
   std::vector<DeviceData*> global_devices_data;
   HWND game_window = 0; // This is fixed forever (in almost all games (e.g. Prey))
#if DEVELOPMENT
   HHOOK game_window_proc_hook = nullptr;
   WNDPROC game_window_original_proc = nullptr;
   WNDPROC game_window_custom_proc = nullptr;
#endif
   thread_local bool last_swapchain_linear_space = false;
   thread_local bool waiting_on_upgraded_resource_init = false;
   thread_local reshade::api::resource_desc upgraded_resource_init_desc = {};
   thread_local void* upgraded_resource_init_data = {};
   thread_local std::unordered_map<uint64_t, reshade::api::subresource_data*> upgraded_mapped_resources;
#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
   // Temporary cache of the live patched shader that points at the original shader
   thread_local const void* last_live_patched_original_shader_code = {};
   thread_local size_t last_live_patched_original_shader_size = {};
   // The actual current pre-calculated shader hash (of the live patched shader, if it was)
   thread_local uint64_t last_live_patched_shader_hash = -1;
#endif
#if DEVELOPMENT
   // ReShade specific design as we don't get a rejection between the create and init events if creation failed
   thread_local reshade::api::format last_attempted_upgraded_resource_creation_format = reshade::api::format::unknown;
   thread_local reshade::api::format last_attempted_upgraded_resource_view_creation_view_format = reshade::api::format::unknown;

   bool trace_scheduled = false; // For next frame
   bool trace_running = false; // For this frame
   uint32_t trace_count = 0; // Not exactly necessary but... it might help

   std::string last_drawn_shader = ""; // Not exactly thread safe but it's fine...

   thread_local reshade::api::command_list* thread_local_cmd_list = nullptr; // Hacky global variable (possibly not cleared, stale), only use to quickly tell the command list of the thread
   thread_local DrawDispatchData last_draw_dispatch_data = {};

   std::unordered_map<const ID3D11InputLayout*, std::vector<D3D11_INPUT_ELEMENT_DESC>> input_layouts_descs;

   // Textures debug drawing
   namespace
   {
      uint32_t debug_draw_shader_hash = 0;
      char debug_draw_shader_hash_string[HASH_CHARACTERS_LENGTH + 1] = {};
      uint64_t debug_draw_pipeline = 0; // In DX11 shaders and pipelines are the same thing so this is kinda useless (excluding the rare case where multiple pipelines (compiled shader objects) were based on the same shader, and hence had the same hash)
      int32_t debug_draw_pipeline_target_instance = -1;
      std::thread::id debug_draw_pipeline_target_thread = std::thread::id(); // Filter target instances by thread, which would hopefully reliably match the command lists, over multiple frames

      std::atomic<int32_t> debug_draw_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases

      DebugDrawMode debug_draw_mode = DebugDrawMode::Custom;
      bool debug_draw_freeze_inputs = false; // Allows freezing the inputs/state of this pass, allowing you to edit the shader and seeing the results live. This is bundled with the textures debug draw feature because generally you don't need to freeze the inputs of any prior pass if not the very same one you are analyzing (or iterating upon). If the game stopped drawing the shader, debugging would also stop.
      int32_t debug_draw_view_index = 0;
      uint32_t debug_draw_options = (uint32_t)DebugDrawTextureOptionsMask::Fullscreen | (uint32_t)DebugDrawTextureOptionsMask::BackgroundPassthrough | (uint32_t)DebugDrawTextureOptionsMask::Tonemap;
      int32_t debug_draw_mip = 0;
      bool debug_draw_pipeline_instance_filter_by_thread = false; // Not true by default as it seems like quite a few games set up multiple render threads, so this conflicts between frames
      bool debug_draw_auto_clear_texture = false;
      bool debug_draw_replaced_pass = false; // Whether we print the debugging of the original or replaced pass (the resources bindings etc might be different, though this won't forcefully run the original pass if it was skipped by the game's mod custom code, so this isn't fully reliable)
      bool debug_draw_auto_gamma = true;
   }

   // Constant Buffers tracking
   namespace
   {
      uint64_t track_buffer_pipeline = 0; // Can be any type of shader
      int32_t track_buffer_pipeline_target_instance = -1;
      int32_t track_buffer_index = 0;

      std::atomic<int32_t> track_buffer_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases
   }

   // CB Dev settings settings
   namespace
   {
      CB::LumaDevSettings cb_luma_dev_settings_default_value(0.f);
      CB::LumaDevSettings cb_luma_dev_settings_min_value(0.f);
      CB::LumaDevSettings cb_luma_dev_settings_max_value(1.f);
      std::array<std::string, CB::LumaDevSettings::SettingsNum> cb_luma_dev_settings_names;
      bool cb_luma_dev_settings_set_from_code = false; // Set this to true to disable dev settings reflections from shaders
      bool cb_luma_dev_settings_edit_mode = false;
   }
#endif // DEVELOPMENT

   // Forward declares:
   void DumpShader(uint32_t shader_hash);
   void AutoDumpShaders();
   void AutoLoadShaders(DeviceData* device_data);
   void OnDestroyPipeline(reshade::api::device* device, reshade::api::pipeline pipeline);

   // Returns true if any shader or pipeline has been replaced, meaning that the mod will at least do something (this is representative of how most, but not necessarily all, mods work)
   bool IsModActive(const DeviceData& device_data)
   {
   #if GAME_UNREAL_ENGINE
      return true;
   #endif
      // Note: we don't check "custom_shaders_enabled" here because we want to simulate the mod still being treated as active in that case
      return device_data.cloned_pipeline_count != 0;
   }

   // Quick and unsafe. Passing in the hash instead of the string is the only way make sure strings hashes are calculate them at compile time.
   __forceinline ShaderDefineData& GetShaderDefineData(uint32_t hash)
   {
#if 0 // We don't lock "s_mutex_shader_defines" here as it wouldn't be particularly relevant (it won't lead to crashes, as generaly they are not edited in random threads, though having it enabled could lead to deadlocks if there's nested locks!).
      const std::shared_lock lock(s_mutex_shader_defines);
#endif
      assert(shader_defines_data_index.contains(hash));
#if DEVELOPMENT // Just to avoid returning a random variable while developing
      if (!shader_defines_data_index.contains(hash))
      {
         static ShaderDefineData defaultShaderDefineData;
         return defaultShaderDefineData;
      }
#endif
      return shader_defines_data[shader_defines_data_index[hash]];
   }
   __forceinline uint8_t GetShaderDefineCompiledNumericalValue(uint32_t hash)
   {
      return GetShaderDefineData(hash).GetCompiledNumericalValue();
   }

   // TODO: add a setting to compile all the shaders in all game folders at once to quickly test them
   // Ideally only to be called once on boot and cached
   std::filesystem::path GetShadersRootPath()
   {
      std::filesystem::path shaders_path = System::GetModulePath();
      shaders_path = shaders_path.parent_path();
      std::string name_safe = Globals::MOD_NAME;
      // Remove the common invalid characters
      std::replace(name_safe.begin(), name_safe.end(), ' ', '-');
      std::replace(name_safe.begin(), name_safe.end(), ':', '-');
      std::replace(name_safe.begin(), name_safe.end(), '"', '-');
      std::replace(name_safe.begin(), name_safe.end(), '?', '-');
      std::replace(name_safe.begin(), name_safe.end(), '*', '-');
      std::replace(name_safe.begin(), name_safe.end(), '/', '-');
      std::replace(name_safe.begin(), name_safe.end(), '\\', '-');
      shaders_path /= name_safe;

#if DEVELOPMENT
      if (!custom_shaders_path.empty() && (!std::filesystem::is_directory(custom_shaders_path) || std::filesystem::is_empty(custom_shaders_path)))
      {
         shaders_path = custom_shaders_path;
      }
#endif

#if DEVELOPMENT && defined(SOLUTION_DIR) && (!defined(REMOTE_BUILD) || !REMOTE_BUILD)
      // Fall back on the solution "Shaders" folder if we are in development mode and there's no luma shaders folder created in the game side (or if it's empty, as it was accidentally quickly generated by a non dev build).
      // This will only work when built locally, and should be avoided on remote build machines!
      if (!std::filesystem::is_directory(shaders_path) || std::filesystem::is_empty(shaders_path))
      {
         std::filesystem::path solution_shaders_path = SOLUTION_DIR;
         solution_shaders_path /= "Shaders";
         if (std::filesystem::is_directory(solution_shaders_path))
         {
            shaders_path = solution_shaders_path;
         }
      }
#endif
      return shaders_path;
   }

   // TODO: if this was ever too slow (it is, at least in dev builds because they use the shader folder with all the games), given we iterate through the shader folder which also contains (possibly hundreds of) dumps and our built binaries,
   // we could split it up in 3 main branches (shaders code, shaders binaries and shaders dump).
   // Alternatively we could make separate iterators for each main shaders folder, however, we've now gotten it fast enough.
   //
   // Note: the paths here might also be hardcoded in GitHub actions (build scripts)
   // This expects a file path not a directory path.
   bool IsValidShadersSubPath(const std::filesystem::path& shader_directory, const std::filesystem::path& entry_path, bool& out_is_global)
   {
      const std::filesystem::path entry_directory = entry_path.parent_path();

#if DEVELOPMENT
      if (compile_clear_all_shaders)
      {
         // Return true on all first folders in the shaders directory (so, the first folder for each mod, and the global/include folders),
         // except the includes and other special folders, otherwise it might try to compile shaders in them (unwanted).
         // TODO: specify these directories somewhere globally, and also maybe rename out includes from ".hlsl" to ".h" or ".hlsli" or something
         if (entry_directory != (shader_directory / "Includes") && entry_directory != (shader_directory / "Decompiler") && entry_directory.parent_path() == shader_directory)
         {
            return true;
         }
      }
#endif

      // Global shaders (game independent)
      const auto global_shader_directory = shader_directory / "Global";
      if (entry_directory == global_shader_directory)
      {
         out_is_global = true;
         return true;
      }
      
      const auto game_shader_directory = shader_directory / Globals::GAME_NAME;
      if (entry_directory == game_shader_directory)
      {
         return true;
      }
      // Note: we could add a sub game name path for generic mods (e.g. Unity, Unreal), but we already have an acronym in front of their shaders name, and support per game shader defines, so it's not particularly needed

#if DEVELOPMENT && ALLOW_LOADING_DEV_SHADERS
      // WIP and test and unused shaders (they expect ".../" in front of their include dirs, given the nested path)
      // Note: these might be hardcoded in github actions!
      const auto dev_directory = game_shader_directory / "Dev";
      const auto unused_directory = game_shader_directory / "Unused";
      if (entry_directory == dev_directory || entry_path == unused_directory)
      {
         return true;
      }
#endif
      return false;
   }

   void ClearCustomShader(uint64_t shader_hash)
   {
      const std::unique_lock lock(s_mutex_loading);
      auto custom_shader = custom_shaders_cache.find(shader_hash);
      // TODO: why not just remove it from the array or call the default initializer?
      if (custom_shader != custom_shaders_cache.end() && custom_shader->second != nullptr)
      {
         custom_shader->second->code.clear();
         custom_shader->second->is_hlsl = false;
         custom_shader->second->is_luma_native = false;
         custom_shader->second->file_path.clear();
         custom_shader->second->preprocessed_hash = 0;
         custom_shader->second->compilation_errors.clear();
#if DEVELOPMENT || TEST
         custom_shader->second->compilation_error = false;
         custom_shader->second->preprocessed_code.clear();
         custom_shader->second->disasm.clear();
#if DEVELOPMENT
         custom_shader->second->definition = {};
#endif
#endif
      }
   }

   // Ambiguous name but this one clears the pipeline
   bool ClearCustomShader(DeviceData& device_data, CachedPipeline* cached_pipeline, bool clean_custom_shader = true)
   {
      if (clean_custom_shader) // A bit hacky for this to be here, should ideally be moved
      {
         for (auto shader_hash : cached_pipeline->shader_hashes)
         {
            ClearCustomShader(shader_hash);
         }
      }
      
      if (!cached_pipeline->cloned) return false;

      cached_pipeline->cloned = false; // This stops the cloned pipeline from being used in the next frame, allowing us to destroy it
      device_data.cloned_pipeline_count--;
      device_data.cloned_pipelines_changed = true;
      cached_pipeline->pipeline_clone = {0};
      return true;
   }

   void UnloadCustomShaders(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool clean_custom_shader = true, std::unordered_map<uint64_t, reshade::api::device*>* out_pipelines_to_destroy = nullptr)
   {
      std::unordered_map<uint64_t, reshade::api::device*> pipelines_to_destroy;

      // In case this is a full "unload" of all shaders
      if (!pipelines_filter.empty())
      {
         clean_custom_shader = false;
      }

      {
         const std::unique_lock lock(s_mutex_generic);
         for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
         {
            Shader::CachedPipeline* cached_pipeline = pair.second;
            if (cached_pipeline == nullptr || (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle))) continue;

            auto pipeline_clone_handle = cached_pipeline->pipeline_clone.handle;
            if (ClearCustomShader(device_data, cached_pipeline, clean_custom_shader))
            {
               device_data.pipeline_cache_by_pipeline_clone_handle.erase(pipeline_clone_handle);
               pipelines_to_destroy[pipeline_clone_handle] = cached_pipeline->device;
            }
         }
      }

      // Needs "s_mutex_generic" to be released because it accesses device mutexes that could otherwise deadlock if the game is multithreaded on rendering
      if (out_pipelines_to_destroy) // Delayed destruction!
      {
         out_pipelines_to_destroy->insert(pipelines_to_destroy.begin(), pipelines_to_destroy.end()); // Keep what was there
      }
      else
      {
         for (auto pair : pipelines_to_destroy)
         {
            pair.second->destroy_pipeline(reshade::api::pipeline{pair.first});
         }
      }
   }

   // Expects "s_mutex_loading" to make sure we don't try to compile/load any other files we are currently deleting
   // Note: this will create the "meta" files too!
   void CleanShadersCache()
   {
      if (!std::filesystem::exists(shaders_path))
      {
         return;
      }

      for (const auto& entry : std::filesystem::recursive_directory_iterator(shaders_path)
         | std::views::filter([](const std::filesystem::directory_entry& e) {
            return e.is_regular_file() &&
               (e.path().extension() == ".cso" ||
                  e.path().extension() == ".meta");
            }))
      {
         bool is_global = false;
         const auto& entry_path = entry.path();
         if (!IsValidShadersSubPath(shaders_path, entry_path, is_global))
         {
            continue;
         }
         if (!entry.is_regular_file())
         {
            continue;
         }
         const bool is_cso_or_meta = entry_path.extension().compare(".cso") == 0 || entry_path.extension().compare(".meta") == 0;
         if (!entry_path.has_extension() || !entry_path.has_stem() || !is_cso_or_meta)
         {
            continue;
         }

         const auto filename_no_extension_string = entry_path.stem().string();

#if 1 // Optionally leave any "raw" cso that was likely copied from the dumped shaders folder (these were not compiled from a custom hlsl shader by the same hash)
         if (filename_no_extension_string.length() >= (HASH_CHARACTERS_LENGTH + 2) && filename_no_extension_string[0] == '0' && filename_no_extension_string[1] == 'x') // Matches "strlen("0x12345678")"
         {
            continue;
         }
#endif

         std::filesystem::remove(entry_path);
      }
   }

   // Expects "s_mutex_loading" and "s_mutex_shader_objects"
   template<typename T = ID3D11DeviceChild>
   void CreateShaderObject(ID3D11Device* native_device, uint32_t native_shader_hash, com_ptr<T>& shader_object, bool force_delete_previous = !(bool)FORCE_KEEP_CUSTOM_SHADERS_LOADED, bool trigger_assert = false)
   {
      if (force_delete_previous)
      {
         // The shader changed, so we should clear its previous version resource anyway (to avoid keeping an outdated version)
         shader_object = nullptr;
      }

      const uint64_t shader_hash_64 = Shader::ShiftHash32ToHash64(native_shader_hash);
      // No warning if this fails, it can happen on boot depending on the execution order
      if (custom_shaders_cache.contains(shader_hash_64))
      {
         // Delay the deletion
         if (!force_delete_previous)
         {
            shader_object = nullptr;
         }

         const CachedCustomShader* custom_shader_cache = custom_shaders_cache[shader_hash_64];

         if constexpr (typeid(T) == typeid(ID3D11GeometryShader))
         {
            HRESULT hr = native_device->CreateGeometryShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
            assert(!trigger_assert || SUCCEEDED(hr));
         }
         else if constexpr (typeid(T) == typeid(ID3D11VertexShader))
         {
            HRESULT hr = native_device->CreateVertexShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
            assert(!trigger_assert || SUCCEEDED(hr));
         }
         else if constexpr (typeid(T) == typeid(ID3D11PixelShader))
         {
            HRESULT hr = native_device->CreatePixelShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
            assert(!trigger_assert || SUCCEEDED(hr));
         }
         else if constexpr (typeid(T) == typeid(ID3D11ComputeShader))
         {
            HRESULT hr = native_device->CreateComputeShader(custom_shader_cache->code.data(), custom_shader_cache->code.size(), nullptr, &shader_object);
            assert(!trigger_assert || SUCCEEDED(hr));
         }
         else
         {
            static_assert(false);
         }
      }
   }

   // Expects "s_mutex_loading"
   void CreateDeviceNativeShaders(DeviceData& device_data, const std::set<uint32_t>* native_shaders_hashes_filter = nullptr, bool lock = true)
   {
      if (lock) s_mutex_shader_objects.lock();
      for (const auto& native_shader_definition : native_shaders_definitions)
      {
         if (!native_shaders_hashes_filter || native_shaders_hashes_filter->contains(native_shader_definition.first))
         {
            switch (native_shader_definition.second.type)
            {
            case reshade::api::pipeline_subobject_type::vertex_shader:
               CreateShaderObject(device_data.native_device, native_shader_definition.first, device_data.native_vertex_shaders[native_shader_definition.first]); break;
#if GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::geometry_shader:
               CreateShaderObject(device_data.native_device, native_shader_definition.first, device_data.native_geometry_shaders[native_shader_definition.first]); break;
#endif
            case reshade::api::pipeline_subobject_type::pixel_shader:
               CreateShaderObject(device_data.native_device, native_shader_definition.first, device_data.native_pixel_shaders[native_shader_definition.first]); break;
            case reshade::api::pipeline_subobject_type::compute_shader:
               CreateShaderObject(device_data.native_device, native_shader_definition.first, device_data.native_compute_shaders[native_shader_definition.first]); break;
            default:
               ASSERT_ONCE(false); break;
            }
         }

#if DEVELOPMENT
         bool shader_key_found = device_data.native_vertex_shaders.contains(native_shader_definition.first)
#if GEOMETRY_SHADER_SUPPORT
            || device_data.native_geometry_shaders.contains(native_shader_definition.first)
#endif
            || device_data.native_pixel_shaders.contains(native_shader_definition.first)
            || device_data.native_compute_shaders.contains(native_shader_definition.first);
         ASSERT_ONCE_MSG(shader_key_found, "Some of the custom shaders failed to added to the list"); // Make sure they are all added to the array by boot, otherwise follow-up map lookups would add it to the array and make it non thread safe
         if (shader_key_found)
         {
            bool shader_key_valid = false;
            switch (native_shader_definition.second.type)
            {
            case reshade::api::pipeline_subobject_type::vertex_shader:
               shader_key_valid = device_data.native_vertex_shaders[native_shader_definition.first].get(); break;
#if GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::geometry_shader:
               shader_key_valid = device_data.native_geometry_shaders[native_shader_definition.first].get(); break;
#endif
            case reshade::api::pipeline_subobject_type::pixel_shader:
               shader_key_valid = device_data.native_pixel_shaders[native_shader_definition.first].get(); break;
            case reshade::api::pipeline_subobject_type::compute_shader:
               shader_key_valid = device_data.native_compute_shaders[native_shader_definition.first].get(); break;
            default:
               ASSERT_ONCE(false); break;
            }
            ASSERT_ONCE_MSG(shader_key_valid, "Some of the custom shaders failed to be found/compiled");
         }
#endif
      }
      device_data.created_native_shaders = true; // Some of the shader object creations above might have failed due to filtering, but they will likely be compiled soon after anyway
      if (lock) s_mutex_shader_objects.unlock();
   }

   // Compiles all the "custom" shaders we have in our shaders folder
   void CompileCustomShaders(DeviceData* optional_device_data = nullptr, bool warn_about_duplicates = false, const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>())
   {
      std::vector<std::string> shader_defines;
      // Cache them for consistency and to avoid threads from halting
      {
         const std::shared_lock lock(s_mutex_shader_defines);
         uint32_t cbuffer_defines = 3;
#if DEVELOPMENT
         cbuffer_defines++;
#endif
         uint32_t game_specific_defines = 1;
         if (sub_game_shader_define != nullptr)
         {
            game_specific_defines++;
         }
         if (custom_sdr_gamma > 0.f)
         {
            game_specific_defines++;
         }
         const uint32_t total_extra_defines = cbuffer_defines + game_specific_defines;
         shader_defines.assign((shader_defines_data.size() + total_extra_defines) * 2, "");

         size_t shader_defines_index = shader_defines.size() - (total_extra_defines * 2);
         
         // Clean up the game name from non letter characters (including spaces), and make it all upper case
         std::string game_name = Globals::GAME_NAME;
			RemoveNonLetterOrNumberCharacters(game_name.data(), '_'); // Ideally we should remove all weird characters and turn spaces into underscores
         std::transform(game_name.begin(), game_name.end(), game_name.begin(),
            [](unsigned char c) { return std::toupper(c); });
         shader_defines[shader_defines_index++] = "GAME_" + game_name;
         shader_defines[shader_defines_index++] = "1";

         if (sub_game_shader_define != nullptr)
         {
            shader_defines[shader_defines_index++] = sub_game_shader_define;
            shader_defines[shader_defines_index++] = "1";
         }
         if (custom_sdr_gamma > 0.f)
         {
            shader_defines[shader_defines_index++] = "CUSTOM_SDR_GAMMA";
            shader_defines[shader_defines_index++] = std::to_string(custom_sdr_gamma);
         }

         // Define 3 shader cbuffers indexes (e.g. "(b13)")
         // We automatically generate unique values for each cbuffer to make sure they don't overlap.
         // This is because in case the users disabled some of them, we don't want them to bother to
         // define unique indexes for each of them, but the shader compiler fails if two cbuffers have the same value,
         // so we have to find the "next" unique one.
         uint32_t luma_settings_cbuffer_define_index, luma_data_cbuffer_define_index, luma_ui_cbuffer_define_index;
         std::unordered_set<uint32_t> excluded_values;
         if (luma_settings_cbuffer_index <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1)
            luma_settings_cbuffer_define_index = luma_settings_cbuffer_index;
         else
            luma_settings_cbuffer_define_index = FindNextUniqueNumberInRange(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT / 2, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1, excluded_values);
         excluded_values.emplace(luma_settings_cbuffer_define_index);
         if (luma_data_cbuffer_index <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1)
            luma_data_cbuffer_define_index = luma_data_cbuffer_index;
         else
            luma_data_cbuffer_define_index = FindNextUniqueNumberInRange(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT / 2 - 1, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1, excluded_values);
         excluded_values.emplace(luma_data_cbuffer_define_index);
         if (luma_ui_cbuffer_index <= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1)
            luma_ui_cbuffer_define_index = luma_ui_cbuffer_index;
         else
            luma_ui_cbuffer_define_index = FindNextUniqueNumberInRange(D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT / 2 - 2, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1, excluded_values);
         excluded_values.emplace(luma_ui_cbuffer_define_index);

#if DEVELOPMENT // We need to do this to force compile the Luma Settings cbuffer dev settings, otherwise there would be a cbuffer struct mismatch between CPU and GPU in case the "DEVELOPMENT" shader defined was turned off in development builds.
         shader_defines[shader_defines_index++] = "CPU_DEVELOPMENT";
         shader_defines[shader_defines_index++] = "1";
#endif

         shader_defines[shader_defines_index++] = "LUMA_SETTINGS_CB_INDEX";
         shader_defines[shader_defines_index++] = "b" + std::to_string(luma_settings_cbuffer_define_index); // TODO: try and do e.g. "register(b##7)" in the shader, without appending "b" to it, so we could compare it in shaders against other values too
         shader_defines[shader_defines_index++] = "LUMA_DATA_CB_INDEX";
         shader_defines[shader_defines_index++] = "b" + std::to_string(luma_data_cbuffer_define_index);
         shader_defines[shader_defines_index++] = "LUMA_UI_DATA_CB_INDEX";
         shader_defines[shader_defines_index++] = "b" + std::to_string(luma_ui_cbuffer_define_index);

			ASSERT_ONCE(shader_defines_index == shader_defines.size());

         for (uint32_t i = 0; i < shader_defines_data.size(); i++)
         {
            shader_defines[(i * 2)] = shader_defines_data[i].compiled_data.name;
            shader_defines[(i * 2) + 1] = shader_defines_data[i].compiled_data.value;
         }
      }

      // We need to clear this every time "CompileCustomShaders()" is called as we can't clear previous logs from it. We do this even if we have some "pipelines_filter"
      {
         const std::unique_lock lock(s_mutex_loading);
         shaders_compilation_errors.clear();
      }

      auto directory = shaders_path;
      bool shaders_directory_created_or_empty = false;
      // Create it if it doesn't exist
      if (!std::filesystem::exists(directory))
      {
         if (!std::filesystem::create_directories(directory))
         {
            const std::unique_lock lock(s_mutex_loading);
            shaders_compilation_errors = "Cannot find nor create shaders directory";
            return;
         }
         shaders_directory_created_or_empty = true; // Largely redundant
      }
      if (std::filesystem::is_empty(directory))
      {
         shaders_directory_created_or_empty = true;
      }

      if (pipelines_filter.empty())
      {
         const std::unique_lock lock_shader_defines(s_mutex_shader_defines);

         code_shaders_defines.clear();
#if DEVELOPMENT
         const auto prev_cb_luma_dev_settings_default_value = cb_luma_dev_settings_default_value;
         if (!cb_luma_dev_settings_set_from_code)
         {
            cb_luma_dev_settings_default_value = CB::LumaDevSettings(0.f);
            cb_luma_dev_settings_min_value = CB::LumaDevSettings(0.f);
            cb_luma_dev_settings_max_value = CB::LumaDevSettings(1.f);
            cb_luma_dev_settings_names = {};
         }
#endif

         // Add the global (generic) include and the game specific one
         auto settings_directories = { directory / "Includes" / "Settings.hlsl", directory / Globals::GAME_NAME / "Includes" / "Settings.hlsl" };
         bool is_global_settings = true;
         for (auto settings_directory : settings_directories)
         {
            if (std::filesystem::is_regular_file(settings_directory))
            {
               try
               {
                  std::ifstream file;
                  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                  file.open(settings_directory.c_str()); // Open file
                  std::stringstream str_stream;
                  str_stream << file.rdbuf(); // Read the file (we could use "D3DReadFileToBlob" to append the hsls includes in the file, but then stuff that is defined out wouldn't be part of the text)
                  std::string str = str_stream.str(); // str holds the content of the file
                  size_t i = -1;
                  int settings_count = 0;
                  while (true)
                  {
                     // Iterate the string line (break) by line (break),
                     // and check for defines values.

                     size_t i0 = i + 1;
                     i = str.find('\n', i0);
                     bool finished = false;
                     if (i0 == i) continue;
                     if (i == std::string::npos)
                     {
                        i = str.length();
                        finished = true;
                     }

                     // TODO: make this more flexible, allowing spaces around "#" and "define" etc,
                     // and defines values that are not numerical (from 0 to 9)
                     // TODO: this is only used by Prey so make it optional!
                     std::string_view str_view(&str[i0], i - i0);
                     if (str_view.rfind("#define ", 0) == 0)
                     {
                        str_view = str_view.substr(strlen("#define "));
                        size_t space_index = str_view.find(' ');
                        if (space_index != std::string::npos)
                        {
                           std::string_view define_name = str_view.substr(0, space_index);
                           size_t second_space_index = str_view.find(' ', space_index);
                           if (second_space_index != std::string::npos)
                           {
                              std::string_view define_value = str_view.substr(space_index + 1, second_space_index);
                              uint8_t define_int_value = define_value[0] - '0';
                              if (define_int_value <= 9)
                              {
                                 code_shaders_defines.emplace(define_name, define_int_value);
                              }
                           }
                        }
                     }
#if DEVELOPMENT
                     // Reflections on dev settings.
                     // They can have a comment like "// Default, Min, Max, Name" next to them (e.g. "// 0.5, 0, 1.3, Custom Name").
                     const auto dev_setting_pos = str_view.find("float DevSetting");
                     if (!cb_luma_dev_settings_set_from_code && is_global_settings && dev_setting_pos != std::string::npos)
                     {
                        if (settings_count >= CB::LumaDevSettings::SettingsNum) continue;
                        settings_count++;
                        const auto meta_data_pos = str_view.find("//");
                        if (meta_data_pos == std::string::npos || dev_setting_pos >= meta_data_pos) continue;
                        i0 += meta_data_pos + 2;
                        std::string str_line(&str[i0], i - i0);
                        std::stringstream ss(str_line);
                        if (!ss.good()) continue;

                        int settings_float_count = 0;
                        float str_float;
                        bool reached_end = false;
                        while (ss.peek() == ' ')
                        {
                           ss.ignore();
                           if (!ss.good()) { reached_end = true; break; }
                        }
                        // The float read would seemengly advance some state in the stream buffer even if it failed finding it, so skip it in case the next value is not a number (ignore ".3f" like definitions...).
                        // Float heading spaces are automatically ignored.
                        while (!reached_end && ss.peek() >= '0' && ss.peek() <= '9' && ss >> str_float)
                        {
                           if (settings_float_count == 0) cb_luma_dev_settings_default_value[settings_count - 1] = str_float;
                           else if (settings_float_count == 1) cb_luma_dev_settings_min_value[settings_count - 1] = str_float;
                           else if (settings_float_count == 2) cb_luma_dev_settings_max_value[settings_count - 1] = str_float;
                           settings_float_count++;
                           if (!ss.good()) { reached_end = true; break; };
                           // Remove known (supported) characters to ignore (spaces are already ignored above anyway)
                           while (ss.peek() == ',' || ss.peek() == ' ')
                           {
                              ss.ignore();
                              if (!ss.good()) { reached_end = true; break; }
                           }
                        }

                        std::string str;
                        auto ss_pos = ss.tellg();
                        // If we found a string, read the whole remaining stream buffer, otherwise the "str" string would end at the first space
                        if (!reached_end && ss >> str)
                        {
                           cb_luma_dev_settings_names[settings_count - 1] = ss.str();
                           cb_luma_dev_settings_names[settings_count - 1] = cb_luma_dev_settings_names[settings_count - 1].substr(ss_pos, cb_luma_dev_settings_names[settings_count - 1].length() - ss_pos);
                        }
                     }
   #endif

                     if (finished) break;
                  }
               }
               catch (const std::exception& e)
               {
               }
#if DEVELOPMENT
               // Re-apply the default settings if they changed
               if (is_global_settings && memcmp(&cb_luma_dev_settings_default_value, &prev_cb_luma_dev_settings_default_value, sizeof(cb_luma_dev_settings_default_value)) != 0)
               {
                  const std::unique_lock lock_reshade(s_mutex_reshade);
                  cb_luma_global_settings.DevSettings = cb_luma_dev_settings_default_value;
               }
#endif
            }
            else
            {
               ASSERT_ONCE(shaders_directory_created_or_empty || !is_global_settings); // Missing "Settings.hlsl" file (the game specific one doesn't need to exist) (ignored if we just created the shaders folder)
            }
            is_global_settings = false; // Only the first instance is the global settings
         }
      }

      std::set<uint32_t> changed_luma_native_shaders_hashes;
      for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)
         | std::views::filter([](const std::filesystem::directory_entry& e) {
            return e.is_regular_file() &&
               (e.path().extension() == ".cso" ||
                  e.path().extension() == ".hlsl");
            }))
      {
         bool is_global = false;
         const auto& entry_path = entry.path();
         if (!IsValidShadersSubPath(directory, entry_path, is_global))
         {
            continue;
         }
         if (!entry.is_regular_file())
         {
#if _DEBUG && LOG_VERBOSE
            reshade::log::message(reshade::log::level::warning, "LoadCustomShaders(not a regular file)");
#endif
            continue;
         }
         const bool is_hlsl = entry_path.extension().compare(".hlsl") == 0;
         const bool is_cso = entry_path.extension().compare(".cso") == 0;
         if (!entry_path.has_extension() || !entry_path.has_stem() || (!is_hlsl && !is_cso))
         {
#if _DEBUG && LOG_VERBOSE
            std::stringstream s;
            s << "LoadCustomShaders(Missing extension or stem or unknown extension: ";
            s << entry_path.string();
            s << ")";
            reshade::log::message(reshade::log::level::warning, s.str().c_str());
#endif
            continue;
         }

         const auto filename_no_extension_string = entry_path.stem().string();
         std::vector<std::pair<std::string, const ShaderDefinition*>> shader_definitions_and_hashes;
         std::string shader_target;

         bool is_luma_native = is_global;

         const char* native_prefix = "Luma_";
         const char* hash_sample = "0x12345678";
         const auto sm_length = strlen("xs_n_n"); // Shader Model min length

         if (is_hlsl)
         {
            const bool has_hash = filename_no_extension_string.find("0x") != std::string::npos;
            const bool is_native_shader = filename_no_extension_string.starts_with(native_prefix);
            ASSERT_ONCE_MSG(!is_native_shader || !has_hash, "Luma's native shaders (whether they are global or game specific) should ideally have \"Luma_\" in front of their name, and have no shader hash, otherwise they might not get detected/compiled.");
            if (!has_hash && is_native_shader)
            {
               is_luma_native = true;
            }
            const auto length = filename_no_extension_string.length();
            const auto hash_length = strlen(hash_sample); // HASH_CHARACTERS_LENGTH+2
            const auto min_expected_length = is_luma_native ? strlen(native_prefix) : (hash_length + 1 + sm_length); // The shader model is appended after any name, so we add 1 for a dot (e.g. "0x12345678.ps_5_0")
            
            if (length < min_expected_length) continue;

            if (is_luma_native)
            {
               for (const auto& native_shader_definition : native_shaders_definitions)
               {
                  if (native_shader_definition.second.file_name == filename_no_extension_string)
                  {
                     // Add the shader target type, to make sure the same luma native shader names can be used with different shader types (by generating a different hash for it)
                     shader_definitions_and_hashes.emplace_back(std::make_pair(Shader::Hash_NumToStr(native_shader_definition.first), &native_shader_definition.second)); // Store a pointer to the shader definition, "native_shaders_definitions" is immutable at this point
                  }
               }
            }
            else
            {
               ASSERT_ONCE(length > min_expected_length); // HLSL files are expected to have a name in front of the hash or shader model. They can still be loaded, but they won't be distinguishable from raw cso files (and thus fail to have priority?)

               shader_target = filename_no_extension_string.substr(length - sm_length, sm_length);
               if (shader_target[2] != '_') continue;
               if (shader_target[4] != '_') continue;

               size_t first_hash_pos = filename_no_extension_string.find("0x");
               size_t next_hash_pos = first_hash_pos;
               if (next_hash_pos == std::string::npos) continue;
               do
               {
                  std::string current_hash = filename_no_extension_string.substr(next_hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH);
                  // Special identifier (0x########) to redirect the hash list to be defined in c++, by shader name.
                  // It should only be specified once per file!
                  // Other specific hashes can still be added too.
                  if (current_hash == "########")
                  {
                     std::string file_name = filename_no_extension_string.substr(0, first_hash_pos);
                     if (file_name.ends_with('_'))
                     {
                        file_name.erase(file_name.length() - 1, 1);
                     }
                     const auto this_redirected_shader_hashes = redirected_shader_hashes.find(file_name);
                     if (this_redirected_shader_hashes != redirected_shader_hashes.end())
                     {
                        for (const auto& redirected_shader_hash : this_redirected_shader_hashes->second)
                           shader_definitions_and_hashes.emplace_back(std::make_pair(redirected_shader_hash, nullptr));
                     }
                  }
                  else
                  {
                     shader_definitions_and_hashes.emplace_back(std::make_pair(current_hash, nullptr)); // The definition here is not needed as none of the data is defined at the moment
                  }
                  next_hash_pos = filename_no_extension_string.find("0x", next_hash_pos + 1);
               } while (next_hash_pos != std::string::npos);
            }
         }
         // We don't load global CSOs, given these are shaders we made ourselves (unless we wanted to ship pre-built global CSOs, but that's not wanted for now)
         else if (is_cso && !is_global)
         {
            // As long as cso starts from "0x12345678", it's good, they don't need the shader type specified
            size_t hash_pos = filename_no_extension_string.find("0x");
            if (hash_pos != 0) continue; // TODO: what is this condition even for? Is it because when we find an hlsl we'd automatically load its matching cso anyway?
            if (hash_pos == std::string::npos) continue; // Silently skip if the cso has no hash, we'd do nothing with it
            if (hash_pos + 2 /*0x*/ + HASH_CHARACTERS_LENGTH > filename_no_extension_string.size())
            {
               std::stringstream s;
               s << "LoadCustomShaders(Invalid cso file format: ";
               s << filename_no_extension_string;
               s << ")";
               reshade::log::message(reshade::log::level::warning, s.str().c_str());
               continue;
            }
            shader_definitions_and_hashes.emplace_back(std::make_pair(filename_no_extension_string.substr(hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH), nullptr));

            // Only directly load the cso if no hlsl by the same name exists,
            // which implies that we either did not ship the hlsl and shipped the pre-compiled cso(s),
            // or that this is a vanilla cso dumped from the game.
            // If the hlsl also exists, we load the cso though the hlsl code loading below (by redirecting it).
            // 
            // Note that if we have two shaders with the same hash but different overall name (e.g. the description part of the shader),
            // whichever is iterated last will load on top of the previous one (whether they are both cso, or cso and hlsl etc).
            // We don't care about "fixing" that because it's not a real world case.
            const auto filename_hlsl_string = filename_no_extension_string + ".hlsl";
            if (std::filesystem::exists(filename_hlsl_string)) continue;
         }
         // Any other case (non hlsl non cso) is already earlied out above

         for (const auto& shader_definition_and_hash : shader_definitions_and_hashes)
         {
            const std::string& hash_string = shader_definition_and_hash.first;
            uint32_t original_shader_hash;
            uint64_t shader_hash;
            try
            {
               original_shader_hash = Shader::Hash_StrToNum(hash_string);
               shader_hash = original_shader_hash;
            }
            catch (const std::exception& e)
            {
               continue;
            }

            // To avoid polluting the game's own shaders hashes with Luma native shaders hashes (however unlikely),
            // shift their hash by 32 bits, to a 64 bits unique one.
            if (is_luma_native)
            {
               shader_hash = Shader::ShiftHash32ToHash64(shader_hash);
            }

            // Early out before compiling (even if it's a luma native shader, yes)
            ASSERT_ONCE(pipelines_filter.empty() || optional_device_data); // We can't apply a filter if we didn't pass in the "DeviceData"
            if (!pipelines_filter.empty() && optional_device_data)
            {
               if (is_luma_native)
               {
                  break;
               }
               bool pipeline_found = false;
               const std::shared_lock lock(s_mutex_generic);
               for (const auto& pipeline_pair : optional_device_data->pipeline_cache_by_pipeline_handle)
               {
                  if (std::find(pipeline_pair.second->shader_hashes.begin(), pipeline_pair.second->shader_hashes.end(), shader_hash) == pipeline_pair.second->shader_hashes.end()) continue;
                  if (pipelines_filter.contains(pipeline_pair.first))
                  {
                     pipeline_found = true;
                  }
                  break;
               }
               if (!pipeline_found)
               {
                  continue;
               }
            }

            std::string local_shader_target;
            if (is_hlsl)
            {
               if (shader_definition_and_hash.second)
               {
                  local_shader_target = ShaderTypeToTarget(shader_definition_and_hash.second->type);
                  if (shader_definition_and_hash.second->shader_target_version)
                  {
                     local_shader_target += shader_definition_and_hash.second->shader_target_version; // No further safety checks here...
                  }
                  else // DX11 default (5_1 doesn't add any features we might need)
                  {
                     local_shader_target += "5_0";
                  }
               }
               else
               {
                  local_shader_target = shader_target;
               }
               if (local_shader_target.starts_with('x') || local_shader_target.length() < sm_length)
               {
                  ASSERT_ONCE_MSG(false, "Invalid shader target");
                  continue;
               }
            }

            // Add defines to specify the current "target" hash we are building the shader with (some shaders can share multiple permutations (hashes) within the same hlsl)
            std::vector<std::string> local_shader_defines = shader_defines;
            if (!is_luma_native)
            {
               local_shader_defines.push_back("_" + hash_string);
               local_shader_defines.push_back("1");
            }
            // Add the shader target type to (e.g.) allow unifying a PS and CS under the same file
            if (shader_definition_and_hash.second)
            {
               switch (shader_definition_and_hash.second->type)
               {
               case reshade::api::pipeline_subobject_type::vertex_shader:
               {
                  local_shader_defines.push_back("VS");
                  local_shader_defines.push_back("1");
                  break;
               }
               case reshade::api::pipeline_subobject_type::geometry_shader:
               {
                  local_shader_defines.push_back("GS");
                  local_shader_defines.push_back("1");
                  break;
               }
               case reshade::api::pipeline_subobject_type::pixel_shader:
               {
                  local_shader_defines.push_back("PS");
                  local_shader_defines.push_back("1");
                  break;
               }
               case reshade::api::pipeline_subobject_type::compute_shader:
               {
                  local_shader_defines.push_back("CS");
                  local_shader_defines.push_back("1");
                  break;
               }
               }
            }
            if (!is_global)
            {
#if defined(LUMA_GAME_CB_STRUCTS) && DEVELOPMENT && 0 // Disabled as it's not particularly useful, if the CB struct is missing, shaders won't compile anyway
               // Highlight that the luma game settings struct (nested into the global luma cb) is required by all includes,
               // making sure it wasn't accidentally left out, which would leave the cb definition off.
               local_shader_defines.push_back("REQUIRES_LUMA_GAME_CB_STRUCTS");
               local_shader_defines.push_back("1");
#endif
            }
            if (shader_definition_and_hash.second)
            {
               for (const auto& define_data : shader_definition_and_hash.second->defines_data)
               {
                  local_shader_defines.push_back(define_data.name);
                  local_shader_defines.push_back(define_data.value);
               }
            }

            // Note that we "shader_hash" might have been modified in the "is_luma_native" case,
            // so it'd be outdated here, but it shouldn't really matter, as the chances of conflict are ~0,
            // and even then, this is just the precompilation phase hash.
            char config_name[std::string_view("Shader#").size() + HASH_CHARACTERS_LENGTH + 1] = "";
            sprintf(&config_name[0], "Shader#%s", hash_string.c_str());

            const std::unique_lock lock(s_mutex_loading); // Don't lock until now as we didn't access any shared data
            auto& custom_shader = custom_shaders_cache[shader_hash]; // Add default initialized shader
            const bool has_custom_shader = (custom_shaders_cache.find(shader_hash) != custom_shaders_cache.end()) && (custom_shader != nullptr); // Weird code...
            std::wstring trimmed_file_path_cso; // Only valid for hlsl files. Identical for "is_luma_native" files.

            if (is_hlsl)
            {
               std::wstring file_name_cso = entry_path.stem().wstring();

               // Add the hash to the file name as luma shaders can use different shader defines or function entry points, so they need to be uniquely identifiable
               if (is_luma_native)
               {
                  size_t pos = file_name_cso.rfind(L"."); // Add it before the shader model
                  if (pos != std::wstring::npos)
                  {
                     file_name_cso.insert(pos, L"_0x" + std::wstring(hash_string.begin(), hash_string.end()));
                  }
                  else
                  {
                     file_name_cso += L"_0x" + std::wstring(hash_string.begin(), hash_string.end());
                  }
               }

               size_t first_hash_pos = file_name_cso.find(L"0x");
               if (!is_luma_native && first_hash_pos != std::string::npos)
               {
                  // Remove all the non first shader hashes in the file (and anything in between them),
                  // we then replace the first hash with our target one
                  size_t prev_hash_pos = first_hash_pos;
                  size_t next_hash_pos = file_name_cso.find(L"0x", prev_hash_pos + 1);
                  while (next_hash_pos != std::string::npos && (file_name_cso.length() - next_hash_pos) >= 10) // HASH_CHARACTERS_LENGTH+2
                  {
                     file_name_cso = file_name_cso.substr(0, prev_hash_pos + 10) + file_name_cso.substr(next_hash_pos + 10);
                     prev_hash_pos = first_hash_pos;
                     next_hash_pos = file_name_cso.find(L"0x", prev_hash_pos + 1);
                  }
                  std::wstring hash_wstring = std::wstring(hash_string.begin(), hash_string.end());
                  file_name_cso.replace(first_hash_pos + 2 /*0x*/, HASH_CHARACTERS_LENGTH, hash_wstring.c_str());
               }
               trimmed_file_path_cso = entry_path.parent_path() / (file_name_cso + L".cso");
            }

            // Fill up the shader data the first time it's found
            if (!has_custom_shader)
            {
               custom_shader = new CachedCustomShader();

               std::size_t preprocessed_hash = custom_shader->preprocessed_hash; // Empty
               // Note that if anybody manually changed the config hash, the data here could mismatch and end up recompiling when not needed or skipping recompilation even if needed (near impossible chance)
               // TODO: ignore the per game ini config value in dev mode for global shaders, given they are shared between all projects in the repository shaders, and thus there would be a mismatch between the last built cso (by another game) and the current game's config hash, but it'd end up loading it anyway. We could always force re-calculate the hash from these, from the CSO, or store a file with the name of the last game that generated them.
               const bool should_load_compiled_shader = is_hlsl && !prevent_shader_cache_loading; // If this shader doesn't have an hlsl, we should never read it or save it on disk, there's no need (we can still fall back on the original .cso if needed)
               if (should_load_compiled_shader && reshade::get_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], preprocessed_hash))
               {
                  // This will load the matching cso if it exists
                  // TODO: move these to a "Bin" sub folder called "cache"? It'd make everything cleaner (and the "CompileCustomShaders()" could simply nuke a directory then, and we could remove the restriction where hlsl files need to have a name in front of the hash),
                  // but it would make it harder to manually remove a single specific shader cso we wanted to nuke for test reasons (especially if we exclusively put the hash in their cso name).
                  if (Shader::LoadCompiledShaderFromFile(custom_shader->code, trimmed_file_path_cso.c_str()))
                  {
                     // If both reading the pre-processor hash from config and the compiled shader from disk succeeded, then we are free to continue as if this shader was working
                     custom_shader->file_path = entry_path;
                     custom_shader->is_hlsl = is_hlsl;
                     custom_shader->is_luma_native = is_luma_native;
                     custom_shader->preprocessed_hash = preprocessed_hash;
#if DEVELOPMENT
                     if (shader_definition_and_hash.second)
                        custom_shader->definition = *shader_definition_and_hash.second;
                     else
                        custom_shader->definition = {};
#endif
                     // We'll need to create the shader objects for these given they were just loaded!
                     if (is_luma_native)
                     {
                        changed_luma_native_shaders_hashes.emplace(original_shader_hash);
                     }
                     // Theoretically at this point, the shader pre-processor below should skip re-compiling this shader unless the hash changed

                     // TODO: add a shader version here, taken from the file name like "name_v3_hash", so we could ignore old versions of the shader hashes with a different file name
                  }
               }
            }
            else if (warn_about_duplicates)
            {
               warn_about_duplicates = false;
#if !DEVELOPMENT
               const std::string warn_message = "It seems like you have duplicate shaders in your \"" + std::string(NAME) + "\" folder, please delete it and re-apply the files from the latest version of the mod.";
               MessageBoxA(game_window, warn_message.c_str(), NAME, MB_SETFOREGROUND);
#endif
            }

            CComPtr<ID3DBlob> uncompiled_code_blob;

            if (is_hlsl)
            {
               constexpr bool compile_from_current_path = false; // Set this to true to include headers from the current directory instead of the file root folder

               const auto previous_path = std::filesystem::current_path();
               if (compile_from_current_path)
               {
                  // Set the current path to the shaders directory, it can be needed by the DX compilers (specifically by the preprocess functions)
                  std::filesystem::current_path(directory);
               }

               std::string compilation_errors;

               // Skip compiling the shader if it didn't change
               // Note that this won't replace "custom_shader->compilation_error" unless there was any new error/warning, and that's kind of what we want
               // Note that this will not try to build the shader again if the last compilation failed and its files haven't changed
               bool error = false;
               std::string preprocessed_code;
					std::string* preprocessed_code_ref = &preprocessed_code;
#if DEVELOPMENT
               preprocessed_code_ref = &custom_shader->preprocessed_code;
#endif
               const bool needs_compilation = Shader::PreprocessShaderFromFile(entry_path.c_str(), compile_from_current_path ? entry_path.filename().c_str() : entry_path.c_str(), local_shader_target.c_str(), *preprocessed_code_ref, custom_shader->preprocessed_hash, uncompiled_code_blob, local_shader_defines, error, &compilation_errors);

               // Only overwrite the previous compilation error if we have any preprocessor errors
               if (!compilation_errors.empty() || error)
               {
                  custom_shader->compilation_errors = compilation_errors;
#if DEVELOPMENT || TEST
                  custom_shader->compilation_error = error;
#endif
#if !DEVELOPMENT && !TEST // Ignore warnings for public builds
                  if (error)
#endif
                  {
                     shaders_compilation_errors.append(filename_no_extension_string);
                     shaders_compilation_errors.append(": ");
                     shaders_compilation_errors.append(compilation_errors);
                  }
               }
               // Print out the same (last) compilation errors again if the shader still needs to be compiled but hasn't changed.
               // We might want to ignore this case for public builds (we can't know whether this was an error or a warning atm),
               // but it seems like this can only trigger after a shader had previous failed to build, so these should be guaranteed to be errors,
               // and thus we should be able to print them to all users (we don't want warnings in public builds).
               else if (!needs_compilation && custom_shader->code.size() == 0 && !custom_shader->compilation_errors.empty())
               {
                  shaders_compilation_errors.append(filename_no_extension_string);
                  shaders_compilation_errors.append(": ");
                  shaders_compilation_errors.append(custom_shader->compilation_errors);
               }

               if (compile_from_current_path)
               {
                  // Restore it to avoid unknown consequences
                  std::filesystem::current_path(previous_path);
               }

               if (!needs_compilation)
               {
                  ASSERT_ONCE(custom_shader->is_luma_native == is_luma_native); // Make 100% all the branches above cached right flags
                  continue;
               }
            }

            // If we reached this place, we can consider this shader as "changed" even if it will fail compiling.
            // We don't care to avoid adding duplicate elements to this list.
            if (is_luma_native)
            {
               changed_luma_native_shaders_hashes.emplace(original_shader_hash);
            }

            // For extra safety, just clear everything that will be re-assigned below if this custom shader already existed
            // Note: this code makes little sense and should be cleaned
            if (has_custom_shader)
            {
               auto preprocessed_hash = custom_shader->preprocessed_hash;
#if DEVELOPMENT || TEST
               auto preprocessed_code = custom_shader->preprocessed_code;
#endif
               ClearCustomShader(shader_hash);
               // Keep the data we just filled up
               custom_shader->preprocessed_hash = preprocessed_hash;
#if DEVELOPMENT || TEST
               custom_shader->preprocessed_code = preprocessed_code;
#if DEVELOPMENT
               if (shader_definition_and_hash.second)
                  custom_shader->definition = *shader_definition_and_hash.second;
               else
                  custom_shader->definition = {};
#endif
#endif
            }
            custom_shader->file_path = entry_path;
            custom_shader->is_hlsl = is_hlsl;
            custom_shader->is_luma_native = is_luma_native;
            // Clear these in case the compiler didn't overwrite them
            custom_shader->code.clear();
            custom_shader->compilation_errors.clear();
#if DEVELOPMENT || TEST
            custom_shader->compilation_error = false;
#endif

            if (is_hlsl)
            {
#if _DEBUG && LOG_VERBOSE
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Compiling file: ";
                  s << entry_path.string();
                  s << ", global: " << is_global;
                  s << ", luma native: " << is_luma_native;
                  s << ", hash: " << PRINT_CRC32(shader_hash);
                  s << ", target: " << local_shader_target;
                  s << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
               }
#endif

               bool error = false;
               Shader::CompileShaderFromFile(
                  custom_shader->code,
                  uncompiled_code_blob,
                  entry_path.c_str(),
                  local_shader_target.c_str(),
                  local_shader_defines,
                  // Save to disk for faster loading after the first compilation
                  !prevent_shader_cache_saving,
                  error,
                  &custom_shader->compilation_errors,
                  trimmed_file_path_cso.c_str(),
                  shader_definition_and_hash.second ? shader_definition_and_hash.second->function_name : nullptr);
               ASSERT_ONCE(!trimmed_file_path_cso.empty()); // If we got here, this string should always be valid, as it means the shader read from disk was an hlsl

               if (!custom_shader->compilation_errors.empty())
               {
#if DEVELOPMENT || TEST
                  custom_shader->compilation_error = error;
#endif
#if !DEVELOPMENT && !TEST // Ignore warnings for public builds
                  if (error)
#endif
                  {
                     shaders_compilation_errors.append(filename_no_extension_string);
                     shaders_compilation_errors.append(": ");
                     shaders_compilation_errors.append(custom_shader->compilation_errors);
                  }
               }

               if (custom_shader->code.empty())
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Compilation failed: ";
                  s << entry_path.string();
                  s << ")";
                  reshade::log::message(reshade::log::level::warning, s.str().c_str());

                  continue;
               }
               // Save the matching the pre-compiled shader hash in the config, so we can skip re-compilation on the next boot
               else if (!prevent_shader_cache_saving)
               {
                  reshade::set_config_value(nullptr, NAME_ADVANCED_SETTINGS.c_str(), &config_name[0], custom_shader->preprocessed_hash);
               }

#if _DEBUG && LOG_VERBOSE
               {
                  std::stringstream s;
                  s << "LoadCustomShaders(Shader built with size: " << custom_shader->code.size() << ")";
                  reshade::log::message(reshade::log::level::debug, s.str().c_str());
               }
#endif
            }
            else if (is_cso)
            {
               try
               {
                  std::ifstream file;
                  file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                  file.open(entry_path, std::ios::binary);
                  file.seekg(0, std::ios::end);
                  custom_shader->code.resize(file.tellg());
#if _DEBUG && LOG_VERBOSE
                  {
                     std::stringstream s;
                     s << "LoadCustomShaders(Reading " << custom_shader->code.size() << " from " << filename_no_extension_string << ")";
                     reshade::log::message(reshade::log::level::debug, s.str().c_str());
                  }
#endif
                  if (!custom_shader->code.empty())
                  {
                     file.seekg(0, std::ios::beg);
                     file.read(reinterpret_cast<char*>(custom_shader->code.data()), custom_shader->code.size());
                  }
               }
               catch (const std::exception& e)
               {
               }
            }
         }
      }

      // TODO: theoretically if "prevent_shader_cache_saving" is true, we should clean all the shader hashes and defines from the config, though hopefully it's fine without
      if (pipelines_filter.empty() && !prevent_shader_cache_saving)
      {
         const std::shared_lock lock(s_mutex_shader_defines);
         // Only save after compiling, to make sure the config data aligns with the serialized compiled shaders data (blobs)
         ShaderDefineData::Save(shader_defines_data, NAME_ADVANCED_SETTINGS);
      }

      // Refresh the persistent custom shaders we have.
      if (optional_device_data)
      {
         CreateDeviceNativeShaders(*optional_device_data, &changed_luma_native_shaders_hashes);
      }
   }

   // Optionally compiles all the shaders we have in our data folder and links them with the game rendering pipelines
   void LoadCustomShaders(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines_filter = std::unordered_set<uint64_t>(), bool recompile_shaders = true)
   {
#if _DEBUG && LOG_VERBOSE
      reshade::log::message(reshade::log::level::info, "LoadCustomShaders()");
#endif

      if (recompile_shaders)
      {
         CompileCustomShaders(&device_data, false, pipelines_filter);
      }

      // Make sure the pipelines we read below stay alive (aren't removed/destroyed), even when "s_mutex_generic" is unlocked, hence we need to keep this locked for the whole time.
      // Note: this isn't entirely safe as we still call device functions while it's being locked, however, there should hopefully be no deadlocks,
      // as this is only used in a couple rare functions. if there were, we could either swap the lock order of this with the "s_mutex_generic" (delaying this below in the function),
      // and then caching a list of recently destroyed pipelines and skip them from being cloned at the end.
      std::shared_lock lock_pipeline_destroy(device_data.pipeline_cache_destruction_mutex);
      // We can, and should, only lock this after compiling new shaders (above).
      // We keep this locked across the function to avoid pollution in pipelines from other threads while this is running.
      std::unique_lock lock(s_mutex_generic);

      std::unordered_map<uint64_t, reshade::api::device*> pipelines_to_destroy;

      // Clear all previously loaded custom shaders
      UnloadCustomShaders(device_data, pipelines_filter, false, &pipelines_to_destroy);

      std::vector<std::tuple<CachedPipeline*, uint32_t, const CachedCustomShader*>> pipelines_to_clone;
#if DEVELOPMENT
      std::unordered_set<uint64_t> iterated_pipelines;
#endif

      const std::shared_lock lock_loading(s_mutex_loading);

      for (const auto& custom_shader_pair : custom_shaders_cache)
      {
         const uint32_t shader_hash = custom_shader_pair.first;
         const CachedCustomShader* custom_shader = custom_shader_pair.second;

         // Skip shaders that don't have code binaries at the moment, and luma native shaders as they aren't meant to replace game shaders
         if (custom_shader == nullptr || custom_shader->is_luma_native || custom_shader->code.empty()) continue;

         // Check if any pipelines use this shader
         auto pipelines_pair = device_data.pipeline_caches_by_shader_hash.find(shader_hash);
         if (pipelines_pair == device_data.pipeline_caches_by_shader_hash.end())
         {
#if _DEBUG && LOG_VERBOSE
            // It's likely the game hasn't loaded this shader yet, or anyway we have shaders for multiple games in a mod etc
            std::stringstream s;
            s << "LoadCustomShaders(Unknown hash: ";
            s << PRINT_CRC32(shader_hash);
            s << ")";
            reshade::log::message(reshade::log::level::warning, s.str().c_str());
#endif
            continue;
         }

         // Re-clone all the pipelines that used this shader hash (except the ones that are filtered out)
         for (CachedPipeline* cached_pipeline : pipelines_pair->second)
         {
            if (cached_pipeline == nullptr) continue;
            if (!pipelines_filter.empty() && !pipelines_filter.contains(cached_pipeline->pipeline.handle)) continue;

#if DEVELOPMENT // A bit redundant probably
            if (iterated_pipelines.contains(cached_pipeline->pipeline.handle)) { assert(false); continue; }
            iterated_pipelines.emplace(cached_pipeline->pipeline.handle);
#endif
            pipelines_to_clone.emplace_back(std::make_tuple(cached_pipeline, shader_hash, custom_shader));

            // Force clear this pipeline's clone in case it was already cloned
            auto pipeline_clone_handle = cached_pipeline->pipeline_clone.handle;
            if (ClearCustomShader(device_data, cached_pipeline, false))
            {
               device_data.pipeline_cache_by_pipeline_clone_handle.erase(pipeline_clone_handle);
               pipelines_to_destroy[pipeline_clone_handle] = cached_pipeline->device;
            }
         }
      }

      lock.unlock(); // Calls into the device could deadlock if the game rendering is multithreaded

      for (auto pair : pipelines_to_destroy)
      {
         pair.second->destroy_pipeline(reshade::api::pipeline{pair.first}); // TODO: verify the device is the same as the "device_data" we passed in here? Otherwise it might not properly support two devices. Theoretically we should pass in a device filter to the funcs to unload cloned pipelines
      }

      std::vector<std::tuple<CachedPipeline*, reshade::api::pipeline>> cloned_pipelines_data;

      // "s_mutex_loading" is expected to still be locked
      for (const auto& pipeline_to_clone : pipelines_to_clone)
      {
         CachedPipeline* cached_pipeline = std::get<0>(pipeline_to_clone);
         const uint32_t shader_hash = std::get<1>(pipeline_to_clone);
         const CachedCustomShader* custom_shader = std::get<2>(pipeline_to_clone);

#if _DEBUG && LOG_VERBOSE
         {
            std::stringstream s;
            s << "LoadCustomShaders(Read ";
            s << custom_shader->code.size() << " bytes ";
            s << " from " << custom_shader->file_path.string();
            s << ")";
            reshade::log::message(reshade::log::level::debug, s.str().c_str());
         }
#endif

         // DX12 can use PSO objects that need to be cloned
         const uint32_t subobject_count = cached_pipeline->subobject_count;
         reshade::api::pipeline_subobject* subobjects = cached_pipeline->subobjects_cache;
         reshade::api::pipeline_subobject* new_subobjects = Shader::ClonePipelineSubobjects(subobject_count, subobjects);

#if _DEBUG && LOG_VERBOSE
         {
            std::stringstream s;
            s << "LoadCustomShaders(Cloning pipeline ";
            s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
            s << " with " << subobject_count << " object(s)";
            s << ")";
            reshade::log::message(reshade::log::level::debug, s.str().c_str());
         }
         reshade::log::message(reshade::log::level::debug, "Iterating pipeline...");
#endif

         for (uint32_t i = 0; i < subobject_count; ++i)
         {
            const auto& subobject = subobjects[i];
            switch (subobject.type)
            {
            case reshade::api::pipeline_subobject_type::geometry_shader:
            case reshade::api::pipeline_subobject_type::vertex_shader:
            case reshade::api::pipeline_subobject_type::compute_shader:
            case reshade::api::pipeline_subobject_type::pixel_shader:
            break;
            default:
            continue;
            }

            auto& clone_subject = new_subobjects[i];

            auto* clone_desc = static_cast<reshade::api::shader_desc*>(clone_subject.data);

            free(const_cast<void*>(clone_desc->code));
            clone_desc->code_size = custom_shader->code.size();
            clone_desc->code = malloc(custom_shader->code.size());
            ASSERT_ONCE(custom_shader->code.size() != 0);
            std::memcpy(const_cast<void*>(clone_desc->code), custom_shader->code.data(), custom_shader->code.size());

#if _DEBUG && LOG_VERBOSE
            const auto new_hash = Shader::BinToHash(static_cast<const uint8_t*>(clone_desc->code), clone_desc->code_size);
            {
               std::stringstream s;
               s << "LoadCustomShaders(Injected pipeline data";
               s << " with " << PRINT_CRC32(new_hash);
               s << " (" << custom_shader->code.size() << " bytes)";
               s << ")";
               reshade::log::message(reshade::log::level::debug, s.str().c_str());
            }
#endif
         }

#if _DEBUG && LOG_VERBOSE
         {
            std::stringstream s;
            s << "Creating pipeline clone (";
            s << "hash: " << PRINT_CRC32(shader_hash);
            s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
            s << ", subobject_count: " << subobject_count;
            s << ")";
            reshade::log::message(reshade::log::level::debug, s.str().c_str());
         }
#endif

         reshade::api::pipeline pipeline_clone = {};
         // For DX11, this is "D3D11Device::CreatePixelShader()" or equivalent functions
         const bool built_pipeline_ok = cached_pipeline->device->create_pipeline(
            cached_pipeline->layout,
            subobject_count,
            new_subobjects,
            &pipeline_clone);
#if !_DEBUG || !LOG_VERBOSE
         if (!built_pipeline_ok)
#endif
         {
            std::stringstream s;
            s << "LoadCustomShaders(Cloned ";
            s << reinterpret_cast<void*>(cached_pipeline->pipeline.handle);
            s << " => " << reinterpret_cast<void*>(pipeline_clone.handle);
            s << ", layout: " << reinterpret_cast<void*>(cached_pipeline->layout.handle);
            s << ", size: " << subobject_count;
            s << ", " << (built_pipeline_ok ? "OK" : "FAILED!");
            s << ")";
            reshade::log::message(built_pipeline_ok ? reshade::log::level::info : reshade::log::level::error, s.str().c_str());
         }

         if (built_pipeline_ok)
         {
            cloned_pipelines_data.push_back(std::make_tuple(cached_pipeline, pipeline_clone));
         }
         // Clean up unused cloned subobjects
         else
         {
            ASSERT_ONCE(pipeline_clone.handle == 0);
            DestroyPipelineSubojects(new_subobjects, subobject_count);
            new_subobjects = nullptr;
         }
      }

      lock.lock(); // Needed for "pipeline_cache_by_pipeline_clone_handle"

      for (const auto& cloned_pipeline_data : cloned_pipelines_data)
      {
         CachedPipeline* cached_pipeline = std::get<0>(cloned_pipeline_data);
         reshade::api::pipeline pipeline_clone = std::get<1>(cloned_pipeline_data);

         assert(!cached_pipeline->cloned && cached_pipeline->pipeline_clone.handle == 0); // We destroy the potential previous one above in "UnloadCustomShaders"
         cached_pipeline->pipeline_clone = pipeline_clone;
         cached_pipeline->cloned = true;
         // TODO: make sure the pixel shaders have the same signature (through reflections) unless the vertex shader was also changed and has a different output signature? Just to make sure random hashes didn't end up replacing an accidentally equal hash (however unlikely)
         device_data.pipeline_cache_by_pipeline_clone_handle[pipeline_clone.handle] = cached_pipeline;
         device_data.cloned_pipeline_count++;
         device_data.cloned_pipelines_changed = true;
      }
   }

   void OnDisplayModeChanged()
   {
      // s_mutex_reshade should already be locked here, it's not necessary anyway
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).editable = cb_luma_global_settings.DisplayMode != DisplayModeType::SDR; //TODOFT4: necessary to disable this in SDR?

      game->OnDisplayModeChanged();
   }

   bool IsSupportedGraphicsAPI(reshade::api::device_api api)
   {
      return api == reshade::api::device_api::d3d11;
   }

   bool OnCreateDevice(reshade::api::device_api api, uint32_t& api_version)
   {
#if !CHECK_GRAPHICS_API_COMPATIBILITY
#if DEVELOPMENT || TEST
      ASSERT_ONCE_MSG(api == reshade::api::device_api::d3d11, "Luma only supports DirectX 11 at the moment, add \"CHECK_GRAPHICS_API_COMPATIBILITY\" to ignore calls from other APIs");
#else
      if (api != reshade::api::device_api::d3d11)
         MessageBoxA(NULL, "The application tried to create a non DirectX 11 device. Luma currently only supports DirectX 11, the application might crash.", NAME, MB_SETFOREGROUND);
#endif
#endif

#if DEVELOPMENT && 0 // Test: force the latest version to access all the latest features (it doesn't seem to work! nor is much needed, but we should try again as ReShade had a bug with it)
      api_version = D3D_FEATURE_LEVEL_12_2;
      return true;
#endif

#if ENABLE_FIDELITY_SK
      // Required by FSR 3 on DX11. Also goes to determine whether we have to use D3D11_1_UAV_SLOT_COUNT or (the older) D3D11_PS_CS_UAV_REGISTER_COUNT.
      // This is usually fully retro compatible with all games.
      if (api_version == D3D_FEATURE_LEVEL_11_0)
      {
         api_version = D3D_FEATURE_LEVEL_11_1;
         return true;
      }
#endif

      return false;
   }

   void OnInitDevice(reshade::api::device* device)
   {
      // Always skip this one, don't use "SKIP_UNSUPPORTED_DEVICE_API", some games create devices of other graphics API without actually doing anything with them (e.g. Deus Ex HR)
      if (!IsSupportedGraphicsAPI(device->get_api())) return;

      ID3D11Device* native_device = (ID3D11Device*)(device->get_native()); // This is the unproxied device, the one the game tried to natively create was instead created as a proxy by reshade, but we don't want that one, as that will pass all our calls through ReShade too, while we want to keep that layer only for stuff coming from the game
      DeviceData& device_data = *device->create_private_data<DeviceData>();
      device_data.native_device = native_device;

      device_data.uav_max_count = (native_device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1) ? D3D11_1_UAV_SLOT_COUNT : D3D11_PS_CS_UAV_REGISTER_COUNT;

      native_device->GetImmediateContext(&device_data.primary_command_list);

      {
         const std::unique_lock lock(s_mutex_device);
         // Inherit a minimal set of states from the possible previous device. Any other state wouldn't be relevant or could be outdated, so we might as well reset all to default.
         if (!global_devices_data.empty())
         {
            device_data.output_resolution = global_devices_data[0]->output_resolution;
            device_data.render_resolution = global_devices_data[0]->render_resolution;
         }
         // Fallback on the display resolution as default, it's the best guess we can make, most games will start fullscreen
         else
         {
            device_data.output_resolution = float2(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
            device_data.render_resolution = device_data.output_resolution;
         }
         device_data.previous_render_resolution = device_data.render_resolution;
         // In case there already was a device, we could copy some states from it, but given that the previous device might still be rendering a frame and is in a "random" state, let's keep them completely independent
         global_native_devices.push_back(native_device);
         global_devices_data.push_back(&device_data);
      }

      game->OnCreateDevice(native_device, device_data);

      HRESULT hr;

      D3D11_BUFFER_DESC buffer_desc = {};
      // From MS docs: you must set the ByteWidth value of D3D11_BUFFER_DESC in multiples of 16, and less than or equal to D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT.
      buffer_desc.ByteWidth = sizeof(CB::LumaGlobalSettingsPadded);
      buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
      buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      D3D11_SUBRESOURCE_DATA data = {};
      {
         const std::unique_lock lock_reshade(s_mutex_reshade);
         data.pSysMem = &cb_luma_global_settings;
         hr = native_device->CreateBuffer(&buffer_desc, &data, &device_data.luma_global_settings);
         device_data.cb_luma_global_settings_dirty = false;
      }
      assert(SUCCEEDED(hr));
      if (luma_data_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
      {
         buffer_desc.ByteWidth = sizeof(CB::LumaInstanceDataPadded);
#if 1 // Start it with no specific data, we always write the data before bidning it to the GPU
         hr = native_device->CreateBuffer(&buffer_desc, nullptr, &device_data.luma_instance_data);
#else
         static CB::LumaInstanceDataPadded cb_luma_instance_data = {};
         data.pSysMem = &device_data.cb_luma_instance_data;
         hr = native_device->CreateBuffer(&buffer_desc, &data, &device_data.luma_instance_data);
#endif
         assert(SUCCEEDED(hr));
      }
      if (luma_ui_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
      {
         buffer_desc.ByteWidth = sizeof(CB::LumaUIDataPadded);
         data.pSysMem = &device_data.cb_luma_ui_data;
         hr = native_device->CreateBuffer(&buffer_desc, &data, &device_data.luma_ui_data);
         assert(SUCCEEDED(hr));
      }

      D3D11_SAMPLER_DESC sampler_desc = {};
      sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; // Bilinear filtering
      sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP; // Clamp by default (though returning black would also be good?)
      sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_desc.MipLODBias = 0.f;
      sampler_desc.MaxAnisotropy = 1;
      sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
      sampler_desc.MinLOD = 0.f;
      sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
      hr = native_device->CreateSamplerState(&sampler_desc, &device_data.sampler_state_linear);
      assert(SUCCEEDED(hr));
      sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // Nearest filtering
      hr = native_device->CreateSamplerState(&sampler_desc, &device_data.sampler_state_point);
      assert(SUCCEEDED(hr));

      D3D11_BLEND_DESC blend_desc = {};
      blend_desc.AlphaToCoverageEnable = FALSE;
      blend_desc.IndependentBlendEnable = FALSE;
      // We only need RT 0
      blend_desc.RenderTarget[0].BlendEnable = FALSE;
      blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
      blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
      blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
      blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
      blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
      blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
      blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      hr = native_device->CreateBlendState(&blend_desc, &device_data.default_blend_state);
      assert(SUCCEEDED(hr));

      D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = {};
      depth_stencil_desc.DepthEnable = false;
      depth_stencil_desc.StencilEnable = false;
      hr = native_device->CreateDepthStencilState(&depth_stencil_desc, &device_data.default_depth_stencil_state);
      assert(SUCCEEDED(hr));

#if DEVELOPMENT
      depth_stencil_desc.DepthEnable = true;
      depth_stencil_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
      depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
      hr = native_device->CreateDepthStencilState(&depth_stencil_desc, &device_data.depth_test_false_write_true_stencil_false_state);
      assert(SUCCEEDED(hr));
#endif

#if ENABLE_NVAPI
      Display::InitNVApi();
#endif

#if ENABLE_SR
      com_ptr<IDXGIDevice> native_dxgi_device;
      hr = native_device->QueryInterface(&native_dxgi_device);
      com_ptr<IDXGIAdapter> native_adapter;
      if (SUCCEEDED(hr))
      {
         hr = native_dxgi_device->GetAdapter(&native_adapter);
      }
      assert(SUCCEEDED(hr));

      bool selected_sr_implementation = false;
      for (auto& sr_implementation : sr_implementations)
      {
         if (sr_implementation.second != nullptr)
         {
            device_data.sr_implementations_instances[sr_implementation.first] = nullptr; // Create an empty element
            // We always do this, which will force the SR dll to load, but it should be fast enough to not bother users from other vendors
            sr_implementation.second->Init(device_data.sr_implementations_instances[sr_implementation.first], native_device, native_adapter.get());

            if (device_data.sr_implementations_instances[sr_implementation.first] && !device_data.sr_implementations_instances[sr_implementation.first]->is_supported)
            {
               sr_implementation.second->Deinit(device_data.sr_implementations_instances[sr_implementation.first], native_device); // No need to keep it initialized if it's not supported
            }

            if (device_data.sr_implementations_instances[sr_implementation.first] && device_data.sr_implementations_instances[sr_implementation.first]->is_supported)
            {
               const std::shared_lock lock_reshade(s_mutex_reshade);
               if (!selected_sr_implementation && SR::AreTypesEqual(sr_user_type, sr_implementation.first))
               {
                  selected_sr_implementation = true; // Take the first supported selected one
                  device_data.sr_type = sr_implementation.first;
                  // For now we continue initializing the other ones, which usually shouldn't have a huge cost as no relevant resources are allocated,
                  // however, if we wanted, we could dynamically init and deinit leaving just the currently used one.
               }
            }
            else
            {
               device_data.sr_implementations_instances.erase(sr_implementation.first);
            }
         }
      }
      if (device_data.sr_type == SR::Type::None)
      {
         const std::unique_lock lock_reshade(s_mutex_reshade);
         sr_user_type = SR::UserType::None; // Reset the global user setting if it's not supported, we want to grey it out in the UI (there's no need to serialize the new value for it though!)
		}
#endif // ENABLE_SR

      game->OnInitDevice(native_device, device_data);

      // If we upgrade textures, make sure that MSAA DXGI_FORMAT_R16G16B16A16_FLOAT is supported on our GPU, given that it's optional.
      // Most games don't have MSAA, but it might be enforced at driver level.
      if (texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled)
      {
         UINT quality_levels = 0;
         HRESULT hr;
         // We could go up to "D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT" but realistically no game ever does more than 8x (and odd values are not supported)
         hr = native_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT, 2, &quality_levels);
         ASSERT_ONCE(SUCCEEDED(hr) && quality_levels > 0);
         hr = native_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT, 4, &quality_levels);
         ASSERT_ONCE(SUCCEEDED(hr) && quality_levels > 0);
         hr = native_device->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT, 8, &quality_levels);
         ASSERT_ONCE(SUCCEEDED(hr) && quality_levels > 0);
      }

      // If all custom shaders from boot already loaded/compiled, but the custom device shaders weren't created, create them
      if (precompile_custom_shaders && block_draw_until_device_custom_shaders_creation)
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         const std::unique_lock lock_shader_objects(s_mutex_shader_objects);
         if (!thread_auto_compiling_running && !device_data.created_native_shaders)
         {
            CreateDeviceNativeShaders(device_data, nullptr, false);
         }
      }
   }

   void OnDestroyDevice(reshade::api::device* device)
   {
      if (!IsSupportedGraphicsAPI(device->get_api())) return;

      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>(); // No need to lock the data mutex here, it could be concurrently used at this point

      game->OnDestroyDeviceData(device_data);

      // It can apparently happen that in DX11 the device destructor callback is sent before its pipelines, so make sure we empty the memory before.
      // Note: ReShade has fixed this bug now, but as of 6.6 it simply skips ever calling the destroy event for these.
      {
         std::unordered_map<uint64_t, Shader::CachedPipeline*> pipeline_cache_by_pipeline_handle;
         {
            const std::unique_lock lock(s_mutex_generic); // Nobody should be accessing our device data at this point but who knows
            pipeline_cache_by_pipeline_handle = device_data.pipeline_cache_by_pipeline_handle;
            device_data.pipeline_cache_by_pipeline_handle.clear(); // Redundant but, let's do it anyway (change the mutex lock to shared if you change this)
         }
         for (auto& pipeline_pair : pipeline_cache_by_pipeline_handle)
         {
            OnDestroyPipeline(device, reshade::api::pipeline{ pipeline_pair.first });
         }
      }

      {
         const std::unique_lock lock(s_mutex_device);
         if (std::vector<ID3D11Device*>::iterator position = std::find(global_native_devices.begin(), global_native_devices.end(), native_device); position != global_native_devices.end())
         {
            global_native_devices.erase(position);
         }
         else
         {
            ASSERT_ONCE(false);
         }
         if (std::vector<DeviceData*>::iterator position = std::find(global_devices_data.begin(), global_devices_data.end(), &device_data); position != global_devices_data.end())
         {
            global_devices_data.erase(position);
         }
         else
         {
            ASSERT_ONCE(false);
         }
      }

      ASSERT_ONCE(device_data.swapchains.empty()); // Hopefully this is forcefully garbage collected when the device is destroyed (it is!)

      if (device_data.thread_auto_loading.joinable())
      {
         device_data.thread_auto_loading.join();
         device_data.thread_auto_loading_running = false;
      }

      assert(device_data.cb_per_view_global_buffer_map_data == nullptr); // It's fine (but not great) if we map wasn't unmapped before destruction (not our fault anyway)

      {
         const std::unique_lock lock_samplers(s_mutex_samplers);
         ASSERT_ONCE(device_data.custom_sampler_by_original_sampler.empty()); // These should be guaranteed to have been cleared already ("OnDestroySampler()")
         device_data.custom_sampler_by_original_sampler.clear(); // Redundant but, let's do it anyway (change the mutex lock to shared if you change this)
      }

#if ENABLE_SR
      auto* sr_instance_data = device_data.GetSRInstanceData();
      if (sr_instance_data)
      {
         sr_implementations[device_data.sr_type]->Deinit(sr_instance_data); // NOTE: this could stutter the game on closure as it forces unloading the SR DLL (if it's the last device instance?), but we can't avoid it
      }
#endif // ENABLE_SR

      device->destroy_private_data<DeviceData>();
   }

#if DEVELOPMENT
   // Prevent games from pausing when alt tabbing out of it (e.g. when editing shaders) by silencing focus loss events
   LRESULT WINAPI CustomWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
   {
      if (lParam == WM_KILLFOCUS)
      {
         // Lost keyboard focus
         return 0; // block it
      }
      else if (lParam == WM_ACTIVATE)
      {
         if (wParam == WA_INACTIVE)
         {
            // Lost foreground activation
            return 0; // block it
         }
      }
      return CallWindowProc(game_window_original_proc, hWnd, msg, wParam, lParam);
   }
   LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam)
   {
      if (nCode >= 0)
      {
         CWPSTRUCT* pCwp = (CWPSTRUCT*)lParam;
         if (pCwp->message == WM_KILLFOCUS)
         {
            // Lost keyboard focus
            return 1; // block it
         }
         else if (pCwp->message == WM_ACTIVATE)
         {
            if (LOWORD(pCwp->wParam) == WA_INACTIVE)
            {
               // Lost foreground activation
               return 1; // block it
				}
         }
      }
      return CallNextHookEx(NULL, nCode, wParam, lParam);
   }
#endif

   bool OnCreateSwapchain(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void* hwnd)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api(), false);

      // There's only one swapchain so it's fine if this is global ("OnInitSwapchain()" will always be called later anyway)
      bool changed = false;

#if DEVELOPMENT
      ASSERT_ONCE(desc.back_buffer.texture.format != reshade::api::format::unknown); // With the latest ReShade changes, this should never be set to Unknown, even if the game did a swapchain buffer resize and preserved the previous format.
      last_attempted_upgraded_resource_creation_format = desc.back_buffer.texture.format;
#endif

      ASSERT_ONCE(desc.back_buffer.texture.samples == 1); // Neither flip model nor scRGB HDR are supported with MSAA

      // sRGB formats don't support flip modes, if we previously upgraded the swapchain, select a flip mode compatible format when the swapchain resizes, as we can't change it anymore after creation
      if (swapchain_format_upgrade_type < TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type > SwapchainUpgradeType::None && (desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::b8g8r8a8_unorm_srgb))
      {
         if (desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb)
            desc.back_buffer.texture.format = reshade::api::format::r8g8b8a8_unorm;
         else
            desc.back_buffer.texture.format = reshade::api::format::b8g8r8a8_unorm;
         changed = true;
      }

      // TODO: add a flag to disable these for the "GRAPHICS_ANALYZER"? They are still needed for HDR though
      // Generally we want to add these flags in all cases, they seem to work in all games
      {
         desc.back_buffer_count = max(desc.back_buffer_count, 2); // Needed by flip models, which is mandatory for HDR. Note that DX10/11 will still only create one buffer, even if their desc says they have two.
         if ((swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type > SwapchainUpgradeType::None) || (desc.back_buffer.texture.format != reshade::api::format::r8g8b8a8_unorm_srgb && desc.back_buffer.texture.format != reshade::api::format::b8g8r8a8_unorm_srgb)) // sRGB formats don't support flip modes
         {
            desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
#if !GRAPHICS_ANALYZER // TODO: investigate "DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT", and anyway make it optional as it lowers lag at the cost of not quequing up frames
            // Only works in "DXGI_SWAP_EFFECT_FLIP_DISCARD" mode and non FSE mode (and given we can't change it live when FSE is toggled, we only allow it if FSE is prevented by the game)
            if (prevent_fullscreen_state)
            {
               desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            }
#endif
         }
         ASSERT_ONCE((desc.present_flags & DXGI_SWAP_CHAIN_FLAG_FULLSCREEN_VIDEO) == 0); // Uh?

         desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // Games will still need to call "Present()" with "DXGI_PRESENT_ALLOW_TEARING" for this to do anything (ReShade will automatically do it if this flag is set)
         if (prevent_fullscreen_state) // Not sure this helps but it doesn't seem to hurt
         {
            desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
         }
         else
         {
            desc.present_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
         }
         desc.fullscreen_refresh_rate = 0.f; // This fixes games forcing a specific refresh rate (e.g. Mafia III forces 60Hz for no reason)
         if (prevent_fullscreen_state)
         {
            desc.fullscreen_state = false; // Force disable FSE (see "OnSetFullscreenState()")
         }
         changed = true;
      }

      // Note that occasionally this breaks after resizing the swapchain, because some games resize the swapchain maintaining whatever format it had before
      last_swapchain_linear_space = desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::b8g8r8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::r16g16b16a16_float;

      if (swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type > SwapchainUpgradeType::None)
      {
         ASSERT_ONCE(desc.back_buffer.texture.format == reshade::api::format::r10g10b10a2_unorm || desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm || desc.back_buffer.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb || desc.back_buffer.texture.format == reshade::api::format::r16g16b16a16_float); // Just a bunch of formats we encountered and we are sure we can upgrade (or that have already been upgraded)
         // DXGI_FORMAT_R16G16B16A16_FLOAT will automatically pick DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 on first creation
         desc.back_buffer.texture.format = swapchain_upgrade_type == SwapchainUpgradeType::scRGB ? reshade::api::format::r16g16b16a16_float : reshade::api::format::r10g10b10a2_unorm;
         changed = true;
      }

      return changed;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain, bool resize)
   {
      SKIP_UNSUPPORTED_DEVICE_API(swapchain->get_device()->get_api());

      IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
#if 0
      DXGI_SWAP_CHAIN_DESC desc;
      native_swapchain->GetDesc(&desc);
      const size_t back_buffer_count = desc.BufferCount;
#else // Always 1 on DX10/11, even if the swapchain desc says 2...
      const size_t back_buffer_count = swapchain->get_back_buffer_count();
#endif
      auto* device = swapchain->get_device();
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      SwapchainData& swapchain_data = *swapchain->create_private_data<SwapchainData>();
      ASSERT_ONCE(&device_data != nullptr); // Hacky nullptr check (should ever be able to happen)

#if DEVELOPMENT
      const reshade::api::resource_desc resource_desc = device->get_resource_desc(swapchain->get_back_buffer(0));
      if (last_attempted_upgraded_resource_creation_format != resource_desc.texture.format) // Upgraded
      {
         const std::unique_lock lock(device_data.mutex);
         for (uint32_t index = 0; index < back_buffer_count; index++)
         {
            device_data.original_upgraded_resources_formats[swapchain->get_back_buffer(index).handle] = last_attempted_upgraded_resource_creation_format;
         }
      }
#endif

      swapchain_data.vanilla_was_linear_space = last_swapchain_linear_space || game->ForceVanillaSwapchainLinear();
#if !GAME_MAFIA_III // We don't care for this case, it's dev only, if we didn't do this, when we unload shaders the game would be washed out (the UI still is)
      // We expect this define to be set to linear if the swapchain was already linear in Vanilla SDR (there might be code that makes such assumption)
      ASSERT_ONCE(!swapchain_data.vanilla_was_linear_space || (GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1));
#endif

      {
         const std::unique_lock lock(swapchain_data.mutex); // Not much need to lock this on its own creation, but let's do it anyway...
         for (uint32_t index = 0; index < back_buffer_count; index++)
         {
            auto buffer = swapchain->get_back_buffer(index);
            swapchain_data.back_buffers.emplace(buffer.handle);
            swapchain_data.display_composition_rtvs.push_back(nullptr);
         }
      }

      {
         const std::unique_lock lock(device_data.mutex);
         device_data.swapchains.emplace(swapchain);
         ASSERT_ONCE(SUCCEEDED(device_data.swapchains.size() == 1)); // Having more than one swapchain per device is probably supported but unexpected

         for (uint32_t index = 0; index < back_buffer_count; index++)
         {
            auto buffer = swapchain->get_back_buffer(index);
            device_data.back_buffers.emplace(buffer.handle);
         }
      }

      // We assume there's only one swapchain (there is!), given that the resolution would theoretically be by swapchain and not device.
      // If the game created more than one, the previous one is likely discared and not garbage collected yet.
      // If any games broke these assumptions, we could refine this design.
      DXGI_SWAP_CHAIN_DESC swapchain_desc;
      HRESULT hr = native_swapchain->GetDesc(&swapchain_desc);
      ASSERT_ONCE(SUCCEEDED(hr));
      if (SUCCEEDED(hr))
      {
         ASSERT_ONCE_MSG(swapchain_desc.SampleDesc.Count == 1, "MSAA is unexpectedly enabled on the Swapchain, Luma might not be compatible with it");
         device_data.output_resolution.x = swapchain_desc.BufferDesc.Width;
         device_data.output_resolution.y = swapchain_desc.BufferDesc.Height;
         device_data.render_resolution.x = device_data.output_resolution.x;
         device_data.render_resolution.y = device_data.output_resolution.y;
      }

		device_data.ui_texture = nullptr;
      device_data.ui_texture_rtv = nullptr;
      device_data.ui_texture_srv = nullptr;
      // At the moment this is just created when the swapchain changes anything about it,
      // so we don't support changing this shader define live, but if needed, we could always move these allocations
      if (GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) >= 3)
      {
         device_data.ui_texture = CloneTexture<ID3D11Texture2D>(native_device, (ID3D11Texture2D*)swapchain->get_back_buffer(0).handle, ui_separation_format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_BIND_UNORDERED_ACCESS, true, false, nullptr); // Unordered Access is not needed until proven otherwise
         native_device->CreateRenderTargetView(device_data.ui_texture.get(), nullptr, &device_data.ui_texture_rtv);
         native_device->CreateShaderResourceView(device_data.ui_texture.get(), nullptr, &device_data.ui_texture_srv);
      }

      IDXGISwapChain3* native_swapchain3;
      // The cast pointer is actually the same, we are just making sure the type is right.
      hr = native_swapchain->QueryInterface(&native_swapchain3);
      ASSERT_ONCE(SUCCEEDED(hr)); // This is required by LUMA, but all systems should have this swapchain by now

      // This is basically where we verify and update the user display settings
      if (native_swapchain3 != nullptr)
      {
         const std::unique_lock lock_reshade(s_mutex_reshade);
         Display::GetHDRMaxLuminance(native_swapchain3, device_data.default_user_peak_white, srgb_white_level);
         Display::IsHDRSupportedAndEnabled(swapchain_desc.OutputWindow, hdr_supported_display, hdr_enabled_display, native_swapchain3);
         const bool window_changed = game_window != swapchain_desc.OutputWindow;
         if (window_changed)
         {
            game_window = swapchain_desc.OutputWindow; // This shouldn't really need any thread safety protection
#if DEVELOPMENT //TODOFT: test/fix/finish
            if (game_window)
            {
#if 1
               WNDPROC game_window_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(game_window, GWLP_WNDPROC));
               if (game_window_proc != game_window_custom_proc)
               {
                  game_window_original_proc = game_window_proc;
                  ASSERT_ONCE(game_window_original_proc != nullptr);
                  WNDPROC game_window_prev_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CustomWndProc)));
                  ASSERT_ONCE(game_window_prev_proc == game_window_proc); // The above returns the ptr before replacement
                  game_window_custom_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(game_window, GWLP_WNDPROC));
               }
#else
               if (!game_window_proc_hook)
               {
                  DWORD threadId = GetWindowThreadProcessId(game_window, NULL);
                  game_window_proc_hook = SetWindowsHookEx(WH_CALLWNDPROC, CallWndProc, NULL, threadId);
                  ASSERT_ONCE(game_window_proc_hook);
               }
#endif
            }
            else
            {
               game_window_custom_proc = nullptr;
               if (game_window_proc_hook)
               {
                  UnhookWindowsHookEx(game_window_proc_hook);
                  game_window_proc_hook = nullptr;
               }
            }
#endif // DEVELOPMENT
         }

         if (!hdr_enabled_display)
         {
            // Force the display mode to SDR if HDR is not engaged
            cb_luma_global_settings.DisplayMode = DisplayModeType::SDR;
            OnDisplayModeChanged();
            cb_luma_global_settings.ScenePeakWhite = srgb_white_level;
            cb_luma_global_settings.ScenePaperWhite = srgb_white_level;
            cb_luma_global_settings.UIPaperWhite = srgb_white_level;
         }
         // Avoid increasing the peak if the user has SDR mode set, SDR mode might still rely on the peak white value being set to "srgb_white_level"
         else if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR && hdr_display_mode_pending_auto_peak_white_calibration)
         {
            cb_luma_global_settings.ScenePeakWhite = device_data.default_user_peak_white;
         }
         else if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR)
         {
            // Making sure SDR is all defaulted to the right values
            ASSERT_ONCE(cb_luma_global_settings.ScenePeakWhite == srgb_white_level && cb_luma_global_settings.ScenePaperWhite == srgb_white_level);
         }
         device_data.cb_luma_global_settings_dirty = true;
         hdr_display_mode_pending_auto_peak_white_calibration = false;

         if (swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type > SwapchainUpgradeType::None)
         {
#if 0 // Not needed until proven otherwise (we already upgrade in "OnCreateSwapchain()", which should always be called when resizing the swapchain too)
            UINT flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            DXGI_FORMAT format = swapchain_upgrade_type == SwapchainUpgradeType::scRGB ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A2_UNORM;
            hr = native_swapchain3->ResizeBuffers(0, 0, 0, format, flags); // Pass in zero to not change any values if not the format
            ASSERT_ONCE(SUCCEEDED(hr));
#endif
         }

#if !GAME_PREY
         DXGI_COLOR_SPACE_TYPE color_space;
         bool set_color_space = false;
         if (swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type > SwapchainUpgradeType::None)
         {
            if (swapchain_upgrade_type == SwapchainUpgradeType::scRGB)
               color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            else         
               color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            set_color_space = true;
         }
         // Note: if we are not upgrading the swapchain, we don't know the original color space,
         // nor we can reset it to the original one if we stopped upgrading the swapchain after boot (ReShade uses debug information used by MS but it's not "safe" to access it).
         else
         {
            color_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; // SDR/Default
            if (swapchain_desc.BufferDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
            {
               color_space = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
            }
#if 0 // 10 bit could be SDR or HDR, it's impossible to know for sure... For now, assume SDR given that Luma could always upgrade the swapchain to HDR10 anyway
            else if (swapchain_desc.BufferDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM)
            {
               color_space = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
            }
#endif
            set_color_space = false;
         }
         if (set_color_space)
         {
            hr = native_swapchain3->SetColorSpace1(color_space);
            ASSERT_ONCE(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
               // SpecialK and ReShade use this private data GUID to track the current swap chain color space, so just do the same
               constexpr GUID SKID_SwapChainColorSpace = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}}; // {018B57E4-1493-4953-ADF2-DE6D99CC05E5}
               native_swapchain3->SetPrivateData(SKID_SwapChainColorSpace, sizeof(color_space), &color_space);
            }
         }
#endif

         // We release the resource because the swapchain lifespan is, and should be, controlled by the game.
         // We already have "OnDestroySwapchain()" to handle its destruction.
         native_swapchain3->Release();
      }

      static std::atomic<bool> warning_sent;
      if (device_data.output_resolution.x == device_data.output_resolution.y && texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0) && !warning_sent.exchange(true))
      {
         MessageBoxA(game_window, "Your current game output resolution has an aspect ratio of 1:1 (a squared resolution), that might cause issues with texture upgrades by aspect ratio, given that shadow maps and other things are often rendered in squared textures.", NAME, MB_SETFOREGROUND);
      }

#if ENABLE_NVAPI // TODO: finish this... Make it optional, feed the game metadata (peak brightness, color gamut etc)
      Display::EnableHdr10PlusDisplayOutput(game_window);
#endif

      {
         // TODO: put code to track all recently created resources and late upgraded them if the size/aspect ratio now matches the swapchain (some games resize the swapchain after resources, so in that case we should handle indirect upgrades like this)
      }

      game->OnInitSwapchain(swapchain);
   }

   void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize)
   {
      SKIP_UNSUPPORTED_DEVICE_API(swapchain->get_device()->get_api());

      auto* device = swapchain->get_device();
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      ASSERT_ONCE(&device_data != nullptr); // Hacky nullptr check (should ever be able to happen)
      SwapchainData& swapchain_data = *swapchain->get_private_data<SwapchainData>();
      {
         const std::shared_lock lock_swapchain(swapchain_data.mutex);

         {
            const std::unique_lock lock_device(device_data.mutex);
            device_data.swapchains.erase(swapchain);
            for (const uint64_t handle : swapchain_data.back_buffers)
            {
               device_data.back_buffers.erase(handle);
#if DEVELOPMENT
               device_data.original_upgraded_resources_formats.erase(handle);
#endif // DEVELOPMENT
            }
         }

         // Before resizing the swapchain, we need to make sure any of its resources/views are not bound to any state
         if (resize && !swapchain_data.display_composition_rtvs.empty())
         {
            ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            device_data.primary_command_list->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &depth_stencil_view);
            bool rts_changed = false;
            for (size_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            {
               for (const auto& display_composition_rtv : swapchain_data.display_composition_rtvs)
               {
                  if (rtvs[i].get() != nullptr && rtvs[i].get() == display_composition_rtv.get())
                  {
                     rtvs[i] = nullptr;
                     rts_changed = true;
                  }
               }
            }
            if (rts_changed)
            {
               ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(rtvs[0]);
               device_data.primary_command_list->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());
            }
         }
      }

      swapchain->destroy_private_data<SwapchainData>();
   }

   void CenterWindowAndRemoveBorders()
   {
      HMONITOR h_monitor = MonitorFromWindow(game_window, MONITOR_DEFAULTTONEAREST);

      MONITORINFO mi = {};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfo(h_monitor, &mi))
      {
         constexpr bool exclude_task_bar = true;
         const RECT& rc_monitor = exclude_task_bar ? mi.rcMonitor : mi.rcWork; // work area (excludes taskbar)
         int screen_width = rc_monitor.right - rc_monitor.left;
         int screen_height = rc_monitor.bottom - rc_monitor.top;

         // Remove window borders (force borderless)
         LONG_PTR style = GetWindowLongPtr(game_window, GWL_STYLE);
         style &= ~(WS_OVERLAPPEDWINDOW | WS_BORDER | WS_THICKFRAME | WS_DLGFRAME); // remove title bar + borders
         style |= WS_POPUP | WS_VISIBLE; // popup style, no borders
         SetWindowLongPtr(game_window, GWL_STYLE, style);

         // Also remove extended window styles that may add shadows/borders (extra safety, probably useless)
         LONG_PTR ex_style = GetWindowLongPtr(game_window, GWL_EXSTYLE);
         ex_style &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
         SetWindowLongPtr(game_window, GWL_EXSTYLE, ex_style);

         // Get current window rectangle (it should match with the last swapchain resolution)
         RECT rc_window;
         GetWindowRect(game_window, &rc_window);
         int win_width = rc_window.right - rc_window.left;
         int win_height = rc_window.bottom - rc_window.top;

         const bool force_resize_to_screen = false; // Some games properly response to forced window resize events, other will reject it or stretch the image, expose this if ever necessary
         const bool resize_to_screen = force_resize_to_screen || win_width > screen_width || win_height > screen_height; // Prevent games from going bigger than the window, this handles a bug in Thumper where it resized too big, hopefully it won't break other games
         if (resize_to_screen)
         {
            win_width = screen_width;
            win_height = screen_height;
         }

         // This will center it in the monitor it currently is in
         int x = rc_monitor.left + (screen_width - win_width) / 2;
         int y = rc_monitor.top + (screen_height - win_height) / 2;

         HWND top_state = 0; // None
         const bool make_top = false; // Make the window at the top now
         // TODO: this makes the game always on top, and "minimize" when alt tabbing out of it, do we really want it? Probably not? It doesn't seem to work in Burnout Paradise anyway
         const bool make_top_most = false; // Force always on top, so it renders on top of the start bar etc
         if (make_top_most)
            top_state = HWND_TOPMOST;
         else if (make_top)
            top_state = HWND_TOP;
         const bool z_order_changed = make_top_most || make_top;

         SetWindowPos(
            game_window,
            top_state,
            x, y,
            resize_to_screen ? screen_width : 0, resize_to_screen ? screen_height : 0,                                         // Preserve the size by default
            (resize_to_screen ? 0x0 : SWP_NOSIZE) | (z_order_changed ? 0x0 : SWP_NOZORDER) | SWP_NOACTIVATE
            | SWP_FRAMECHANGED  // Apply style changes
            | SWP_SHOWWINDOW // Show it just in case it was hidden (mmm, not sure why...)
            | SWP_ASYNCWINDOWPOS // This prevents this set from waiting on the window thread to have executed our request. As long as we are not changing the size, the game swapchain will stay the same and thus we should be able to do it freely, without hanging this thread
         );
      }
   }

   bool OnSetFullscreenState(reshade::api::swapchain* swapchain, bool fullscreen, void* hmonitor)
   {
      SKIP_UNSUPPORTED_DEVICE_API(swapchain->get_device()->get_api(), false);

      // Center the window in case it stayed where it was
      if (prevent_fullscreen_state && (fullscreen || force_borderless)) // TODO: test with Mafia 3 and BS2 if actually needed
      {
         CenterWindowAndRemoveBorders();
      }
      // TODO: keep track of FS state and send warnings to users in games where it doesn't work
      return prevent_fullscreen_state;
   }

   void OnInitCommandList(reshade::api::command_list* cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      CommandListData& cmd_list_data = *cmd_list->create_private_data<CommandListData>();

      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = reinterpret_cast<ID3D11DeviceChild*>(cmd_list->get_native()); // This could either be a "ID3D11CommandList" or a "ID3D11DeviceContext"
      HRESULT hr = device_child->QueryInterface(&native_device_context);
      if (SUCCEEDED(hr) && native_device_context)
      {
         DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
         if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE)
         {
            ASSERT_ONCE(!device_data.primary_command_list_data); // There should never be more than one of these?
            ASSERT_ONCE(device_data.primary_command_list == reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()));
            device_data.primary_command_list_data = &cmd_list_data;
            cmd_list_data.is_primary = true;
         }

#if DEVELOPMENT
         // Pre-allocate to make it faster (whether it's primary or not)
         // Check for the memory as this can crash in x86 games that had little consecutive memory left (e.g. Burnout Paradise Remastered)
#ifdef WIN32
         constexpr size_t reserve_size = 2500;
#else
         constexpr size_t reserve_size = 5000;
#endif
         if (System::CanAllocate(sizeof(decltype(cmd_list_data.trace_draw_calls_data)::value_type) * reserve_size * 2)) // Check for double the space, just to be sure
            cmd_list_data.trace_draw_calls_data.reserve(reserve_size);
#endif
      }
      else
      {
         com_ptr<ID3D11CommandList> native_cmd_list;
         hr = device_child->QueryInterface(&native_cmd_list);
         if (SUCCEEDED(hr) && native_cmd_list)
         {
            // No need to set "cmd_list_data.is_primary" here, this is a temporary "ID3D11CommandList" object to transfer a list of commands from a deferred to the immediate device context
         }
         else
         {
            ASSERT_ONCE(false);
         }
      }
   }

   void OnDestroyCommandList(reshade::api::command_list* cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      cmd_list->destroy_private_data<CommandListData>();
   }

#pragma optimize("t", on) // Temporarily override optimization, this function is too slow in debug otherwise (comment this out if ever needed)
#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
   bool OnCreatePipeline(
      reshade::api::device* device,
      reshade::api::pipeline_layout layout,
      uint32_t subobject_count,
      const reshade::api::pipeline_subobject* subobjects)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api(), false);

      ASSERT_ONCE(!last_live_patched_original_shader_code); // This should never happen, it means our conditions don't match, or well, that shader creation failed, which is possible, even in the original game code
      last_live_patched_original_shader_code = nullptr;
      last_live_patched_original_shader_size = 0;
      last_live_patched_shader_hash = -1;

      bool any_edited = false;

      DeviceData& device_data = *device->get_private_data<DeviceData>();

      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            switch (subobject.type)
            {
#if GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::geometry_shader:
#endif // GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::vertex_shader:
            case reshade::api::pipeline_subobject_type::compute_shader:
            case reshade::api::pipeline_subobject_type::pixel_shader:
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);

               ASSERT_ONCE(original_shader_desc->code_size > 0);
               if (original_shader_desc->code_size == 0) break;

               // These should never happen
               {
                  const DXBCHeader* original_shader_header = (const DXBCHeader*)original_shader_desc->code;
                  if (memcmp(original_shader_header->format_name, "DXBC", 4) != 0) break;
                  if (original_shader_header->file_size != original_shader_desc->code_size) break;
               }

               // These should all be updated at the same time.
               // They represent the version of the shader we are currently iterating upon, whether it's stripped of debug data, or live patched etc.
               struct CurrentShaderData
               {
                  const void* code = nullptr;
                  size_t code_size = 0;
                  DXBCHeader* header = nullptr;

                  void Set(const void* _code, size_t _code_size)
                  {
                     code = _code;
                     code_size = _code_size;
                     header = (DXBCHeader*)code;
                  }
               };
               CurrentShaderData current_shader_data = {};
               current_shader_data.Set(original_shader_desc->code, original_shader_desc->code_size);

               // Optional
               std::unique_ptr<std::byte[]> patched_shader_code; // Make sure to always update "current_shader_data" to the latest version of this

               uint64_t shader_luma_hash = -1;

               com_ptr<ID3DBlob> stripped_code_blob;
               // Strip debug data from the shader to unify them in games where different permutations of shaders are actually deep down identical.
               // Heavy Rain is a notorious example of this, having almost every single object in the game have its own unique shader, with the location and object name hardcoded in samplers and textures etc.
               // Given we need to fix them from issues they have when using FLOAT render targets (as opposed to the original UNORM) (e.g. NaNs and negative alpha etc),
               // we unify them, so we can create a few fixes hlsl for all, that gets automatically loaded.
               //
               // The native MD5 shader hash is already updated the by D3D strip data function.
               // TODO: instead of doing this, we could simply calculate the hash of the shader byte code section and replace them by that?
               if (strip_original_shaders_debug_data && SUCCEEDED(d3d_stripShader(current_shader_data.code, current_shader_data.code_size, D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS /*| D3DCOMPILER_STRIP_PRIVATE_DATA*/ | D3DCOMPILER_STRIP_ROOT_SIGNATURE, &stripped_code_blob)))
               {
                  // We still need to allocate this in our persistent storage, and can't directly use "stripped_code_blob" (unless we redesigned the code)
                  const size_t patched_shader_code_size = stripped_code_blob->GetBufferSize();
                  patched_shader_code = std::make_unique<std::byte[]>(patched_shader_code_size);
                  std::memcpy(patched_shader_code.get(), stripped_code_blob->GetBufferPointer(), patched_shader_code_size);
                  stripped_code_blob.reset();

                  current_shader_data.Set(patched_shader_code.get(), patched_shader_code_size);

                  // Recalculate the hash after stripping, we don't want to use the "native"/original one anymore,
                  // given that one of the reasons we strip data is to try and unify shaders that would otherwise be identical
                  shader_luma_hash = Shader::BinToHash(static_cast<const uint8_t*>(current_shader_data.code), current_shader_data.code_size);
               }

               Hash::MD5::Digest* pre_patched_code_hash = nullptr;
               bool needs_new_md5_hash = false;
               bool found_code_chunk = false;

               for (uint32_t i = 0; i < current_shader_data.header->chunk_count; ++i)
               {
                  DXBCChunk* chunk = reinterpret_cast<DXBCChunk*>((uint8_t*)current_shader_data.code + current_shader_data.header->chunk_offsets[i]);

                  // These are the chunks with the actual shaders byte code:
                  // SHEX is SM5
                  // SHDR is SM4
                  // DXIL is SM6 (DX12)
                  // 
                  // http://timjones.io/blog/archive/2015/09/02/parsing-direct3d-shader-bytecode#shdr-chunk
                  // Usually this is the last chunk.
                  if (memcmp(&chunk->type_name, "SHEX", 4) == 0 || memcmp(&chunk->type_name, "SHDR", 4) == 0)
                  {
                     DXBCByteCodeChunk* chunk_byte_code = reinterpret_cast<DXBCByteCodeChunk*>(chunk->chunk_data);

                     // We need to calculate this before any live patching, given that luma shaders are replaced based on the original shader hash, not the live patched one,
                     // it's used right to find if we previously created a shader by the same hash and skip re-patching it.
                     if (shader_luma_hash == -1)
                     {
                        shader_luma_hash = Shader::BinToHash(static_cast<const uint8_t*>(current_shader_data.code), current_shader_data.code_size);
                     }

                     // If the same shader (hash) was previous patched, just fish the previously cached data
                     constexpr bool always_repatch_live_patched_shaders = false; // TODO: expose for dev modes in case we wanted to dynamically change the patching logic by level (however it makes little sense as we are usually not in control of how games load shaders)
                     if (!always_repatch_live_patched_shaders)
                     {
                        const std::shared_lock lock_device(device_data.mutex);
                        auto modified_shader_byte_code = device_data.modified_shaders_byte_code.find(shader_luma_hash);
                        if (modified_shader_byte_code != device_data.modified_shaders_byte_code.end())
                        {
                           current_shader_data.Set(std::get<0>(modified_shader_byte_code->second).get(), std::get<1>(modified_shader_byte_code->second));
                           pre_patched_code_hash = &std::get<2>(modified_shader_byte_code->second);
                           needs_new_md5_hash = true;
                           break;
                        }
                     }

                     ASSERT_ONCE(!found_code_chunk); // Why does a shader byte code have two chunks that we can replace? It's supported but weird
                     found_code_chunk = true;

#if DEVELOPMENT // Verify that the size is right
                     if (i < (current_shader_data.header->chunk_count - 1))
                     {
                        uint32_t* next_chunk = (uint32_t*)((uint8_t*)current_shader_data.code + current_shader_data.header->chunk_offsets[i + 1]);
                        uint32_t* guessed_next_chunk = (uint32_t*)((uint8_t*)current_shader_data.code + current_shader_data.header->chunk_offsets[i] + chunk->chunk_size + 8);
                        ASSERT_ONCE(next_chunk == guessed_next_chunk);
                     }
#endif

                     // Unused, but good to be checked.
                     // Should always match with ReShade subobject type, unless the application passed in a binary of one type in the shader creation func of another type
                     DXBCProgramType reshade_program_type = DXBCProgramType(-1);
                     switch (subobject.type)
                     {
                     case reshade::api::pipeline_subobject_type::geometry_shader: { reshade_program_type = DXBCProgramType::GeometryShader; break; }
                     case reshade::api::pipeline_subobject_type::vertex_shader: { reshade_program_type = DXBCProgramType::VertexShader; break; }
                     case reshade::api::pipeline_subobject_type::compute_shader: { reshade_program_type = DXBCProgramType::ComputeShader; break; }
                     case reshade::api::pipeline_subobject_type::pixel_shader: { reshade_program_type = DXBCProgramType::PixelShader; break; }
                     }
                     ASSERT_ONCE(reshade_program_type == chunk_byte_code->program_type); // We should probably stop in this case

                     uint32_t prev_size = 8;
                     uint32_t byte_code_size = (chunk_byte_code->chunk_size_dword * sizeof(uint32_t)) - prev_size; // The size is stored in "DWORD" elements and counted the size and program version/type in its count, so we remove them
#if DEVELOPMENT
                     // Verify that the size is right
                     ASSERT_ONCE(chunk->chunk_size == (byte_code_size + prev_size)); // Add back the two DWORD that we excluded from the size

                     uint8_t version_major = uint8_t((chunk_byte_code->version_major_and_minor >> 4) & 0xF); // E.g. SM4/5
                     uint8_t version_minor = uint8_t(chunk_byte_code->version_major_and_minor & 0xF);
                     ASSERT_ONCE(version_major >= 4 && version_major <= 6);
#endif
                     const std::byte* byte_code = reinterpret_cast<const std::byte*>(chunk_byte_code->byte_code);

                     size_t new_byte_code_size = byte_code_size;
                     if (std::unique_ptr<std::byte[]> patched_byte_code = game->ModifyShaderByteCode(byte_code, new_byte_code_size, subobject.type, shader_luma_hash, static_cast<const std::byte*>(current_shader_data.code), current_shader_data.code_size))
                     {
                        const size_t byte_code_offset = byte_code - (std::byte*)current_shader_data.code;
                        const bool byte_code_size_changed = new_byte_code_size != byte_code_size;
                        const int32_t byte_code_size_diff = int32_t(new_byte_code_size) - int32_t(byte_code_size); // int32 should always be enough
                        if (!patched_shader_code || byte_code_size_changed)
                        {
                           // Allocate a new instance to patch the shader
                           {
                              size_t new_patched_shader_code_size = current_shader_data.code_size + byte_code_size_diff;
                              std::unique_ptr<std::byte[]> new_patched_shader_code = std::make_unique<std::byte[]>(new_patched_shader_code_size); // "current_shader_data" might be pointing to "patched_shader_code" so make a new temporary one

                              // Copy everything until the byte code, the actual byte code "body" is copied below
                              std::memcpy(new_patched_shader_code.get(), current_shader_data.code, byte_code - (std::byte*)current_shader_data.code);

                              // Copy anything after this chunk
                              uint32_t old_tail_offset = current_shader_data.header->chunk_offsets[i] + chunk->chunk_size;
                              uint32_t new_tail_offset = old_tail_offset + byte_code_size_diff;
                              uint32_t tail_size = current_shader_data.code_size - old_tail_offset; // Should be 0 if this is the last chunk? Is there any "padding" data after?
                              std::memcpy(new_patched_shader_code.get() + new_tail_offset, (std::byte*)current_shader_data.code + old_tail_offset, tail_size);

                              patched_shader_code = std::move(new_patched_shader_code);
                              current_shader_data.Set(patched_shader_code.get(), new_patched_shader_code_size);

                              // Update the chunks to point to the new data
                              chunk = reinterpret_cast<DXBCChunk*>((uint8_t*)current_shader_data.code + current_shader_data.header->chunk_offsets[i]);
                              chunk_byte_code = reinterpret_cast<DXBCByteCodeChunk*>(chunk->chunk_data);
                           }

                           // Check if size changed, if it did we have to reconstruct the shader headers and chunk index as well as the replace with a new shader description for dx11
                           if (byte_code_size_changed)
                           {
                              // Update sizes
                              current_shader_data.header->file_size += byte_code_size_diff;
                              chunk->chunk_size += byte_code_size_diff; // Chunk size
                              ASSERT_ONCE(byte_code_size_diff % sizeof(uint32_t) == 0); // Make sure it's a multiple of DWORD (4 bytes), it's probably mandatory for it to be
                              chunk_byte_code->chunk_size_dword += byte_code_size_diff / sizeof(uint32_t); // Byte code size in DWORD (4 bytes)
                              // Update chunk offsets of all chunks after this one
                              for (uint32_t j = i + 1; j < current_shader_data.header->chunk_count; ++j)
                              {
                                 current_shader_data.header->chunk_offsets[j] += byte_code_size_diff;
                              }
                           }
                        }

                        // Calculate the offset relative to original code and copy the replaced byte code in it
                        // (we don't overwrite the original shader code because it's const, ReShade or the game's memory might expect it to not change,
                        // and plus it'd break other mods that load before us that replace shaders by hash on creation)
                        std::memcpy(patched_shader_code.get() + byte_code_offset, patched_byte_code.get(), byte_code_size + byte_code_size_diff);

                        needs_new_md5_hash = true;
                     }
                  }
               }

               if (current_shader_data.code != original_shader_desc->code)
               {
                  ASSERT_ONCE_MSG((subobject_count == 1 || subobject_count == 2) && subobject.count == 1 && !last_live_patched_original_shader_code, "This behaviour is hardcoded to work with DX9-11, with one object (shader) per pipeline"); // input layouts have two subobjects (input layout and vertex shader)
                  ASSERT_ONCE(!patched_shader_code.get() || patched_shader_code.get() == current_shader_data.code);

                  if (needs_new_md5_hash)
                  {
                     // Recalculate and set the hash, otherwise the shader might fail to load (or be used later on anyway)
                     // Official implementation is here: https://github.com/doitsujin/dxbc-spirv/blob/32866c0d0a0236b93681d25405e57a3e9d6868d3/dxbc/dxbc_container.cpp#L11
                     // This implementation should match it 100%
                     Hash::MD5::Digest md5_digest = pre_patched_code_hash ? *pre_patched_code_hash : Shader::CalcDXBCHash(current_shader_data.code, current_shader_data.code_size);
                     std::memcpy(current_shader_data.header->hash, &md5_digest.data, DXBCHeader::hash_size);
                  }

#if DEVELOPMENT // Slow, but good for prevention
                  constexpr bool verify_live_patched_shaders = true; // TODO: set false
                  if (verify_live_patched_shaders)
                  {
                     ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
                     HRESULT hr = S_OK;
                     if (subobject.type == reshade::api::pipeline_subobject_type::pixel_shader)
                     {
                        com_ptr<ID3D11PixelShader> shader_object;
                        hr = native_device->CreatePixelShader(current_shader_data.code, current_shader_data.code_size, nullptr, &shader_object);
                     }
                     else if (subobject.type == reshade::api::pipeline_subobject_type::vertex_shader)
                     {
                        com_ptr<ID3D11VertexShader> shader_object;
                        hr = native_device->CreateVertexShader(current_shader_data.code, current_shader_data.code_size, nullptr, &shader_object);
                     }
                     if (FAILED(hr))
                     {
                        assert(false);
                        break; // Skip replacing the shader if it can't be compiled, the game would likely crash otherwise!
                     }
                  }
#endif

                  any_edited = true;

                  // Store the original shader so it can later be accessed in ReShade's pipeline init callback (it'd still be valid as it's an external ptr, unless ReShade changed its implementation)
                  last_live_patched_original_shader_code = original_shader_desc->code;
                  last_live_patched_original_shader_size = original_shader_desc->code_size;
                  last_live_patched_shader_hash = shader_luma_hash;

                  // Update ReShade pointers to code and its size, so they point at the new code (that is kept alive by unique ptrs).
                  // ReShade doesn't handle the memory of the pointer at all, it simply passes it to the native function.
                  auto* shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
                  shader_desc->code = current_shader_data.code;
                  shader_desc->code_size = current_shader_data.code_size;

                  // Cache the newly patched
                  if (patched_shader_code.get())
                  {
                     const std::unique_lock lock_device(device_data.mutex);

                     // Allocate in chunks to avoid constant allocations
                     constexpr size_t reserve_size = 32; // Conservative value
                     size_t size_distance = device_data.modified_shaders_byte_code.size() % reserve_size;
                     if (size_distance == 0)
                     {
                        device_data.modified_shaders_byte_code.reserve(device_data.modified_shaders_byte_code.size() + reserve_size);
                     }

                     auto& modified_shader_byte_code = device_data.modified_shaders_byte_code[uint32_t(shader_luma_hash)];
                     std::get<0>(modified_shader_byte_code) = std::move(patched_shader_code); // Equal to "current_shader_data.code"
                     std::get<1>(modified_shader_byte_code) = current_shader_data.code_size;
                     std::get<2>(modified_shader_byte_code) = *reinterpret_cast<const Hash::MD5::Digest*>(current_shader_data.header->hash);
                  }
               }

               break;
            }
            }
         }
      }

      return any_edited;
   }
#endif // ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS

   void OnInitPipeline(
      reshade::api::device* device,
      reshade::api::pipeline_layout layout,
      uint32_t subobject_count,
      const reshade::api::pipeline_subobject* subobjects,
      reshade::api::pipeline pipeline)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      const void* live_patched_shader_code = nullptr;
      size_t live_patched_shader_size = 0;
      uint64_t precalculated_shader_hash = -1;

      // In DX11 each pipeline should only have one subobject (e.g. a shader)
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         switch (subobject.type)
         {
#if DEVELOPMENT
         case reshade::api::pipeline_subobject_type::input_layout:
         {
            for (uint32_t j = 0; j < subobject.count; ++j)
            {
               reshade::api::input_element* desc = reinterpret_cast<reshade::api::input_element*>(subobject.data) + j;
               D3D11_INPUT_ELEMENT_DESC internal_desc;
               internal_desc.SemanticName = desc->semantic;
               internal_desc.SemanticIndex = desc->semantic_index;
               internal_desc.Format = DXGI_FORMAT(desc->format);
               internal_desc.InputSlot = desc->buffer_binding;
               internal_desc.AlignedByteOffset = desc->offset;
               internal_desc.InputSlotClass = desc->instance_step_rate > 0 ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
               internal_desc.InstanceDataStepRate = desc->instance_step_rate;
               if (desc->semantic == nullptr)
               {
                  internal_desc.SemanticName = "TEXCOORD";
                  internal_desc.SemanticIndex = desc->location;
               }
               input_layouts_descs[reinterpret_cast<ID3D11InputLayout*>(pipeline.handle)].push_back(internal_desc);
            }
            return;
         }
#endif
#if GEOMETRY_SHADER_SUPPORT // Simply skipping cloning geom shaders pipelines is enough to stop the whole functionality (and the only place that is performance relevant)
         case reshade::api::pipeline_subobject_type::geometry_shader:
#endif // GEOMETRY_SHADER_SUPPORT
         case reshade::api::pipeline_subobject_type::vertex_shader:
         case reshade::api::pipeline_subobject_type::compute_shader:
         case reshade::api::pipeline_subobject_type::pixel_shader:
         {
#if !DX12
            ASSERT_ONCE(subobject_count == 1 || subobject_count == 2); // input layouts have two subobjects (input layout and vertex shader)
#endif
            ASSERT_ONCE(subobject.count == 1);
#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
            if (last_live_patched_shader_hash != -1)
            {
               precalculated_shader_hash = last_live_patched_shader_hash;
               last_live_patched_shader_hash = -1;
            }
            // Restore the original shader at this point in ReShades pipeline data, so we don't end up analyzing the hash of the live patched shader,
            // or dumping it, it should only apply to rendering really without influencing the rest of the application.
            if (last_live_patched_original_shader_code)
            {
               auto* shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);

               // Cache the previous information for development debugging reasons
               // DX11 hardcoded behaviour, assuming one shader per pipeline
               live_patched_shader_code = shader_desc->code;
               live_patched_shader_size = shader_desc->code_size;

#if 1 // This is optional but convenient, see "original_shader_desc" below
               shader_desc->code = last_live_patched_original_shader_code;
               shader_desc->code_size = last_live_patched_original_shader_size;
#endif

               last_live_patched_original_shader_code = nullptr;
               last_live_patched_original_shader_size = 0;

               // At this point, if we wanted, we could always destroy the shaders ptr we temporarily allocated
               // to pass into ReShade (the shader creation DX function is split into two functions by it, so
               // we have to resort to inventive ways). Note that if the shader creation failed,
               // we wouldn't be able to clear it until the next function run, but barely a problem (it shouldn't happen).
               // On x86 processes, space is limited so this might help.
               // On the other end, if games constantly re-created the same shaders (e.g. CryEngine every time a level is reloaded)
               // this could lead to additional stutters.
               constexpr bool always_clear_live_patched_shaders = false; // TODO: expose
               if (always_clear_live_patched_shaders)
               {
                  DeviceData& device_data = *device->get_private_data<DeviceData>();
                  const std::unique_lock lock_device(device_data.mutex);
                  device_data.modified_shaders_byte_code.clear();
               }
            }
#endif
            break;
         }
         default:
         {
            return; // Nothing to do here, we don't want to clone the pipeline
         }
         }
      }

      // Clone the pipeline so we can potentially customize it with our custom shaders etc,
      // and live swap it when the game sets it.
      // If we reached here, the pipeline has at least one shader object that we might replace (either now, or later, given that we don't know yet).
      reshade::api::pipeline_subobject* subobjects_cache = Shader::ClonePipelineSubobjects(subobject_count, subobjects);

      auto* cached_pipeline = new CachedPipeline{
          pipeline,
          device,
          layout,
          subobjects_cache
#if DX12
          , subobject_count
#endif
      };

      bool found_replaceable_shader = false;
      bool found_custom_shader_file = false;

      DeviceData& device_data = *device->get_private_data<DeviceData>();

      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            switch (subobject.type)
            {
#if GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::geometry_shader:
#endif // GEOMETRY_SHADER_SUPPORT
            case reshade::api::pipeline_subobject_type::vertex_shader:
            case reshade::api::pipeline_subobject_type::compute_shader:
            case reshade::api::pipeline_subobject_type::pixel_shader:
            {
               auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects_cache[i].data);
               ASSERT_ONCE(original_shader_desc->code_size > 0);
               if (original_shader_desc->code_size == 0) break;
               found_replaceable_shader = true;

               // TODO: this isn't possible in DX11 but we should check if the shader has any private data and clone that too?

               // TODO: when out of "DEVELOPMENT" or "TEST" builds, we could early out before cloning the pipeline based on whether "custom_shaders_cache.contains(shader_hash)" is true. That'd avoid some stutters on shader loading. Note that below we might modify even shaders that we don't replace so consider that too.
               // TODO: use the native DX hash stored at the beginning of shaders binaries? It might not always be reliable though. It's too late anyway now, all of our hashes are in content.
               uint32_t shader_hash = (precalculated_shader_hash != -1) ? uint32_t(precalculated_shader_hash) : Shader::BinToHash(static_cast<const uint8_t*>(original_shader_desc->code), original_shader_desc->code_size);

#if ALLOW_SHADERS_DUMPING || DEVELOPMENT
               {
                  const std::unique_lock lock_dumping(s_mutex_dumping); // Note: this isn't optimized, we should only lock it white reading/writing the dump data, not disassembling (?)

                  constexpr bool keep_duplicate_cached_shaders = true;

                  CachedShader* cached_shader = nullptr;
                  bool found_reflections = false;

                  // Optionally delete any previous shader with the same hash (unlikely to happen, as games usually compile the same shader binary once, but safer nonetheless, especially because sometimes two permutations of the same shader might have the same result)
                  if (auto previous_shader_pair = shader_cache.find(shader_hash); previous_shader_pair != shader_cache.end() && previous_shader_pair->second != nullptr)
                  {
                     auto& previous_shader = previous_shader_pair->second;
                     // Make sure that two shaders have the same hash, their code size also matches (theoretically we could check even more, but the chances hashes overlapping is extremely small)
                     assert(previous_shader->size == original_shader_desc->code_size);
                     if (!keep_duplicate_cached_shaders)
                     {
#if DEVELOPMENT
                        shader_cache_count--;
#endif // DEVELOPMENT
                        delete previous_shader; // This should already de-allocate the internally allocated data
                     }
                     else
                     {
                        cached_shader = previous_shader;
                        found_reflections = true; // We already did the reflections procedure (whether it previously failed or not ("cached_shader->found_reflections"), given the result wouldn't change now)
                     }
                  }

                  if (!cached_shader)
                  {
                     cached_shader = new CachedShader{ malloc(original_shader_desc->code_size), original_shader_desc->code_size, subobject.type };
                     std::memcpy(cached_shader->data, original_shader_desc->code, cached_shader->size);

#if DEVELOPMENT
                     shader_cache_count++;
                     cached_shader->live_patched_data = live_patched_shader_code;
                     cached_shader->live_patched_size = live_patched_shader_size;
#endif // DEVELOPMENT
                     shader_cache[shader_hash] = cached_shader;
#if ALLOW_SHADERS_DUMPING
                     shaders_to_dump.emplace(shader_hash);

#if DEVELOPMENT // Try to load reflections on disk if we previously stored them in the meta file
                     const std::string hash_str = Shader::Hash_NumToStr(shader_hash, true);

#if 0 // Not needed anymore
                     // We still don't know the shader model here, so search by hash and *
                     auto FindMetaFile = [&hash_str]() -> std::optional<std::filesystem::path>
                        {
                           if (!std::filesystem::exists(shaders_dump_path) || !std::filesystem::is_directory(shaders_dump_path))
                              return std::nullopt;
                           for (auto& entry : std::filesystem::directory_iterator(shaders_dump_path))
                           {
                              if (!entry.is_regular_file())
                                 continue;
                              std::filesystem::path file = entry.path();
                              if (file.extension() != ".meta")
                                 continue;
                              std::string stem = file.stem().string(); // Everything before the extension
                              if (stem.rfind(hash_str, 0) == 0) // Starts with hash
                                 return file; // First match
                           }
                           return std::nullopt;
                        };

                     const auto meta_file_path_opt = FindMetaFile();
                     if (meta_file_path_opt.has_value())
                     {
                        const auto& meta_file_path = meta_file_path_opt.value();
#else
                     const auto meta_file_path_it = dumped_shaders_meta_paths.find(shader_hash);
                     if (meta_file_path_it != dumped_shaders_meta_paths.end())
                     {
                        const auto& meta_file_path = meta_file_path_it->second;
#endif
                        try
                        {
                           std::ifstream ifs(meta_file_path, std::ios::binary);
                           if (!ifs) throw std::runtime_error("");

                           uint8_t found_version = 1;
                           ifs.read(reinterpret_cast<char*>(&found_version), sizeof(found_version));

                           bool valid_version_found = true;

                           if (found_version != Shader::meta_version)
                           {
                              // Add version handling here (or below, by branching), if necessary
                              valid_version_found = false;
                              dumped_shaders_meta_paths.erase(meta_file_path_it); // Make sure it's dumped again with updates, otherwise we'd be stuck forever with the old version
                           }

                           if (valid_version_found)
                           {
                              uint32_t type_and_version_size = 0;
                              ifs.read(reinterpret_cast<char*>(&type_and_version_size), sizeof(type_and_version_size));
                              if (type_and_version_size > 0)
                              {
                                 cached_shader->type_and_version.resize(type_and_version_size);
                                 ifs.read(cached_shader->type_and_version.data(), type_and_version_size);
                              }

                              auto ReadBoolArray = [&](bool* arr, size_t count)
                                 {
                                    for (size_t i = 0; i < count; ++i)
                                    {
                                       uint8_t val = 0;
                                       ifs.read(reinterpret_cast<char*>(&val), sizeof(val));
                                       arr[i] = (val != 0);
                                    }
                                 };
                              ReadBoolArray(cached_shader->cbs, std::size(cached_shader->cbs));
                              ReadBoolArray(cached_shader->samplers, std::size(cached_shader->samplers));
                              ReadBoolArray(cached_shader->srvs, std::size(cached_shader->srvs));
                              ReadBoolArray(cached_shader->rtvs, std::size(cached_shader->rtvs));
                              ReadBoolArray(cached_shader->uavs, std::size(cached_shader->uavs));

                              cached_shader->found_reflections = true;
                              found_reflections = cached_shader->found_reflections;
                           }
                        }
                        catch (const std::exception& e)
                        {
                        }
                     }
#endif // DEVELOPMENT
#endif // ALLOW_SHADERS_DUMPING
                  }

                  // Try with native DX11 reflections first, they are much faster than disassembly
                  if (!found_reflections)
                  {
                     bool skip_reflections = false;
                     HRESULT hr;
#if 0
                     // Optional check to avoid failure cases, it seems to be useless/redundant
                     com_ptr<ID3DBlob> reflections_blob;
                     hr = d3d_getBlobPart(cached_shader->data, cached_shader->size, D3D_BLOB_INPUT_SIGNATURE_BLOB, 0, &reflections_blob);
                     if (FAILED(hr))
                     {
                        skip_reflections = true;
                     }
#endif

                     com_ptr<ID3D11ShaderReflection> shader_reflector;
                     hr = Shader::d3d_reflect(cached_shader->data, cached_shader->size, IID_ID3D11ShaderReflection, (void**)&shader_reflector);
                     if (!skip_reflections && SUCCEEDED(hr))
                     {
                        D3D11_SHADER_DESC shader_desc;
                        hr = shader_reflector->GetDesc(&shader_desc);
                        if (SUCCEEDED(hr))
                        {
                           // Determine shader type prefix
                           std::string type_prefix = "xs";
                           D3D11_SHADER_VERSION_TYPE type = (D3D11_SHADER_VERSION_TYPE)D3D11_SHVER_GET_TYPE(shader_desc.Version);
                           // The asserts might trigger if devs tried to bind the wrong shader type in a slot? Probably impossible.
                           switch (cached_shader->type)
                           {
                           case reshade::api::pipeline_subobject_type::vertex_shader:   type_prefix = "vs"; assert(type == D3D11_SHVER_VERTEX_SHADER); break;
                           case reshade::api::pipeline_subobject_type::geometry_shader: type_prefix = "gs"; assert(type == D3D11_SHVER_GEOMETRY_SHADER); break;
                           case reshade::api::pipeline_subobject_type::pixel_shader:    type_prefix = "ps"; assert(type == D3D11_SHVER_PIXEL_SHADER); break;
                           case reshade::api::pipeline_subobject_type::compute_shader:  type_prefix = "cs"; assert(type == D3D11_SHVER_COMPUTE_SHADER); break;
                           }

                           // Version: high byte = minor, low byte = major
                           UINT major_version = D3D11_SHVER_GET_MAJOR(shader_desc.Version);
                           UINT minor_version = D3D11_SHVER_GET_MINOR(shader_desc.Version);

                           // e.g. "ps_5_0"
                           ASSERT_ONCE(major_version >= 4 && major_version <= 6); // Unexpected version (maybe the data is invalid?)
                           cached_shader->type_and_version = type_prefix + "_" + std::to_string(major_version) + "_" + std::to_string(minor_version);

#if DEVELOPMENT
                           bool found_any_rtvs = false;
                           bool found_any_other_bindings = false;

#if 0 // This doesn't seem to be reliable, the version below works better
                           // CBs
                           for (UINT i = 0; i < shader_desc.ConstantBuffers; ++i)
                           {
                              ID3D11ShaderReflectionConstantBuffer* cbuffer = shader_reflector->GetConstantBufferByIndex(i);
                              if (!cbuffer)
                                 continue;

                              // Doesn't mean much
                              D3D11_SHADER_BUFFER_DESC cb_desc;
                              ASSERT_ONCE(SUCCEEDED(cbuffer->GetDesc(&cb_desc)));

                              cached_shader->cbs[i] = true;
                           }
#endif

                           // RTVs
                           for (UINT i = 0; i < shader_desc.OutputParameters; ++i)
                           {
                              D3D11_SIGNATURE_PARAMETER_DESC shader_output_desc;
                              hr = shader_reflector->GetOutputParameterDesc(i, &shader_output_desc);
                              if (SUCCEEDED(hr) && strcmp(shader_output_desc.SemanticName, "SV_Target") == 0)
                              {
                                 ASSERT_ONCE(shader_output_desc.SemanticIndex == shader_output_desc.Register && shader_output_desc.SemanticIndex < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
                                 cached_shader->rtvs[shader_output_desc.SemanticIndex] = true;
                                 found_any_rtvs = true;
                              }
                           }

                           // SRVs and UAVs and samplers (it works for compute shaders too)
                           for (UINT i = 0; i < shader_desc.BoundResources; ++i)
                           {
                              D3D11_SHADER_INPUT_BIND_DESC bind_desc;
                              hr = shader_reflector->GetResourceBindingDesc(i, &bind_desc);
                              if (SUCCEEDED(hr))
                              {
                                 for (UINT j = 0; j < bind_desc.BindCount; ++j)
                                 {
                                    if (bind_desc.Type == D3D_SIT_TEXTURE)
                                    {
                                       ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
                                       cached_shader->srvs[bind_desc.BindPoint + j] = true;
                                       found_any_other_bindings = true;
                                    }
                                    else if (bind_desc.Type == D3D_SIT_UAV_RWTYPED)
                                    {
                                       ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_1_UAV_SLOT_COUNT);
                                       cached_shader->uavs[bind_desc.BindPoint + j] = true;
                                       found_any_other_bindings = true;
                                    }
                                    // Verify that the cbuffer index and bind points match (they should always do)
                                    else if (bind_desc.Type == D3D_SIT_CBUFFER)
                                    {
                                       ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
                                       //ASSERT_ONCE(cached_shader->cbs[bind_desc.BindPoint + j]);
                                       cached_shader->cbs[bind_desc.BindPoint + j] = true;
                                    }
                                    // These are always valid, even when the other ones aren't, so don't set "found_any_other_bindings" to true
                                    else if (bind_desc.Type == D3D_SIT_SAMPLER)
                                    {
                                       ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
                                       cached_shader->samplers[bind_desc.BindPoint + j] = true;
                                    }
                                 }
                              }
                           }

                           // TODO: does this issue still happen now that we added more early out checks?
                           // Note: sometimes the "GetResourceBindingDesc" above fails (it gives success, but then returns no bindings)... It might be related to "D3DCOMPILER_STRIP_REFLECTION_DATA",
                           // but we don't know for sure nor we can retrieve that flag, we wrote some heuristics to guess when it failed, then we pretend all bound resources are "valid" (to show them in the debug data).
                           if (found_any_other_bindings || (shader_desc.FloatInstructionCount != 0 || shader_desc.IntInstructionCount != 0 || shader_desc.UintInstructionCount != 0))
                           {
                              // In Dishonored 2, RTVs fail to be found from the DX11 reflections, but SRVs/UAVs work (we could check for depth too but whatever, these are too unreliable)
                              if (found_any_rtvs || cached_shader->type != reshade::api::pipeline_subobject_type::pixel_shader)
                                 found_reflections = true;
                           }
#endif // DEVELOPMENT
                        }
                     }
                  }

                  // Fall back on disassembly to find the information. Note that this is extremely slow.
                  // TODO: allow delaying the disassembly until "Capture" is first clicked?
#if DEVELOPMENT
                  if (!found_reflections || cached_shader->type_and_version.empty())
#else // !DEVELOPMENT
                  if (cached_shader->type_and_version.empty())
#endif // DEVELOPMENT
                  {
                     assert(cached_shader); // Shouldn't ever happen
                     bool valid_disasm = false;
                     if (cached_shader->disasm.empty())
                     {
                        auto disasm_code = Shader::DisassembleShader(cached_shader->data, cached_shader->size);
                        if (disasm_code.has_value())
                        {
                           cached_shader->disasm.assign(disasm_code.value());
                           valid_disasm = true;
                        }
                        else
                        {
                           ASSERT_ONCE(false); // Shouldn't happen?
                           cached_shader->disasm.assign("DISASSEMBLY FAILED");
                        }
                     }

                     if (valid_disasm && cached_shader->type_and_version.empty())
                     {
                        if (cached_shader->type == reshade::api::pipeline_subobject_type::geometry_shader
                           || cached_shader->type == reshade::api::pipeline_subobject_type::vertex_shader
                           || cached_shader->type == reshade::api::pipeline_subobject_type::pixel_shader
                           || cached_shader->type == reshade::api::pipeline_subobject_type::compute_shader)
                        {
                           static const std::string template_shader_model_version_name = "x_x";

                           std::string_view template_shader_name = ShaderTypeToTarget(cached_shader->type);
                           for (char i = '0'; i <= '9'; i++)
                           {
                              std::string type_wildcard = std::string(template_shader_name) + i + '_';
                              const auto type_index = cached_shader->disasm.find(type_wildcard);
                              if (type_index != std::string::npos)
                              {
                                 cached_shader->type_and_version = cached_shader->disasm.substr(type_index, template_shader_name.length() + template_shader_model_version_name.length());
                                 break;
                              }
                           }
                        }
                     }

#if DEVELOPMENT // TODO: move this into a function that is re-usable later, and allow delaying this until we actually need it in the visualizer
                     if (valid_disasm && !found_reflections)
                     {
                        std::istringstream iss(cached_shader->disasm);
                        std::string line;

                        // Regex explanation:
                        // Capture 1 to 3 digits after the specified digit (e.g. t/u/o etc) at the end
                        const std::regex pattern_cbs(R"(dcl_constantbuffer.*[cC][bB]([0-9]{1,2}))");
                        const std::regex pattern_samplers(R"(dcl_sampler.*[sS]([0-9]{1,2}))");
                        const std::regex pattern_srv(R"(.*dcl_resource_texture.*[tT]([0-9]{1,3})$)");
                        const std::regex pattern_uav(R"(.*dcl_uav_.*[uU]([0-9]{1,2})$)"); // TODO: verify that all the UAV binding types have incremental numbers, or whether they grow in parallel
                        const std::regex pattern_rtv(R"(dcl_output.*[oO]([0-9]{1,1}))"); // Match up to 9 even if theoretically "D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT" is up to 7
                        const std::regex pattern_depth(R"(dcl_output_depth)");

                        bool matching_line = false;
                        while (std::getline(iss, line)) {
                           // Trim leading spaces
                           size_t first = line.find_first_not_of(" \t");
                           if (first != std::string::npos && (first + 1) < line.size())
                           {
                              // Stop if line starts with "0 ", as that's the first line that highlights the code beginning
                              if (line.at(first) == '0' && line.at(first + 1) == ' ')
                                 break;
                           }

                           bool prev_matching_line = matching_line;

                           bool cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
                           std::smatch match;
                           // Use search for CBs and RTVs etc because they have additional text after we found the number we are looking for
                           if (std::regex_search(line, match, pattern_cbs) && match.size() >= 2) {
                              int num = std::stoi(match[1].str());
                              if (num >= 0 && num < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) {
                                 cached_shader->cbs[num] = true;
                                 matching_line = true;
                              }
                           }
                           if (std::regex_search(line, match, pattern_samplers) && match.size() >= 2) {
                              int num = std::stoi(match[1].str());
                              if (num >= 0 && num < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
                                 cached_shader->samplers[num] = true;
                                 matching_line = true;
                              }
                           }
                           // Use match for SRVs and UAVs as they end with their letter and a number
                           if (std::regex_match(line, match, pattern_srv) && match.size() >= 2) {
                              int num = std::stoi(match[1].str());
                              if (num >= 0 && num < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) {
                                 cached_shader->srvs[num] = true;
                                 matching_line = true;
                              }
                           }
                           if ((cached_shader->type == reshade::api::pipeline_subobject_type::pixel_shader || cached_shader->type == reshade::api::pipeline_subobject_type::compute_shader)
                              && std::regex_match(line, match, pattern_uav) && match.size() >= 2) {
                              int num = std::stoi(match[1].str());
                              if (num >= 0 && num < D3D11_1_UAV_SLOT_COUNT) {
                                 cached_shader->uavs[num] = true;
                                 matching_line = true;
                              }
                           }
                           if (cached_shader->type == reshade::api::pipeline_subobject_type::pixel_shader
                              && std::regex_search(line, match, pattern_rtv) && match.size() >= 2) {
                              int num = std::stoi(match[1].str());
                              if (num >= 0 && num < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT) {
                                 cached_shader->rtvs[num] = true;
                                 matching_line = true;
                              }
                           }
#if 0 // Not very useful information, we can query the depth read/write info from other sources, and whether it's customized or not isn't relevant
                           if (cached_shader->type == reshade::api::pipeline_subobject_type::pixel_shader
                              && std::regex_search(line, match, pattern_depth) && match.size() >= 1) {
                              cached_shader->custom_depth = true;
                              matching_line = true;
                           }
#endif
                           // Stop searching after the initial section, otherwise it's way too slow
                           if (prev_matching_line && !matching_line) break;
                        }

                        found_reflections = true;
                     }
#endif // DEVELOPMENT
                  }
#if DEVELOPMENT
                  cached_shader->found_reflections = found_reflections;
                  // Default all of them to true if we can't tell which ones are used
                  if (!found_reflections)
                  {
                     for (bool& b : cached_shader->cbs) b = true;
                     for (bool& b : cached_shader->samplers) b = true;
                     for (bool& b : cached_shader->srvs) b = true;
                     for (bool& b : cached_shader->rtvs) b = true;
                     for (bool& b : cached_shader->uavs) b = true;
                  }
#endif // DEVELOPMENT
               }
#endif // ALLOW_SHADERS_DUMPING || DEVELOPMENT

               // Indexes
               assert(std::find(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(), shader_hash) == cached_pipeline->shader_hashes.end());
#if DX12
               cached_pipeline->shader_hashes.emplace_back(shader_hash);
#else
               cached_pipeline->shader_hashes[0] = shader_hash;
               ASSERT_ONCE(cached_pipeline->shader_hashes.size() == 1); // Just to make sure if this actually happens
#endif

#if DEVELOPMENT
               // Not protected by mutex as this data is generally const after boot
               auto forced_shader_names_it = forced_shader_names.find(shader_hash);
               if (forced_shader_names_it != forced_shader_names.end())
               {
                  cached_pipeline->custom_name = forced_shader_names_it->second;
               }
#endif

               {
                  const std::shared_lock lock(s_mutex_loading);
                  found_custom_shader_file |= custom_shaders_cache.contains(shader_hash);
               }

#if _DEBUG && LOG_VERBOSE
               // Metrics
               {
                  std::stringstream s2;
                  s2 << "caching shader(";
                  s2 << "hash: " << PRINT_CRC32(shader_hash);
                  s2 << ", type: " << subobject.type;
                  s2 << ", pipeline: " << reinterpret_cast<void*>(pipeline.handle);
                  s2 << ")";
                  reshade::log::message(reshade::log::level::info, s2.str().c_str());
               }
#endif // DEVELOPMENT
               break;
            }
            }
         }
      }
      if (!found_replaceable_shader)
      {
         delete cached_pipeline;
         cached_pipeline = nullptr;
         DestroyPipelineSubojects(subobjects_cache, subobject_count);
         subobjects_cache = nullptr;
         return;
      }

      {
         const std::unique_lock lock(s_mutex_generic);
         for (const auto shader_hash : cached_pipeline->shader_hashes)
         {
            // Make sure we didn't already have a valid pipeline in there (this should never happen, if not with input layout vertex shaders?, or anyway unless the game compiled the same shader twice)
            auto pipelines_pair = device_data.pipeline_caches_by_shader_hash.find(shader_hash);
            if (pipelines_pair != device_data.pipeline_caches_by_shader_hash.end())
            {
               pipelines_pair->second.emplace(cached_pipeline);
            }
            else
            {
               device_data.pipeline_caches_by_shader_hash[shader_hash] = { cached_pipeline };
            }

            ASSERT_ONCE(device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle) == device_data.pipeline_cache_by_pipeline_handle.end());
            device_data.pipeline_cache_by_pipeline_handle[pipeline.handle] = cached_pipeline;
         }
      }

      // Automatically load any custom shaders that might have been bound to this pipeline.
      // To avoid this slowing down everything, we only do it if we detect the user already had a matching shader in its custom shaders folder.
      if (auto_load && !last_pressed_unload && found_custom_shader_file)
      {
         // Immediately cloning and replacing the pipeline might be unsafe, we might need to delay it to the next frame.
         // NOTE: this is totally fine to be done immediately (inline) in DX11, it's only unsafe in DX12.
         // TODO: maybe this is always completely fine independently of the API? Maybe the problem was calling device functions concurrently as we lock global mutexes, causing deadlocks.
         if (precompile_custom_shaders)
         {
            LoadCustomShaders(device_data, { pipeline.handle }, !precompile_custom_shaders);
         }
         else
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            device_data.pipelines_to_reload.emplace(pipeline.handle);
         }
      }
   }
#pragma optimize("", on) // Restore the previous state

   void OnDestroyPipeline(
      reshade::api::device* device,
      reshade::api::pipeline pipeline)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      ASSERT_ONCE(&device_data != nullptr); // Hacky nullptr check (should ever be able to happen) // TODO: it does in Hollow Knight: Silksong on exit

      // It can happen that the "pipelines" destructor in DX is called after the device destructor (weird)
      if (&device_data != nullptr)
      {
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            device_data.pipelines_to_reload.erase(pipeline.handle);
         }
         
         std::vector<uint64_t> pipelines_to_destroy; // TODO: define a pointer size for ReShade handles, given they are just a pointer 64bit in x64 and 32bit in x32
         
         std::unique_lock lock_pipeline_destroy(device_data.pipeline_cache_destruction_mutex);
         std::unique_lock lock(s_mutex_generic);
         if (auto pipeline_cache_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle); pipeline_cache_pair != device_data.pipeline_cache_by_pipeline_handle.end())
         {
            auto& cached_pipeline = pipeline_cache_pair->second;

            if (cached_pipeline != nullptr)
            {
               // Clean other references to the pipeline
               for (auto& pipelines_cache_pair : device_data.pipeline_caches_by_shader_hash)
               {
                  auto& cached_pipelines = pipelines_cache_pair.second;
                  cached_pipelines.erase(cached_pipeline);
               }

               // Destroy our cloned subojects
               DestroyPipelineSubojects(cached_pipeline->subobjects_cache, cached_pipeline->subobject_count);
               cached_pipeline->subobjects_cache = nullptr;
#if DX12 && 0 // Redundant
               cached_pipeline->subobject_count = 0;
#endif

               // Destroy our cloned version of the pipeline (and leave the original intact, because the life time is not handled by us)
               if (cached_pipeline->cloned)
               {
                  cached_pipeline->cloned = false;
                  assert(cached_pipeline->device == device);
                  pipelines_to_destroy.push_back(cached_pipeline->pipeline_clone.handle);
                  device_data.pipeline_cache_by_pipeline_clone_handle.erase(cached_pipeline->pipeline_clone.handle);
#if 0 // Redundant
                  cached_pipeline->pipeline_clone.handled = 0;
#endif
                  device_data.cloned_pipeline_count--;
                  device_data.cloned_pipelines_changed = true;
               }
               delete cached_pipeline;
               cached_pipeline = nullptr;
            }

            device_data.pipeline_cache_by_pipeline_handle.erase(pipeline_cache_pair);
         }

         lock.unlock(); // Calls into the device could deadlock if the game rendering is multithreaded
         lock_pipeline_destroy.unlock(); // Unlock these two in reverse order!

         for (auto pipeline_to_destroy : pipelines_to_destroy)
         {
            // For DX11, this simply releases the reference to a shader (or whatever else the pipeline contains)
            device->destroy_pipeline(reshade::api::pipeline{pipeline_to_destroy});
         }
      }

#if DEVELOPMENT
      input_layouts_descs.erase(reinterpret_cast<ID3D11InputLayout*>(pipeline.handle));
#endif
   }

   void OnBindPipeline(
      reshade::api::command_list* cmd_list,
      reshade::api::pipeline_stage stages,
      reshade::api::pipeline pipeline)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      constexpr reshade::api::pipeline_stage supported_stages = reshade::api::pipeline_stage::compute_shader | reshade::api::pipeline_stage::vertex_shader | reshade::api::pipeline_stage::pixel_shader
#if GEOMETRY_SHADER_SUPPORT
         | reshade::api::pipeline_stage::geometry_shader
#endif
         ;

      // Nothing to do, the pipeline isn't supported
      if ((stages & supported_stages) == 0)
         return;

      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();

      const Shader::CachedPipeline* cached_pipeline = nullptr;

      if (pipeline.handle != 0)
      {
         const DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
         const std::shared_lock lock(s_mutex_generic);
         auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline.handle);
         if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end())
         {
            ASSERT_ONCE(pipeline_pair->second != nullptr); // Shouldn't usually happen but if it did, it's supported anyway and innocuous
            cached_pipeline = pipeline_pair->second; // This is guaranteed to not be destroyed while it's bound
         }
         else
         {
#if ENABLE_GAME_PIPELINE_STATE_READBACK // Allow either game engines or other mods between the game and ReShade to read back states we had changed
            // "Deus Ex: Human Revolution" with the golden filter restoration mod sets back customized shaders to the pipeline,
            // as it also read back the state of DX etc, so make sure to search it from the cloned shaders list too!
            auto pipeline_pair_2 = device_data.pipeline_cache_by_pipeline_clone_handle.find(pipeline.handle);
            if (pipeline_pair_2 != device_data.pipeline_cache_by_pipeline_clone_handle.end())
            {
               cached_pipeline = pipeline_pair_2->second;
            }
#endif
            ASSERT_ONCE(cached_pipeline != nullptr); // Why can't we find the shader?
         }
      }

      // Matches "reshade::addon_event::reset_command_list" (sometimes this is called instead of that)
      if (stages == reshade::api::pipeline_stage::all && pipeline.handle == 0)
      {
#if DEVELOPMENT
         //CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
         //ASSERT_ONCE(cmd_list_data.is_primary || cmd_list_data.trace_draw_calls_data.empty()); // This should already be the case (no, sometimes it triggers with acceptable cases, no need to check for this until proven otherwise)
         cmd_list_data.any_draw_done = false;
         cmd_list_data.any_dispatch_done = false;
#endif
      }

      if ((stages & reshade::api::pipeline_stage::compute_shader) != 0)
      {
         ASSERT_ONCE(stages == reshade::api::pipeline_stage::compute_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
         cmd_list_data.pipeline_state_original_compute_shader = pipeline;

         if (cached_pipeline)
         {
#if DX12
            cmd_list_data.pipeline_state_original_compute_shader_hashes.compute_shaders = std::unordered_set<uint32_t>(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end());
#else
            cmd_list_data.pipeline_state_original_compute_shader_hashes.compute_shaders[0] = cached_pipeline->shader_hashes[0];
#endif
            cmd_list_data.pipeline_state_has_custom_compute_shader = cached_pipeline->cloned;
         }
         else
         {
#if DX12
            cmd_list_data.pipeline_state_original_compute_shader_hashes.compute_shaders.clear();
#else
            cmd_list_data.pipeline_state_original_compute_shader_hashes.compute_shaders[0] = UINT64_MAX;
#endif
            cmd_list_data.pipeline_state_has_custom_compute_shader = false;
         }
      }
      if ((stages & reshade::api::pipeline_stage::vertex_shader) != 0)
      {
         ASSERT_ONCE(stages == reshade::api::pipeline_stage::vertex_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
         cmd_list_data.pipeline_state_original_vertex_shader = pipeline;

         if (cached_pipeline)
         {
#if DX12
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.vertex_shaders = std::unordered_set<uint32_t>(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end());
#else
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.vertex_shaders[0] = cached_pipeline->shader_hashes[0];
#endif
            cmd_list_data.pipeline_state_has_custom_vertex_shader = cached_pipeline->cloned;
         }
         else
         {
#if DX12
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.vertex_shaders.clear();
#else
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.vertex_shaders[0] = UINT64_MAX;
#endif
            cmd_list_data.pipeline_state_has_custom_vertex_shader = false;
         }
         cmd_list_data.pipeline_state_has_custom_graphics_shader = cmd_list_data.pipeline_state_has_custom_pixel_shader || cmd_list_data.pipeline_state_has_custom_vertex_shader;
      }
      if ((stages & reshade::api::pipeline_stage::pixel_shader) != 0)
      {
         ASSERT_ONCE(stages == reshade::api::pipeline_stage::pixel_shader || stages == reshade::api::pipeline_stage::all); // Make sure only one stage happens at a time (it does in DX11)
         cmd_list_data.pipeline_state_original_pixel_shader = pipeline;

#if DEVELOPMENT
         cmd_list_data.temp_custom_depth_stencil = cached_pipeline ? cached_pipeline->custom_depth_stencil : ShaderCustomDepthStencilType::None;
#endif

         if (cached_pipeline)
         {
#if DX12
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.pixel_shaders = std::unordered_set<uint32_t>(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end());
#else
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.pixel_shaders[0] = cached_pipeline->shader_hashes[0];
#endif
            cmd_list_data.pipeline_state_has_custom_pixel_shader = cached_pipeline->cloned;
         }
         else
         {
#if DX12
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.pixel_shaders.clear();
#else
            cmd_list_data.pipeline_state_original_graphics_shader_hashes.pixel_shaders[0] = UINT64_MAX;
#endif
            cmd_list_data.pipeline_state_has_custom_pixel_shader = false;
         }
         cmd_list_data.pipeline_state_has_custom_graphics_shader = cmd_list_data.pipeline_state_has_custom_pixel_shader || cmd_list_data.pipeline_state_has_custom_vertex_shader;
      }

      if (cached_pipeline)
      {
#if DEVELOPMENT
         if (cached_pipeline->replace_draw_type == ShaderReplaceDrawType::Purple
            || cached_pipeline->replace_draw_type == ShaderReplaceDrawType::NaN)
         {
            // TODO: automatically generate a pixel shader that has a matching input and output signature as the one the pass would have had instead,
            // given that sometimes drawing purple fails with warnings. Or replace the vertex shader too with "Copy VS"? Though the shape will then not match, likely.
            ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            if ((stages & reshade::api::pipeline_stage::compute_shader) != 0)
            {
               static_assert(CompileTimeStringHash("Draw Purple CS") != CompileTimeStringHash("Draw NaN CS"));
               auto shader_hash = cached_pipeline->replace_draw_type == ShaderReplaceDrawType::Purple ? CompileTimeStringHash("Draw Purple CS") : CompileTimeStringHash("Draw NaN CS");
               native_device_context->CSSetShader(device_data.native_compute_shaders[shader_hash].get(), nullptr, 0);
            }
            if ((stages & reshade::api::pipeline_stage::pixel_shader) != 0)
            {
               static_assert(CompileTimeStringHash("Draw Purple PS") != CompileTimeStringHash("Draw NaN PS")); // TODO: delete both
               auto shader_hash = cached_pipeline->replace_draw_type == ShaderReplaceDrawType::Purple ? CompileTimeStringHash("Draw Purple PS") : CompileTimeStringHash("Draw NaN PS");
               native_device_context->PSSetShader(device_data.native_pixel_shaders[shader_hash].get(), nullptr, 0);
            }
         }
         else if (cached_pipeline->replace_draw_type == ShaderReplaceDrawType::Skip)
         {
            // This will make the shader output black, or skip drawing, so we can easily detect it. This might not be very safe but seems to work in DX11.
            cmd_list->bind_pipeline(stages, reshade::api::pipeline{ 0 });
         }
         else
#endif
         {
            // TODO: have a high performance mode that swaps the original shader binary with the custom one on creation, so we don't have to analyze shader binding calls (probably wouldn't really speed up performance anyway).
            // This would also help save some memory in x86 games where we keep all shaders binaries in memory ("custom_shaders_cache::code").
            std::shared_lock lock(s_mutex_generic);
            if (cached_pipeline->cloned && custom_shaders_enabled)
            {
               cmd_list->bind_pipeline(stages, cached_pipeline->pipeline_clone);
            }
         }
      }

#if DEVELOPMENT // Note: we only print the supported ones!
      const std::shared_lock lock_trace(s_mutex_trace);
      if (trace_running)
      {
         CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         TraceDrawCallData trace_draw_call_data;
         trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::BindPipeline;
         trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
         //trace_draw_call_data.custom_name = std::string("Bind Shader ") + Shader::Hash_NumToStr(cached_pipeline->shader_hashes[0], true); //TODOFT: fix ...
         cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
      }
#endif
   }

   // This function takes a "source resource", which would be the reason we are creating this new resource, because a copy (or anything else like that) from a source to a target had the target not upgraded to the same format
   bool FindOrCreateIndirectUpgradedResource(reshade::api::device* device, const uint64_t in_source_resource, const uint64_t in_resource, uint64_t& out_resource, DeviceData& device_data, bool allow_create, reshade::api::resource_usage initial_state, std::shared_lock<std::shared_mutex>& lock_device_read, bool leave_locked = true)
   {
      bool replaced = false;

      auto original_resource_to_mirrored_upgraded_resource = device_data.original_resources_to_mirrored_upgraded_resources.find(in_resource);
      if (original_resource_to_mirrored_upgraded_resource != device_data.original_resources_to_mirrored_upgraded_resources.end())
      {
         out_resource = original_resource_to_mirrored_upgraded_resource->second;
         replaced = true;
      }
      // Igore all swapchain textures, we can't directly upgrade these (even in case they weren't directly upgraded), given that it's the ultimate target and somehow we'll need to write the values in it
      else if (allow_create && in_resource != 0 && !device_data.upgraded_resources.contains(in_resource) && !device_data.back_buffers.contains(in_resource))
      {
         lock_device_read.unlock(); // Avoids deadlocks with the device

         reshade::api::resource mirrored_upgraded_resource;
         reshade::api::resource_desc desc = device->get_resource_desc({in_resource});
         reshade::api::resource_desc source_desc = desc;
         bool needs_upgraded_resource;
         if (in_source_resource)
         {
            source_desc = device->get_resource_desc({in_source_resource});
            needs_upgraded_resource = !AreFormatsCopyCompatible(DXGI_FORMAT(source_desc.texture.format), DXGI_FORMAT(desc.texture.format));
            // TODO: instead of checking the formats for compatibility, also check if the source was upgraded and in that case force the target to be upgraded (faster checks)
         }
         else // Hardcoded upgrade format
         {
            source_desc.texture.format = reshade::api::format::r16g16b16a16_float;
            needs_upgraded_resource = source_desc.texture.format != desc.texture.format;
         }
         // TODO: optionally copy the content of "in_resource"?
         if (needs_upgraded_resource && device->create_resource(source_desc, nullptr, initial_state, &mirrored_upgraded_resource))
         {
            std::unique_lock lock_device_write(device_data.mutex);
            if (!device_data.original_resources_to_mirrored_upgraded_resources.contains(in_resource))
            {
               device_data.original_resources_to_mirrored_upgraded_resources[in_resource] = mirrored_upgraded_resource.handle;
               out_resource = mirrored_upgraded_resource.handle;
            }
            else // Destroy it if it was accidentally created at the same time by another thread
            {
               out_resource = device_data.original_resources_to_mirrored_upgraded_resources[in_resource];
               lock_device_write.unlock(); // Not really necessary, reshade "destroy_resource" simply clears a com ptr
               device->destroy_resource(mirrored_upgraded_resource);
            }

            replaced = true;
         }

         if (leave_locked)
            lock_device_read.lock();
      }

      // Let the upgrades happen above, but ignore the override
      if (ignore_indirect_upgraded_textures)
      {
         out_resource = in_resource;
         return false;
      }

      return replaced;
   }

   bool FindOrCreateIndirectUpgradedResourceView(reshade::api::device* device, const uint64_t in_rv, uint64_t& out_rv, DeviceData& device_data, bool allow_create, reshade::api::resource_usage usage, std::shared_lock<std::shared_mutex>& lock_device_read)
   {
      bool replaced = false;

      auto original_resource_view_to_mirrored_upgraded_resource_view = device_data.original_resource_views_to_mirrored_upgraded_resource_views.find(in_rv);
      if (original_resource_view_to_mirrored_upgraded_resource_view != device_data.original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         replaced = true;
         out_rv = original_resource_view_to_mirrored_upgraded_resource_view->second;
      }
      else if (allow_create && in_rv != 0 && !device_data.original_resources_to_mirrored_upgraded_resources.empty())
      {
         reshade::api::resource resource = device->get_resource_from_view({ in_rv }); // Note: this is likely fine to be done under a luma mutex... but it's a risk, as all device calls are
         auto original_resource_to_mirrored_upgraded_resource = device_data.original_resources_to_mirrored_upgraded_resources.find(resource.handle);
         if (original_resource_to_mirrored_upgraded_resource != device_data.original_resources_to_mirrored_upgraded_resources.end())
         {
            lock_device_read.unlock(); // Avoids deadlocks with the device

            reshade::api::resource_view_desc resource_view_desc = device->get_resource_view_desc({ in_rv });

            reshade::api::resource_view mirrored_upgraded_resource_view;
            if (device->create_resource_view({original_resource_to_mirrored_upgraded_resource->second}, usage, resource_view_desc, &mirrored_upgraded_resource_view))
            {
               std::unique_lock lock_device_write(device_data.mutex);
               if (!device_data.original_resource_views_to_mirrored_upgraded_resource_views.contains(mirrored_upgraded_resource_view.handle))
               {
                  device_data.original_resource_views_to_mirrored_upgraded_resource_views[in_rv] = mirrored_upgraded_resource_view.handle;
                  out_rv = mirrored_upgraded_resource_view.handle;
               }
               else // Destroy it if it was accidentally created at the same time by another thread
               {
                  out_rv = device_data.original_resource_views_to_mirrored_upgraded_resource_views[in_rv];
                  lock_device_write.unlock(); // Not really necessary, reshade "destroy_resource" simply clears a com ptr
                  device->destroy_resource_view(mirrored_upgraded_resource_view);
               }
               replaced = true;
            }

            lock_device_read.lock();
         }
      }

      // Let the upgrades happen above, but ignore the override
      if (ignore_indirect_upgraded_textures)
      {
         out_rv = in_rv;
         return false;
      }

      return replaced;
   }

   void OnBindRenderTargetsAndDepthStencil(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* rtvs, reshade::api::resource_view dsv)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      std::vector<reshade::api::resource_view> replaced_rtvs(rtvs, rtvs + count);
      bool any_replaced = false;

      {
         DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
         std::shared_lock lock_device_read(device_data.mutex);

         for (uint32_t i = 0; i < count; i++)
         {
            any_replaced |= FindOrCreateIndirectUpgradedResourceView(cmd_list->get_device(), rtvs[i].handle, replaced_rtvs[i].handle, device_data, enable_automatic_indirect_texture_format_upgrades, reshade::api::resource_usage::render_target, lock_device_read);
         }
         any_replaced |= FindOrCreateIndirectUpgradedResourceView(cmd_list->get_device(), dsv.handle, dsv.handle, device_data, enable_automatic_indirect_texture_format_upgrades, reshade::api::resource_usage::depth_stencil, lock_device_read); // Usually not needed but won't hurt
      }

      if (any_replaced)
      {
         cmd_list->bind_render_targets_and_depth_stencil(replaced_rtvs.size(), replaced_rtvs.data(), dsv);
      }
   }

   enum class LumaConstantBufferType
   {
      // Global/frame settings
      LumaSettings,
      // Per draw/instance data
      LumaData,
      LumaUIData
   };

   void SetLumaConstantBuffers(ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, LumaConstantBufferType type, uint32_t custom_data_1 = 0, uint32_t custom_data_2 = 0, float custom_data_3 = 0.f, float custom_data_4 = 0.f)
   {
      constexpr bool force_update = false;

      auto SetConstantBuffer = [&](uint32_t slot, ID3D11Buffer* const buffer)
         {
#if DEVELOPMENT
            bool failed = false;
            if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
            {
               com_ptr<ID3D11Buffer> cb;
               native_device_context->VSGetConstantBuffers(slot, 1, &cb);
               failed |= cb.get() != nullptr && cb.get() != buffer;
            }
            if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
            {
               com_ptr<ID3D11Buffer> cb;
               native_device_context->GSGetConstantBuffers(slot, 1, &cb);
               failed |= cb.get() != nullptr && cb.get() != buffer;
            }
            if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
            {
               com_ptr<ID3D11Buffer> cb;
               native_device_context->PSGetConstantBuffers(slot, 1, &cb);
               failed |= cb.get() != nullptr && cb.get() != buffer;
            }
            if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
            {
               com_ptr<ID3D11Buffer> cb;
               native_device_context->CSGetConstantBuffers(slot, 1, &cb);
               failed |= cb.get() != nullptr && cb.get() != buffer;
            }
            // The game might have used the same cbuffer slot we are using for this very pass, or just left a cbuffer bound even if unused by the current pass (in that case it's likely ok)
            ASSERT_ONCE_MSG(!failed, "This game might have used the same cbuffer indexes we use for Luma, there's a chance the game will break unless we reset them after we are done with the custom passes that use them");
#endif

            if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
               native_device_context->VSSetConstantBuffers(slot, 1, &buffer);
            if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
               native_device_context->GSSetConstantBuffers(slot, 1, &buffer);
            if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
               native_device_context->PSSetConstantBuffers(slot, 1, &buffer);
            if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
               native_device_context->CSSetConstantBuffers(slot, 1, &buffer);
         };

      // Most games (e.g. Prey, Dishonored 2) doesn't ever use these buffer slots, so it's fine to re-apply them once per frame if they didn't change.
      // For other games, it'd be good to re-apply the previously set cbuffer after temporarily changing it, as they might only set them once per frame.
      switch (type)
      {
      case LumaConstantBufferType::LumaSettings:
      {
         if (luma_settings_cbuffer_index >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) break;

         {
            const std::shared_lock lock_reshade(s_mutex_reshade);
            if (force_update || device_data.cb_luma_global_settings_dirty)
            {
               // My understanding is that "Map" doesn't immediately copy the memory to the GPU, but simply stores it on the side (in the command list),
               // and then copies it to the GPU when the command list is executed, so the resource is updated with deterministic order.
               // From our point of view, we don't really know what command list is currently running and what it is doing,
               // so we could still end up first updating the resource in a command list that will be executed with a delay,
               // and then updating the resource again in a command list that executes first, leaving the GPU buffer with whatever latest data it got (which might not be based on our latest version).
               // Fortunately we rarely use our cbuffers in any of the non main/primary command lists, and if we do, we generally don't change them within the frame,
               // so we can consider it all single threaded and deterministic. If ever needed, we could force a map to happen every time if we are in a new (non main) command list,
               // but then again, that would cross pollute the buffer across thread (plus, a command list is not guaranteed to ever be executed, it could be cleared before executing),
               // so the best solution would be to have these cbuffers per thread or per pass, or to not change them within a frame (or to always write the buffer again!)!
               if (D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  SUCCEEDED(native_device_context->Map(device_data.luma_global_settings.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
               {
                  if (!cmd_list_data.is_primary)
                  {
                     if (device_data.cb_luma_global_settings_dirty)
                     {
                        cmd_list_data.async_set_cb_luma_global_settings = true;
                     }
#if DEVELOPMENT
                     cmd_list_data.requires_join = true; // Make sure this command list is "merged" otherwise this "Map" call would go lost, leaving our buffer dirty
#endif
                  }
                  else
                  {
                     device_data.cb_luma_global_settings_dirty = false;
                  }
                  std::memcpy(mapped_buffer.pData, &cb_luma_global_settings, sizeof(cb_luma_global_settings));
                  native_device_context->Unmap(device_data.luma_global_settings.get(), 0);
               }
            }
         }

         ID3D11Buffer* const buffer = device_data.luma_global_settings.get();
         SetConstantBuffer(luma_settings_cbuffer_index, buffer);
         break;
      }
      case LumaConstantBufferType::LumaData:
      {
         if (luma_data_cbuffer_index >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) break;

         CB::LumaInstanceDataPadded cb_luma_instance_data;
         cb_luma_instance_data.CustomData1 = custom_data_1;
         cb_luma_instance_data.CustomData2 = custom_data_2;
         cb_luma_instance_data.CustomData3 = custom_data_3;
         cb_luma_instance_data.CustomData4 = custom_data_4;

         cb_luma_instance_data.RenderResolutionScale.x = device_data.render_resolution.x / device_data.output_resolution.x;
         cb_luma_instance_data.RenderResolutionScale.y = device_data.render_resolution.y / device_data.output_resolution.y;
         // Always do this relative to the current output resolution
         cb_luma_instance_data.PreviousRenderResolutionScale.x = device_data.previous_render_resolution.x / device_data.output_resolution.x;
         cb_luma_instance_data.PreviousRenderResolutionScale.y = device_data.previous_render_resolution.y / device_data.output_resolution.y;

         game->UpdateLumaInstanceDataCB(cb_luma_instance_data, cmd_list_data, device_data);

         if (force_update || cmd_list_data.force_cb_luma_instance_data_dirty || memcmp(&cmd_list_data.cb_luma_instance_data, &cb_luma_instance_data, sizeof(cb_luma_instance_data)) != 0)
         {
            cmd_list_data.cb_luma_instance_data = cb_luma_instance_data;
            cmd_list_data.force_cb_luma_instance_data_dirty = false;
            if (D3D11_MAPPED_SUBRESOURCE mapped_buffer;
               SUCCEEDED(native_device_context->Map(device_data.luma_instance_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
            {
               std::memcpy(mapped_buffer.pData, &cmd_list_data.cb_luma_instance_data, sizeof(cmd_list_data.cb_luma_instance_data));
               native_device_context->Unmap(device_data.luma_instance_data.get(), 0);
            }
         }

         ID3D11Buffer* const buffer = device_data.luma_instance_data.get();
         SetConstantBuffer(luma_data_cbuffer_index, buffer);
         break;
      }
      case LumaConstantBufferType::LumaUIData:
      {
         ASSERT_ONCE_MSG(false, "Luma UI Data is not implemented (yet?)"); //TODOFT5: do it?
         break;
      }
      }
   }

   // The way we interpret this is probably DX11 specific, as in DX12 this doesn't reset the state
   void OnResetCommandList(reshade::api::command_list* cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      cmd_list_data.pipeline_state_original_compute_shader = reshade::api::pipeline(0);
      cmd_list_data.pipeline_state_original_vertex_shader = reshade::api::pipeline(0);
      cmd_list_data.pipeline_state_original_pixel_shader = reshade::api::pipeline(0);

      cmd_list_data.pipeline_state_original_graphics_shader_hashes.Clear();
      cmd_list_data.pipeline_state_original_compute_shader_hashes.Clear();
      cmd_list_data.pipeline_state_has_custom_vertex_shader = false;
      cmd_list_data.pipeline_state_has_custom_pixel_shader = false;
      cmd_list_data.pipeline_state_has_custom_graphics_shader = false;
      cmd_list_data.pipeline_state_has_custom_compute_shader = false;

      ASSERT_ONCE(!cmd_list_data.async_set_cb_luma_global_settings);
      cmd_list_data.async_set_cb_luma_global_settings = false;

#if DEVELOPMENT
      //ASSERT_ONCE(cmd_list_data.is_primary || cmd_list_data.trace_draw_calls_data.empty()); // This should already be the case (no, sometimes it triggers with acceptable cases, no need to check for this until proven otherwise)
      cmd_list_data.any_draw_done = false;
      cmd_list_data.any_dispatch_done = false;

      ASSERT_ONCE(!cmd_list_data.requires_join);
      cmd_list_data.requires_join = false;

      cmd_list_data.temp_custom_depth_stencil = ShaderCustomDepthStencilType::None;

      const std::shared_lock lock_trace(s_mutex_trace);
      if (trace_running)
      {
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         TraceDrawCallData trace_draw_call_data;
         trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ResetCommmandList;
         trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
         cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
      }
#endif
   }

#if DEVELOPMENT
   // "queue" and "cmd_list" both point to the device context in DX11
   // For DX11, this is only called on the immediately device context
   void OnExecuteCommandList(reshade::api::command_queue* queue, reshade::api::command_list* cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      const std::shared_lock lock_trace(s_mutex_trace);
      if (trace_running)
      {
         CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         TraceDrawCallData trace_draw_call_data;
         trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::FlushCommandList;
         trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
         cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
      }
   }
#endif

   // For DX11 this is linked to "FinishCommandList".
   // Before that, there's no "ID3D11CommandList".
   // After a deferred context calls that (immediate contexts shouldn't call it),
   // it creates a cmd list and ReShade makes a proxy, so we get a "init_command_list" event, with the actual "ID3D11CommandList",
   // where we can actually assign the "CommandListData" to it, and then we receive a call to "execute_secondary_command_list".
   // The application will then need to call "ExecuteCommandList" on the immediate context with the cmd list from the deferred context.
   // In that case we will also receive a call to "ExecuteCommandList", though with the context and command list objects order swapped.
   // All the other "reshade::api::command_list* cmd_list" around the code are "ID3D11DeviceContext", except for this function,
   // where one of the two is "ID3D11CommandList".
   void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      CommandListData& secondary_cmd_list_data = *secondary_cmd_list->get_private_data<CommandListData>();

      bool is_finish_command_list = false; // It should have been called "create command list", but probably internally it already existed so they called it "finish"
      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = reinterpret_cast<ID3D11DeviceChild*>(secondary_cmd_list->get_native()); // This could either be a "ID3D11CommandList" or a "ID3D11DeviceContext"
      HRESULT hr = device_child->QueryInterface(&native_device_context);
      if (SUCCEEDED(hr) && native_device_context)
      {
         is_finish_command_list = true;
      }

      cmd_list_data.async_set_cb_luma_global_settings = secondary_cmd_list_data.async_set_cb_luma_global_settings;
      secondary_cmd_list_data.async_set_cb_luma_global_settings = false;
      if (!is_finish_command_list && cmd_list_data.async_set_cb_luma_global_settings)
      {
         DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
         cmd_list_data.async_set_cb_luma_global_settings = false;

         // The command list has been joined, so any cbuffer that was updated in it is from now already updated an any further commands
         device_data.cb_luma_global_settings_dirty = false;
      }

#if DEVELOPMENT
      if (is_finish_command_list)
      {
         cmd_list_data.requires_join = secondary_cmd_list_data.requires_join;
      }
      secondary_cmd_list_data.requires_join = false;

      const std::shared_lock lock_trace(s_mutex_trace);
      if (trace_running)
      {
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         TraceDrawCallData trace_draw_call_data;
         trace_draw_call_data.type = is_finish_command_list ? TraceDrawCallData::TraceDrawCallType::CreateCommandList : TraceDrawCallData::TraceDrawCallType::AppendCommandList;
         trace_draw_call_data.command_list = is_finish_command_list ? (ID3D11DeviceContext*)(secondary_cmd_list->get_native()) : (ID3D11DeviceContext*)(cmd_list->get_native()); // Show the target command list (device context), not the source one (the source draw calls will be below)
         cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);

         const std::unique_lock lock_trace_3(secondary_cmd_list_data.mutex_trace);
         cmd_list_data.trace_draw_calls_data.append_range(secondary_cmd_list_data.trace_draw_calls_data);
         secondary_cmd_list_data.trace_draw_calls_data.clear(); // Clear the command list where the data was from (given it was moved to the other one)
      }
#endif
   }

   void OnPresent(
      reshade::api::command_queue* queue,
      reshade::api::swapchain* swapchain,
      const reshade::api::rect* source_rect,
      const reshade::api::rect* dest_rect,
      uint32_t dirty_rect_count,
      const reshade::api::rect* dirty_rects
   )
   {
      SKIP_UNSUPPORTED_DEVICE_API(swapchain->get_device()->get_api());

      ID3D11Device* native_device = (ID3D11Device*)(queue->get_device()->get_native());
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(queue->get_immediate_command_list()->get_native());
      DeviceData& device_data = *queue->get_device()->get_private_data<DeviceData>();
      SwapchainData& swapchain_data = *swapchain->get_private_data<SwapchainData>();
      CommandListData& cmd_list_data = *queue->get_immediate_command_list()->get_private_data<CommandListData>();

#if DEVELOPMENT
      // Allow to tank performance to test auto rendering resolution scaling etc
      if (frame_sleep_ms > 0 && cb_luma_global_settings.FrameIndex % frame_sleep_interval == 0)
         Sleep(frame_sleep_ms);
#endif  // DEVELOPMENT

      // If there are no shaders being currently replaced in the game,
      // we can assume that we either missed replacing some shaders, or that we have unloaded all of our shaders.
      bool mod_active = IsModActive(device_data);
      // Theoretically we should simply check the current swapchain buffer format, but this also works
      const bool output_linear = (swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type == SwapchainUpgradeType::scRGB) || swapchain_data.vanilla_was_linear_space;
      const bool output_pq_bt2020 = swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled && swapchain_upgrade_type == SwapchainUpgradeType::HDR10;
      bool input_linear = swapchain_data.vanilla_was_linear_space;
#if GAME_PREY // Prey's native code hooks already make the swapchain linear, but don't change the shaders
      input_linear = false;
#endif
      if (mod_active)
      {
         // "POST_PROCESS_SPACE_TYPE" 1 means that the final image was stored in textures in linear space (e.g. float or sRGB texture formats),
         // any other type would have been in gamma space, so it needs to be linearized for scRGB HDR (linear) output.
         // "GAMMA_CORRECTION_TYPE" 2 is always re-corrected (e.g. from sRGB) in the final shader.
         input_linear = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1;
      }
      // Note that not all these combinations might be handled by the shader
      bool needs_reencoding = (output_linear != input_linear) || output_pq_bt2020;
      bool early_display_encoding = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 1 && GetShaderDefineCompiledNumericalValue(EARLY_DISPLAY_ENCODING_HASH) >= 1;
      bool needs_scaling = mod_active ? !early_display_encoding : (cb_luma_global_settings.DisplayMode >= DisplayModeType::HDR);
      bool early_gamma_correction = early_display_encoding && GetShaderDefineCompiledNumericalValue(GAMMA_CORRECTION_TYPE_HASH) < 2;
      // If the vanilla game was already doing post processing in linear space, it would have used sRGB buffers, hence it needs a sRGB<->2.2 gamma mismatch fix (we assume the vanilla game was running in SDR, not scRGB HDR).
      bool in_out_gamma_different = GetShaderDefineCompiledNumericalValue(VANILLA_ENCODING_TYPE_HASH) != GetShaderDefineCompiledNumericalValue(GAMMA_CORRECTION_TYPE_HASH);
      // If we are outputting SDR on SDR Display on a scRGB HDR swapchain, we might need Gamma 2.2/sRGB mismatch correction, because Windows would encode the scRGB buffer with sRGB (instead of Gamma 2.2, which the game would likely have expected)
      bool display_mode_needs_gamma_correction = swapchain_data.vanilla_was_linear_space ? false : (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR);
      bool needs_gamma_correction = (mod_active ? (!early_gamma_correction && in_out_gamma_different) : in_out_gamma_different) || display_mode_needs_gamma_correction;
      // If this is true, the UI and Scene were both drawn with a brightness that is relative to each other, so we need to normalize it back to the scene brightness range
      bool ui_needs_scaling = mod_active && GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) == 2;
      // If this is true, the UI was drawn on a separate buffer and needs to be composed onto the scene (which allows for UI background tonemapping, for increased visibility in HDR)
      // Note: we could skip this if "device_data.has_drawn_main_post_processing" is false and our scene and UI paper whites are matching, however, let's not make it any more complicated.
      bool ui_needs_composition = mod_active && enable_ui_separation && GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) >= 3 && device_data.ui_texture.get();
      bool needs_gamut_mapping = mod_active && GetShaderDefineCompiledNumericalValue(GAMUT_MAPPING_TYPE_HASH) != 0;
      // TODO: add "TEST_SDR_HDR_SPLIT_VIEW_MODE" and "TEST_2X_ZOOM" as drawing conditions etc
      bool force_disable_display_composition = false;

#if DEVELOPMENT
      bool needs_debug_draw_texture = device_data.debug_draw_texture.get() != nullptr; // Note that this might look wrong if "output_linear" is false
#else
      constexpr bool needs_debug_draw_texture = false;
#if DISABLE_DISPLAY_COMPOSITION
      force_disable_display_composition = true;
#endif
#endif
      if (!force_disable_display_composition && (needs_debug_draw_texture || needs_reencoding || needs_gamma_correction || ui_needs_scaling || ui_needs_composition || needs_gamut_mapping))
      {
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
         if (device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")] && device_data.native_pixel_shaders[CompileTimeStringHash("Display Composition")])
         {
            IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());

            UINT back_buffer_index = 0;
            com_ptr<IDXGISwapChain3> native_swapchain3;
            // The cast pointer is actually the same, we are just making sure the type is right (it should always be).
            // This would always be 1 in DX11, even if two buffers were requested.
            if (SUCCEEDED(native_swapchain->QueryInterface(&native_swapchain3)))
            {
               back_buffer_index = native_swapchain3->GetCurrentBackBufferIndex();
            }
            com_ptr<ID3D11Texture2D> back_buffer;
            native_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));
            assert(back_buffer != nullptr && swapchain_data.back_buffers.size() >= back_buffer_index + 1);
            assert(swapchain_data.display_composition_rtvs.size() >= back_buffer_index + 1);

            D3D11_TEXTURE2D_DESC target_desc;
            back_buffer->GetDesc(&target_desc);
            ASSERT_ONCE((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0);
            // For now we only support these formats, nothing else wouldn't really make sense
            ASSERT_ONCE(target_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || target_desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);

            uint32_t custom_const_buffer_data_1 = 0;
            uint32_t custom_const_buffer_data_2 = 0;
            float custom_const_buffer_data_3 = 0.f;

#if DEVELOPMENT // See "debug_draw_srv_slot_numbers" etc...
            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
#else
            DrawStateStack<DrawStateStackType::SimpleGraphics> draw_state_stack;
#endif
            draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

#if DEVELOPMENT
            UINT debug_draw_srv_slot = 2; // 0 is for background, 1 is for UI, 2+ for debug draw types
            constexpr UINT debug_draw_srv_slot_numbers = 7; // The max amount of debug draw types, determined by the display composition shader too

            if (device_data.debug_draw_texture.get())
            {
               // We might not be able to rely on SRVs automatic generation (by passing a nullptr desc), because depth resources take a custom view format etc

               // Note: it's possible to use "ID3D11Resource::GetType()" instead of this
               com_ptr<ID3D11Texture2D> debug_draw_texture_2d;
               device_data.debug_draw_texture->QueryInterface(&debug_draw_texture_2d);
               com_ptr<ID3D11Texture3D> debug_draw_texture_3d;
               device_data.debug_draw_texture->QueryInterface(&debug_draw_texture_3d);
               com_ptr<ID3D11Texture1D> debug_draw_texture_1d;
               device_data.debug_draw_texture->QueryInterface(&debug_draw_texture_1d);
               D3D11_SHADER_RESOURCE_VIEW_DESC debug_srv_desc = {};
               DXGI_FORMAT debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
               if (debug_draw_texture_2d)
               {
                  D3D11_TEXTURE2D_DESC texture_2d_desc;
                  debug_draw_texture_2d->GetDesc(&texture_2d_desc);

                  debug_draw_texture_format = texture_2d_desc.Format;
                  debug_srv_desc.Format = device_data.debug_draw_texture_format;
                  if (texture_2d_desc.SampleDesc.Count <= 1 && texture_2d_desc.ArraySize <= 1) // Non Array Non MS
                  {
                     debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                     debug_srv_desc.Texture2D.MostDetailedMip = 0;
                     debug_srv_desc.Texture2D.MipLevels = UINT(-1); // Use all
                     debug_draw_srv_slot = 2;
                  }
                  if (texture_2d_desc.SampleDesc.Count > 1) // Array
                  {
                     if (texture_2d_desc.ArraySize <= 1)
                     {
                        debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
                        debug_srv_desc.Texture2DMS.UnusedField_NothingToDefine = 0; // Useless, but good to make it explicit
                        debug_draw_srv_slot = 3;
                     }
                     debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::TextureMultiSample;
                  }
                  else
                  {
                     debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::TextureMultiSample;
                  }
                  if (texture_2d_desc.ArraySize > 1) // Array
                  {
                     if (texture_2d_desc.SampleDesc.Count > 1) // Array + MS
                     {
                        debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
                        debug_srv_desc.Texture2DMSArray.FirstArraySlice = 0;
                        debug_srv_desc.Texture2DMSArray.ArraySize = texture_2d_desc.ArraySize;
                        debug_draw_srv_slot = 5;
                     }
                     else
                     {
                        debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
                        debug_srv_desc.Texture2DArray.MostDetailedMip = 0;
                        debug_srv_desc.Texture2DArray.MipLevels = UINT(-1); // Use all
                        debug_srv_desc.Texture2DArray.FirstArraySlice = 0;
                        debug_srv_desc.Texture2DArray.ArraySize = texture_2d_desc.ArraySize;
                        debug_draw_srv_slot = 4;
                     }
                     debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::TextureArray;
                  }
                  else
                  {
                     debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::TextureArray;
                  }
                  debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Texture2D;
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Texture3D;
               }
               else if (debug_draw_texture_3d)
               {
                  D3D11_TEXTURE3D_DESC texture_3d_desc;
                  debug_draw_texture_3d->GetDesc(&texture_3d_desc);

                  debug_draw_texture_format = texture_3d_desc.Format;
                  debug_srv_desc.Format = device_data.debug_draw_texture_format;
                  debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                  debug_srv_desc.Texture3D.MostDetailedMip = 0;
                  debug_srv_desc.Texture3D.MipLevels = UINT(-1); // Use all
                  debug_draw_srv_slot = 6;
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::TextureMultiSample;
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::TextureArray;
                  debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Texture3D;
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Texture2D;
               }
               else if (debug_draw_texture_1d)
               {
                  D3D11_TEXTURE1D_DESC texture_1d_desc;
                  debug_draw_texture_1d->GetDesc(&texture_1d_desc);

                  debug_draw_texture_format = texture_1d_desc.Format;
                  debug_srv_desc.Format = device_data.debug_draw_texture_format;
                  if (texture_1d_desc.ArraySize > 1)
                  {
                     debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
                     debug_srv_desc.Texture1DArray.MostDetailedMip = 0;
                     debug_srv_desc.Texture1DArray.MipLevels = UINT(-1); // Use all
                     debug_srv_desc.Texture1DArray.FirstArraySlice = 0;
                     debug_srv_desc.Texture1DArray.ArraySize = texture_1d_desc.ArraySize;
                     debug_draw_srv_slot = 8;
                     debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::TextureArray;
                  }
                  else
                  {
                     debug_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
                     debug_srv_desc.Texture1D.MostDetailedMip = 0;
                     debug_srv_desc.Texture1D.MipLevels = UINT(-1); // Use all
                     debug_draw_srv_slot = 7;
                     debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::TextureArray;
                  }
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::TextureMultiSample;
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Texture3D;
                  debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Texture2D;
               }

               if (debug_draw_auto_gamma)
               {
                  // TODO: if this is depth and depth is inverted (or not), should we flip the gamma direction? Gamma to linear looks good on direct/linear depth
                  if (!IsLinearFormat(device_data.debug_draw_texture_format)) // We don't use the view format as we create a new view with the native format
                  {
                     debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                  }
                  else
                  {
                     debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                     debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                     // If the game rendering or post processing was in gamma space, automatically linearize color textures (just a guess, it's usually right)
                     if (IsRGBAFormat(device_data.debug_draw_texture_format, false) && GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) == 0)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                     }
                  }
               }

               com_ptr<ID3D11ShaderResourceView> debug_srv;
               // We recreate this every frame, it doesn't really matter (and this is allowed to fail in case of quirky formats)
               HRESULT hr = native_device->CreateShaderResourceView(device_data.debug_draw_texture.get(), &debug_srv_desc, &debug_srv);
               // Try again with the resource format in case the above failed...
               if (FAILED(hr))
               {
                  debug_srv_desc.Format = debug_draw_texture_format;
                  debug_srv = nullptr; // Extra safety
                  hr = native_device->CreateShaderResourceView(device_data.debug_draw_texture.get(), &debug_srv_desc, &debug_srv);
               }
               ASSERT_ONCE(SUCCEEDED(hr));

               ID3D11ShaderResourceView* const debug_srv_const = debug_srv.get();
               native_device_context->PSSetShaderResources(debug_draw_srv_slot, 1, &debug_srv_const); // Use index 1 (0 is already used)

               auto temp_debug_draw_options = debug_draw_options;
               bool debug_draw_saturate = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Saturate) != 0;
               // TODO: save two versions of "debug_draw_options", one that matches the user settings and one that is the current automated version of it
               if (debug_draw_saturate || !IsFloatFormat(device_data.debug_draw_texture_format))
               {
                  temp_debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Tonemap;
               }
               custom_const_buffer_data_2 = temp_debug_draw_options;

               custom_const_buffer_data_3 = float(debug_draw_mip);
            }
            // Empty the shader resources so the shader can tell there isn't one
            else
            {
               ID3D11ShaderResourceView* const debug_srvs_const[debug_draw_srv_slot_numbers] = {};
               native_device_context->PSSetShaderResources(debug_draw_srv_slot, debug_draw_srv_slot_numbers, &debug_srvs_const[0]);
            }
#endif

            bool had_full_mips = false;
            bool wants_mips = false;
#if GAME_GENERIC && 0 // use this for AutoHDR deblooming //TODOFT: finish this stuff, the conditions make no sense. Maybe add "doAutoHDR"?
            wants_mips = !mod_active;
#endif
            D3D11_TEXTURE2D_DESC proxy_target_desc;
            if (device_data.display_composition_texture.get() != nullptr)
            {
               device_data.display_composition_texture->GetDesc(&proxy_target_desc);
               had_full_mips = proxy_target_desc.MipLevels == 0 || proxy_target_desc.MipLevels == GetTextureMaxMipLevels(proxy_target_desc.Width, proxy_target_desc.Height);
            }
            if (device_data.display_composition_texture.get() == nullptr || proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height || proxy_target_desc.Format != target_desc.Format || had_full_mips != wants_mips)
            {
               proxy_target_desc = target_desc;
               proxy_target_desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
               proxy_target_desc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
               proxy_target_desc.BindFlags &= ~D3D11_BIND_UNORDERED_ACCESS;
               proxy_target_desc.CPUAccessFlags = 0;
               proxy_target_desc.Usage = D3D11_USAGE_DEFAULT;

               if (wants_mips)
               {
                  proxy_target_desc.MipLevels = 0; // All mips
                  proxy_target_desc.BindFlags |= D3D11_BIND_RENDER_TARGET; // Needed by "GenerateMips()"
                  proxy_target_desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS; // For AutoHDR "bloom" feature
               }

               device_data.display_composition_texture = nullptr;
               device_data.display_composition_srv = nullptr;
               {
                  const std::unique_lock lock_swapchain(swapchain_data.mutex);
                  // Don't change the allocation number
                  for (size_t i = 0; i < swapchain_data.display_composition_rtvs.size(); ++i)
                  {
                     swapchain_data.display_composition_rtvs[i] = nullptr;
                  }
               }
               HRESULT hr = native_device->CreateTexture2D(&proxy_target_desc, nullptr, &device_data.display_composition_texture);
               assert(SUCCEEDED(hr));

               D3D11_TEXTURE2D_DESC tex_desc = {};
               device_data.display_composition_texture->GetDesc(&tex_desc);

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = proxy_target_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = tex_desc.MipLevels == 0 ? UINT(-1) : tex_desc.MipLevels; // For some reason this requires -1 instead of 0 to specify all mips
               hr = native_device->CreateShaderResourceView(device_data.display_composition_texture.get(), &srv_desc, &device_data.display_composition_srv);
               assert(SUCCEEDED(hr));

               if (wants_mips)
               {
                  UINT support = 0;
                  hr = native_device->CheckFormatSupport(tex_desc.Format, &support);
                  assert(SUCCEEDED(hr));
                  assert(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN);
               }
            }

            // We need to copy the texture to read back from it, even if we only exclusively write to the same pixel we read and thus there couldn't be any race condition. Unfortunately DX works like that.
            if (wants_mips)
            {
               native_device_context->CopySubresourceRegion(device_data.display_composition_texture.get(), 0, 0, 0, 0, back_buffer.get(), 0, nullptr); // Copy the base mip only
               native_device_context->GenerateMips(device_data.display_composition_srv.get());
            }
            else
            {
               native_device_context->CopyResource(device_data.display_composition_texture.get(), back_buffer.get());
            }

            com_ptr<ID3D11RenderTargetView> target_resource_texture_view;
            {
               const std::unique_lock lock_swapchain(swapchain_data.mutex);
               target_resource_texture_view = swapchain_data.display_composition_rtvs[back_buffer_index];
               // If we already had a render target view (set by the game), we can assume it was already set to the swapchain,
               // but it's good to make sure of it nonetheless, it might have been changed already.
               if (draw_state_stack.state->render_target_views[0] != nullptr && draw_state_stack.state->render_target_views[0] != swapchain_data.display_composition_rtvs[back_buffer_index])
               {
                  com_ptr<ID3D11Resource> render_target_resource;
                  draw_state_stack.state->render_target_views[0]->GetResource(&render_target_resource);
                  if (render_target_resource.get() == back_buffer.get())
                  {
                     target_resource_texture_view = draw_state_stack.state->render_target_views[0];
                     swapchain_data.display_composition_rtvs[back_buffer_index] = nullptr;
                  }
               }
               if (!target_resource_texture_view)
               {
                  swapchain_data.display_composition_rtvs[back_buffer_index] = nullptr;
                  HRESULT hr = native_device->CreateRenderTargetView(back_buffer.get(), nullptr, &swapchain_data.display_composition_rtvs[back_buffer_index]);
                  ASSERT_ONCE(SUCCEEDED(hr));
                  target_resource_texture_view = swapchain_data.display_composition_rtvs[back_buffer_index];
               }
            }

            // Push our settings cbuffer in case where no other custom shader run this frame
            {
               DeviceData& device_data = *queue->get_device()->get_private_data<DeviceData>();
               const std::shared_lock lock(s_mutex_reshade); // TODO: why is this locked here? It could cause a deadlock with the device below!
               // Force a custom display mode in case we have no game custom shaders loaded, so the custom linearization shader can linearize anyway, independently of "POST_PROCESS_SPACE_TYPE"
               bool force_reencoding_or_gamma_correction = !mod_active; // We ignore "s_mutex_generic", it doesn't matter
               if (force_reencoding_or_gamma_correction)
               {
                  // No need for "s_mutex_reshade" here or above, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
                  custom_const_buffer_data_1 = input_linear ? 2 : 1;
               }
               float custom_const_buffer_data_4 = (ui_needs_composition && device_data.has_drawn_main_post_processing) ? 1.f : 0.f;
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, custom_const_buffer_data_1, custom_const_buffer_data_2, custom_const_buffer_data_3, custom_const_buffer_data_4);
            }

            // Set UI texture (limited by "DrawStateStackType::SimpleGraphics")
            ID3D11ShaderResourceView* const ui_texture_srv_const = (ui_needs_composition && (!device_data.has_drawn_main_post_processing || !hide_ui)) ? device_data.ui_texture_srv.get() : nullptr;
            native_device_context->PSSetShaderResources(1, 1, &ui_texture_srv_const);

            // Set the sampler, in case we needed it (limited by "DrawStateStackType::SimpleGraphics")
            // This can be useful to debug draw textures too as they have a linear sampling mode
            if (wants_mips || needs_debug_draw_texture)
            {
               ID3D11SamplerState* const sampler_state_linear = device_data.sampler_state_linear.get();
               native_device_context->PSSetSamplers(0, 1, &sampler_state_linear);
            }

            // Note: we don't need to re-apply our custom cbuffers in most games (e.g. Prey), they are on indexes that are never used by the game's code
            DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), device_data.native_pixel_shaders[CompileTimeStringHash("Display Composition")].get(), device_data.display_composition_srv.get(), target_resource_texture_view.get(), target_desc.Width, target_desc.Height, false);

#if DEVELOPMENT
            {
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  TraceDrawCallData trace_draw_call_data;
                  trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                  trace_draw_call_data.command_list = native_device_context;
                  trace_draw_call_data.custom_name = "Luma Display Composition";
                  cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
               }
            }
#endif // DEVELOPMENT

            if (ui_needs_composition)
            {
               // UI render target should be all zero at the beginning of each frame (the next one)
               const FLOAT ColorRGBA[4] = { 0.f, 0.f, 0.f, 0.f };
               native_device_context->ClearRenderTargetView(device_data.ui_texture_rtv.get(), ColorRGBA);

               // Reset this, it shouldn't persist between frames
               device_data.ui_latest_original_rtv = nullptr;
               device_data.ui_initial_original_rtv = nullptr;
            }

            draw_state_stack.Restore(native_device_context);
         }
         else
         {
#if DEVELOPMENT
            ASSERT_ONCE_MSG(false, "The display composition Luma native shaders failed to be found (they have either been unloaded or failed to compile, or simply missing in the files)");
#else // !DEVELOPMENT
            static std::atomic<bool> warning_sent;
            if (!warning_sent.exchange(true))
            {
               const std::string warn_message = "Some of the shader files are missing from the \"" + std::string(NAME) + "\" folder, or failed to compile for some unknown reason, please re-install the mod.";
               HWND window = game_window;
#if 1 // This can hang the game on boot in Prey, without even showing the warning, it probably can in other games too, the message will appear on top anyway
               window = 0;
#endif
               MessageBoxA(window, warn_message.c_str(), NAME, MB_SETFOREGROUND);
            }
#endif // DEVELOPMENT
         }
      }
      else
      {
         device_data.display_composition_texture = nullptr;
         device_data.display_composition_srv = nullptr;

         const std::unique_lock lock_swapchain(swapchain_data.mutex);
         // Don't change the allocation number
         for (size_t i = 0; i < swapchain_data.display_composition_rtvs.size(); ++i)
         {
            swapchain_data.display_composition_rtvs[i] = nullptr;
         }
      }

#if DEVELOPMENT
      // Clear at the end of every frame and re-capture it in the next frame if its still available
      if (debug_draw_auto_clear_texture)
      {
         device_data.debug_draw_texture = nullptr;
         // Leave "debug_draw_texture_format" and "debug_draw_texture_size", we need that in ImGUI (we'd need to clear it after ImGUI if necessary, but we skip drawing it if the texture isn't valid)
      }
      debug_draw_pipeline_instance = 0;

#if 0 // Optionally clear it every frame, to remove it if it wasn't found (this needs to be done elsewhere for now, because we print it below)
      device_data.track_buffer_data = {};
#endif
      track_buffer_pipeline_instance = 0;
#endif // DEVELOPMENT

      device_data.has_drawn_main_post_processing_previous = device_data.has_drawn_main_post_processing;
#if ENABLE_SR
      device_data.has_drawn_sr_imgui = device_data.has_drawn_sr;
#endif // ENABLE_SR

      game->OnPresent(native_device, device_data);

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *queue->get_immediate_command_list()->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Present;
            trace_draw_call_data.command_list = native_device_context;
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif // DEVELOPMENT

      cb_luma_global_settings.FrameIndex++;
      device_data.cb_luma_global_settings_dirty = true;
   }

   //TODOFT3: merge all the shader permutations that use the same code in Prey (and then move shader binaries to bin folder? Add shader files to VS project?)

   // Return false to prevent the original draw call from running (e.g. if you replaced it or just want to skip it)
   // Most games (e.g. Prey, Dishonored 2) always draw in direct mode (as opposed to indirect), but uses different command lists on different threads (e.g. on Prey, that's almost only used for the shadow projection maps, in Dishonored 2, for almost every separate pass).
   // Usually there's a few compute shaders but most passes are "classic" pixel shaders.
   bool OnDrawOrDispatch_Custom(reshade::api::command_list* cmd_list, bool is_dispatch /*= false*/, std::function<void()>* original_draw_dispatch_func = nullptr)
   {
      auto* device = cmd_list->get_device();
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      DeviceData& device_data = *device->get_private_data<DeviceData>();

      // Only for custom shaders
      reshade::api::shader_stage stages = reshade::api::shader_stage(0); // None

      bool is_custom_pass = false;
      bool updated_cbuffers = false;

      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();

      const auto& original_shader_hashes = is_dispatch ? cmd_list_data.pipeline_state_original_compute_shader_hashes : cmd_list_data.pipeline_state_original_graphics_shader_hashes;

      if (is_dispatch)
      {
         is_custom_pass = cmd_list_data.pipeline_state_has_custom_compute_shader;
         if (original_shader_hashes.HasAny(reshade::api::shader_stage::compute))
         {
            stages = reshade::api::shader_stage::compute;
         }
      }
      else
      {
         is_custom_pass = cmd_list_data.pipeline_state_has_custom_graphics_shader;
         if (original_shader_hashes.HasAny(reshade::api::shader_stage::vertex))
         {
            stages = reshade::api::shader_stage::vertex;
         }
         if (original_shader_hashes.HasAny(reshade::api::shader_stage::pixel))
         {
            stages |= reshade::api::shader_stage::pixel;
         }
      }

#if DEVELOPMENT
      if (!cmd_list_data.is_primary)
      {
         // If these cases ever triggered, we can either cache assign all the commands for the deferred context without actually applying them, and then actually build the deferred context when it's merged, given that then we'd know the pipeline state it will inherit from the immediate context (e.g. whatever Render Targets or Shaders were set).
         // One partial solution is to replace shaders when they are created at binary level, instead of live swapping them, but we still won't be able to reliably write mod behaviours on the pipeline state given we don't know what the deferred context will inherit yet (until it's merged).
         if (is_dispatch)
         {
            if (!cmd_list_data.any_dispatch_done)
            {
               ASSERT_ONCE_MSG(cmd_list_data.pipeline_state_original_compute_shader_hashes.HasAny(reshade::api::shader_stage::compute),
                  "A dispatch was triggered on a fresh deferred device context without previously setting a compute shader, it could be that the engine relies on whatever pipeline state will be set on the immediate device context at the time of joining the deferred context");

               bool any_uav = false;
               com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
               native_device_context->CSGetUnorderedAccessViews(0, device_data.uav_max_count, &uavs[0]);
               for (UINT i = 0; i < device_data.uav_max_count; i++)
               {
                  if (uavs[i] != nullptr)
                  {
                     any_uav = true;
                     break;
                  }
               }
               ASSERT_ONCE_MSG(any_uav, "A dispatch was triggered on a fresh deferred device context without any UAVs bound, that is suspicious and might be a hint that the engine relies on inheriting the immediate device context state at the time of joining (later)");
            }
         }
         else
         {
            if (!cmd_list_data.any_dispatch_done)
            {
               ASSERT_ONCE_MSG(cmd_list_data.pipeline_state_original_graphics_shader_hashes.HasAny(reshade::api::shader_stage::pixel) || cmd_list_data.pipeline_state_original_graphics_shader_hashes.HasAny(reshade::api::shader_stage::vertex),
                  "A draw was triggered on a fresh deferred device context without previously setting a vertex/pixel shader, it could be that the engine relies on whatever pipeline state will be set on the immediate device context at the time of joining the deferred context");

               bool any_rtv_or_dsv_or_uav = false;
               com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
               com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
               com_ptr<ID3D11DepthStencilView> dsv;
               native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv, 0, device_data.uav_max_count, &uavs[0]);
               for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
               {
                  if (rtvs[i] != nullptr)
                  {
                     any_rtv_or_dsv_or_uav = true;
                     break;
                  }
               }
               if (dsv != nullptr)
               {
                  any_rtv_or_dsv_or_uav = true;
               }
               for (UINT i = 0; i < (any_rtv_or_dsv_or_uav ? 0 : device_data.uav_max_count); i++)
               {
                  if (uavs[i] != nullptr)
                  {
                     any_rtv_or_dsv_or_uav = true;
                     break;
                  }
               }
               ASSERT_ONCE_MSG(any_rtv_or_dsv_or_uav, "A draw was triggered on a fresh deferred device context without any RTVs/DSV/UAVs bound, that is suspicious and might be a hint that the engine relies on inheriting the immediate device context state at the time of joining (later)");
            }
         }
      }

      if (is_dispatch)
      {
         last_drawn_shader = cmd_list_data.pipeline_state_original_compute_shader_hashes.HasAny(reshade::api::shader_stage::compute) ? Shader::Hash_NumToStr(*cmd_list_data.pipeline_state_original_compute_shader_hashes.compute_shaders.begin()) : ""; // String hash to int
         cmd_list_data.any_dispatch_done = true;
      }
      else
      {
         last_drawn_shader = cmd_list_data.pipeline_state_original_graphics_shader_hashes.HasAny(reshade::api::shader_stage::pixel) ? Shader::Hash_NumToStr(*cmd_list_data.pipeline_state_original_graphics_shader_hashes.pixel_shaders.begin()) : ""; // String hash to int
         cmd_list_data.any_draw_done = true;
      }
      thread_local_cmd_list = cmd_list;

      {
         // Do this before any custom code runs as the state might change
         const std::shared_lock lock_trace(s_mutex_trace); // TODO: it's not safe to lock global mutexes that might have already been locked by other concurrent functions, while also calling functions in the device or primary device context (in DX11) (because they have their own locks inside that might cause deadlocks!). The solution here would be to cache the data upfront and then lock a mutex to add it to our array. This might be safe if the game threading was already safe though, I'm not 100% sure.
         if (trace_running)
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
            const std::shared_lock lock_device(device_data.mutex);
            ASSERT_ONCE(native_device_context);
            if (is_dispatch)
            {
               AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_compute_shader.handle, shader_cache, input_layouts_descs, last_draw_dispatch_data, device_data.original_resource_views_to_mirrored_upgraded_resource_views);
            }
            else
            {
               AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_vertex_shader.handle, shader_cache, input_layouts_descs, last_draw_dispatch_data, device_data.original_resource_views_to_mirrored_upgraded_resource_views);
               if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0) // Somehow this can happen (e.g. query tests don't require pixel shaders)
               {
                  AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_pixel_shader.handle, shader_cache, input_layouts_descs, last_draw_dispatch_data, device_data.original_resource_views_to_mirrored_upgraded_resource_views);
               }
            }
         }
      }
#endif //DEVELOPMENT

      const bool mod_active = IsModActive(device_data);

      if (enable_ui_separation && mod_active)
      {
         ID3D11RenderTargetView* const ui_texture_rtv_const = device_data.ui_texture_rtv.get();
         // We can either provide an include list, of all the UI shaders (we check if the render target matches below),
         // or an exclude list, of all the scene post processing shaders, and then manually setting "has_drawn_main_post_processing" somewhere in your game's code (and exclude any non UI shader that possibly runs after it).
         // If the main post processing shaders didn't run, it means the scene isn't rendering, or showing anyway, so we don't need to separate the UI,
         // as it'd likely already draw correctly on the swapchain or whatever is its render target.
         // 
         // We expect the UI to draw on the immediate context, as it does in most games, if not, handle the render targets yourself for the custom case.
         if ((device_data.has_drawn_main_post_processing && native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE) || original_shader_hashes.Contains(shader_hashes_UI))
         {
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            if (rtv && !original_shader_hashes.Contains(shader_hashes_UI_excluded))
            {
               bool do_ui = false;

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv->GetDesc(&rtv_desc);

               // Usually the swapchain, if the UI was already drawn on the side, we wouldn't really need to change the render target manually (we could just upgrade that texture format, and replace the shader that composes the UI if it wasn't done properly),
               // however some games compose the UI on a render target that contains the UI, but that isn't the swapchain yet, and then later copy it on the swapchain (possibly in a shader pass that does gamma/brightness adjustments, which we might as well skip on for the UI in HDR anyway, given we'd want them to be neutral).
               bool targeting_swapchain = false;
               if (!device_data.ui_initial_original_rtv)
               {
                  const std::shared_lock lock(device_data.mutex);
                  targeting_swapchain = device_data.back_buffers.contains((uint64_t)rtv.get());
               }
               if (targeting_swapchain || (AreViewsOfSameResource(rtv.get(), device_data.ui_initial_original_rtv.get()) && rtv_desc.Texture2D.MipSlice == 0)) // Make sure it was writing to the base mip (just in case the game did weird stuff)
               {
                  device_data.ui_latest_original_rtv = rtv;

                  // Render target was set to the UI one by the game, whether at the beginning of UI drawing, or again inside of it (sometimes UI draws stuff on the side to compose its textures).
                  // Note: for now we don't restore this back to the original value, as we haven't found any game that reads back the RT, or doesn't set it every frame or draw call.
                  // Most of the times rendering the original UI on a black (0 0 0 0 cleared) render target preserves the original blends look identically.
                  native_device_context->OMSetRenderTargets(1, &ui_texture_rtv_const, nullptr);
                  do_ui = true;

#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     AddCustomTraceDrawCallData(cmd_list_data.trace_draw_calls_data, native_device_context, "Redirect UI", rtv.get(), true);
                  }
#endif
               }
               else if (rtv == ui_texture_rtv_const)
               {
                  // Render target was already previously changed by us, and the game hasn't changed it back
                  do_ui = true;
               }

               if (do_ui)
               {
                  // UI render target has been effectively replaced, any extra code can be put here
               }
            }
            // if any of the excluded shaders drew after or in between UI shaders, and the game was originally using the same RTV for all and didn't re-apply the RTV for every draw call,
            // swap back the last rtv the game set on UI shaders. This can happen for games that have a final swapchain copy shader etc.
            else if (rtv && rtv == ui_texture_rtv_const)
            {
               native_device_context->OMSetRenderTargets(1, &device_data.ui_latest_original_rtv, nullptr);
               device_data.ui_latest_original_rtv = nullptr;
            }
         }
      }

      const bool had_drawn_main_post_processing = device_data.has_drawn_main_post_processing;

      DrawOrDispatchOverrideType draw_or_dispatch_override_type = DrawOrDispatchOverrideType::None;
      if (!original_shader_hashes.Empty())
      {
         if (texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled) // Creation here still needs to go through independently of "ignore_indirect_upgraded_textures"
         {
            const auto pixel_shader_hashes = auto_texture_format_upgrade_shader_hashes.find(is_dispatch ? original_shader_hashes.compute_shaders[0] : original_shader_hashes.pixel_shaders[0]); // This data is meant to be immutable
            if (pixel_shader_hashes != auto_texture_format_upgrade_shader_hashes.end())
            {
               com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
               com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
               com_ptr<ID3D11DepthStencilView> dsv;
               bool any_changed = false;
               std::shared_lock lock_device_read(device_data.mutex);
               if (!is_dispatch)
               {
                  native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv, 0, device_data.uav_max_count, &uavs[0]);
                  for (UINT i = 0; i < pixel_shader_hashes->second.first.size() && i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                  {
                     if (rtvs[pixel_shader_hashes->second.first[i]] != nullptr)
                     {
                        const uint64_t prev_resource_view = reinterpret_cast<uint64_t>(rtvs[pixel_shader_hashes->second.first[i]].get());
                        const uint64_t prev_resource = device->get_resource_from_view({ prev_resource_view }).handle;
                        uint64_t resource = prev_resource;
                        if (FindOrCreateIndirectUpgradedResource(device, 0, prev_resource, resource, device_data, true, reshade::api::resource_usage::render_target, lock_device_read) && resource != prev_resource)
                        {
                           uint64_t resource_view = prev_resource_view;
                           if (FindOrCreateIndirectUpgradedResourceView(device, prev_resource_view, resource_view, device_data, true, reshade::api::resource_usage::render_target, lock_device_read))
                           {
                              rtvs[pixel_shader_hashes->second.second[i]] = reinterpret_cast<ID3D11RenderTargetView*>(resource_view);
                              any_changed = true;
                           }
                        }
                     }
                  }
               }
               else
               {
                  native_device_context->CSGetUnorderedAccessViews(0, device_data.uav_max_count, &uavs[0]);
               }
               for (UINT i = 0; i < pixel_shader_hashes->second.second.size() && i < device_data.uav_max_count; i++)
               {
                  if (uavs[pixel_shader_hashes->second.second[i]] != nullptr)
                  {
                     const uint64_t prev_resource_view = reinterpret_cast<uint64_t>(rtvs[pixel_shader_hashes->second.second[i]].get());
                     const uint64_t prev_resource = device->get_resource_from_view({prev_resource_view}).handle;
                     uint64_t resource = prev_resource;
                     if (FindOrCreateIndirectUpgradedResource(device, 0, prev_resource, resource, device_data, true, reshade::api::resource_usage::unordered_access, lock_device_read) && resource != prev_resource)
                     {
                        uint64_t resource_view = prev_resource_view;
                        if (FindOrCreateIndirectUpgradedResourceView(device, prev_resource_view, resource_view, device_data, true, reshade::api::resource_usage::unordered_access, lock_device_read))
                        {
                           uavs[pixel_shader_hashes->second.second[i]] = reinterpret_cast<ID3D11UnorderedAccessView*>(resource_view);
                           any_changed = true;
                        }
                     }
                  }
               }
               if (any_changed)
               {
                  ID3D11UnorderedAccessView* const* uavs_const = (ID3D11UnorderedAccessView**)std::addressof(uavs[0]);
                  if (!is_dispatch)
                  {
                     UINT valid_render_target_views_bound = 0;
                     for (size_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                     {
                        if (rtvs[i].get() == nullptr)
                        {
                           valid_render_target_views_bound++;
                        }
                     }

                     ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(rtvs[0]);
                     native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(valid_render_target_views_bound, rtvs_const, dsv.get(), valid_render_target_views_bound, device_data.uav_max_count - valid_render_target_views_bound, uavs_const, nullptr);
                  }
                  else
                  {
                     native_device_context->CSSetUnorderedAccessViews(0, device_data.uav_max_count, uavs_const, nullptr);
                  }
               }
            }
         }

#if DEVELOPMENT && !defined(NDEBUG)
         // Test whether our cbuffers persistent and polluted buffers that used the same cbuffer slot in shaders we don't replace later on (e.g. Thumper uses all DX11 14 slots, and doesn't necessarily re-apply them every frame).
         const CachedShader* cached_shader = nullptr;
         com_ptr<ID3D11Buffer> constant_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
         // Only do the most commonly replaced shader types (DX11 design)
         if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
         {
            if (original_shader_hashes.HasAny(reshade::api::shader_stage::pixel))
            {
               if (auto shader_cache_it = shader_cache.find(original_shader_hashes.pixel_shaders[0]); shader_cache_it != shader_cache.end())
                  cached_shader = shader_cache_it->second;
            }
            native_device_context->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &constant_buffers[0]);
         }
         else if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
         {
            if (original_shader_hashes.HasAny(reshade::api::shader_stage::compute))
            {
               if (auto shader_cache_it = shader_cache.find(original_shader_hashes.compute_shaders[0]); shader_cache_it != shader_cache.end())
                  cached_shader = shader_cache_it->second;
            }
            native_device_context->CSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &constant_buffers[0]);
         }
         if (cached_shader)
         {
            // Only check against cbuffers we found to be actively read by this shader
            if (luma_settings_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT && cached_shader->cbs[luma_settings_cbuffer_index])
            {
               ASSERT_ONCE(constant_buffers[luma_settings_cbuffer_index] == nullptr || constant_buffers[luma_settings_cbuffer_index] != device_data.luma_global_settings.get());
            }
            if (luma_data_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT && cached_shader->cbs[luma_data_cbuffer_index])
            {
               ASSERT_ONCE(constant_buffers[luma_data_cbuffer_index] == nullptr || constant_buffers[luma_data_cbuffer_index] != device_data.luma_instance_data.get());
            }
         }
#endif

         //TODOFT: optimize these shader searches by simply marking "CachedPipeline" with a tag on what they are (and whether they have a particular role) (also we can restrict the search to pixel shaders or compute shaders?) upfront. And move these into their own functions. Update: we optimized this enough.

         if (test_index == 9) return false;
         draw_or_dispatch_override_type = game->OnDrawOrDispatch(native_device, native_device_context, cmd_list_data, device_data, stages, original_shader_hashes, is_custom_pass, updated_cbuffers, original_draw_dispatch_func);
         if (draw_or_dispatch_override_type != DrawOrDispatchOverrideType::None)
         {
            // The pass was cancelled, there's no point in doing anything more
            return true;
         }
      }

      // We have a way to track whether this data changed to avoid sending them again when not necessary, we could further optimize it by adding a flag to the shader hashes that need the cbuffers, but it really wouldn't help much
      if (is_custom_pass && !updated_cbuffers)
      {
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
         updated_cbuffers = true;
      }

#if !DEVELOPMENT || !GAME_PREY //TODOFT2: re-enable once we are sure we replaced all the post tonemap shaders and we are done debugging the blend states. Compute Shaders are also never used in UI and by all stuff below...!!!
      if (!is_custom_pass) return false;
#else // ("GAME_PREY") We can't do any further checks in this case because some UI draws at the beginning of the frame (in world computers, in Prey), and sometimes the scene doesn't draw, but we still need to update the cbuffers (though maybe we could clear it up on present, to avoid problems)
      //if (device_data.has_drawn_main_post_processing_previous && !device_data.has_drawn_main_post_processing) return false;
#endif // !DEVELOPMENT

      // Skip the rest in cases where the UI isn't passing through our custom linear blends that emulate SDR gamma->gamma blends.
      if (GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) != 1) return false;

      CB::LumaUIDataPadded ui_data = {};

      com_ptr<ID3D11RenderTargetView> render_target_view;
      // Theoretically we should also retrieve the UAVs and all other RTs but in reality it doesn't ever really matter
      native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);
      if (render_target_view)
      {
         com_ptr<ID3D11Resource> render_target_resource;
         render_target_view->GetResource(&render_target_resource);
         if (render_target_resource != nullptr)
         {
            bool targeting_swapchain;
            {
               const std::shared_lock lock(device_data.mutex);
               targeting_swapchain = device_data.back_buffers.contains((uint64_t)render_target_resource.get());
            }
            // We check across all the swap chain back buffers, not just the one that will be presented this frame (we have no 100% of what swapchain will draw this frame).
            // Note that some games (e.g. Prey) compose the UI in separate render targets to then be drawn into the world (e.g. in game interactive computer screens), but these usually don't draw on any of the swapchain buffers.
            if (targeting_swapchain)
            {
               ui_data.targeting_swapchain = 1;

               const bool paused = game->IsGamePaused(device_data);
               // Highlight that the game is paused or that we are in a menu with no scene rendering (e.g. allows us to fully skip lens distortion on the UI, as sometimes it'd apply in loading screen menus).
               if (paused || !device_data.has_drawn_main_post_processing)
               {
                  ui_data.fullscreen_menu = 1;
               }
            }
            render_target_resource = nullptr;
         }
         render_target_view = nullptr;
      }

      // No need to lock "s_mutex_reshade" for "cb_luma_global_settings" here, it's not relevant
      // We could use "has_drawn_composed_gbuffers" here instead of "has_drawn_main_post_processing", but then again, they should always match (pp should always be run)
      ui_data.background_tonemapping_amount = (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR && device_data.has_drawn_main_post_processing_previous && ui_data.targeting_swapchain) ? game->GetTonemapUIBackgroundAmount(device_data) : 0.0;

      com_ptr<ID3D11BlendState> blend_state;
      native_device_context->OMGetBlendState(&blend_state, nullptr, nullptr);
      if (blend_state)
      {
         D3D11_BLEND_DESC blend_desc;
         blend_state->GetDesc(&blend_desc);
         // Mirrored from UI shaders:
         // 0 No alpha blend (or other unknown blend types that we can ignore)
         // 1 Straight alpha blend: "result = (source.RGB * source.A) + (dest.RGB * (1 - source.A))" or "result = lerp(dest.RGB, source.RGB, source.A)"
         // 2 Pre-multiplied alpha blend (alpha is also pre-multiplied, not just rgb): "result = source.RGB + (dest.RGB * (1 - source.A))"
         // 3 Additive alpha blend (source is "Straight alpha" while destination is retained at 100%): "result = (source.RGB * source.A) + dest.RGB"
         // 4 Additive blend (source and destination are simply summed up, ignoring the alpha): result = source.RGB + dest.RGB
         // 
         // We don't care for the alpha blend operation (source alpha * dest alpha) as alpha is never read back from destination
         if (blend_desc.RenderTarget[0].BlendEnable
            && blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
         {
            // Do both the "straight alpha" and "pre-multiplied alpha" cases
            if ((blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA || blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE)
               && (blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA || blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE))
            {
               if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
               {
                  ui_data.blend_mode = 4;
               }
               else if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
               {
                  ui_data.blend_mode = 3;
               }
               else if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
               {
                  ui_data.blend_mode = 2;
               }
               else /*if (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)*/
               {
                  ui_data.blend_mode = 1;
#if GAME_PREY
                  assert(!had_drawn_main_post_processing || !ui_data.targeting_swapchain || (blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA));
#endif // GAME_PREY
               }
#if GAME_PREY
               //if (ui_data.blend_mode == 1 || ui_data.blend_mode == 3) // Old check
               {
                  // In "blend_mode == 1", Prey seems to erroneously use "D3D11_BLEND::D3D11_BLEND_SRC_ALPHA" as source blend alpha, thus it multiplies alpha by itself when using pre-multiplied alpha passes,
                  // which doesn't seem to make much sense, at least not for the first write on a separate new texture (it means that the next blend with the final target background could end up going beyond 1 because the background darkening intensity is lower than it should be).
                  ASSERT_ONCE(!had_drawn_main_post_processing || (ui_data.targeting_swapchain ?
                     // Make sure we never read back from the swap chain texture (which means we can ignore all the alpha blend ops on previous to it)
                     (blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND::D3D11_BLEND_DEST_ALPHA
                        && blend_desc.RenderTarget[0].DestBlend != D3D11_BLEND::D3D11_BLEND_DEST_ALPHA
                        && blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND::D3D11_BLEND_DEST_COLOR
                        && blend_desc.RenderTarget[0].DestBlend != D3D11_BLEND::D3D11_BLEND_DEST_COLOR)
                     // Make sure that writes to separate textures always use known alpha blends modes, because we'll be reading back that alpha for later (possibly)
                     : (blend_desc.RenderTarget[0].BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD
                        && (blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA
                           || blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE))));
               }
#endif // GAME_PREY
            }
            else
            {
               ASSERT_ONCE(!had_drawn_main_post_processing || !ui_data.targeting_swapchain);
            }
         }
         assert(!had_drawn_main_post_processing || !ui_data.targeting_swapchain || !blend_desc.RenderTarget[0].BlendEnable || blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD);
         blend_state = nullptr;
      }

      if (is_custom_pass && luma_ui_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
      {
         if (memcmp(&device_data.cb_luma_ui_data, &ui_data, sizeof(ui_data)) != 0)
         {
            device_data.cb_luma_ui_data = ui_data;
            if (D3D11_MAPPED_SUBRESOURCE mapped_buffer;
               SUCCEEDED(native_device_context->Map(device_data.luma_ui_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer)))
            {
               std::memcpy(mapped_buffer.pData, &device_data.cb_luma_ui_data, sizeof(device_data.cb_luma_ui_data));
               native_device_context->Unmap(device_data.luma_ui_data.get(), 0);
            }
         }

         ID3D11Buffer* const buffer = device_data.luma_ui_data.get();
         if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
            native_device_context->VSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
            native_device_context->GSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
            native_device_context->PSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
         if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
            native_device_context->CSSetConstantBuffers(luma_ui_cbuffer_index, 1, &buffer);
      }

      return false; // Return true to cancel this draw call
   }

#if DEVELOPMENT
   // For texture debugging
   bool HandlePipelineRedirections(ID3D11DeviceContext* native_device_context, const DeviceData& device_data, const CommandListData& cmd_list_data, bool is_dispatch, std::function<void()>& draw_func)
   {
      CachedPipeline::RedirectData redirect_data;
      if (is_dispatch)
      {
         const std::shared_lock lock(s_mutex_generic);
         const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_compute_shader.handle);
         if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
         {
            redirect_data = pipeline_pair->second->redirect_data;
         }
      }
      else
      {
         const std::shared_lock lock(s_mutex_generic);
         const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_pixel_shader.handle);
         if (pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
         {
            redirect_data = pipeline_pair->second->redirect_data;
         }
      }

      if (redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && redirect_data.target_type != CachedPipeline::RedirectData::RedirectTargetType::None)
      {
			com_ptr<ID3D11Resource> source_resource;
         com_ptr<ID3D11Resource> target_resource;

         switch (redirect_data.source_type)
         {
         case CachedPipeline::RedirectData::RedirectSourceType::SRV:
         {
            if (redirect_data.source_index >= 0 && redirect_data.source_index < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)
            {
               com_ptr<ID3D11ShaderResourceView> srv;
               if (is_dispatch)
                  native_device_context->CSGetShaderResources(redirect_data.source_index, 1, &srv);
               else
                  native_device_context->PSGetShaderResources(redirect_data.source_index, 1, &srv);
               if (srv)
                  srv->GetResource(&source_resource);
            }
         }
         break;
         case CachedPipeline::RedirectData::RedirectSourceType::UAV:
         {
            com_ptr<ID3D11UnorderedAccessView> uav;
            if (is_dispatch)
            {
               if (redirect_data.source_index >= 0 && redirect_data.source_index < device_data.uav_max_count)
                  native_device_context->CSGetUnorderedAccessViews(redirect_data.source_index, 1, &uav);
            }
            else
            {
               if (redirect_data.source_index >= 0 && redirect_data.source_index < device_data.uav_max_count)
                  native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, redirect_data.source_index, 1, &uav);
            }
            if (uav)
               uav->GetResource(&source_resource);
         }
         break;
         }

         switch (redirect_data.target_type)
         {
         case CachedPipeline::RedirectData::RedirectTargetType::RTV:
         {
            if (redirect_data.target_index >= 0 && redirect_data.target_index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !is_dispatch)
            {
               com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
               native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], nullptr);
               if (rtvs[redirect_data.target_index])
                  rtvs[redirect_data.target_index]->GetResource(&target_resource);
            }
         }
         break;
         case CachedPipeline::RedirectData::RedirectTargetType::UAV:
         {
            com_ptr<ID3D11UnorderedAccessView> uav;
            if (is_dispatch)
            {
               if (redirect_data.target_index >= 0 && redirect_data.target_index < device_data.uav_max_count)
                  native_device_context->CSGetUnorderedAccessViews(redirect_data.target_index, 1, &uav);
            }
            else
            {
               if (redirect_data.target_index >= 0 && redirect_data.target_index < device_data.uav_max_count)
                  native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, redirect_data.target_index, 1, &uav);
            }
            if (uav)
               uav->GetResource(&target_resource);
         }
         break;
         }

         // TODO: fall back to pixel shader if the formats/sizes are not compatible? Otherwise add a safety check to avoid crashing (it doesn't seem to)
         if (source_resource.get() && target_resource.get() && source_resource.get() != target_resource.get())
         {
#if 0 // We don't actually need to force run the original draw call
            draw_func();
#endif
            native_device_context->CopyResource(target_resource.get(), source_resource.get());
            return true; // Make sure the original draw call is cancelled, otherwise the target resource would get overwritten
         }
      }

      return false;
   }
#endif //DEVELOPMENT

   bool OnDraw(
      reshade::api::command_list* cmd_list,
      uint32_t vertex_count,
      uint32_t instance_count,
      uint32_t first_vertex,
      uint32_t first_instance)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      std::function<void()>* original_draw_dispatch_func = nullptr;

#if DEVELOPMENT || ENABLE_POST_DRAW_CALLBACK
      std::function<void()> draw_lambda = [&]()
      {
         if (instance_count > 1)
         {
            native_device_context->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
         }
         else
         {
            ASSERT_ONCE(first_instance == 0);
            native_device_context->Draw(vertex_count, first_vertex);
         }
      };
      original_draw_dispatch_func = &draw_lambda;
#endif

#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle);
      wants_debug_draw &= (debug_draw_shader_hash == 0 || cmd_list_data.pipeline_state_original_graphics_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::pixel)) && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id());

      com_ptr<ID3D11DepthStencilState> original_depth_stencil_state;
      UINT original_stencil_ref = 0;
      // We do this here and not in the depth state set as it's just much easier to keep track of the state changes, given that here they are wrapped around a draw call
      if (cmd_list_data.temp_custom_depth_stencil != ShaderCustomDepthStencilType::None)
      {
         native_device_context->OMGetDepthStencilState(&original_depth_stencil_state, &original_stencil_ref);

         native_device_context->OMSetDepthStencilState((cmd_list_data.temp_custom_depth_stencil == ShaderCustomDepthStencilType::IgnoreTestWriteDepth_IgnoreStencil) ? device_data.default_depth_stencil_state.get() : device_data.depth_test_false_write_true_stencil_false_state.get(), 0);
      }

      DrawStateStack pre_draw_state_stack;
      if (wants_debug_draw)
      {
         if (debug_draw_freeze_inputs)
         {
            // The cached (frozen) state is empty, cache it, and then duplicate the resources
            if (!device_data.debug_draw_frozen_draw_state_stack)
            {
               device_data.debug_draw_frozen_draw_state_stack = std::make_shared<DrawStateStack<DrawStateStackType::FullGraphics>>();
               // TODO: do this properly... we had header include problems. Also this needs to be swapped to the compute type of compute shaders, and graphics for pixel (we need to refresh it if the type was wrong).
               ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Cache(native_device_context, device_data.uav_max_count);
               ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Clone(native_device_context, device_data.GetLumaCBuffers());
            }
            // Restore the new or cached cloned state
            // We keep the original RTVs, UAVs and shaders otherwise live editing of custom shaders won't work.
            // Making the draw on a non frozen UAV could cause iterative passes to not 100% freeze, but if ever needed, we could add a flag for that too (and e.g. copy back their value from the cached clone).
            ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Restore(native_device_context, false, false);
         }

         pre_draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
      }

      // TODO: it'd be nicer to do this better.
      last_draw_dispatch_data = {};
      last_draw_dispatch_data.vertex_count = vertex_count;
      last_draw_dispatch_data.instance_count = instance_count;
      last_draw_dispatch_data.first_vertex = first_vertex;
      last_draw_dispatch_data.first_instance = first_instance;
#endif
      // TODO: add performance tracing around these
      bool cancelled_or_replaced = OnDrawOrDispatch_Custom(cmd_list, false, original_draw_dispatch_func);
#if DEVELOPMENT
#if 0 // TODO: We should do this manually when replacing each draw call, we don't know if it was replaced or cancelled here
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (cancelled_or_replaced && trace_running)
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0)
            {
               cmd_list_data.trace_draw_calls_data[cmd_list_data.trace_draw_calls_data.size() - 1].skipped = true;
            }
         }
      }
#endif

      // First run the draw call (don't delegate it to ReShade) and then copy its output
      if (wants_debug_draw)
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack post_draw_state_stack;

            if (!cancelled_or_replaced)
            {
               draw_lambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               post_draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               pre_draw_state_stack.Restore(native_device_context);
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, false);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               post_draw_state_stack.Restore(native_device_context);
            }
            cancelled_or_replaced = true;
         }
      }
      bool track_buffer_pipeline_ps = track_buffer_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle;
      bool track_buffer_pipeline_vs = track_buffer_pipeline == cmd_list_data.pipeline_state_original_vertex_shader.handle;
      if (track_buffer_pipeline != 0 && (track_buffer_pipeline_ps || track_buffer_pipeline_vs))
      {
         auto local_track_buffer_pipeline_instance = track_buffer_pipeline_instance.fetch_add(1);
         // TODO: make "track_buffer_pipeline_target_instance" by thread (like "debug_draw_pipeline_target_instance"), though it's rarely useful
         if (track_buffer_pipeline_target_instance == -1 || local_track_buffer_pipeline_instance == track_buffer_pipeline_target_instance)
         {
            com_ptr<ID3D11Buffer> cb;
            if (track_buffer_pipeline_ps)
            {
               native_device_context->PSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            else if (track_buffer_pipeline_vs)
            {
               native_device_context->VSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            // Copy the buffer in our data
            CopyBuffer(cb, native_device_context, device_data.track_buffer_data.data);
            device_data.track_buffer_data.hash = std::to_string(std::hash<void*>{}(cb.get()));
         }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         draw_lambda = []() {};
      }

      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, false, draw_lambda);

      // Restore the state
      if (cmd_list_data.temp_custom_depth_stencil != ShaderCustomDepthStencilType::None)
      {
         if (!cancelled_or_replaced)
         {
            draw_lambda();
            cancelled_or_replaced = true;
         }
         native_device_context->OMSetDepthStencilState(original_depth_stencil_state.get(), original_stencil_ref);
      }
#endif

#if ENABLE_POST_DRAW_CALLBACK
      if (!cancelled_or_replaced)
      {
         draw_lambda();
         cancelled_or_replaced = true;
         OnDrawOrDispatch_Custom(cmd_list, false);
      }
#endif

      return cancelled_or_replaced;
   }

   bool OnDrawIndexed(
      reshade::api::command_list* cmd_list,
      uint32_t index_count,
      uint32_t instance_count,
      uint32_t first_index,
      int32_t vertex_offset,
      uint32_t first_instance)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      std::function<void()>* original_draw_dispatch_func = nullptr;

#if DEVELOPMENT || ENABLE_POST_DRAW_CALLBACK
      std::function<void()> draw_lambda = [&]()
      {
         if (instance_count > 1)
         {
            native_device_context->DrawIndexedInstanced(index_count, instance_count, first_index, vertex_offset, first_instance);
         }
         else
         {
            ASSERT_ONCE(first_instance == 0);
            native_device_context->DrawIndexed(index_count, first_index, vertex_offset);
         }
      };
      original_draw_dispatch_func = &draw_lambda;
#endif

#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle);
      wants_debug_draw &= (debug_draw_shader_hash == 0 || cmd_list_data.pipeline_state_original_graphics_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::pixel)) && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id());

      com_ptr<ID3D11DepthStencilState> original_depth_stencil_state;
      UINT original_stencil_ref = 0;
      if (cmd_list_data.temp_custom_depth_stencil != ShaderCustomDepthStencilType::None)
      {
         native_device_context->OMGetDepthStencilState(&original_depth_stencil_state, &original_stencil_ref);

         native_device_context->OMSetDepthStencilState((cmd_list_data.temp_custom_depth_stencil == ShaderCustomDepthStencilType::IgnoreTestWriteDepth_IgnoreStencil) ? device_data.default_depth_stencil_state.get() : device_data.depth_test_false_write_true_stencil_false_state.get(), 0);
      }

      DrawStateStack pre_draw_state_stack;
      if (wants_debug_draw)
      {
         if (debug_draw_freeze_inputs)
         {
            if (!device_data.debug_draw_frozen_draw_state_stack)
            {
               device_data.debug_draw_frozen_draw_state_stack = std::make_shared<DrawStateStack<DrawStateStackType::FullGraphics>>();
               ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Cache(native_device_context, device_data.uav_max_count);
               ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Clone(native_device_context, device_data.GetLumaCBuffers());
            }
            ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Restore(native_device_context, false, false);
         }

         pre_draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
      }

      last_draw_dispatch_data = {};
      last_draw_dispatch_data.instance_count = instance_count;
      last_draw_dispatch_data.first_instance = first_instance;

      last_draw_dispatch_data.vertex_offset = vertex_offset;
      last_draw_dispatch_data.first_index = first_index;
      last_draw_dispatch_data.index_count = index_count;

      last_draw_dispatch_data.indexed = true;
#endif
      bool cancelled_or_replaced = OnDrawOrDispatch_Custom(cmd_list, false, original_draw_dispatch_func);
#if DEVELOPMENT
      // First run the draw call (don't delegate it to ReShade) and then copy its output
      if (wants_debug_draw)
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack post_draw_state_stack;

            if (!cancelled_or_replaced)
            {
               draw_lambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               post_draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               pre_draw_state_stack.Restore(native_device_context);
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, false);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               post_draw_state_stack.Restore(native_device_context);
            }
            cancelled_or_replaced = true;
         }
      }
      bool track_buffer_pipeline_ps = track_buffer_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle;
      bool track_buffer_pipeline_vs = track_buffer_pipeline == cmd_list_data.pipeline_state_original_vertex_shader.handle;
      if (track_buffer_pipeline != 0 && (track_buffer_pipeline_ps || track_buffer_pipeline_vs))
      {
         auto local_track_buffer_pipeline_instance = track_buffer_pipeline_instance.fetch_add(1);
         if (track_buffer_pipeline_target_instance == -1 || local_track_buffer_pipeline_instance == track_buffer_pipeline_target_instance)
         {
            com_ptr<ID3D11Buffer> cb;
            if (track_buffer_pipeline_ps)
            {
               native_device_context->PSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            else if (track_buffer_pipeline_vs)
            {
               native_device_context->VSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            // Copy the buffer in our data
            CopyBuffer(cb, native_device_context, device_data.track_buffer_data.data);
            device_data.track_buffer_data.hash = std::to_string(std::hash<void*>{}(cb.get()));
         }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         draw_lambda = []() {};
      }

      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, false, draw_lambda);

      // Restore the state
      if (cmd_list_data.temp_custom_depth_stencil != ShaderCustomDepthStencilType::None)
      {
         if (!cancelled_or_replaced)
         {
            draw_lambda();
            cancelled_or_replaced = true;
         }
         native_device_context->OMSetDepthStencilState(original_depth_stencil_state.get(), original_stencil_ref);
      }
#endif
      return cancelled_or_replaced;
   }

   bool OnDispatch(reshade::api::command_list* cmd_list, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      std::function<void()>* original_draw_dispatch_func = nullptr;

#if DEVELOPMENT || ENABLE_POST_DRAW_CALLBACK
      std::function<void()> draw_lambda = [&]()
      {
         native_device_context->Dispatch(group_count_x, group_count_y, group_count_z);
      };
      original_draw_dispatch_func = &draw_lambda;
#endif

#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= (debug_draw_pipeline == 0 || debug_draw_pipeline == cmd_list_data.pipeline_state_original_compute_shader.handle);
      wants_debug_draw &= (debug_draw_shader_hash == 0 || cmd_list_data.pipeline_state_original_compute_shader_hashes.Contains(debug_draw_shader_hash, reshade::api::shader_stage::compute)) && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id());

      DrawStateStack<DrawStateStackType::Compute> pre_draw_state_stack;
      if (wants_debug_draw)
      {
         if (debug_draw_freeze_inputs)
         {
            if (!device_data.debug_draw_frozen_draw_state_stack)
            {
               device_data.debug_draw_frozen_draw_state_stack = std::make_shared<DrawStateStack<DrawStateStackType::Compute>>();
               ((DrawStateStack<DrawStateStackType::Compute>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Cache(native_device_context, device_data.uav_max_count);
               ((DrawStateStack<DrawStateStackType::Compute>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Clone(native_device_context, device_data.GetLumaCBuffers());
            }
            ((DrawStateStack<DrawStateStackType::Compute>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Restore(native_device_context, false, false);
         }

         pre_draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
      }

      last_draw_dispatch_data = {};
      last_draw_dispatch_data.dispatch_count = uint3( group_count_x, group_count_y, group_count_z );
#endif
      bool cancelled_or_replaced = OnDrawOrDispatch_Custom(cmd_list, true, original_draw_dispatch_func);
#if DEVELOPMENT
      // First run the draw call (don't delegate it to ReShade) and then copy its output
      if (wants_debug_draw)
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack<DrawStateStackType::Compute> post_draw_state_stack;

            if (!cancelled_or_replaced)
            {
               draw_lambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               post_draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               pre_draw_state_stack.Restore(native_device_context);
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, true);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               post_draw_state_stack.Restore(native_device_context);
            }
            cancelled_or_replaced = true;
         }
      }
      bool track_buffer_pipeline_cs = track_buffer_pipeline == cmd_list_data.pipeline_state_original_compute_shader.handle;
      if (track_buffer_pipeline != 0 && track_buffer_pipeline_cs)
      {
         auto local_track_buffer_pipeline_instance = track_buffer_pipeline_instance.fetch_add(1);
         if (track_buffer_pipeline_target_instance == -1 || local_track_buffer_pipeline_instance == track_buffer_pipeline_target_instance)
         {
            com_ptr<ID3D11Buffer> cb;
            native_device_context->CSGetConstantBuffers(track_buffer_index, 1, &cb);
            // Copy the buffer in our data
            CopyBuffer(cb, native_device_context, device_data.track_buffer_data.data);
            device_data.track_buffer_data.hash = std::to_string(std::hash<void*>{}(cb.get()));
         }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         draw_lambda = []() {};
      }

      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, true, draw_lambda);
#endif
      return cancelled_or_replaced;
   }

   bool OnDrawOrDispatchIndirect(
      reshade::api::command_list* cmd_list,
      reshade::api::indirect_command type,
      reshade::api::resource buffer,
      uint64_t offset,
      uint32_t draw_count,
      uint32_t stride)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      // Not used by Dishonored 2 (DrawIndexedInstancedIndirect() and DrawInstancedIndirect() weren't used in Void Engine). Happens in Vertigo (Unity).
      const bool is_dispatch = type == reshade::api::indirect_command::dispatch;
      // Unsupported types (not used in DX11)
      ASSERT_ONCE(type != reshade::api::indirect_command::dispatch_mesh && type != reshade::api::indirect_command::dispatch_rays);
      // NOTE: according to ShortFuse, this can be "reshade::api::indirect_command::unknown" too, so we'd need to fall back on checking what shader is bound to know if this is a compute shader draw
      ASSERT_ONCE(type != reshade::api::indirect_command::unknown);

      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      std::function<void()>* original_draw_dispatch_func = nullptr;

#if DEVELOPMENT || ENABLE_POST_DRAW_CALLBACK
      std::function<void()> draw_lambda = [&]()
      {
         // We only support one draw for now (it couldn't be otherwise in DX11)
         ASSERT_ONCE(draw_count == 1);
         uint32_t i = 0;

         if (is_dispatch)
         {
            native_device_context->DispatchIndirect(reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset) + i * stride);
         }
         else
         {
            if (type == reshade::api::indirect_command::draw_indexed)
            {
               native_device_context->DrawIndexedInstancedIndirect(reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset) + i * stride);
            }
            else
            {
               native_device_context->DrawInstancedIndirect(reinterpret_cast<ID3D11Buffer*>(buffer.handle), static_cast<UINT>(offset) + i * stride);
            }
         }
      };
      original_draw_dispatch_func = &draw_lambda;
#endif

#if DEVELOPMENT
      CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

      const auto& original_shader_hashes = is_dispatch ? cmd_list_data.pipeline_state_original_compute_shader_hashes : cmd_list_data.pipeline_state_original_graphics_shader_hashes;

      bool wants_debug_draw = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0;
      wants_debug_draw &= debug_draw_pipeline == 0 || debug_draw_pipeline == (is_dispatch ? cmd_list_data.pipeline_state_original_compute_shader.handle : cmd_list_data.pipeline_state_original_pixel_shader.handle);
      wants_debug_draw &= (debug_draw_shader_hash == 0 || original_shader_hashes.Contains(debug_draw_shader_hash, is_dispatch ? reshade::api::shader_stage::compute : reshade::api::shader_stage::pixel)) && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id());

      com_ptr<ID3D11DepthStencilState> original_depth_stencil_state;
      UINT original_stencil_ref = 0;
      if (!is_dispatch && cmd_list_data.temp_custom_depth_stencil != ShaderCustomDepthStencilType::None)
      {
         native_device_context->OMGetDepthStencilState(&original_depth_stencil_state, &original_stencil_ref);

         native_device_context->OMSetDepthStencilState((cmd_list_data.temp_custom_depth_stencil == ShaderCustomDepthStencilType::IgnoreTestWriteDepth_IgnoreStencil) ? device_data.default_depth_stencil_state.get() : device_data.depth_test_false_write_true_stencil_false_state.get(), 0);
      }

      DrawStateStack<DrawStateStackType::FullGraphics> pre_draw_state_stack_graphics;
      DrawStateStack<DrawStateStackType::Compute> pre_draw_state_stack_compute;
      if (wants_debug_draw)
      {
         if (debug_draw_freeze_inputs)
         {
            if (!is_dispatch)
            {
               if (!device_data.debug_draw_frozen_draw_state_stack)
               {
                  device_data.debug_draw_frozen_draw_state_stack = std::make_shared<DrawStateStack<DrawStateStackType::FullGraphics>>();
                  ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Cache(native_device_context, device_data.uav_max_count);
                  ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Clone(native_device_context, device_data.GetLumaCBuffers());
               }
               ((DrawStateStack<DrawStateStackType::FullGraphics>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Restore(native_device_context, false, false);
            }
            else
            {
               if (!device_data.debug_draw_frozen_draw_state_stack)
               {
                  device_data.debug_draw_frozen_draw_state_stack = std::make_shared<DrawStateStack<DrawStateStackType::Compute>>();
                  ((DrawStateStack<DrawStateStackType::Compute>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Cache(native_device_context, device_data.uav_max_count);
                  ((DrawStateStack<DrawStateStackType::Compute>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Clone(native_device_context, device_data.GetLumaCBuffers());
               }
               ((DrawStateStack<DrawStateStackType::Compute>*)(device_data.debug_draw_frozen_draw_state_stack.get()))->Restore(native_device_context, false, false);
            }
         }

         if (is_dispatch)
            pre_draw_state_stack_compute.Cache(native_device_context, device_data.uav_max_count);
         else
            pre_draw_state_stack_graphics.Cache(native_device_context, device_data.uav_max_count);
      }

      last_draw_dispatch_data = {};
      // All the data is unknown as it's delegated to the GPU
      last_draw_dispatch_data.indirect = true;
      last_draw_dispatch_data.indexed = type == reshade::api::indirect_command::draw_indexed;
#endif
      bool cancelled_or_replaced = OnDrawOrDispatch_Custom(cmd_list, is_dispatch, original_draw_dispatch_func);
#if DEVELOPMENT
      if (wants_debug_draw)
      {
         auto local_debug_draw_pipeline_instance = debug_draw_pipeline_instance.fetch_add(1);
         if (debug_draw_pipeline_target_instance == -1 || local_debug_draw_pipeline_instance == debug_draw_pipeline_target_instance)
         {
            DrawStateStack<DrawStateStackType::FullGraphics> post_draw_state_stack_graphics;
            DrawStateStack<DrawStateStackType::Compute> post_draw_state_stack_compute;

            if (!cancelled_or_replaced)
            {
               draw_lambda();
            }
            else if (!debug_draw_replaced_pass)
            {
               if (is_dispatch)
               {
                  post_draw_state_stack_compute.Cache(native_device_context, device_data.uav_max_count);
                  pre_draw_state_stack_compute.Restore(native_device_context);
               }
               else
               {
                  post_draw_state_stack_graphics.Cache(native_device_context, device_data.uav_max_count);
                  pre_draw_state_stack_graphics.Restore(native_device_context);
               }
            }

            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, cmd_list, is_dispatch);

            if (cancelled_or_replaced && !debug_draw_replaced_pass)
            {
               if (is_dispatch)
                  post_draw_state_stack_compute.Restore(native_device_context);
               else
                  post_draw_state_stack_graphics.Restore(native_device_context);
            }
            cancelled_or_replaced = true;
         }
      }
      bool track_buffer_pipeline_ps = is_dispatch ? false : (track_buffer_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle);
      bool track_buffer_pipeline_vs = is_dispatch ? false : (track_buffer_pipeline == cmd_list_data.pipeline_state_original_vertex_shader.handle);
      bool track_buffer_pipeline_cs = is_dispatch ? (track_buffer_pipeline == cmd_list_data.pipeline_state_original_compute_shader.handle) : false;
      if (track_buffer_pipeline != 0 && (track_buffer_pipeline_ps || track_buffer_pipeline_vs || track_buffer_pipeline_cs))
      {
         auto local_track_buffer_pipeline_instance = track_buffer_pipeline_instance.fetch_add(1);
         if (track_buffer_pipeline_target_instance == -1 || local_track_buffer_pipeline_instance == track_buffer_pipeline_target_instance)
         {
            com_ptr<ID3D11Buffer> cb;
            if (track_buffer_pipeline_ps)
            {
               native_device_context->PSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            else if (track_buffer_pipeline_ps)
            {
               native_device_context->VSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            else if (track_buffer_pipeline_cs)
            {
               native_device_context->CSGetConstantBuffers(track_buffer_index, 1, &cb);
            }
            // Copy the buffer in our data
            CopyBuffer(cb, native_device_context, device_data.track_buffer_data.data);
            device_data.track_buffer_data.hash = std::to_string(std::hash<void*>{}(cb.get()));
         }
      }
      if (cancelled_or_replaced)
      {
         // Cancel the lambda as we've already drawn once, we don't want to do it further below
         draw_lambda = []() {};
      }

      cancelled_or_replaced |= HandlePipelineRedirections(native_device_context, device_data, cmd_list_data, is_dispatch, draw_lambda);

      // Restore the state
      if (!is_dispatch && cmd_list_data.temp_custom_depth_stencil != ShaderCustomDepthStencilType::None)
      {
         if (!cancelled_or_replaced)
         {
            draw_lambda();
            cancelled_or_replaced = true;
         }
         native_device_context->OMSetDepthStencilState(original_depth_stencil_state.get(), original_stencil_ref);
      }
#endif
      return cancelled_or_replaced;
   }

   // Expects "s_mutex_samplers" to already be locked
   com_ptr<ID3D11SamplerState> CreateCustomSampler(const DeviceData& device_data, ID3D11Device* device, const D3D11_SAMPLER_DESC& original_desc)
   {
      D3D11_SAMPLER_DESC desc = original_desc;
#if !DEVELOPMENT
      if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)
      {
         desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
         if (samplers_upgrade_mode == 5) // Bruteforce the offset
         {
            desc.MipLODBias = std::clamp(device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX); // Setting this out of range (~ +/- 16) will make DX11 crash
         }
         else if (samplers_upgrade_mode == 4)
         {
            desc.MipLODBias = std::clamp(desc.MipLODBias + device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX); // Setting this out of range (~ +/- 16) will make DX11 crash
         }
         // TODO: Clean up the code. Other "samplers_upgrade_mode" values aren't supported outside of development (because they aren't even needed, until proven otherwise)
      }
      else
      {
         return nullptr;
      }
#else
      if (samplers_upgrade_mode <= 0)
         return nullptr;

      // Prey's CryEngine (and most games) only uses:
      // D3D11_FILTER_ANISOTROPIC
      // D3D11_FILTER_COMPARISON_ANISOTROPIC
      // D3D11_FILTER_MIN_MAG_MIP_POINT
      // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT
      // D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
      // D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
      // D3D11_FILTER_MIN_MAG_MIP_LINEAR
      // D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
      //TODOFT: check the cases for other games!

      // This could theoretically make some textures that have moire patters, or were purposely blurry, "worse", but the positives of upgrading still outweight the negatives.
      // Note that this might not fix all cases because there's still "ID3D11DeviceContext::SetResourceMinLOD()" and textures that are blurry for other reasons
      // because they use other types of samplers (unfortunately it seems like some decals use "D3D11_FILTER_MIN_MAG_MIP_LINEAR").
      // Note that the AF on different textures in the game seems is possibly linked with other graphics settings than just AF (maybe textures or objects quality).
      if (desc.Filter == D3D11_FILTER_ANISOTROPIC || desc.Filter == D3D11_FILTER_COMPARISON_ANISOTROPIC)
      {
         // Note: this doesn't seem to affect much
         if (samplers_upgrade_mode == 1)
         {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 2, D3D11_REQ_MAXANISOTROPY);
         }
         else if (samplers_upgrade_mode == 2)
         {
            desc.MaxAnisotropy = min(desc.MaxAnisotropy * 4, D3D11_REQ_MAXANISOTROPY);
         }
         else if (samplers_upgrade_mode >= 3)
         {
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
         }
         // Note: this is the main ingredient in making textures less blurry
         if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f)
         {
            desc.MipLODBias = std::clamp(desc.MipLODBias + device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         else if (samplers_upgrade_mode >= 5)
         {
            desc.MipLODBias = std::clamp(device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         // Note: this never seems to affect anything in Prey, probably also doesn't in most games
         if (samplers_upgrade_mode >= 6)
         {
            desc.MinLOD = min(desc.MinLOD, 0.f);
         }
      }
      else if ((desc.Filter == D3D11_FILTER_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 1) // This is the most common (main/only) format being used other than AF
         || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR && samplers_upgrade_mode_2 >= 2)
         || (desc.Filter == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 3)
         || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT && samplers_upgrade_mode_2 >= 4)
         || (desc.Filter == D3D11_FILTER_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 5)
         || (desc.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT && samplers_upgrade_mode_2 >= 6))
      {
         //TODOFT: research. Force this on to see how it behaves. Doesn't work, it doesn't really help any further with (e.g.) blurry decal textures
         // Note: this doesn't seem to do anything really, it doesn't help with the occasional blurry texture (probably because all samplers that needed anisotropic already had it set)
         if (samplers_upgrade_mode >= 7)
         {
            desc.Filter == (desc.ComparisonFunc != D3D11_COMPARISON_NEVER && samplers_upgrade_mode == 7) ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
            desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
         }
         // Note: changing the lod bias of non anisotropic filters makes reflections (cubemap samples?) a lot more specular (shiny) in Prey (and probably does in other games too), so it's best avoided (it can look better is some screenshots, but it's likely not intended).
         // Even if we only fix up textures that didn't have a positive bias, we run into the same problem.
         if (samplers_upgrade_mode == 4 && desc.MipLODBias <= 0.f)
         {
            desc.MipLODBias = std::clamp(desc.MipLODBias + device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         else if (samplers_upgrade_mode >= 5)
         {
            desc.MipLODBias = std::clamp(device_data.texture_mip_lod_bias_offset, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
         }
         if (samplers_upgrade_mode >= 6)
         {
            desc.MinLOD = min(desc.MinLOD, 0.f);
         }
      }
#endif // !DEVELOPMENT

#if DEVELOPMENT && !defined(NDEBUG) && 0 // Make sure that the device we are using to create samples is the "native" one (the final one), not the proxy created by ReShade // TODO: delete if ReShade doesn't take my PR for this
      ID3D11Device* parent_device = nullptr;
      // Special ID used by ReShade (and possibly other applications) that returns the parent object of a proxy
      constexpr GUID ID_IDeviceChildParent = { 0x7f2c9a11, 0x3b4e, 0x4d6a, { 0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42 } };
      struct __declspec(uuid("7F2C9A11-3B4E-4D6A-812F-5E9CD37A1B42")) IDeviceChildParent : IUnknown { };
      device->QueryInterface(__uuidof(IDeviceChildParent), (void**)&parent_device);
      if (parent_device)
      {
         ASSERT_ONCE_MSG(false, "We are possibly creating a sampler through the ReShade proxy device, which means ReShade will see it and call the init function on it, which we don't want"); // Note that if other applications implemented "ID_IDeviceChildParent", this could result in false positives
         parent_device->Release();
      }
#endif

      com_ptr<ID3D11SamplerState> sampler;
      device->CreateSamplerState(&desc, &sampler); // Note: in DX11 all state objects are shared, so if we create one with the same desc as an existing one, it will return the ptr to that one instead.
      ASSERT_ONCE(sampler != nullptr);
      return sampler;
   }

   void OnInitSampler(reshade::api::device* device, const reshade::api::sampler_desc& desc, reshade::api::sampler sampler)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      if (sampler == 0)
         return;

      DeviceData& device_data = *device->get_private_data<DeviceData>();

#if DEVELOPMENT && 0 // Assert in case we got unexpected samplers
      if (desc.filter == reshade::api::filter_mode::anisotropic || desc.filter == reshade::api::filter_mode::compare_anisotropic)
      {
         assert(desc.max_anisotropy >= 2); // Doesn't seem to happen
         assert(desc.min_lod == 0); // Doesn't seem to happen
         assert(desc.mip_lod_bias == 0.f); // This seems to happen when enabling TAA (but not with SMAA 2TX), some new samplers are created with bias -1 and then persist, it's unclear if they are used though.
      }
      else
      {
         assert(desc.max_anisotropy <= 1); // This can happen (like once) in Prey. AF is probably ignored for these anyway so it's innocuous
      }
      assert(desc.filter != reshade::api::filter_mode::min_mag_anisotropic_mip_point && desc.filter != reshade::api::filter_mode::compare_min_mag_anisotropic_mip_point); // Doesn't seem to happen

      ASSERT_ONCE(desc.filter == reshade::api::filter_mode::anisotropic
         || desc.filter == reshade::api::filter_mode::compare_anisotropic
         || desc.filter == reshade::api::filter_mode::min_mag_mip_linear
         || desc.filter == reshade::api::filter_mode::compare_min_mag_mip_linear
         || desc.filter == reshade::api::filter_mode::min_mag_linear_mip_point
         || desc.filter == reshade::api::filter_mode::compare_min_mag_linear_mip_point); // Doesn't seem to happen
#endif // DEVELOPMENT

      ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);

      std::shared_lock shared_lock_samplers(s_mutex_samplers);

#if 0 // In DX11 all state objects are shared, so the game creates one that is identical to one (upgraded) that we already created before, it will return the ptr to that one instead. In that case, we still go through and add it to our map, even if it might not need to be upgraded.
      // Custom samplers lifetime should never be tracked by ReShade, otherwise we'd recursively create custom samplers out of custom samplers
      // (it's unclear if engines (e.g. CryEngine) somehow do anything with these samplers or if ReShade captures our own samplers creation events (it probably does as we create them directly through the DX native funcs (no it's doesn't! We have the proxy object)))
      for (const auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
      {
         for (const auto& custom_sampler_handle : samplers_handle.second)
         {
            if (custom_sampler_handle.second.get() == native_sampler)
            {
               return;
            }
         }
      }
#endif

      shared_lock_samplers.unlock(); // This is fine!
      D3D11_SAMPLER_DESC native_desc;
      native_sampler->GetDesc(&native_desc);
      com_ptr<ID3D11SamplerState> custom_sampler = CreateCustomSampler(device_data, (ID3D11Device*)device->get_native(), native_desc);
      std::unique_lock unique_lock_samplers(s_mutex_samplers);
      device_data.custom_sampler_by_original_sampler[sampler.handle][device_data.texture_mip_lod_bias_offset] = custom_sampler;
   }

   void OnDestroySampler(reshade::api::device* device, reshade::api::sampler sampler)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      // This only seems to happen when the game shuts down in Prey (as any destroy callback, it can be called from an arbitrary thread, but that's fine).
      // We don't need to check the custom samplers within the map even if they might be the same object, because they are strong pointers and thus wouldn't get destroyed if they were non null.
      const std::unique_lock lock_samplers(s_mutex_samplers);
      device_data.custom_sampler_by_original_sampler.erase(sampler.handle);
   }

   // TODO: cache the last "almost" upgraded texture resolution to make sure that when the swapchain changes res, we didn't fail to upgrade resources before
   std::optional<reshade::api::format> ShouldUpgradeResource(const reshade::api::resource_desc& desc, const DeviceData& device_data, bool has_initial_data = false)
   {
      if (texture_format_upgrades_type < TextureFormatUpgradesType::AllowedEnabled)
      {
         return std::nullopt;
      }

      const bool is_rt_or_ua = (desc.usage & (reshade::api::resource_usage::render_target | reshade::api::resource_usage::unordered_access)) != 0;
      // Convoluted check to test if the resource is "D3D11_USAGE_DEFAULT" (the only usage type that can be both used as SRV, and be the target of a CopyResource(), otherwise we'd never need to upgrade them).
      // We also check the initial data for extra safety, in case this was a "static" content texture that accidentally wasn't created as immutable.
      const bool is_writable_sr = (desc.usage & reshade::api::resource_usage::shader_resource) != 0 && desc.heap == reshade::api::memory_heap::gpu_only && !has_initial_data && (desc.flags & reshade::api::resource_flags::immutable) == 0;

      const bool is_depth = (desc.usage & reshade::api::resource_usage::depth_stencil) != 0;

      bool enable_preventive_automatic_indirect_texture_format_upgrades = enable_automatic_indirect_texture_format_upgrades && false; // TODO: add a new flag for this, in can often break games
#if GAME_THUMPER // It's needed there?
      enable_preventive_automatic_indirect_texture_format_upgrades = enable_automatic_indirect_texture_format_upgrades;
#endif
      if ((!(is_rt_or_ua || (enable_automatic_indirect_texture_format_upgrades ? is_writable_sr : false)) || !texture_upgrade_formats.contains(desc.texture.format)) && (!is_depth || !texture_depth_upgrade_formats.contains(desc.texture.format)))
      {
         return std::nullopt;
      }

      // At least in DX11, any resource that isn't exclusively accessible by the GPU, can't be set as output (render target/unordered access).
		// These probably wouldn't have the RT/UA usage flags set anyway, or they'd fail on creation if they did.
      if (desc.heap != reshade::api::memory_heap::gpu_only)
      {
         ASSERT_ONCE(desc.heap != reshade::api::memory_heap::unknown && desc.heap != reshade::api::memory_heap::custom); // Unexpected heap types
         return std::nullopt;
      }

      const bool is_cube = (desc.flags & reshade::api::resource_flags::cube_compatible) != 0 && (desc.texture.depth_or_layers % 6) == 0 && desc.texture.depth_or_layers != 0;

      // Note: we can't fully exclude texture 2D arrays here, because they might still have 1 layer
      bool type_and_size_filter = desc.type == reshade::api::resource_type::texture_2d && (desc.texture.depth_or_layers == 1 || is_cube);

      if (texture_format_upgrades_2d_size_filters != (uint32_t)TextureFormatUpgrades2DSizeFilters::All)
      {
         bool size_filter = false;

         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution) != 0)
         {
            size_filter |= desc.texture.width == device_data.output_resolution.x && desc.texture.height == device_data.output_resolution.y;
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionWidth) != 0)
         {
            size_filter |= desc.texture.width == device_data.output_resolution.x;
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionHeight) != 0)
         {
            size_filter |= desc.texture.height == device_data.output_resolution.y;
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderResolution) != 0)
         {
            size_filter |= desc.texture.width == device_data.render_resolution.x && desc.texture.height == device_data.render_resolution.y;
         }
         // Flipped condition, given we already allowed them above in "type_and_size_filter"
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Cubes) == 0)
         {
            size_filter &= !is_cube;
         }
         // No limits on the size, we can assume it was square
         else
         {
            size_filter |= is_cube;
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0)
         {
            size_filter &= desc.texture.width != 1 && desc.texture.height != 1;
         }

         // Always scale from the smallest dimension, as that gives up more threshold, depending on how the devs scaled down textures (they can use multiple rounding models)
         float min_aspect_ratio = desc.texture.width <= desc.texture.height ? ((float)(desc.texture.width - texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.texture.height) : ((float)desc.texture.width / (float)(desc.texture.height + texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         float max_aspect_ratio = desc.texture.width <= desc.texture.height ? ((float)(desc.texture.width + texture_format_upgrades_2d_aspect_ratio_pixel_threshold) / (float)desc.texture.height) : ((float)desc.texture.width / (float)(desc.texture.height - texture_format_upgrades_2d_aspect_ratio_pixel_threshold));
         bool generating_manual_mips = false;
#if DEVELOPMENT
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0
            || (texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio) != 0
            || (texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio) != 0)
         {
            static thread_local UINT last_texture_width = desc.texture.width;
            static thread_local UINT last_texture_height = desc.texture.height;
            // If this was a chain of downscaling, don't send a warning! This is just a heuristics based check... The creation order might have been random, or inverted (from smaller to bigger mips).
            // Note that this isn't thread safe but whatever
            if (max(desc.texture.width, desc.texture.height) == 1)
            {
               generating_manual_mips = (last_texture_width / 2) == desc.texture.width && (last_texture_height / 2) == desc.texture.height;
            }
            last_texture_width = desc.texture.width;
            last_texture_height = desc.texture.height;
         }
#endif
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio) != 0)
         {
            float target_aspect_ratio = (float)device_data.output_resolution.x / (float)device_data.output_resolution.y;
            bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
            size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
            ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.texture.width, desc.texture.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted"); // TODO: add a min size for upgrades? Like >1 or >32 on the smallest axis? Or ... scan if the allocations shrink in size over time
#endif
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio) != 0)
         {
            float target_aspect_ratio = (float)device_data.render_resolution.x / (float)device_data.render_resolution.y;
            bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
            size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
            ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.texture.width, desc.texture.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#endif
         }
         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio) != 0)
         {
            const std::shared_lock lock_texture_upgrades(s_mutex_texture_upgrades);
            for (auto texture_format_upgrades_2d_custom_aspect_ratio : texture_format_upgrades_2d_custom_aspect_ratios)
            {
               float target_aspect_ratio = texture_format_upgrades_2d_custom_aspect_ratio;
               bool aspect_ratio_filter = target_aspect_ratio >= (min_aspect_ratio - FLT_EPSILON) && target_aspect_ratio <= (max_aspect_ratio + FLT_EPSILON);
               size_filter |= aspect_ratio_filter;
#if DEVELOPMENT
               ASSERT_ONCE_MSG(!aspect_ratio_filter || max(desc.texture.width, desc.texture.height) > 1 || generating_manual_mips || ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px) != 0), "Upgrading 1x1 resource by aspect ratio, this is possibly unwanted");
#else
               if (size_filter) break;
#endif
            }
         }

         if ((texture_format_upgrades_2d_size_filters & (uint32_t)TextureFormatUpgrades2DSizeFilters::Mips) != 0)
         {
            float2 max_resolution = device_data.output_resolution.y >= device_data.render_resolution.y ? device_data.output_resolution : device_data.render_resolution;
            size_filter |= IsMipOf(max_resolution.x, max_resolution.y, desc.texture.width, desc.texture.height);
         }

         type_and_size_filter &= size_filter;
      }

      if (is_depth)
      {
         if (type_and_size_filter)
         {
            // Preserve the stencil if we can!
            if (desc.texture.format == reshade::api::format(DXGI_FORMAT_R24G8_TYPELESS)
               || desc.texture.format == reshade::api::format(DXGI_FORMAT_D24_UNORM_S8_UINT))
            {
               return reshade::api::format::r32_g8_typeless;
            }
            return reshade::api::format::r32_typeless; // Create it as typeless of the maximum depth, so we can also cast it as SRV
         }
         return std::nullopt;
      }

      switch (texture_format_upgrades_lut_dimensions)
      {
      case LUTDimensions::_1D:
      {
         // For 1D, "texture_format_upgrades_lut_size" is the whole width (usually they extend in width)
         type_and_size_filter |= desc.type == reshade::api::resource_type::texture_1d && desc.texture.width == texture_format_upgrades_lut_size && desc.texture.height == 1 && desc.texture.depth_or_layers == 1 && desc.texture.levels == 1;
         break;
      }
      default:
      case LUTDimensions::_2D:
      {
         // For 2D, "texture_format_upgrades_lut_size" is the height, usually they extend in width and that's squared
         type_and_size_filter |= desc.type == reshade::api::resource_type::texture_2d && desc.texture.width == (texture_format_upgrades_lut_size * texture_format_upgrades_lut_size) && desc.texture.height == texture_format_upgrades_lut_size && desc.texture.depth_or_layers == 1 && desc.texture.levels == 1;
         break;
      }
      case LUTDimensions::_3D:
      {
         // For 3D, all the dimensions usually match
         type_and_size_filter |= desc.type == reshade::api::resource_type::texture_3d && desc.texture.width == texture_format_upgrades_lut_size && desc.texture.height == texture_format_upgrades_lut_size && desc.texture.depth_or_layers == texture_format_upgrades_lut_size && desc.texture.levels == 1;
         break;
      }
      }

      if (type_and_size_filter)
      {
         return reshade::api::format::r16g16b16a16_float; // TODO: if the source format was like R8G8_UNORM, only upgrade it to R16G16_FLOAT unless otherwise specified? Just expose the format! Do the same for the indirect upgraded textures. This format is hardcoded in a few places to check around.
      }
      return std::nullopt;
   }

   // Returns true if it changed the data.
   // Remember to manually de-allocate the data later.
   bool ConvertResourceData(const reshade::api::subresource_data* data, reshade::api::subresource_data& new_data, const reshade::api::resource_desc& desc, reshade::api::format new_format, bool pre_allocated = false)
   {
      ASSERT_ONCE(data->data != nullptr);

      // Only supported format atm
      if (new_format != reshade::api::format::r16g16b16a16_float)
      {
         ASSERT_ONCE_MSG(false, "Unsupported target resource data format (due to texture upgrades)");
         return false;
      }

      constexpr size_t bytes_per_pixel = 8; // 4 for 8bpc, 8 for 16bpc
      const size_t buffer_size = desc.texture.width * desc.texture.height * desc.texture.depth_or_layers * bytes_per_pixel;

      switch (desc.texture.format)
      {
      case reshade::api::format::r8g8b8a8_unorm:
      case reshade::api::format::r8g8b8a8_unorm_srgb:
      case reshade::api::format::r8g8b8x8_unorm:
      case reshade::api::format::r8g8b8x8_unorm_srgb:
      {
         if (!pre_allocated)
            new_data.data = new uint8_t[buffer_size];
         new_data.row_pitch = desc.texture.width * bytes_per_pixel;
         new_data.slice_pitch = new_data.row_pitch * desc.texture.height;
         ConvertR8G8B8A8toR16G16B16A16((R8G8B8A8_UNORM*)data->data, (R16G16B16A16_FLOAT*)new_data.data, desc.texture.width, desc.texture.height, desc.texture.depth_or_layers);
         return true;
      }
      case reshade::api::format::b8g8r8a8_unorm:
      case reshade::api::format::b8g8r8a8_unorm_srgb:
      case reshade::api::format::b8g8r8x8_unorm:
      case reshade::api::format::b8g8r8x8_unorm_srgb:
      {
         if (!pre_allocated)
            new_data.data = new uint8_t[buffer_size];
         new_data.row_pitch = desc.texture.width * bytes_per_pixel;
         new_data.slice_pitch = new_data.row_pitch * desc.texture.height;
         ConvertR8G8B8A8toR16G16B16A16((B8G8R8A8_UNORM*)data->data, (R16G16B16A16_FLOAT*)new_data.data, desc.texture.width, desc.texture.height, desc.texture.depth_or_layers);
         return true;
      }
      case reshade::api::format::r16g16b16a16_float:
      {
         break;
      }
      default:
      {
         ASSERT_ONCE_MSG(false, "Unsupported source resource data format (due to texture upgrades)"); // TODO: add support for these
         break;
      }
      }
      return false;
   }

   void OnInitResource(
      reshade::api::device* device,
      const reshade::api::resource_desc& desc,
      const reshade::api::subresource_data* initial_data,
      reshade::api::resource_usage initial_state,
      reshade::api::resource resource)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      std::unique_lock lock(device_data.mutex);
      if (waiting_on_upgraded_resource_init)
      {
         // If this happened, some resource creation failed an "OnInitResource()" was never called after "OnCreateResource()".
         // It might have to do with the fact that the resource was shared.
         // Either way we could never catch all cases as we don't know when that happens, and this might have false positives as a desc isn't a unique pointer (two resources might use the same desc).
         // Then again, if the description was identical and the first would have failed, so would have the second one.
         ASSERT_ONCE(std::memcmp(&upgraded_resource_init_desc, &desc, sizeof(upgraded_resource_init_desc)) == 0);
         if (std::memcmp(&upgraded_resource_init_desc, &desc, sizeof(upgraded_resource_init_desc)) == 0)
         {
            ASSERT_ONCE(upgraded_resource_init_data == nullptr || initial_data->data == upgraded_resource_init_data);
         }
         else
         {
            ASSERT_ONCE(upgraded_resource_init_data == nullptr || initial_data->data != upgraded_resource_init_data);
         }
         // Delete the converted data we created (there's a chance of memory leaks if creation failed, though unlikely)
         if (upgraded_resource_init_data)
         {
            delete[] static_cast<uint8_t*>(upgraded_resource_init_data); // TODO: this has a memory leak when a thread is closed if it last failed. Make it a smart ptr.
            upgraded_resource_init_data = nullptr; // optional, avoid dangling pointer
         }

         if (enable_indirect_texture_format_upgrades)
         {
            ASSERT_ONCE(desc.texture.format != reshade::api::format::r16g16b16a16_float); // Why did we get here?
            reshade::api::resource_desc upgraded_desc = desc;
            upgraded_desc.texture.format = reshade::api::format::r16g16b16a16_float;

            lock.unlock(); // Avoids deadlocks with the device

            reshade::api::subresource_data* new_initial_data_ptr = nullptr;
            reshade::api::subresource_data new_initial_data;
            if (initial_data)
            {
               new_initial_data = *initial_data;
               if (ConvertResourceData(initial_data, new_initial_data, desc, upgraded_desc.texture.format))
                  new_initial_data_ptr = &new_initial_data;
            }

            reshade::api::resource mirrored_upgraded_resource;
            if (device->create_resource(upgraded_desc, new_initial_data_ptr, initial_state, &mirrored_upgraded_resource))
            {
               lock.lock();
               device_data.original_resources_to_mirrored_upgraded_resources.emplace(resource.handle, mirrored_upgraded_resource.handle);
            }
            if (new_initial_data_ptr)
               delete[] static_cast<uint8_t*>(new_initial_data_ptr->data);
         }
         else
         {
            device_data.upgraded_resources.emplace(resource.handle);
#if DEVELOPMENT
            device_data.original_upgraded_resources_formats[resource.handle] = last_attempted_upgraded_resource_creation_format;
#endif
         }
         waiting_on_upgraded_resource_init = false;
      }
      else
      {
         // Code mirrored from "OnCreateResource()" (though some filter cases are still missing)
         if (desc.type == reshade::api::resource_type::texture_2d && desc.texture.depth_or_layers == 1)
         {
            if ((desc.usage & (reshade::api::resource_usage::render_target | reshade::api::resource_usage::unordered_access)) != 0 && texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && texture_upgrade_formats.contains(desc.texture.format))
            {
               // Shared resources can call "OnInitResource()" without a "OnCreateResource()" (see "D3D11Device::OpenSharedResource"), as they've been created on a different device (and possibly API), so we can't always upgrade them,
               // though Luma only allows DX11 devices so it's probably guaranteed!
               ASSERT_ONCE((desc.flags & reshade::api::resource_flags::shared) == 0);
            }
         }
      }
   }

   // This is called before "OnInitResource()"
   // Note that swapchain textures (backbuffer) don't pass through here!
   bool OnCreateResource(
      reshade::api::device* device,
      reshade::api::resource_desc& desc,
      reshade::api::subresource_data* initial_data,
      reshade::api::resource_usage initial_state)
   {
#if CHECK_GRAPHICS_API_COMPATIBILITY
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api(), false);
#else // Check this one anyway
      ASSERT_ONCE(device->get_api() == reshade::api::device_api::d3d11);
#endif

      // No need to clear "upgraded_resource_init_desc"/"upgraded_resource_init_data" from its last value
      waiting_on_upgraded_resource_init = false; // If the same thread called another "OnCreateResource()" before we got a "OnInitResource()", it implies the resource creation has failed and we didn't get that event (we have to do it by thread as device objects creation is thread safe and can be done by multiple threads concurrently)

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      std::shared_lock lock(device_data.mutex); // Note: we possibly don't even need this (or well, we might want to use a different mutex)
      
      if (std::optional<reshade::api::format> upgraded_format = ShouldUpgradeResource(desc, device_data, initial_data && initial_data->data))
      {
         lock.unlock();

         const reshade::api::resource_desc original_desc = desc;

#if DEVELOPMENT
         last_attempted_upgraded_resource_creation_format = desc.texture.format;
#endif

         // Note that upgrading typeless texture could have unforeseen consequences in some games, especially when the textures are then used as unsigned int or signed int etc (e.g. Trine 5)
         if (!enable_indirect_texture_format_upgrades)
         {
            ASSERT_ONCE(desc.texture.format != upgraded_format.value()); // Why did we get here?
            desc.texture.format = upgraded_format.value();
         }
         else
         {
#if DEVELOPMENT && 0 // TODO: WIP size upgrades. We can't really do this without upgrading every single render target and depth/stencil texture, as all RTs need to have the same size. We'd also need to scale the viewport.
            //desc.texture.samples = max(desc.texture.samples, 4); // Unlikely MSAA will work
            if (indirect_upgraded_textures_size_scaling != 1.f && desc.type == reshade::api::resource_type::texture_2d)
            {
               // Ideally we'd either scale by 0.5 or 2, to keep them compatibile with linear sampler,
               // and to be able to scale mips
               desc.texture.width *= uint(indirect_upgraded_textures_size_scaling);
               desc.texture.height *= uint(indirect_upgraded_textures_size_scaling);
               // Make one more mip if we duplicate the size
               if (desc.texture.levels > 1)
               {
                  if (indirect_upgraded_textures_size_scaling > 1.f)
                  {
                     desc.texture.levels++;
                  }
                  else
                  {
                     desc.texture.levels--;
                  }
               }
            }
#endif
         }

         waiting_on_upgraded_resource_init = true;
         upgraded_resource_init_desc = desc;
         bool converted_initial_data = false;
         // We need to convert the initial data to the new format
         if (initial_data != nullptr && !enable_indirect_texture_format_upgrades)
         {
            reshade::api::subresource_data new_initial_data = *initial_data;
            converted_initial_data = ConvertResourceData(initial_data, new_initial_data, original_desc, upgraded_format.value());
            if (converted_initial_data)
            {
               *initial_data = new_initial_data;
            }
         }
         // Note: this expects a "uint8_t" c array
         upgraded_resource_init_data = converted_initial_data ? initial_data->data : nullptr;

         return !enable_indirect_texture_format_upgrades;
      }
      return false;
   }

   void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      if (!device || device->get_private_data<DeviceData>() == nullptr)
      {
#if 0
         ASSERT_ONCE(false); // Happens when BioShock Infinite closes down (due to it using shared resources!), though it seems to be almost safe (could it be that the (now stale) device pointer has already been re-allocated? Probably not as this call comes from within the device object itself?)
#endif
         return;
      }
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      if (&device_data == nullptr)
         return;
      std::unique_lock lock(device_data.mutex);
      auto original_resource_to_mirrored_upgraded_resource = device_data.original_resources_to_mirrored_upgraded_resources.find(resource.handle);
      if (original_resource_to_mirrored_upgraded_resource != device_data.original_resources_to_mirrored_upgraded_resources.end())
      {
         const auto mirrored_upgraded_resource = original_resource_to_mirrored_upgraded_resource->second;
         device_data.original_resources_to_mirrored_upgraded_resources.erase(original_resource_to_mirrored_upgraded_resource);
         lock.unlock(); // Avoids deadlocks with the device
         device->destroy_resource({ mirrored_upgraded_resource });
         lock.lock();
      }
      device_data.upgraded_resources.erase(resource.handle);
#if DEVELOPMENT
      device_data.original_upgraded_resources_formats.erase(resource.handle);
#endif // DEVELOPMENT

      // TODO: thread safe
      int count = 0;
#if DEVELOPMENT
      count = device_data.cb_per_view_global_buffers.erase(reinterpret_cast<ID3D11Buffer*>(resource.handle));
#endif
   }

   bool OnCreateResourceView(
      reshade::api::device* device,
      reshade::api::resource resource,
      reshade::api::resource_usage usage_type,
      reshade::api::resource_view_desc& desc)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api(), false);

		reshade::api::resource_view_type lut_dimensions = reshade::api::resource_view_type::unknown;
      switch (texture_format_upgrades_lut_dimensions)
      {
      case LUTDimensions::_1D:
      {
         lut_dimensions = reshade::api::resource_view_type::texture_1d;
         break;
      }
      default:
      case LUTDimensions::_2D:
      {
         lut_dimensions = reshade::api::resource_view_type::texture_2d;
         break;
      }
      case LUTDimensions::_3D:
      {
         lut_dimensions = reshade::api::resource_view_type::texture_3d;
         break;
      }
      }

      // In DX11 apps can not pass a "DESC" when creating resource views, and DX11 will automatically generate the default one from it, we handle it through "reshade::api::resource_view_type::unknown".
      if (resource.handle != 0 && (desc.type == reshade::api::resource_view_type::unknown || desc.type == lut_dimensions || desc.type == reshade::api::resource_view_type::texture_2d || desc.type == reshade::api::resource_view_type::texture_2d_array || desc.type == reshade::api::resource_view_type::texture_2d_multisample || desc.type == reshade::api::resource_view_type::texture_2d_multisample_array || desc.type == reshade::api::resource_view_type::texture_cube || desc.type == reshade::api::resource_view_type::texture_cube_array))
      {
         const reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);

         DeviceData& device_data = *device->get_private_data<DeviceData>();
         const std::shared_lock lock(device_data.mutex);

         // Needed because these were not in the upgraded resources list, but we upgraded the swapchain's textures,
         // some games randomly pick a view format when they can't pick a proper one (due to the format upgrades).
         if (swapchain_upgrade_type > SwapchainUpgradeType::None && device_data.back_buffers.contains(resource.handle))
         {
#if DEVELOPMENT
            last_attempted_upgraded_resource_view_creation_view_format = desc.format;
            if (last_attempted_upgraded_resource_view_creation_view_format == reshade::api::format::unknown && device_data.original_upgraded_resources_formats.contains(resource.handle))
            {
               last_attempted_upgraded_resource_view_creation_view_format = device_data.original_upgraded_resources_formats[resource.handle];
            }
#endif // DEVELOPMENT

            if (desc.type == reshade::api::resource_view_type::unknown)
            {
               desc.type = resource_desc.texture.samples <= 1 ? (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d : ((resource_desc.flags & reshade::api::resource_flags::cube_compatible) != 0 ? (resource_desc.texture.depth_or_layers == 6 ? reshade::api::resource_view_type::texture_cube : reshade::api::resource_view_type::texture_cube_array) : reshade::api::resource_view_type::texture_2d_array)) : (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d_multisample : reshade::api::resource_view_type::texture_2d_multisample_array);
            }
            desc.texture.level_count = 1; // "Deus Ex: Human Revolution - Director's Cut" sets this to 0 (at least when we upgrade the swapchain texture), it might be fine, but the DX11 docs only talk about setting it to -1 to use all levels (which are always 1 for swapchain textures anyway)
            // Redirect typeless formats (not even sure they are supported, but it won't hurt to check)
            switch (resource_desc.texture.format)
            {
            case reshade::api::format::r16g16b16a16_typeless:
            {
               desc.format = reshade::api::format::r16g16b16a16_float;
               break;
            }
            case reshade::api::format::r8g8b8a8_typeless:
            {
               if (desc.format != reshade::api::format::r8g8b8a8_unorm_srgb)
                  desc.format = reshade::api::format::r8g8b8a8_unorm;
               break;
            }
            case reshade::api::format::b8g8r8a8_typeless:
            {
               if (desc.format != reshade::api::format::b8g8r8a8_unorm_srgb)
                  desc.format = reshade::api::format::b8g8r8a8_unorm;
               break;
            }
            case reshade::api::format::r10g10b10a2_typeless:
            {
               desc.format = reshade::api::format::r10g10b10a2_unorm;
               break;
            }
            default:
            {
               bool formats_compatible = false;
               bool rgba8_1 = desc.format == reshade::api::format::r8g8b8a8_unorm || desc.format == reshade::api::format::r8g8b8a8_unorm_srgb;
               bool rgba8_2 = resource_desc.texture.format == reshade::api::format::r8g8b8a8_unorm || resource_desc.texture.format == reshade::api::format::r8g8b8a8_unorm_srgb;
               bool bgra8_1 = desc.format == reshade::api::format::b8g8r8a8_unorm || desc.format == reshade::api::format::b8g8r8a8_unorm_srgb;
               bool bgra8_2 = resource_desc.texture.format == reshade::api::format::b8g8r8a8_unorm || resource_desc.texture.format == reshade::api::format::b8g8r8a8_unorm_srgb;
               formats_compatible |= rgba8_1 && rgba8_2;
               formats_compatible |= bgra8_1 && bgra8_2;
               // No need to force replace the view format if formats are compatible
               if (!formats_compatible)
                 desc.format = resource_desc.texture.format; // Should be R16G16B16A16_FLOAT or R10G10B10A2_UNORM if "swapchain_format_upgrade_type" is enabled (depending on "swapchain_upgrade_type")
               break;
            }
            }
            return true;
         }

#if DEVELOPMENT
         bool usage_filter = usage_type == reshade::api::resource_usage::render_target || usage_type == reshade::api::resource_usage::unordered_access || usage_type == reshade::api::resource_usage::shader_resource; // This is all of the possible types anyway...
         if (usage_filter && ShouldUpgradeResource(resource_desc, device_data).has_value() && resource_desc.texture.format == reshade::api::format::r16g16b16a16_float)
         {
            switch (desc.format)
            {
            default:
            {
               // Nobody should be creating a typeless view, though it might be a bug with the game's code in case of unexpected format upgrades
               ASSERT_ONCE(desc.format != reshade::api::format::r16g16b16a16_typeless && desc.format != reshade::api::format::r8g8b8a8_typeless && desc.format != reshade::api::format::b8g8r8a8_typeless && desc.format != reshade::api::format::b8g8r8x8_typeless);
               break;
            }
            case reshade::api::format::unknown:
            {
               // Happens when the call didn't provide a "DESC", creating a default view (because if we reached here, the texture is 16bpc float)
               ASSERT_ONCE(device_data.upgraded_resources.contains(resource.handle));
               break;
            }
            }
         }
#endif

         if (device_data.upgraded_resources.contains(resource.handle))
         {
#if DEVELOPMENT
            last_attempted_upgraded_resource_view_creation_view_format = desc.format;
            // Note: if it's unknown, it usually means the game wanted to auto create a view. But it could also mean they dynamically created views based on the current resource format, and that code failed to find a valid view format for upgraded textures,
            // however if that was the case, it'd be hard to explain why all games still create resources even when upgrading textures.
            if (last_attempted_upgraded_resource_view_creation_view_format == reshade::api::format::unknown && device_data.original_upgraded_resources_formats.contains(resource.handle))
            {
               last_attempted_upgraded_resource_view_creation_view_format = device_data.original_upgraded_resources_formats[resource.handle];
            }
#endif // DEVELOPMENT

            if (desc.type == reshade::api::resource_view_type::unknown)
            {
               if (resource_desc.type == reshade::api::resource_type::texture_3d)
               {
                  desc.type = reshade::api::resource_view_type::texture_3d;
               }
               else
               {
                  desc.type = resource_desc.texture.samples <= 1 ? (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d : ((resource_desc.flags & reshade::api::resource_flags::cube_compatible) != 0 ? (resource_desc.texture.depth_or_layers == 6 ? reshade::api::resource_view_type::texture_cube : reshade::api::resource_view_type::texture_cube_array) : reshade::api::resource_view_type::texture_2d_array)) : (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d_multisample : reshade::api::resource_view_type::texture_2d_multisample_array); // We need to set it in case it was "reshade::api::resource_view_type::unknown", otherwise the format would also need to be unknown
               }
               desc.texture.first_level = 0;
               desc.texture.level_count = -1; // All levels (e.g. Dishonored 2 sets this to invalid values if the resource format was upgraded)
               desc.texture.first_layer = 0;
               desc.texture.layer_count = resource_desc.texture.depth_or_layers;
            }

            if (resource_desc.texture.format == reshade::api::format::r16g16b16a16_float)
            {
               desc.format = reshade::api::format::r16g16b16a16_float;
            }
            else if (resource_desc.texture.format == reshade::api::format::r32_typeless || resource_desc.texture.format == reshade::api::format::r32_float || resource_desc.texture.format == reshade::api::format::d32_float)
            {
               if ((usage_type & reshade::api::resource_usage::depth_stencil) != 0)
               {
                  desc.format = reshade::api::format::d32_float;
               }
               else
               {
                  ASSERT_ONCE(IsFloatFormat(DXGI_FORMAT(desc.format))); // We might not want to use a float view in this case? We probably do anyway!
                  desc.format = reshade::api::format::r32_float;
               }
            }
            else if (resource_desc.texture.format == reshade::api::format::r32_g8_typeless || resource_desc.texture.format == reshade::api::format::d32_float_s8_uint)
            {
               if ((usage_type & reshade::api::resource_usage::depth_stencil) != 0)
               {
                  desc.format = reshade::api::format::d32_float_s8_uint;
               }
               else
               {
                  // If we got here, the game would have originally been using a depth buffer with a stencil, so preserve the right stencil/depth view
                  desc.format = (desc.format == reshade::api::format::x24_unorm_g8_uint) ? reshade::api::format::x32_float_g8_uint : reshade::api::format::r32_float_x8_uint;
               }
            }
            else
            {
               if (IsTypelessFormat(DXGI_FORMAT(resource_desc.texture.format)))
               {
                  ASSERT_ONCE(!IsTypelessFormat(DXGI_FORMAT(desc.format)));
                  ASSERT_ONCE(GetTypelessFormat(DXGI_FORMAT(desc.format)) == DXGI_FORMAT(resource_desc.texture.format));
                  // TODO: return the format we upgraded the resource to, if it's not typeless, otherwise restore the most common one? FLOAT?
               }
               else
               {
                  ASSERT_ONCE(GetTypelessFormat(DXGI_FORMAT(desc.format)) == GetTypelessFormat(DXGI_FORMAT(resource_desc.texture.format))); // Just verify the game tried to create the resource view with the proper (upgraded) format already? This might trigger and we might need to remove it
                  desc.format = resource_desc.texture.format; // TODO: select the best version for e.g. depth views etc
               }
            }
            return true;
         }
      }

#if DEVELOPMENT
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      const std::shared_lock lock(device_data.mutex);
      if (desc.format != reshade::api::format::r16g16b16a16_float)
      {
         ASSERT_ONCE(!device_data.upgraded_resources.contains(resource.handle)); // Why did we get here in this case?
      }
      if (device_data.upgraded_resources.contains(resource.handle))
      {
         ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
         ID3D11Texture2D* texture_2d = nullptr;
         ID3D11Texture3D* texture_3d = nullptr;
         ID3D11Texture1D* texture_1d = nullptr;
         // Note: it's possible to use "ID3D11Resource::GetType()" instead of this
         HRESULT hr_2d = native_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&texture_2d);
         HRESULT hr_3d = native_resource->QueryInterface(__uuidof(ID3D11Texture3D), (void**)&texture_3d);
         HRESULT hr_1d = native_resource->QueryInterface(__uuidof(ID3D11Texture1D), (void**)&texture_1d);
         D3D11_TEXTURE2D_DESC texture_2d_desc;
         D3D11_TEXTURE3D_DESC texture_3d_desc;
         D3D11_TEXTURE1D_DESC texture_1d_desc;
         reshade::api::format format = desc.format;
         if (SUCCEEDED(hr_2d) && texture_2d != nullptr)
         {
            texture_2d->GetDesc(&texture_2d_desc);
            ASSERT_ONCE(desc.type == reshade::api::resource_view_type::texture_2d || desc.type == reshade::api::resource_view_type::texture_2d_array || desc.type == reshade::api::resource_view_type::texture_2d_multisample || desc.type == reshade::api::resource_view_type::texture_2d_multisample_array || desc.type == reshade::api::resource_view_type::texture_cube || desc.type == reshade::api::resource_view_type::texture_cube_array);
            ASSERT_ONCE(desc.type != reshade::api::resource_view_type::unknown);
            format = reshade::api::format(texture_2d_desc.Format);
         }
         else if (SUCCEEDED(hr_3d) && texture_3d != nullptr)
         {
            texture_3d->GetDesc(&texture_3d_desc);
            ASSERT_ONCE(desc.type == reshade::api::resource_view_type::texture_3d);
            ASSERT_ONCE(desc.type != reshade::api::resource_view_type::unknown);
            format = reshade::api::format(texture_3d_desc.Format);
         }
         else if (SUCCEEDED(hr_1d) && texture_1d != nullptr)
         {
            texture_1d->GetDesc(&texture_1d_desc);
            ASSERT_ONCE(desc.type == reshade::api::resource_view_type::texture_1d);
            ASSERT_ONCE(desc.type != reshade::api::resource_view_type::unknown);
            format = reshade::api::format(texture_1d_desc.Format);
         }
         else
         {
            ASSERT_ONCE_MSG(false, "Unexpected texture format");
         }
         if (texture_2d)
            texture_2d->Release();
         if (texture_3d)
            texture_3d->Release();
         if (texture_1d)
            texture_1d->Release();

         last_attempted_upgraded_resource_view_creation_view_format = desc.format;
         if (last_attempted_upgraded_resource_view_creation_view_format == reshade::api::format::unknown && device_data.original_upgraded_resources_formats.contains(resource.handle))
         {
            last_attempted_upgraded_resource_view_creation_view_format = device_data.original_upgraded_resources_formats[resource.handle];
         }
         if (desc.type == reshade::api::resource_view_type::unknown)
         {
            const reshade::api::resource_desc resource_desc = device->get_resource_desc(resource);
            if (resource_desc.type == reshade::api::resource_type::texture_3d)
            {
               desc.type = reshade::api::resource_view_type::texture_3d;
            }
            else if (resource_desc.type == reshade::api::resource_type::texture_1d)
            {
               desc.type = reshade::api::resource_view_type::texture_1d;
            }
            else
            {
               desc.type = resource_desc.texture.samples <= 1 ? (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d : ((resource_desc.flags & reshade::api::resource_flags::cube_compatible) != 0 ? (resource_desc.texture.depth_or_layers == 6 ? reshade::api::resource_view_type::texture_cube : reshade::api::resource_view_type::texture_cube_array) : reshade::api::resource_view_type::texture_2d_array)) : (resource_desc.texture.depth_or_layers <= 1 ? reshade::api::resource_view_type::texture_2d_multisample : reshade::api::resource_view_type::texture_2d_multisample_array); // We need to set it in case it was "reshade::api::resource_view_type::unknown", otherwise the format would also need to be unknown
            }
         }
         desc.format = format;
         return true;
      }
#endif // DEVELOPMENT
      return false;
   }

   void OnInitResourceView(
      reshade::api::device* device,
      reshade::api::resource resource,
      reshade::api::resource_usage usage_type,
      const reshade::api::resource_view_desc& desc,
      reshade::api::resource_view view)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      std::unique_lock lock(device_data.mutex);

#if DEVELOPMENT
      if (device_data.original_upgraded_resources_formats.contains(resource.handle))
      {
         // Only the last attempted creation would matter
         device_data.original_upgraded_resource_views_formats.emplace(
            view.handle, // Key
            std::make_pair(resource.handle, last_attempted_upgraded_resource_view_creation_view_format) // Value
         );
      }
#endif

      auto original_resource_to_mirrored_upgraded_resource = device_data.original_resources_to_mirrored_upgraded_resources.find(resource.handle);
      if (original_resource_to_mirrored_upgraded_resource != device_data.original_resources_to_mirrored_upgraded_resources.end())
      {
         reshade::api::resource_view_desc upgraded_desc = desc;
         upgraded_desc.format = reshade::api::format::r16g16b16a16_float;

         lock.unlock(); // Avoids deadlocks with the device
         reshade::api::resource_view mirrored_upgraded_resource_view;
         if (device->create_resource_view({original_resource_to_mirrored_upgraded_resource->second}, usage_type, upgraded_desc, &mirrored_upgraded_resource_view))
         {
            lock.lock();
            device_data.original_resource_views_to_mirrored_upgraded_resource_views[view.handle] = mirrored_upgraded_resource_view.handle;
         }
      }
   }

   // TODO: put a test for resources that failed to be upgraded after changing resolution because they were created before the the swapchain changed res
   void OnDestroyResourceView(
      reshade::api::device* device,
      reshade::api::resource_view view)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();
      if (&device_data == nullptr) // TODO: this can happen if DX destroyed the device before any of its resource views. We should store a list of devices that have had their destructor called, and thus have lost the device data.
         return;
      std::unique_lock lock(device_data.mutex);

#if DEVELOPMENT
      device_data.original_upgraded_resource_views_formats.erase(view.handle);
#endif

      auto original_resource_view_to_mirrored_upgraded_resource_view = device_data.original_resource_views_to_mirrored_upgraded_resource_views.find(view.handle);
      if (!ignore_indirect_upgraded_textures && original_resource_view_to_mirrored_upgraded_resource_view != device_data.original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         const auto mirrored_upgraded_resource_view = original_resource_view_to_mirrored_upgraded_resource_view->second;
         device_data.original_resource_views_to_mirrored_upgraded_resource_views.erase(original_resource_view_to_mirrored_upgraded_resource_view);
         lock.unlock(); // Avoids deadlocks with the device
         device->destroy_resource_view({ mirrored_upgraded_resource_view });
      }
   }

   void OnPushDescriptors(
      reshade::api::command_list* cmd_list,
      reshade::api::shader_stage stages,
      reshade::api::pipeline_layout layout,
      uint32_t layout_param,
      const reshade::api::descriptor_table_update& update)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      if (test_index == 11) return;

      switch (update.type)
      {
      default:
      break;
      case reshade::api::descriptor_type::texture_unordered_access_view:
      case reshade::api::descriptor_type::texture_shader_resource_view:
      {
         reshade::api::descriptor_table_update replaced_update = update;
         bool any_replaced = false;

         {
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            std::shared_lock lock_device_read(device_data.mutex);

            reshade::api::resource_view* rvs = (reshade::api::resource_view*)replaced_update.descriptors;
            reshade::api::resource_usage resource_usage = reshade::api::resource_usage::shader_resource; // Implied by default
            if (update.type == reshade::api::descriptor_type::texture_unordered_access_view)
            {
               resource_usage = reshade::api::resource_usage::unordered_access;
            }

            for (uint32_t i = 0; i < replaced_update.count; i++)
            {
               any_replaced |= FindOrCreateIndirectUpgradedResourceView(cmd_list->get_device(), rvs[i].handle, rvs[i].handle, device_data, enable_automatic_indirect_texture_format_upgrades, resource_usage, lock_device_read);
            }
         }

         if (any_replaced)
         {
            cmd_list->push_descriptors(stages, layout, layout_param, replaced_update);
         }
         break;
      }
#if DEVELOPMENT
      case reshade::api::descriptor_type::constant_buffer:
      {
         for (uint32_t i = 0; i < update.count; i++)
         {
            const reshade::api::buffer_range& buffer_range = static_cast<const reshade::api::buffer_range*>(update.descriptors)[i];
            ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(buffer_range.buffer.handle);

            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::BindResource;
               trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
               // Re-use the SRV data for simplicity
               GetResourceInfo(buffer, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_type_name[0], &trace_draw_call_data.sr_hash[0]);
               cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
            }
         }
      }
#endif
      case reshade::api::descriptor_type::sampler:
      {
         if (!enable_samplers_upgrade)
            break;

         auto* device = cmd_list->get_device();
         DeviceData& device_data = *device->get_private_data<DeviceData>();

#if 1 // Optimization hack (it's currently fine with ReShade's code, that data is created on the spot and never used again)
         reshade::api::descriptor_table_update& custom_update = const_cast<reshade::api::descriptor_table_update&>(update);
#else
         reshade::api::descriptor_table_update custom_update = update;
#endif
         bool any_modified = false;
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         for (uint32_t i = 0; i < update.count; i++)
         {
            const reshade::api::sampler& sampler = static_cast<const reshade::api::sampler*>(update.descriptors)[i];

            const auto custom_sampler_by_original_sampler_it = device_data.custom_sampler_by_original_sampler.find(sampler.handle);
            if (custom_sampler_by_original_sampler_it != device_data.custom_sampler_by_original_sampler.end())
            {
               auto& custom_samplers = custom_sampler_by_original_sampler_it->second;
               const auto custom_sampler_it = custom_samplers.find(device_data.texture_mip_lod_bias_offset);
               const ID3D11SamplerState* custom_sampler_ptr = nullptr;
               // Create the version of this sampler to match the current mip lod bias
               if (custom_sampler_it == custom_samplers.end())
               {
                  const auto last_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
                  shared_lock_samplers.unlock();
                  {
                     ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                     D3D11_SAMPLER_DESC native_desc;
                     native_sampler->GetDesc(&native_desc);
                     com_ptr<ID3D11SamplerState> custom_sampler = CreateCustomSampler(device_data, (ID3D11Device*)device->get_native(), native_desc);
                     std::unique_lock unique_lock_samplers(s_mutex_samplers); // Only lock for reading if necessary. It doesn't matter if we released the shared lock above for a tiny amount of time, it's safe anyway
                     custom_samplers[last_texture_mip_lod_bias_offset] = custom_sampler;
                     custom_sampler_ptr = custom_samplers[last_texture_mip_lod_bias_offset].get();
                  }
                  shared_lock_samplers.lock();
               }
               else
               {
                  custom_sampler_ptr = custom_sampler_it->second.get();
               }
               // Update the customized descriptor data
               if (custom_sampler_ptr != nullptr)
               {
                  reshade::api::sampler& custom_sampler = ((reshade::api::sampler*)(custom_update.descriptors))[i];
                  custom_sampler.handle = (uint64_t)custom_sampler_ptr;
                  any_modified |= true;
               }
            }
            else
            {
#if DEVELOPMENT && 0
               // If recursive (already cloned) sampler ptrs are set, it's either because:
               // - the game created the same sampler as one of our upgraded ones, and in DX11 state objects used a shared pool memory so if you try to create two with the same desc, it returns the previously created one
               // - the game somehow got the pointers (e.g. DX get samples functions) and is re-using them
               // this seems to happen when we change the ImGui settings for samplers a lot and quickly in Prey. It also happens in Mafia III and BioShock 2 Remastered. It shouldn't really hurt as they don't pass through the same init function.
               bool recursive_or_null = sampler.handle == 0;
               for (const auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
               {
                  for (const auto& custom_sampler_handle : samplers_handle.second)
                  {
                     ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                     recursive_or_null |= custom_sampler_handle.second.get() == native_sampler;
                  }
               }
               ASSERT_ONCE(recursive_or_null || samplers_upgrade_mode == 0); // Shouldn't happen anymore, because now we re-add recursively created samples to the "custom_sampler_by_original_sampler" list anyway! (if we know the sampler set is "recursive", then we are good and don't need to replace this sampler again)
#if 0 // TODO: delete or restore in case the "recursive_or_null" assert above ever triggered (seems like it won't, and the problem has been fixed)
               if (sampler.handle != 0)
               {
                  ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(sampler.handle);
                  D3D11_SAMPLER_DESC native_desc;
                  native_sampler->GetDesc(&native_desc);
                  custom_sampler_by_original_sampler[sampler.handle] = CreateCustomSampler(device_data, (ID3D11Device*)device->get_native(), native_desc);
               }
#endif
#endif // DEVELOPMENT
            }
         }

         if (any_modified)
         {
            cmd_list->push_descriptors(stages, layout, layout_param, custom_update);
         }
         break;
      }
      }
   }

#if DEVELOPMENT
   void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();

      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            auto& cmd_list_data = *device_data.primary_command_list_data;
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = access == reshade::api::map_access::read_only ? TraceDrawCallData::TraceDrawCallType::CPURead : TraceDrawCallData::TraceDrawCallType::CPUWrite; // The writes could be read too, but we don't have a type for that yet
            trace_draw_call_data.command_list = device_data.primary_command_list;
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
            // Re-use the SRV/RTV data for simplicity
            if (trace_draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead)
               GetResourceInfo(target_resource, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_type_name[0], &trace_draw_call_data.sr_hash[0]);
            else
               GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
   }

   bool OnUpdateBufferRegionCommand_Common(reshade::api::device* device, reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size)
   {
      if (!device)
      {
         device = cmd_list->get_device();
      }
      DeviceData& device_data = *device->get_private_data<DeviceData>();

      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            auto& cmd_list_data = cmd_list ? *cmd_list->get_private_data<CommandListData>() : *device_data.primary_command_list_data;
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CPUWrite;
            trace_draw_call_data.command_list = cmd_list ? (ID3D11DeviceContext*)(cmd_list->get_native()) : device_data.primary_command_list;
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }

      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(dest.handle);
      // Verify that we didn't miss any changes to the global g-buffer
      ASSERT_ONCE(!device_data.has_drawn_main_post_processing_previous || !device_data.cb_per_view_global_buffers.contains(buffer));

      return false;
   }

   bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);
      return OnUpdateBufferRegionCommand_Common(nullptr, cmd_list, data, dest, dest_offset, size);
   }

   bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);
      return OnUpdateBufferRegionCommand_Common(device, nullptr, data, resource, offset, size);
   }
#endif // DEVELOPMENT

   void OnMapTextureRegion(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource, const reshade::api::subresource_box* box, reshade::api::map_access access, reshade::api::subresource_data* data)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            auto& cmd_list_data = *device_data.primary_command_list_data;
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = access == reshade::api::map_access::read_only ? TraceDrawCallData::TraceDrawCallType::CPURead : TraceDrawCallData::TraceDrawCallType::CPUWrite; // The writes could be read too, but we don't have a type for that yet
            trace_draw_call_data.command_list = device_data.primary_command_list;
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
            // Re-use the SRV/RTV data for simplicity
            if (trace_draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead)
               GetResourceInfo(target_resource, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_type_name[0], &trace_draw_call_data.sr_hash[0]);
            else
               GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }

#if GAME_PREY // For Prey only (given we manually upgrade resources through native hooks)
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
      com_ptr<ID3D11Texture2D> resource_texture;
      HRESULT hr = native_resource->QueryInterface(&resource_texture);
      if (SUCCEEDED(hr))
      {
         D3D11_TEXTURE2D_DESC texture_2d_desc;
         resource_texture->GetDesc(&texture_2d_desc);
         if ((texture_2d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0)
         {
            // Probably not supported, at least if it's an RT and we upgraded the format
            ASSERT_ONCE(texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS && texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT);
         }
      }
#endif
#endif // DEVELOPMENT

      bool needs_downgrade = false;
      bool needs_conversion = false;
      {
         const std::shared_lock lock(device_data.mutex);
         needs_downgrade = device_data.upgraded_resources.contains(resource.handle);
         needs_conversion = needs_downgrade || device_data.original_resources_to_mirrored_upgraded_resources.contains(resource.handle);
      }
      // If this happened, we need to upgrade the data passed in to match the new format!
      // This happens in Dishonored 2 and Thief on boot, to update videos or splash screens.
      if (needs_conversion)
      {
         ASSERT_ONCE(access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard); // For now we only support write, games generally don't read back textures from the CPU if not for screenshots or for save game snapshots
         ASSERT_ONCE(data && data->data); // If this is nullptr, it might be a "ID3D11DeviceContext3::WriteToSubresource", which we don't support!

         upgraded_mapped_resources[resource.handle] = data;

         if (needs_downgrade)
         {
            reshade::api::resource_desc desc = device->get_resource_desc(resource);
            // Update the stride to match the original format, we want the game to write to the resource as if it was the original format.
            // This assumes two things:
            // -The new format is wider and thus there will be enough memory.
            // -The game doesn't dynamically branch on mapping behaviour based on the current (upgraded) resource format, already acknowledging the upgraded format (most games wouldn't, especially not the old ones)
            constexpr size_t bytes_per_pixel = 4; // 4 for 8bpc, 8 for 16bpc // TODO: we are assuming the original was R8G8B8A8, that is not necessarily true, we need to map what was the original format of all upgraded resources. Same assumption in the unmap function.
            data->row_pitch = desc.texture.width * bytes_per_pixel;
         }
      }
   }

   void OnUnmapTextureRegion(reshade::api::device* device, reshade::api::resource resource, uint32_t subresource)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());

      DeviceData& device_data = *device->get_private_data<DeviceData>();

      if (auto it = upgraded_mapped_resources.find(resource.handle); it != upgraded_mapped_resources.end())
      {
         reshade::api::subresource_data* data = it->second;

         bool direct_upgraded = false;
         uint64_t indirect_upgraded_resource = 0;
         {
            const std::shared_lock lock(device_data.mutex);
            direct_upgraded = device_data.upgraded_resources.contains(resource.handle);
            indirect_upgraded_resource = MapFindOrDefaultValue(device_data.original_resources_to_mirrored_upgraded_resources, resource.handle, 0); // The indirect upgraded resource is guaranteed to be kept alive if the base one also is
         }

         // If we have an indirect upgrade, create an upgraded version of the data and map it in the indirect upgraded texture
         if (indirect_upgraded_resource != 0)
         {
            reshade::api::resource_desc original_desc = device->get_resource_desc(resource);
            reshade::api::resource_desc upgraded_desc = device->get_resource_desc({indirect_upgraded_resource});

            reshade::api::subresource_data new_data = *data;
            if (ConvertResourceData(data, new_data, original_desc, upgraded_desc.texture.format))
            {
               if (device->map_texture_region({indirect_upgraded_resource}, subresource, nullptr, reshade::api::map_access::write_discard, &new_data))
               {
                  device->unmap_texture_region({indirect_upgraded_resource}, subresource);
               }
               delete[] static_cast<uint8_t*>(new_data.data);
            }
         }

         // Usually a resource would be either directly or indirectly upgraded, but let's keep support to have both happen at the same time, especially useful to swap formats live during development
         if (direct_upgraded)
         {
            reshade::api::resource_desc upgraded_desc = device->get_resource_desc(resource);
            reshade::api::resource_desc original_desc = upgraded_desc;
            original_desc.texture.format = reshade::api::format::r8g8b8a8_unorm; // Guessed

            const size_t buffer_size = original_desc.texture.height * original_desc.texture.depth_or_layers * data->row_pitch; // Automatic reconstruction based on what we told the game

            // Copy aside the data the game wrote for the texture format it thought this texture would have,
            // and then write it back in the same mapped memory, where it was originally meant to be anyway.
            reshade::api::subresource_data old_data = *data;
            old_data.data = new uint8_t[buffer_size];
            bool pre_allocated = true; // The "data" was already allocated by DirectX on the direct upgraded resource, so it's of the right size already, and we couldn't replace the pointer anyway.
            ConvertResourceData(&old_data, *data, original_desc, upgraded_desc.texture.format, pre_allocated);
            delete[] static_cast<uint8_t*>(old_data.data);
         }

         upgraded_mapped_resources.erase(it);
      }
   }

   bool OnUpdateTextureRegion(reshade::api::device* device, const reshade::api::subresource_data& data, reshade::api::resource resource, uint32_t subresource, const reshade::api::subresource_box* box)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api(), false);

      DeviceData& device_data = *device->get_private_data<DeviceData>();

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            auto& cmd_list_data = *device_data.primary_command_list_data;
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CPUWrite;
            trace_draw_call_data.command_list = device_data.primary_command_list;
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }

#if GAME_PREY // For Prey only (given we manually upgrade resources through native hooks)
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);
      com_ptr<ID3D11Texture2D> resource_texture;
      HRESULT hr = native_resource->QueryInterface(&resource_texture);
      if (SUCCEEDED(hr))
      {
         D3D11_TEXTURE2D_DESC texture_2d_desc;
         resource_texture->GetDesc(&texture_2d_desc);
         if ((texture_2d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0)
         {
            // Probably not supported, at least if it's an RT and we upgraded the format
            ASSERT_ONCE(texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_TYPELESS && texture_2d_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT);
         }
      }
#endif
#endif // DEVELOPMENT

      bool convert_data = false;
      reshade::api::resource_desc original_desc;
      reshade::api::resource_desc upgraded_desc;
      {
         const std::shared_lock lock(device_data.mutex);
         if (device_data.upgraded_resources.contains(resource.handle)) // This cannot be a swapchain texture, they can't be written by the CPU
         {
            upgraded_desc = device->get_resource_desc(resource);
            original_desc = upgraded_desc;
            // TODO: stop randomly guessing the format and actually cache it aside! Note that some games might dynamically pre-convert the data to match the upgraded target format, however, that's unlikely
            ASSERT_ONCE(false);
            original_desc.texture.format = reshade::api::format::r8g8b8a8_unorm;
            convert_data = true;
         }
         auto original_resource_to_mirrored_upgraded_resource = device_data.original_resources_to_mirrored_upgraded_resources.find(resource.handle);
         if (!ignore_indirect_upgraded_textures && original_resource_to_mirrored_upgraded_resource != device_data.original_resources_to_mirrored_upgraded_resources.end())
         {
            upgraded_desc = device->get_resource_desc({ original_resource_to_mirrored_upgraded_resource->second });
            original_desc = device->get_resource_desc(resource);
            resource.handle = original_resource_to_mirrored_upgraded_resource->second;
            convert_data = true;
         }
      }
      if (convert_data)
      {
         reshade::api::subresource_data new_data = data;
         if (ConvertResourceData(&data, new_data, original_desc, upgraded_desc.texture.format))
         {
            device->update_texture_region(new_data, resource, subresource, box);
            delete[] static_cast<uint8_t*>(new_data.data);
            return true;
         }
      }

      return false;
   }

   bool OnClearDepthStancilView(reshade::api::command_list* cmd_list, reshade::api::resource_view dsv, const float* depth, const uint8_t* stencil, uint32_t rect_count, const reshade::api::rect* rects)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running && dsv.handle != 0)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(dsv).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif
      
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      std::shared_lock lock(device_data.mutex);
      auto original_resource_view_to_mirrored_upgraded_resource_view = device_data.original_resource_views_to_mirrored_upgraded_resource_views.find(dsv.handle);
      if (!ignore_indirect_upgraded_textures && original_resource_view_to_mirrored_upgraded_resource_view != device_data.original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         lock.unlock(); // Avoids deadlock with the device
         cmd_list->clear_depth_stencil_view({ original_resource_view_to_mirrored_upgraded_resource_view->second }, depth, stencil, rect_count, rects);
         return true;
      }

      return false;
   }

   bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running && rtv.handle != 0)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(rtv).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif

      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      std::shared_lock lock(device_data.mutex);
      auto original_resource_view_to_mirrored_upgraded_resource_view = device_data.original_resource_views_to_mirrored_upgraded_resource_views.find(rtv.handle);
      if (!ignore_indirect_upgraded_textures && original_resource_view_to_mirrored_upgraded_resource_view != device_data.original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         lock.unlock(); // Avoids deadlock with the device
         cmd_list->clear_render_target_view({ original_resource_view_to_mirrored_upgraded_resource_view->second }, color, rect_count, rects);
         return true;
      }

      return false;
   }

   bool OnClearUnorderedAccessViewUInt(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const uint32_t values[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(uav).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif

      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      std::shared_lock lock(device_data.mutex);
      auto original_resource_view_to_mirrored_upgraded_resource_view = device_data.original_resource_views_to_mirrored_upgraded_resource_views.find(uav.handle);
      if (!ignore_indirect_upgraded_textures && original_resource_view_to_mirrored_upgraded_resource_view != device_data.original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         lock.unlock(); // Avoids deadlock with the device
         float upgraded_values[4];
         upgraded_values[0] = values[0]; // TODO: do these need to be converted from 0-255 to 0-1 or something?
         upgraded_values[1] = values[1];
         upgraded_values[2] = values[2];
         upgraded_values[3] = values[3];
         cmd_list->clear_unordered_access_view_float({ original_resource_view_to_mirrored_upgraded_resource_view->second }, upgraded_values, rect_count, rects); // Use the float clearing function as all upgraded textures are R16G16B16A16_FLOAT at the moment
         return true;
      }

      return false;
   }

   bool OnClearUnorderedAccessViewFloat(reshade::api::command_list* cmd_list, reshade::api::resource_view uav, const float values[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::ClearResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(uav).handle);
            // Re-use the RTV data for simplicity
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif

      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      std::shared_lock lock(device_data.mutex);
      auto original_resource_view_to_mirrored_upgraded_resource_view = device_data.original_resource_views_to_mirrored_upgraded_resource_views.find(uav.handle);
      if (!ignore_indirect_upgraded_textures && original_resource_view_to_mirrored_upgraded_resource_view != device_data.original_resource_views_to_mirrored_upgraded_resource_views.end())
      {
         lock.unlock(); // Avoids deadlock with the device
         cmd_list->clear_unordered_access_view_float({ original_resource_view_to_mirrored_upgraded_resource_view->second }, values, rect_count, rects);
         return true;
      }

      return false;
   }

   void OnCopyResource_Debug(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN)
   {
#if DEVELOPMENT
      {
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::CopyResource;
            trace_draw_call_data.command_list = (ID3D11DeviceContext*)(cmd_list->get_native());
            ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            // Re-use the SRV and RTV data for simplicity
            GetResourceInfo(source_resource, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_type_name[0], &trace_draw_call_data.sr_hash[0]);
            GetResourceInfo(target_resource, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
         }
      }
#endif
   }

   // Allows copying between incompatible resource formats, passing through a pixel shader
   // TODO: add reshade logs for final users in case a copy failed (incompatible formats due to luma texture upgrades), or a texture map call was made with an upgraded texture, or the texture initial data pointer was for another format (due to upgrades again) etc
   bool OnCopyResource_Internal(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN, bool forced = false)
   {
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

      if (!forced && !enable_upgraded_texture_resource_copy_redirection)
         return false;

      if (!forced && swapchain_format_upgrade_type == TextureFormatUpgradesType::None && texture_format_upgrades_type == TextureFormatUpgradesType::None) // Optimization
         return false;

      bool upgraded_resources = true;
      std::shared_lock device_read_lock(device_data.mutex);
      // Skip if none of the resources match our upgraded ones.
      // This should always be fine, unless the game used the upgraded resource desc to automatically determine other textures (so we try to catch for that in development)
      if (!forced && !device_data.upgraded_resources.contains(source.handle) && !device_data.upgraded_resources.contains(dest.handle) && (swapchain_upgrade_type == SwapchainUpgradeType::None || (!device_data.back_buffers.contains(source.handle) && !device_data.back_buffers.contains(dest.handle))))
      {
#if !DEVELOPMENT
         return false;
#endif
         upgraded_resources = false;
      }
      device_read_lock.unlock();

      ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
      com_ptr<ID3D11Texture2D> source_resource_texture;
      HRESULT hr = source_resource->QueryInterface(&source_resource_texture);
      if (SUCCEEDED(hr))
      {
         ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
         com_ptr<ID3D11Texture2D> target_resource_texture;
         hr = target_resource->QueryInterface(&target_resource_texture);
         if (SUCCEEDED(hr))
         {
            D3D11_TEXTURE2D_DESC source_desc;
            D3D11_TEXTURE2D_DESC target_desc;
            source_resource_texture->GetDesc(&source_desc);
            target_resource_texture->GetDesc(&target_desc);

            // No need to error out on these are the copies would have fail in the vanilla game as well
            if (source_desc.Width != target_desc.Width || source_desc.Height != target_desc.Height)
               return false;
            if (source_desc.ArraySize != target_desc.ArraySize || source_desc.MipLevels != target_desc.MipLevels)
               return false;

            auto isUnorm8 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                  case DXGI_FORMAT_R8G8B8A8_UNORM:
                  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                  case DXGI_FORMAT_B8G8R8A8_UNORM:
                  case DXGI_FORMAT_B8G8R8X8_UNORM:
                  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                  case DXGI_FORMAT_B8G8R8X8_TYPELESS:
                  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                  // Hacky case (just as a shortcut)
                  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                  case DXGI_FORMAT_R10G10B10A2_UNORM:
                  return true;
                  }
                  return false;
               };
            auto isUnorm16 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                  case DXGI_FORMAT_R16G16B16A16_UNORM:
                  return true;
                  }
                  return false;
               };
            auto isFloat16 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                  case DXGI_FORMAT_R16G16B16A16_FLOAT:
                  return true;
                  }
                  return false;
               };
            auto isFloat11 = [](DXGI_FORMAT format)
               {
                  switch (format)
                  {
                  case DXGI_FORMAT_R11G11B10_FLOAT:
                  return true;
                  }
                  return false;
               };

            // If we detected incompatible formats that were likely caused by Luma upgrading texture formats (of render targets only...),
            // do the copy in shader. It should currently cover all texture formats upgradable with "texture_upgrade_formats".
            // If we ever made a new type of "swapchain_upgrade_type", this should be updated for that.
            // Note that generally, formats of the same size might be supported as it simply does a byte copy,
            // like DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UNORM and DXGI_FORMAT_R16G16B16A16_FLOAT are all mutually compatible.
            // TODO: add gamma to linear support (e.g. non sRGB views into sRGB views)? Also just use AreFormatsCopyCompatible(format1, format2)
            if (((isUnorm8(target_desc.Format) || isFloat11(target_desc.Format)) && isFloat16(source_desc.Format))
               || ((isUnorm8(source_desc.Format) || isFloat11(source_desc.Format)) && isFloat16(target_desc.Format)))
            {
               ASSERT_ONCE_MSG(forced || upgraded_resources, "The game seemingly tried to copy incompatible resource formats for resources that were not upgraded by us");

               const auto* device = cmd_list->get_device();
               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

               if (source_desc.SampleDesc.Count != target_desc.SampleDesc.Count)
               {
                  // Replace the source resource with one that has MSAA solved if this copy resource call was both a copy and an MSAA resolve towards a different format
                  if (source_desc.SampleDesc.Count > target_desc.SampleDesc.Count && target_desc.SampleDesc.Count == 1)
                  {
                     com_ptr<ID3D11Texture2D> resolved_source_resource_texture = CloneTexture<ID3D11Texture2D>(native_device, source_resource_texture.get(), DXGI_FORMAT_UNKNOWN, 0, 0, false, false, native_device_context, -1, 1); // Not optimized but whatever
                     native_device_context->ResolveSubresource(resolved_source_resource_texture.get(), 0, source_resource_texture.get(), 0, format);

                     resolved_source_resource_texture->GetDesc(&source_desc);
                     source_resource_texture = resolved_source_resource_texture;
                  }
                  else
                  {
                     ASSERT_ONCE_MSG(!upgraded_resources, "A resource with MSAA cannot be copied on a resource without SMAA (or with a lower MSAA samples count)");
                     return false;
                  }
               }

               // These are not supported at the moment
               if (target_desc.ArraySize != 1 || target_desc.SampleDesc.Count != 1 || target_desc.MipLevels != 1)
               {
                  ASSERT_ONCE_MSG(false, "Unsupported resource desc in redirected resource copy");
                  return false;
               }

               com_ptr<ID3D11VertexShader> vs;
               com_ptr<ID3D11PixelShader> ps;
               com_ptr<ID3D11Texture2D> temp_copy_source_texture;
               com_ptr<ID3D11Texture2D> temp_copy_target_texture;
               {
                  device_read_lock.lock();
                  temp_copy_source_texture = device_data.temp_copy_source_texture;
                  temp_copy_target_texture = device_data.temp_copy_target_texture;

                  const std::shared_lock lock(s_mutex_shader_objects);
                  vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")];
                  ps = device_data.native_pixel_shaders[CompileTimeStringHash("Copy PS")];
                  device_read_lock.unlock();
               }
               if (vs == nullptr || ps == nullptr)
               {
                  ASSERT_ONCE_MSG(false, "The Copy Resource Luma native shaders failed to be found (they have either been unloaded or failed to compile, or simply missing in the files)");
                  // We can't continue, drawing with empty shaders would crash or skip the call
                  return false;
               }

               //
               // Prepare resources:
               //
               com_ptr<ID3D11Texture2D> proxy_source_resource_texture = source_resource_texture;
               // We need to make a double copy if the source texture isn't a shader resource
               if ((source_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
               {
                  D3D11_TEXTURE2D_DESC proxy_source_desc = {};
                  if (temp_copy_source_texture.get() != nullptr)
                  {
                     temp_copy_source_texture->GetDesc(&proxy_source_desc);
                  }
                  if (temp_copy_source_texture.get() == nullptr || proxy_source_desc.Width != source_desc.Width || proxy_source_desc.Height != source_desc.Height || proxy_source_desc.Format != source_desc.Format || proxy_source_desc.ArraySize != source_desc.ArraySize || proxy_source_desc.MipLevels != source_desc.MipLevels || proxy_source_desc.SampleDesc.Count != source_desc.SampleDesc.Count)
                  {
                     temp_copy_source_texture = CloneTexture<ID3D11Texture2D>(native_device, source_resource_texture.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS, false, true, native_device_context);
                     std::unique_lock device_write_lock(device_data.mutex);
                     device_data.temp_copy_source_texture = temp_copy_source_texture;
                  }
                  proxy_source_resource_texture = temp_copy_source_texture;
               }
               com_ptr<ID3D11ShaderResourceView> source_resource_texture_view;
               D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc;
               source_srv_desc.Format = source_desc.Format;
               // Redirect typeless and sRGB formats to classic UNORM, the "copy resource" functions wouldn't distinguish between these, as they copy by byte.
               switch (source_srv_desc.Format)
               {
               case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                  source_srv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                  break;
               case DXGI_FORMAT_R8G8B8A8_TYPELESS:
               case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                  source_srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                  break;
               case DXGI_FORMAT_B8G8R8A8_TYPELESS:
               case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                  source_srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                  break;
               case DXGI_FORMAT_B8G8R8X8_TYPELESS:
               case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                  source_srv_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
                  break;
               case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                  source_srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                  break;
               }
               source_srv_desc.ViewDimension = D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
               source_srv_desc.Texture2D.MipLevels = 1;
               source_srv_desc.Texture2D.MostDetailedMip = 0;
               hr = native_device->CreateShaderResourceView(proxy_source_resource_texture.get(), &source_srv_desc, &source_resource_texture_view);
               ASSERT_ONCE(SUCCEEDED(hr));

               com_ptr<ID3D11Texture2D> proxy_target_resource_texture = target_resource_texture;
               // We need to make a double copy if the target texture isn't a render target, unfortunately (we could intercept its creation and add the flag, or replace any further usage in this frame by redirecting all pointers
               // to the new copy we made, but for now this works)
               // TODO: we could also check if the target texture supports UAV writes (unlikely) and fall back on a Copy Compute Shader instead of a Pixel Shader, to avoid two/three further texture copies, though that's a rare case
               if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
               {
                  // Create the persisting texture copy if necessary (if anything changed from the last copy).
                  // Theoretically all these textures have the same resolution as the screen so having one persistent texture should be ok.
                  // TODO: create more than one texture (one per format and one per resolution?) if ever needed, and maybe in the device context data, not device data
                  D3D11_TEXTURE2D_DESC proxy_target_desc;
                  if (temp_copy_target_texture.get() != nullptr)
                  {
                     temp_copy_target_texture->GetDesc(&proxy_target_desc);
                  }
                  if (temp_copy_target_texture.get() == nullptr || proxy_target_desc.Width != target_desc.Width || proxy_target_desc.Height != target_desc.Height || proxy_target_desc.Format != target_desc.Format || proxy_target_desc.ArraySize != target_desc.ArraySize || proxy_target_desc.MipLevels != target_desc.MipLevels || proxy_target_desc.SampleDesc.Count != target_desc.SampleDesc.Count)
                  {
                     temp_copy_target_texture = CloneTexture<ID3D11Texture2D>(native_device, target_resource_texture.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_RENDER_TARGET, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS, false, false);
                     std::unique_lock device_write_lock(device_data.mutex);
                     device_data.temp_copy_target_texture = temp_copy_target_texture;
                  }
                  proxy_target_resource_texture = temp_copy_target_texture;
               }

               com_ptr<ID3D11RenderTargetView> target_resource_texture_view;
               D3D11_RENDER_TARGET_VIEW_DESC target_rtv_desc;
               target_rtv_desc.Format = target_desc.Format;
               switch (target_rtv_desc.Format)
               {
               case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                  target_rtv_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
                  break;
               case DXGI_FORMAT_R8G8B8A8_TYPELESS:
               case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                  target_rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                  break;
               case DXGI_FORMAT_B8G8R8A8_TYPELESS:
               case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                  target_rtv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                  break;
               case DXGI_FORMAT_B8G8R8X8_TYPELESS:
               case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                  target_rtv_desc.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
                  break;
               case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                  target_rtv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                  break;
               }
               target_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
               target_rtv_desc.Texture2D.MipSlice = 0;
               hr = native_device->CreateRenderTargetView(proxy_target_resource_texture.get(), &target_rtv_desc, &target_resource_texture_view);
               ASSERT_ONCE(SUCCEEDED(hr));

               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

               // Clear the previous render target in case it was still set to the SRV we are using as source,
               // otherwise DX11 will force ignore the attempted SRV binding, to avoid read/write conflicts.
               native_device_context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 0, nullptr, nullptr);

               DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, vs.get(), ps.get(), source_resource_texture_view.get(), target_resource_texture_view.get(), target_desc.Width, target_desc.Height, true);

#if DEVELOPMENT
               {
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::shared_lock lock_generic(s_mutex_generic);
                     CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);

                     // Highlight that the next capture data is redirected
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = "Redirected Copy Resource";
                     GetResourceInfo(source_resource_texture.get(), trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_type_name[0], &trace_draw_call_data.sr_hash[0]);
                     GetResourceInfo(target_resource_texture.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
                  }
               }
#endif

               //
               // Copy our render target target resource into the non render target target resource if necessary:
               //
               if ((target_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0)
               {
                  native_device_context->CopyResource(target_resource_texture.get(), proxy_target_resource_texture.get());
               }

               draw_state_stack.Restore(native_device_context);
               return true;
            }
         }
      }

      return false;
   }

   bool OnCopyResource(reshade::api::command_list* cmd_list, reshade::api::resource source, reshade::api::resource dest)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      OnCopyResource_Debug(cmd_list, source, dest);

      {
         bool any_replaced = false;

         {
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            std::shared_lock lock_device_read(device_data.mutex);

            // Note: there a tiny chance the source is not an upgraded texture while the target one is, that's handled with a pixel shader copy below
            any_replaced |= FindOrCreateIndirectUpgradedResource(cmd_list->get_device(), source.handle, source.handle, source.handle, device_data, false, reshade::api::resource_usage::copy_source, lock_device_read);
            any_replaced |= FindOrCreateIndirectUpgradedResource(cmd_list->get_device(), source.handle, dest.handle, dest.handle, device_data, texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && enable_automatic_indirect_texture_format_upgrades, reshade::api::resource_usage::copy_dest, lock_device_read, false);
         }

         if (any_replaced)
         {
            uint4 size1, size2;
            DXGI_FORMAT format1, format2;
            GetResourceInfo(reinterpret_cast<ID3D11Resource*>(source.handle), size1, format1);
            GetResourceInfo(reinterpret_cast<ID3D11Resource*>(dest.handle), size2, format2);
            // TODO: are these even necessary anymore given that now we have "FindOrCreateIndirectUpgradedResource()"? Probably not! Though it could in case we had previously upgraded the target but not the source
            if (!AreFormatsCopyCompatible(format1, format2))
            {
               bool succeded = OnCopyResource_Internal(cmd_list, source, dest, DXGI_FORMAT_UNKNOWN, true);
               ASSERT_ONCE(succeded);
            }
            else
            {
               cmd_list->copy_resource(source, dest);
            }
            return true;
         }
      }

      return OnCopyResource_Internal(cmd_list, source, dest);
   }

   bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box* dest_box, reshade::api::filter_mode filter /*Unused in DX11*/)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      OnCopyResource_Debug(cmd_list, source, dest);

      {
         bool any_replaced = false;

         {
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            std::shared_lock lock_device_read(device_data.mutex);

            any_replaced |= FindOrCreateIndirectUpgradedResource(cmd_list->get_device(), source.handle, source.handle, source.handle, device_data, false, reshade::api::resource_usage::copy_source, lock_device_read);
            any_replaced |= FindOrCreateIndirectUpgradedResource(cmd_list->get_device(), source.handle, dest.handle, dest.handle, device_data, texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && enable_automatic_indirect_texture_format_upgrades, reshade::api::resource_usage::copy_dest, lock_device_read, false);
         }

         if (any_replaced)
         {
            uint4 size1, size2;
            DXGI_FORMAT format1, format2;
            GetResourceInfo(reinterpret_cast<ID3D11Resource*>(source.handle), size1, format1);
            GetResourceInfo(reinterpret_cast<ID3D11Resource*>(dest.handle), size2, format2);
            if (!AreFormatsCopyCompatible(format1, format2) && source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (!dest_box || (dest_box->left == 0 && dest_box->top == 0)) && (!dest_box || !source_box || (source_box->width() == dest_box->width() && source_box->height() == dest_box->height())))
            {
               bool succeded = OnCopyResource_Internal(cmd_list, source, dest, DXGI_FORMAT_UNKNOWN, true);
               ASSERT_ONCE(succeded);
            }
            else
            {
               cmd_list->copy_texture_region(source, source_subresource, source_box, dest, dest_subresource, dest_box, filter);
            }
            return true;
         }
      }

      if (source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (!dest_box || (dest_box->left == 0 && dest_box->top == 0)) && (!dest_box || !source_box || (source_box->width() == dest_box->width() && source_box->height() == dest_box->height())))
      {
         return OnCopyResource_Internal(cmd_list, source, dest);
      }
#if DEVELOPMENT
      else
      {
         // Make sure the copied resources aren't mismatch in format due to our upgrades!
         {
            ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            const std::shared_lock lock(device_data.mutex);
            if (enable_upgraded_texture_resource_copy_redirection && (device_data.upgraded_resources.contains(source.handle) || device_data.upgraded_resources.contains(dest.handle) || (swapchain_upgrade_type > SwapchainUpgradeType::None && (device_data.back_buffers.contains(source.handle) || device_data.back_buffers.contains(dest.handle)))))
            {
               ASSERT_ONCE(AreResourcesEqual(source_resource, target_resource)); // Note: this might catch some false positives too
            }
         }
      }
#endif
      return false;
   }

   bool OnResolveTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box* source_box, reshade::api::resource dest, uint32_t dest_subresource, uint32_t dest_x, uint32_t dest_y, uint32_t dest_z, reshade::api::format format)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api(), false);

      OnCopyResource_Debug(cmd_list, source, dest);

      {
         bool any_replaced = false;

         {
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            std::shared_lock lock_device_read(device_data.mutex);

            any_replaced |= FindOrCreateIndirectUpgradedResource(cmd_list->get_device(), source.handle, source.handle, source.handle, device_data, false, reshade::api::resource_usage::resolve_source, lock_device_read);
            any_replaced |= FindOrCreateIndirectUpgradedResource(cmd_list->get_device(), source.handle, dest.handle, dest.handle, device_data, texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && enable_automatic_indirect_texture_format_upgrades, reshade::api::resource_usage::resolve_dest, lock_device_read, false);
         }

         if (any_replaced)
         {
#if DEVELOPMENT || TEST
            uint4 size1, size2;
            DXGI_FORMAT format1, format2;
            GetResourceInfo(reinterpret_cast<ID3D11Resource*>(source.handle), size1, format1);
            GetResourceInfo(reinterpret_cast<ID3D11Resource*>(dest.handle), size2, format2);
            ASSERT_ONCE(AreFormatsCopyCompatible(format1, format2));
#endif

            format = reshade::api::format::r16g16b16a16_float;
            cmd_list->resolve_texture_region(source, source_subresource, source_box, dest, dest_subresource, dest_x, dest_y, dest_z, format);
            return true;
         }
      }

      if (source_subresource == 0 && dest_subresource == 0 && (!source_box || (source_box->left == 0 && source_box->top == 0)) && (dest_x == 0 && dest_y == 0 && dest_z == 0))
      {
         ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
         ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
         DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
         // If any of the resources has been upgraded but the format specified by the game doesn't match, enforce the right format
         if ((device_data.upgraded_resources.contains(source.handle) || device_data.upgraded_resources.contains(dest.handle) || (swapchain_upgrade_type > SwapchainUpgradeType::None && device_data.back_buffers.contains(dest.handle))) && DXGI_FORMAT(format) != DXGI_FORMAT_R16G16B16A16_FLOAT)
         {
            if (AreResourcesEqual(source_resource, target_resource, true, false))
            {
               ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
               native_device_context->ResolveSubresource(target_resource, dest_subresource, source_resource, source_subresource, DXGI_FORMAT_R16G16B16A16_FLOAT);
               return true;
            }
         }
         return OnCopyResource_Internal(cmd_list, source, dest, DXGI_FORMAT(format));
      }
#if DEVELOPMENT
      else
      {
         // Make sure the copied resources aren't mismatch in format due to our upgrades!
         {
            ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(source.handle);
            ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(dest.handle);
            DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
            const std::shared_lock lock(device_data.mutex);
            if (enable_upgraded_texture_resource_copy_redirection && (device_data.upgraded_resources.contains(source.handle) || device_data.upgraded_resources.contains(dest.handle) || (swapchain_upgrade_type > SwapchainUpgradeType::None && (device_data.back_buffers.contains(source.handle) || device_data.back_buffers.contains(dest.handle)))))
            {
               ASSERT_ONCE(AreResourcesEqual(source_resource, target_resource)); // Note: this might catch some false positives too
            }
         }
      }
#endif // DEVELOPMENT

      ASSERT_ONCE(false);
      return false;
   }

   void OnReShadePresent(reshade::api::effect_runtime* runtime)
   {
      SKIP_UNSUPPORTED_DEVICE_API(runtime->get_device()->get_api());

      DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();
#if DEVELOPMENT
      {
         const std::unique_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
#if _DEBUG && LOG_VERBOSE
            reshade::log::message(reshade::log::level::info, "present()");
            reshade::log::message(reshade::log::level::info, "--- End Frame ---");
#endif
            trace_running = false;
            CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
            const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
            trace_count = cmd_list_data.trace_draw_calls_data.size();
         }
         else if (trace_scheduled)
         {
#if _DEBUG && LOG_VERBOSE
            reshade::log::message(reshade::log::level::info, "--- Frame ---");
#endif
            // Split the trace logic over "two" frames, to make sure we capture everything in between two present calls
            trace_scheduled = false;
            {
               CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               cmd_list_data.trace_draw_calls_data.clear(); // This leaves the reserved space allocated
            }
            trace_count = 0;
            trace_running = true;
         }
      }
#endif // DEVELOPMENT

      // Dump new shaders (checking the "shaders_to_dump" count is theoretically not thread safe but it should work nonetheless as this is run every frame)
      if (auto_dump && !thread_auto_dumping_running && !shaders_to_dump.empty())
      {
         if (thread_auto_dumping.joinable())
         {
            thread_auto_dumping.join();
         }
         thread_auto_dumping_running = true;
         thread_auto_dumping = std::thread(AutoDumpShaders);
      }

      s_mutex_loading.lock_shared();
      // Load new shaders
      // We avoid running this if "thread_auto_compiling" is still running from boot.
      // Note that this thread doesn't really need to be by "device", but we did so to make it simpler, to automatically handle the "CreateDeviceNativeShaders()" shaders.
      if (auto_load && !last_pressed_unload && !thread_auto_compiling_running && !device_data.thread_auto_loading_running && !device_data.pipelines_to_reload.empty())
      {
         s_mutex_loading.unlock_shared();
         if (device_data.thread_auto_loading.joinable())
         {
            device_data.thread_auto_loading.join();
         }
         device_data.thread_auto_loading_running = true;
         device_data.thread_auto_loading = std::thread(AutoLoadShaders, &device_data);
      }
      else
      {
         s_mutex_loading.unlock_shared();
      }

      if (needs_unload_shaders)
      {
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            shaders_compilation_errors.clear();
         }
         UnloadCustomShaders(device_data);
#if 1 // Optionally unload all custom shaders data
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            custom_shaders_cache.clear();
         }
#endif
         needs_unload_shaders = false;

#if !FORCE_KEEP_CUSTOM_SHADERS_LOADED
         // Unload customly created shader objects (from the shader code/binaries above), to make sure they will re-create
         {
            const std::unique_lock lock(s_mutex_shader_objects);
            device_data.native_vertex_shaders.clear();
#if GEOMETRY_SHADER_SUPPORT
            device_data.native_geometry_shaders.clear();
#endif
            device_data.native_pixel_shaders.clear();
            device_data.native_compute_shaders.clear();
         }
#endif
      }

      if (!block_draw_until_device_custom_shaders_creation) s_mutex_shader_objects.lock_shared();
      // Force re-load shaders (which will also end up re-creating the custom device shaders) if async shaders loading on boot finished without being able to create custom shaders
      if (needs_load_shaders || (!block_draw_until_device_custom_shaders_creation && !thread_auto_compiling_running && !device_data.created_native_shaders))
      {
         if (!block_draw_until_device_custom_shaders_creation) s_mutex_shader_objects.unlock_shared();
         // Cache the defines at compilation time
         {
            const std::unique_lock lock(s_mutex_shader_defines);
            ShaderDefineData::OnCompilation(shader_defines_data);
            shader_defines_data_index.clear();
            for (int i = 0; i < shader_defines_data.size(); i++)
            {
               shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].compiled_data.GetName()))] = i;
            }
         }
         LoadCustomShaders(device_data);
         needs_load_shaders = false;
      }
      else
      {
         if (!block_draw_until_device_custom_shaders_creation) s_mutex_shader_objects.unlock_shared();
      }
   }

   void ForceToggleShaders(reshade::api::effect_runtime* runtime, bool enabled)
   {
      DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();
      // Note that this is not called on startup (even if the ReShade effects are enabled by default)
      // We were going to read custom keyboard events like this "GetAsyncKeyState(VK_ESCAPE) & 0x8000", but this seems like a better design
      needs_unload_shaders = !enabled;
      last_pressed_unload = !enabled;
      needs_load_shaders = enabled; // This also re-compile shaders possibly
      const std::unique_lock lock(s_mutex_loading);
      device_data.pipelines_to_reload.clear();
   }

   bool OnReShadeSetEffectsState(reshade::api::effect_runtime* runtime, bool enabled)
   {
      SKIP_UNSUPPORTED_DEVICE_API(runtime->get_device()->get_api());

#if DEVELOPMENT || TEST
      if (reshade_effects_toggle_to_display_mode_toggle)
      {
         DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();
         if (hdr_enabled_display) // Note: this flag might not be up to date if we toggled HDR outside of the game without opening ImGUI
         {
            if (cb_luma_global_settings.DisplayMode != DisplayModeType::HDR)
            {
               cb_luma_global_settings.DisplayMode = DisplayModeType::HDR; // HDR in HDR
            }
            else
            {
               cb_luma_global_settings.DisplayMode = DisplayModeType::SDRInHDR; // SDR in HDR
            }
            device_data.cb_luma_global_settings_dirty = true;
         }
      }
      else
#endif
      {
         ForceToggleShaders(runtime, enabled);
      }
      return false; // You can return true to deny the change
   }

   // Note that this can also happen when entering or exiting "FSE" games given that often they change resolutions (e.g. old DICE games, Unreal Engine games, ...)
   void OnReShadeReloadedEffects(reshade::api::effect_runtime* runtime)
   {
      SKIP_UNSUPPORTED_DEVICE_API(runtime->get_device()->get_api());

      if (!last_pressed_unload)
      {
         ForceToggleShaders(runtime, true); // This will load and recompile all shaders (there's no need to delete the previous pre-compiled cache)
      }
   }

#pragma optimize("t", on) // Temporarily override optimization, this function is too slow in debug otherwise (comment this out if ever needed)
   // Expects "s_mutex_dumping"
   void DumpShader(uint32_t shader_hash)
   {
#if !ALLOW_SHADERS_DUMPING
      ASSERT_ONCE(false); // Shouldn't call this function if the feature is disabled
#else // Note: this might work with "DEVELOPMENT" too, but possibly not entirely

      auto dump_path = shaders_dump_path;
      // Create it if it doesn't exist
      if (!std::filesystem::exists(dump_path))
      {
         if (!std::filesystem::create_directories(dump_path))
         {
            ASSERT_ONCE_MSG(false, "The target shader dump path failed to be created (lack of write access, the path is already taken by files by the same name etc)");
            return;
         }
      }

      dump_path /= Shader::Hash_NumToStr(shader_hash, true);

      auto* cached_shader = shader_cache.find(shader_hash)->second; // Expected to always be here already

      // Automatically append the shader type and version
      if (!cached_shader->type_and_version.empty())
      {
         dump_path += ".";
         dump_path += cached_shader->type_and_version;
      }

      auto dump_path_cso = dump_path;
      dump_path_cso += ".cso";

      // If the shader was already serialized, make sure the new one is of the same size, to catch the near impossible case
      // of two different shaders having the same hash
      if (std::filesystem::is_regular_file(dump_path_cso))
      {
         ASSERT_ONCE(std::filesystem::file_size(dump_path_cso) == cached_shader->size);
      }

#if DEVELOPMENT
      auto dump_path_meta = dump_path;
      dump_path_meta += ".meta";

      try
      {
         if (cached_shader->found_reflections) // We could save it nonetheless, as likely reflections would fail to be found again the next time, but who knows
         {
            std::ofstream ofs(dump_path_meta, std::ios::binary | std::ios::trunc);
            if (!ofs) throw std::runtime_error("");

            ofs.write(reinterpret_cast<const char*>(&Shader::meta_version), sizeof(Shader::meta_version));

            auto WriteBoolArray = [&](const bool* arr, size_t count)
               {
                  for (size_t i = 0; i < count; ++i)
                  {
                     uint8_t val = arr[i] ? 1 : 0;
                     ofs.write(reinterpret_cast<const char*>(&val), sizeof(val));
                  }
               };

            uint32_t type_and_version_size = static_cast<uint32_t>(cached_shader->type_and_version.size());
            ofs.write(reinterpret_cast<const char*>(&type_and_version_size), sizeof(type_and_version_size));
            if (type_and_version_size > 0)
               ofs.write(cached_shader->type_and_version.data(), type_and_version_size);

            WriteBoolArray(cached_shader->cbs, std::size(cached_shader->cbs));
            WriteBoolArray(cached_shader->samplers, std::size(cached_shader->samplers));
            WriteBoolArray(cached_shader->srvs, std::size(cached_shader->srvs));
            WriteBoolArray(cached_shader->rtvs, std::size(cached_shader->rtvs));
            WriteBoolArray(cached_shader->uavs, std::size(cached_shader->uavs));

            dumped_shaders_meta_paths[shader_hash] = dump_path_meta;
         }
      }
      catch (const std::exception& e)
      {
      }
#endif

      try
      {
         std::ofstream file(dump_path_cso, std::ios::binary);

         file.write(static_cast<const char*>(cached_shader->data), cached_shader->size);

         if (!dumped_shaders.contains(shader_hash))
         {
            dumped_shaders.emplace(shader_hash);
         }
      }
      catch (const std::exception& e)
      {
      }
#endif
   }

   void AutoDumpShaders()
   {
      // Copy the "shaders_to_dump" so we don't have to lock "s_mutex_dumping" all the times
      std::unordered_set<uint32_t> shaders_to_dump_copy;
      {
         const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
         if (shaders_to_dump.empty())
         {
            thread_auto_dumping_running = false;
            return;
         }
         shaders_to_dump_copy = shaders_to_dump;
         shaders_to_dump.clear();
      }

#if DEVELOPMENT && TEST_DUPLICATE_SHADER_HASH
      std::unordered_set<std::filesystem::path> dumped_shaders_paths;
      for (const auto& entry : std::filesystem::directory_iterator(shaders_dump_path))
      {
         if (entry.is_regular_file())
         {
            dumped_shaders_paths.emplace(entry);
         }
      }
#endif

      for (auto shader_to_dump : shaders_to_dump_copy)
      {
         const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
         // Set this to true in case your old dumped shaders have bad naming (e.g. missing the "ps_5_0" appendix) and you want to replace them (on the next boot, the duplicate shaders with the shorter name will be deleted)
         constexpr bool force_redump_shaders = false;
         if (force_redump_shaders || !dumped_shaders.contains(shader_to_dump)
#if DEVELOPMENT
            || !dumped_shaders_meta_paths.contains(shader_to_dump)
#endif
            )
         {
            DumpShader(shader_to_dump);
         }
#if DEVELOPMENT && TEST_DUPLICATE_SHADER_HASH // Warning: very slow
         else
         {
            // Make sure two different shaders didn't have the same hash (we only check if they start by the same name/hash)
            std::string shader_hash_name = Shader::Hash_NumToStr(shader_to_dump, true);
            for (const auto& entry : dumped_shaders_paths)
            {
               if (entry.path().filename().string().rfind(shader_hash_name, 0) == 0)
               {
                  auto* cached_shader = shader_cache.find(shader_to_dump)->second;
                  ASSERT_ONCE(std::filesystem::file_size(entry) == cached_shader->size);
                  // Don't break as there might be more than one by the same hash (but of a different shader type)
               }
            }
         }
#endif
      }
      thread_auto_dumping_running = false;
   }
#pragma optimize("", on) // Restore the previous state

   void AutoLoadShaders(DeviceData* device_data)
   {
      // Copy the "pipelines_to_reload_copy" so we don't have to lock "s_mutex_loading" all the times
      std::unordered_set<uint64_t> pipelines_to_reload_copy;
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         if (device_data->pipelines_to_reload.empty())
         {
            device_data->thread_auto_loading_running = false;
            return;
         }
         pipelines_to_reload_copy = device_data->pipelines_to_reload;
         device_data->pipelines_to_reload.clear();
      }
      if (pipelines_to_reload_copy.size() > 0)
      {
         LoadCustomShaders(*device_data, pipelines_to_reload_copy, !precompile_custom_shaders);
      }
      device_data->thread_auto_loading_running = false;
   }

#pragma optimize("t", on) // Temporarily override optimization, this function is too slow in debug otherwise (comment this out if ever needed)

   // TODO: apply this everywhere!
   template <typename T, bool Serialize = true >
   bool DrawResetButton(
      T& value,                      // The current value to modify
      const T& default_value,        // The default value to compare against
      const char* name,              // Unique ID string for ImGui, and ReShade serialization
      reshade::api::effect_runtime* runtime = nullptr)
   {
      bool edited = false;
      ImGui::SameLine();
      if (value != default_value)
      {
         int id = static_cast<int>(reinterpret_cast<uintptr_t>(name)); // Hacky, but it will do, almost certainly
         ImGui::PushID(id);
         if (ImGui::SmallButton(ICON_FK_UNDO))
         {
            value = default_value;
            edited = true;
            if constexpr (Serialize)
            {
               if (name)
               {
                  reshade::set_config_value(runtime, NAME, name, value);
               }
            }
         }
         ImGui::PopID();
      }
      else // Draw a disabled placeholder so layout stays consistent
      {
         const auto& style = ImGui::GetStyle();
         ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
         size.x += style.FramePadding.x;
         size.y += style.FramePadding.y;
         ImGui::InvisibleButton("", ImVec2(size.x, size.y));
      }
      return edited;
   }

   // @see https://pthom.github.io/imgui_manual_online/manual/imgui_manual.html
   // This runs within the swapchain "Present()" function, and thus it's thread safe
   void OnRegisterOverlay(reshade::api::effect_runtime* runtime)
   {
      SKIP_UNSUPPORTED_DEVICE_API(runtime->get_device()->get_api());

      DeviceData& device_data = *runtime->get_device()->get_private_data<DeviceData>();

      // Always do this in case a user changed the settings through ImGUI, so we don't have to write it in a billion places
      device_data.cb_luma_global_settings_dirty = true;

#if DEVELOPMENT
      const bool refresh_cloned_pipelines = device_data.cloned_pipelines_changed.exchange(false);

      if (ImGui::Button("Frame Capture"))
      {
         trace_scheduled = true;
      }
#if 0 // Currently not necessary
      ImGui::SameLine();
      ImGui::Checkbox("List Unique Shaders Only", &trace_list_unique_shaders_only);
#endif

      ImGui::SameLine();
      ImGui::PushID("##DumpShaders");
      // "ALLOW_SHADERS_DUMPING" is expected to be on here
      if (ImGui::Button(std::format("Dump Shaders ({})", shader_cache_count).c_str()))
      {
         const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
         // Force dump everything here
         for (auto shader : shader_cache)
         {
            DumpShader(shader.first);
         }
         shaders_to_dump.clear();
      }
      ImGui::PopID();

      ImGui::SameLine();
      ImGui::PushID("##AutoDumpCheckBox");
      if (ImGui::Checkbox("Auto Dump Shaders", &auto_dump))
      {
         if (!auto_dump && thread_auto_dumping.joinable())
         {
            thread_auto_dumping.join();
         }
      }
      ImGui::PopID();
#endif // DEVELOPMENT

#if DEVELOPMENT || TEST
      if (ImGui::Button(std::format("Unload Shaders ({})", device_data.cloned_pipeline_count).c_str())) // TODO: show number of custom+native loaded shaders instead of the number of pipelines we currently cloned? Games like Lego City Undercover re-compile the same shader many many times
      {
         needs_unload_shaders = true;
         last_pressed_unload = true;
#if 0  // Not necessary anymore with "last_pressed_unload"
         // For consistency, disable auto load, it makes no sense for them to be on if we have unloaded shaders
         if (auto_load)
         {
            auto_load = false;
            if (device_data.thread_auto_loading.joinable())
            {
               device_data.thread_auto_loading.join();
            }
         }
#endif
         const std::unique_lock lock(s_mutex_loading);
         device_data.pipelines_to_reload.clear();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Unload all compiled and replaced shaders. The numbers shows how many shaders are being replaced at this moment in the game, from the custom loaded/compiled ones.\nThis will also reset many of their debug settings to default.\nYou can use ReShade's Global Effects Toggle Shortcut to toggle these on and off.");
      }
      ImGui::SameLine();
#endif // DEVELOPMENT || TEST

      bool needs_compilation = false;
      {
         const std::shared_lock lock(s_mutex_shader_defines);
         needs_compilation = defines_need_recompilation;
         for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++)
         {
            needs_compilation |= shader_defines_data[i].NeedsCompilation();
         }
      }
#if !DEVELOPMENT && !TEST
      ImGui::BeginDisabled(!needs_compilation);
#endif
      static const std::string reload_shaders_button_title_error = std::string("Reload Shaders ") + std::string(ICON_FK_WARNING);
      static const std::string reload_shaders_button_title_outdated = std::string("Reload Shaders ") + std::string(ICON_FK_REFRESH);
      // We skip locking "s_mutex_loading" just to read the size of "shaders_compilation_errors".
      // We could maybe check "last_pressed_unload" instead of "IsModActive()", but that wouldn't work in case unloading shaders somehow failed.
      const char* reload_shaders_button_name = shaders_compilation_errors.empty() ? (IsModActive(device_data) ? (needs_compilation ? reload_shaders_button_title_outdated.c_str() : "Reload Shaders") : "Load Shaders") : reload_shaders_button_title_error.c_str();
      bool show_reload_shaders_button = (needs_compilation && !auto_recompile_defines) || !shaders_compilation_errors.empty();
#if DEVELOPMENT || TEST // Always show...
      show_reload_shaders_button = true;
#endif
      if ((show_reload_shaders_button && ImGui::Button(reload_shaders_button_name)) || (auto_recompile_defines && needs_compilation))
      {
         needs_unload_shaders = false;
         last_pressed_unload = false;
         needs_load_shaders = true;
         const std::unique_lock lock(s_mutex_loading);
         device_data.pipelines_to_reload.clear();
      }
      if (show_reload_shaders_button && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         const std::shared_lock lock(s_mutex_loading);
#if !DEVELOPMENT
         if (shaders_compilation_errors.empty())
         {
#if TEST
            ImGui::SetTooltip((IsModActive(device_data) && needs_compilation) ? "Shaders recompilation is needed for the changed settings to apply\nYou can use ReShade's Recompile Effects Shortcut to recompile these." : "(Re)Compiles shaders");
#else
            ImGui::SetTooltip((IsModActive(device_data) && needs_compilation) ? "Shaders recompilation is needed for the changed settings to apply" : "(Re)Compiles shaders");
#endif
         }
         else
#endif
         {
#if DEVELOPMENT
            ImGui::SetTooltip("Recompile and load shaders.");
#endif
            if (!shaders_compilation_errors.empty())
            {
               ImGui::SetTooltip(shaders_compilation_errors.c_str());
            }
         }
      }
#if !DEVELOPMENT && !TEST
      ImGui::EndDisabled();
#endif
#if DEVELOPMENT || TEST
      ImGui::SameLine();
      if (ImGui::Button("Clean Shaders Cache"))
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         CleanShadersCache();
         // Force recompile all shaders the next time
         for (const auto& custom_shader_pair : custom_shaders_cache)
         {
            if (custom_shader_pair.second)
            {
               custom_shader_pair.second->preprocessed_hash = 0;
            }
         }
      }
#endif

#if DEVELOPMENT
      ImGui::SameLine();
      ImGui::PushID("##AutoLoadCheckBox");
      if (ImGui::Checkbox("Auto Load Shaders", &auto_load))
      {
         if (!auto_load && device_data.thread_auto_loading.joinable())
         {
            device_data.thread_auto_loading.join();
         }
         const std::unique_lock lock(s_mutex_loading);
         device_data.pipelines_to_reload.clear();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Automatically load (apply/replace) your custom shaders when they are first met in the game.");
      }
      ImGui::PopID();

      ImGui::SameLine();
      ImGui::Checkbox("Compile/Clear All Shaders", &compile_clear_all_shaders);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("If enabled, shaders that aren't used by this mod will be compiled or cleared too.\nUseful to test if they compile properly after changes to c++ or shaders code.\nNote that some mods has specific shader defines configurations that we don't have access too here, but they should build nonetheless.");
      }
#endif // DEVELOPMENT
#if DEVELOPMENT || TEST
      ImGui::SameLine();
      ImGui::Checkbox("Allow Custom Shaders", &custom_shaders_enabled);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Toggles all the mods custom shaders from applying (without unloading them).\nNote that this might break rendering, only use for testing or vanilla comparison.");
      }
#endif

      if (ImGui::BeginTabBar("##TabBar", ImGuiTabBarFlags_None))
      {
         bool open_settings_tab = false;
#if DEVELOPMENT
         static int32_t selected_index = -1; // TODO: rename to capture_, and "changed_selected" too
         static std::string highlighted_resource = {};
         static uint32_t prev_trace_count = -1; // Default to -1 to trigger a change in the first frame

         bool changed_selected = false;
         bool trace_count_changed = prev_trace_count != trace_count;
         bool open_capture_tab = trace_count_changed && trace_count > 0;
         open_settings_tab = trace_count_changed && trace_count <= 0; // Fall back on settings (and also default to it in the first frame)
         prev_trace_count = trace_count;

         ImGui::PushID("##CaptureTab");
         bool handle_shader_tab = trace_count > 0 && ImGui::BeginTabItem(std::format("Captured Commands ({})", trace_count).c_str(), nullptr, open_capture_tab ? ImGuiTabItemFlags_SetSelected : 0); // No need for "s_mutex_trace" here
         ImGui::PopID();
         if (handle_shader_tab)
         {
            bool list_size_changed = false;

            if (ImGui::Button("Clear Capture and Debug Settings"))
            {
               trace_count = 0;
               selected_index = -1;
               changed_selected = true;
               open_settings_tab = true;

               {
                  debug_draw_pipeline = 0;
                  debug_draw_shader_hash = 0;
                  debug_draw_shader_hash_string[0] = 0;
                  debug_draw_pipeline_target_instance = -1;
                  debug_draw_pipeline_target_thread = std::thread::id();

                  device_data.debug_draw_frozen_draw_state_stack.reset();
                  device_data.debug_draw_texture = nullptr;
                  device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                  device_data.debug_draw_texture_size = {};

                  track_buffer_pipeline = 0;
                  track_buffer_pipeline_target_instance = -1;
                  track_buffer_index = 0;

                  device_data.track_buffer_data.hash.clear();
                  device_data.track_buffer_data.data.clear();
               }

               highlighted_resource = "";

               {
                  const std::unique_lock lock(s_mutex_generic);
                  for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
                  {
                     auto& cached_pipeline = pair.second;
                     cached_pipeline->custom_name.clear();
                     // Restore the original forced name
                     auto forced_shader_names_it = forced_shader_names.find(pair.second->shader_hashes[0]);
                     if (forced_shader_names_it != forced_shader_names.end())
                     {
                        cached_pipeline->custom_name = forced_shader_names_it->second;
                     }
                     cached_pipeline->replace_draw_type = ShaderReplaceDrawType::None;
                     cached_pipeline->custom_depth_stencil = ShaderCustomDepthStencilType::None;
                     cached_pipeline->redirect_data = { };
                  }
               }

               // Only the primary command list should have any capture data, the others would have joined their with it
               {
                  CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                  const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
                  cmd_list_data.trace_draw_calls_data.clear();
               }
            }

            ImGui::SameLine();
            list_size_changed |= ImGui::Checkbox("Show Command List / Thread Info", &trace_show_command_list_info);
            ImGui::SameLine();
            list_size_changed |= ImGui::Checkbox("Ignore Vertex Shaders", &trace_ignore_vertex_shaders);
            ImGui::SameLine();
            list_size_changed |= ImGui::Checkbox("Ignore Buffer Writes", &trace_ignore_buffer_writes);
            ImGui::SameLine();
            list_size_changed |= ImGui::Checkbox("Ignore Bindings", &trace_ignore_bindings);
            ImGui::SameLine();
            ImGui::Checkbox("Ignore Non Bound Shader Referenced Resources", &trace_ignore_non_bound_shader_referenced_resources);

            if (ImGui::BeginChild("HashList", ImVec2(500, -FLT_MIN), ImGuiChildFlags_ResizeX))
            {
               if (ImGui::BeginListBox("##HashesListBox", ImVec2(-FLT_MIN, -FLT_MIN)))
               {
                  const std::shared_lock lock_trace(s_mutex_trace); // We don't really need "s_mutex_trace" here as when that data is being written ImGUI isn't running, but...
                  if (!trace_running)
                  {
                     const std::shared_lock lock_generic(s_mutex_generic);
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);

#if 1 // Much more optimized, drawing all items is extremely slow otherwise
                     std::vector<uint32_t> trace_draw_calls_index_redirector; // From full list index to filtered list index
                     trace_draw_calls_index_redirector.reserve(trace_count);
                     std::vector<uint32_t> trace_draw_calls_index_inverse_redirector; // From filtered index to full list index
                     trace_draw_calls_index_inverse_redirector.reserve(trace_count);

                     // Pre count the elements (some are skipped) otherwise the clipper won't work properly
                     // TODO: do this once on capture
                     size_t actual_trace_count = 0;
                     for (auto index = 0; index < trace_count; index++) {
                        trace_draw_calls_index_redirector.emplace_back(uint32_t(trace_draw_calls_index_inverse_redirector.size())); // The closest one
                        auto& draw_call_data = cmd_list_data.trace_draw_calls_data.at(index);
                        if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Shader) {
                           auto pipeline_handle = draw_call_data.pipeline_handle;
                           const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                           const bool is_valid = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
                           if (is_valid) {
                              if (trace_ignore_vertex_shaders && pipeline_pair->second->HasVertexShader()) continue; // DX11 exclusive behaviour
                           }
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPUWrite) {
                           if (trace_ignore_buffer_writes && draw_call_data.rt_type_name[0] == "Buffer") continue;
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindPipeline
                           || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource) {
                           if (trace_ignore_bindings) continue;
                        }
                        actual_trace_count++;
                        trace_draw_calls_index_inverse_redirector.emplace_back(index);
                     }

                     // Scroll to the target (that might have shifted)
                     if (list_size_changed && selected_index >= 0 && selected_index <= trace_draw_calls_index_redirector.size())
                     {
                        uint32_t filtered_index = trace_draw_calls_index_redirector[selected_index];
                        // If our selected index is currently hidden, automatically select the next one in line
                        if (filtered_index >= 0 && filtered_index <= trace_draw_calls_index_inverse_redirector.size())
                        {
                           uint32_t raw_index = trace_draw_calls_index_inverse_redirector[filtered_index];
                           selected_index = raw_index;
                        }
                        float item_height = ImGui::GetTextLineHeightWithSpacing();
                        float scroll_y = filtered_index * item_height;
                        ImGui::SetScrollY(scroll_y - ImGui::GetWindowHeight() * 0.5f + item_height * 0.5f);
                        list_size_changed = false;
                     }

                     ImGuiListClipper clipper;
                     clipper.Begin(actual_trace_count);
                     while (clipper.Step()) {
                        for (int filtered_index = clipper.DisplayStart; filtered_index < clipper.DisplayEnd; filtered_index++) {
                           uint32_t index = trace_draw_calls_index_inverse_redirector[filtered_index]; // Raw index
#else
                     for (uint32_t index = 0; index < trace_count; index++) {
#endif
                        auto& draw_call_data = cmd_list_data.trace_draw_calls_data.at(index);
                        ASSERT_ONCE_MSG(draw_call_data.command_list, "The code below will probably crash if the command list isn't valid, remember to always assign it when adding a new element to the list");
                        if (draw_call_data.command_list)
                        {
                           com_ptr<ID3D11DeviceContext> native_device_context;
                           HRESULT hr = draw_call_data.command_list->QueryInterface(&native_device_context);
                           ASSERT_ONCE(SUCCEEDED(hr) && native_device_context); // Just to make sure it accidentally wasn't saved as a "ID3D11CommandList", given ReShade's design that mixes them up
                        }

                        auto pipeline_handle = draw_call_data.pipeline_handle;
                        auto thread_id = draw_call_data.thread_id._Get_underlying_id(); // Possibly compiler dependent but whatever, cast to int alternatively
                        const bool is_selected = selected_index == index;
                        // Note that the pipelines can be run more than once so this will return the first one matching (there's only one actually, we don't have separate settings for their running instance, as that's runtime stuff)
                        const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                        const bool is_valid = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
                        std::stringstream name;
                        auto text_color = IM_COL32(255, 255, 255, 255); // White

                        std::string command_list_info = "";
                        if (trace_show_command_list_info)
                        {
                           if (draw_call_data.command_list && draw_call_data.command_list->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) // Deferred
                              command_list_info = " - ~> " + std::to_string(reinterpret_cast<uint64_t>(draw_call_data.command_list.get())) + " - " + std::to_string(thread_id);
                           else // Immediate
                              command_list_info = " - " + std::to_string(thread_id); // No need to fill up the immediate command list ptr every time
                        }

                        bool found_highlighted_resource_write = false;
                        bool found_highlighted_resource_read = false;
                        if (!highlighted_resource.empty())
                        {
                           for (UINT i = 0; i < TraceDrawCallData::srvs_size; i++)
                           {
                              if (found_highlighted_resource_read) break;
                              found_highlighted_resource_read |= draw_call_data.sr_hash[i] == highlighted_resource;
                           }
                           for (UINT i = 0; i < TraceDrawCallData::uavs_size; i++)
                           {
                              if (found_highlighted_resource_write) break;
                              found_highlighted_resource_write |= draw_call_data.ua_hash[i] == highlighted_resource; // We consider UAV as write even if it's not necessarily one
                           }
                           for (UINT i = 0; i < TraceDrawCallData::rtvs_size; i++)
                           {
                              if (found_highlighted_resource_write) break;
                              found_highlighted_resource_write |= draw_call_data.rt_hash[i] == highlighted_resource;
                           }
                           for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
                           {
                              if (found_highlighted_resource_read) break;
                              found_highlighted_resource_read |= draw_call_data.cb_hash[i] == highlighted_resource;
                           }
                           // Don't set "found_highlighted_resource_read" for the test and write case, it'd just be more confusing
                           if (draw_call_data.depth_state == TraceDrawCallData::DepthStateType::TestAndWrite
                              || draw_call_data.depth_state == TraceDrawCallData::DepthStateType::WriteOnly)
                              found_highlighted_resource_write |= draw_call_data.ds_hash == highlighted_resource;
                           else
                              found_highlighted_resource_read |= draw_call_data.ds_hash == highlighted_resource;
                        }

                        // TODO: merge pixel and vertex shader traces if they are both present? Yes, and then add two tabs one for VS data and shader code and one for PS (same)
                        if (is_valid && draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Shader)
                        {
                           const auto pipeline = pipeline_pair->second;

                           // Highlight other draw calls with the same shader
                           bool same_as_selected = false;
                           if (/*!is_selected &&*/ selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1 && cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::Shader)
                           {
                              auto pipeline_handle_2 = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                              const auto pipeline_pair_2 = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle_2);
                              const bool is_valid_2 = pipeline_pair_2 != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair_2->second != nullptr;
                              if (is_valid_2)
                              {
                                 const auto pipeline_2 = pipeline_pair_2->second;
                                 if (pipeline_2->shader_hashes == pipeline->shader_hashes)
                                 {
#if 1 // Add a space before it
                                    name << "   ";
#endif
                                    same_as_selected = true;
                                 }
                              }
                           }

                           // Index - Thread ID (command list) - Shader Hash(es) - Shader Name
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0); // Fill up 3 slots for the index so the text is aligned

                           name << command_list_info;

                           const char* sm = nullptr;

                           // Pick the default color by shader type
                           if (pipeline->HasVertexShader())
                           {
                              if (trace_ignore_vertex_shaders)
                              {
                                 continue;
                              }
                              text_color = IM_COL32(192, 192, 0, 255); // Yellow
                              sm = "VS";
                           }
                           else if (pipeline->HasComputeShader())
                           {
                              text_color = IM_COL32(192, 0, 192, 255); // Purple
                              sm = "CS";
                           }
                           else if (pipeline->HasGeometryShader())
                           {
                              text_color = IM_COL32(192, 0, 192, 255); // Purple
                              sm = "GS";
                           }
                           else if (pipeline->HasPixelShader())
                           {
                              sm = "PS";
                           }
                           else // Invalid
                           {
                              text_color = IM_COL32(255, 0, 0, 255); // Red
                              sm = "XS";
                           }

                           for (auto shader_hash : pipeline->shader_hashes)
                           {
                              name << " - " << sm << " " << PRINT_CRC32(shader_hash);
                           }

                           // DX11 specific code
                           bool live_patched = false;
                           {
                              const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                              const auto cached_shader = !pipeline->shader_hashes.empty() ? shader_cache[pipeline->shader_hashes[0]] : nullptr;
                              live_patched = cached_shader ? cached_shader->live_patched_data : false;
                           }
                           const std::shared_lock lock_loading(s_mutex_loading);
                           const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;

                           if (live_patched)
                           {
                              name << "#";
                           }
                           // Find if the shader has been modified
                           if (pipeline->cloned)
                           {
                              name << "*";

                              if (pipeline->HasVertexShader())
                              {
                                 text_color = IM_COL32(128, 255, 0, 255); // Yellow + Green
                              }
                              else if (pipeline->HasComputeShader())
                              {
                                 text_color = IM_COL32(128, 255, 128, 255); // Purple + Green
                              }
                              else
                              {
                                 text_color = IM_COL32(0, 255, 0, 255); // Green
                              }
                           }

                           if (strlen(pipeline->custom_name.c_str()) > 0) // We can not check the string size as it's been allocated to more characters even if they are empty
                           {
                              name << " - " << pipeline->custom_name.c_str(); // Add c string otherwise it will append a billion null terminators
                           }
                           else if (pipeline->cloned)
                           {
                              // For now just force picking the first shader linked to the pipeline, there should always only be one (?)
                              if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty())
                              {
                                 auto filename_string = custom_shader->file_path.filename().string();
                                 if (const auto hash_begin_index = filename_string.find("0x"); hash_begin_index != std::string::npos) // TODO: this doesn't work with hashless shader files
                                 {
                                    filename_string.erase(hash_begin_index); // Start deleting from where the shader hash(es) begin (e.g. "0x12345678.xs_n_n.hlsl")
                                 }
                                 if (filename_string.ends_with("_") || filename_string.ends_with("."))
                                 {
                                    filename_string.erase(filename_string.length() - 1);
                                 }
                                 if (!filename_string.empty())
                                 {
                                    name << " - " << filename_string;
                                 }
                              }
                           }
                           else
                           {
                              std::optional<std::string> optional_name = GetD3DNameW(reinterpret_cast<ID3D11DeviceChild*>(pipeline->pipeline.handle));
                              if (optional_name.has_value())
                              {
                                 name << " - " << optional_name.value().c_str();
                              }
                           }

                           // Print out dangerous blend modes. We only check the first RT for now.
                           // Note: theoretically we should only check this on upgraded textures (and all upgraded textures, not just RGBA) (here and in other places where we do this)! We can't check the format here reliably as it's the non upgraded one in case of indirect upgrades.
                           if (pipeline->HasPixelShader() && IsRGBAFormat(draw_call_data.rtv_format[0], true) /*&& IsSignedFloatFormat(draw_call_data.rtv_format[0])*/ && IsBlendInverted(draw_call_data.blend_desc, 1, false))
                           {
                              text_color = IM_COL32(255, 165, 0, 255); // Orange for Warning
                           }

                           // Highlight loading error
                           if (custom_shader != nullptr && !custom_shader->compilation_errors.empty())
                           {
                              text_color = custom_shader->compilation_error ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 165, 0, 255); // Red for Error, Orange for Warning
                           }

                           if (same_as_selected)
                           {
#if 0 // We already do this better above
                              name << " - (Selected) ";
#endif
                              if (!is_selected)
                              {
                                 text_color = IM_COL32(192, 192, 192, 255); // Grey
                              }
                           }
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CopyResource)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Copy Resource";
                           text_color = IM_COL32(255, 105, 0, 255); // Orange
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindPipeline)
                        {
                           if (trace_ignore_bindings) continue;

                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Bind Pipeline";
                           text_color = IM_COL32(30, 200, 10, 255); // Some green
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource)
                        {
                           if (trace_ignore_bindings) continue;

                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Bind Resource";
                           text_color = IM_COL32(30, 200, 10, 255); // Some green
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Resource CPU Read";
                           text_color = IM_COL32(255, 40, 0, 255); // Bright Red
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPUWrite)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           // Hacky resource type name check
                           if (trace_ignore_buffer_writes && draw_call_data.rt_type_name[0] == "Buffer")
                           {
                              continue;
                           }

                           name << " - Resource CPU Write";

                           text_color = IM_COL32(255, 105, 0, 255); // Orange
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ClearResource)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Clear Resource";
                           text_color = IM_COL32(255, 105, 0, 255); // Orange
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CreateCommandList)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Create Command List";
                           text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::AppendCommandList)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Append Command List";
                           text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ResetCommmandList)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Reset Command List";
                           text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::FlushCommandList)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Flush Command List";
                           text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Present)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - Present";
                           text_color = IM_COL32(50, 80, 190, 255); // Some Blue

                           // Highlight the resource on the swapchain too, given that "Present" traces aren't tracked the same way
                           if (!highlighted_resource.empty() && !device_data.swapchains.empty())
                           {
                              // Not fully safe, but it should do for almost all cases
                              reshade::api::swapchain* swapchain = *device_data.swapchains.begin();
                              IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
                              UINT back_buffer_index = swapchain->get_current_back_buffer_index();
                              com_ptr<ID3D11Texture2D> back_buffer;
                              native_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));

                              std::string backbuffer_hash = std::to_string(std::hash<void*>{}(back_buffer.get()));
                              if (highlighted_resource == backbuffer_hash)
                              {
                                 found_highlighted_resource_read = true;
                              }
                           }
                        }
                        else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                           name << command_list_info;
                           name << " - " << draw_call_data.custom_name;
                           text_color = IM_COL32(70, 130, 180, 255); // Steel Blue
                        }
                        else
                        {
                           text_color = IM_COL32(255, 0, 0, 255); // Red
                           name << "ERROR: Capture data not found"; // The draw call either had an empty (e.g. pixel) shader set, or the game has since unloaded them
                        }

                        if (found_highlighted_resource_write || found_highlighted_resource_read)
                        {
                           if (found_highlighted_resource_write && found_highlighted_resource_read)
                           {
                              // Highlight there's an error in this case as in any DX version, reading and writing from the same resource isn't supported
                              text_color = IM_COL32(255, 0, 0, 255); // Red
                              name << " - (Highlighted Resource Read And Write)";
                           }
                           else
                           {
                              text_color = IM_COL32(255, 192, 203, 255); // Pink
                              name << (found_highlighted_resource_write ? " - (Highlighted R Write)" : " - (Highlighted R Read)");
                           }
                        }

                        ImGui::PushID(index);
                        ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                        if (ImGui::Selectable(name.str().c_str(), is_selected, ImGuiSelectableFlags_AllowItemOverlap))
                        {
                           selected_index = index;
                           changed_selected = true;
                        }
                        ImGui::PopStyleColor();
                        if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Shader)
                        {
                           ImGui::SameLine();
                           ImGui::SetNextItemAllowOverlap(); // Allow the button to be clickable if rects overlap
                           if (ImGui::SmallButton(ICON_FK_SEARCH))
                           {
                              // TODO: move all into a function

                              int32_t target_instance = 0;
                              for (int32_t i = 0; i < index; i++)
                              {
                                 if (pipeline_handle == cmd_list_data.trace_draw_calls_data.at(i).pipeline_handle && draw_call_data.thread_id == cmd_list_data.trace_draw_calls_data.at(i).thread_id)
                                    target_instance++;
                              }

                              debug_draw_pipeline = pipeline_pair->first; // Note: this is probably completely useless at the moment as we don't store the index of the pipeline instance the user had selected (e.g. "debug_draw_pipeline_target_instance")
                              debug_draw_shader_hash = pipeline_pair->second->shader_hashes[0];
                              std::string new_debug_draw_shader_hash_string = Shader::Hash_NumToStr(debug_draw_shader_hash);
                              if (new_debug_draw_shader_hash_string.size() <= HASH_CHARACTERS_LENGTH)
                                 strcpy(&debug_draw_shader_hash_string[0], new_debug_draw_shader_hash_string.c_str());
                              else
                                 debug_draw_shader_hash_string[0] = 0;
                              device_data.debug_draw_frozen_draw_state_stack.reset();
                              device_data.debug_draw_texture = nullptr;
                              device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                              device_data.debug_draw_texture_size = {};
                              debug_draw_pipeline_instance = 0;
                              debug_draw_pipeline_target_instance = target_instance;
                              debug_draw_pipeline_target_thread = debug_draw_pipeline_instance_filter_by_thread ? std::thread::id() : draw_call_data.thread_id;
                              const auto prev_debug_draw_mode = debug_draw_mode;
                              if (prev_debug_draw_mode == DebugDrawMode::Depth)
                              {
                                 debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                              }
                              if (prev_debug_draw_mode == DebugDrawMode::Custom)
                              {
                                 debug_draw_view_index = 0;
                                 debug_draw_mip = 0;
                                 // Don't change it automatically in this pass, it will default to render target (usually) if we come from debugging somewhere else, and keep the last debugging mode otherwise
                                 debug_draw_mode = pipeline_pair->second->HasPixelShader() ? DebugDrawMode::RenderTarget : (pipeline_pair->second->HasComputeShader() ? DebugDrawMode::UnorderedAccessView : DebugDrawMode::ShaderResource);
                              }
                              // Fall back on depth if there main RT isn't valid
                              if (debug_draw_mode == DebugDrawMode::RenderTarget && draw_call_data.rt_format[0] == DXGI_FORMAT_UNKNOWN && draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled)
                              {
                                 debug_draw_mode = DebugDrawMode::Depth;
                                 debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                 debug_draw_mip = 0;
                              }
                              debug_draw_freeze_inputs = false; // Optional, but also unfreeze inputs automatically every time we swap the target pass here
                           }
                        }
                        ImGui::PopID();

                        if (is_selected)
                        {
                           ImGui::SetItemDefaultFocus();
                           if (list_size_changed)
                           {
                              ImGui::SetScrollHereY(0.5f); // 0.0 = top, 0.5 = middle, 1.0 = bottom
                           }
                        }
                     }
#if 1
                     }
                     clipper.End();
#endif
                  }
                  else
                  {
                     selected_index = -1;
                     changed_selected = true;
                  }
                  selected_index = min(selected_index, trace_count - 1); // Extra safety
                  ImGui::EndListBox();
               }
            }
            ImGui::EndChild(); // HashList

            ImGui::SameLine();
            if (ImGui::BeginChild("##ShaderDetails", ImVec2(0, 0)))
            {
               ImGui::BeginDisabled(selected_index == -1);
               if (ImGui::BeginTabBar("##ShadersCodeTab", ImGuiTabBarFlags_None))
               {
                  ImGui::PushID("##SettingsTabItem");
                  const bool open_settings_tab_item = ImGui::BeginTabItem("Info & Settings");
                  ImGui::PopID();
                  if (open_settings_tab_item)
                  {
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                     if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1)
                     {
                        auto& draw_call_data = cmd_list_data.trace_draw_calls_data.at(selected_index);
                        auto pipeline_handle = draw_call_data.pipeline_handle;
                        bool reload = false;
                        bool recompile = false;

                        {
                           std::unique_lock lock(s_mutex_generic);
                           if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                           {
                              if (ImGui::BeginChild("Settings and Info"))
                              {
                                 CachedShader* original_shader = nullptr; // We probably don't need to lock "s_mutex_dumping" for the duration of this read
                                 {
                                    const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                                    for (auto shader_hash : pipeline_pair->second->shader_hashes)
                                    {
                                       auto custom_shader_pair = shader_cache.find(shader_hash);
                                       if (custom_shader_pair != shader_cache.end())
                                       {
                                          original_shader = custom_shader_pair->second;
                                       }
                                    }
                                 }
                                 CachedCustomShader* custom_shader = nullptr; // We probably don't need to lock "s_mutex_loading" for the duration of this read
                                 {
                                    const std::shared_lock lock(s_mutex_loading);
                                    for (auto shader_hash : pipeline_pair->second->shader_hashes)
                                    {
                                       auto custom_shader_pair = custom_shaders_cache.find(shader_hash);
                                       if (custom_shader_pair != custom_shaders_cache.end())
                                       {
                                          custom_shader = custom_shader_pair->second;
                                       }
                                    }
                                 }

                                 if (pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                 {
                                    ImGui::Checkbox("Allow Shader Replace Draw NaNs", &allow_replace_draw_nans);
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                       ImGui::SetTooltip("Avoids accidentally enabling \"Draw NaN\" below, which could \"corrupt\" the game's rendering \"forever\".");
                                    ImGui::SliderInt("Shader Replace Draw Type", reinterpret_cast<int*>(&pipeline_pair->second->replace_draw_type), 0, IM_ARRAYSIZE(CachedPipeline::shader_replace_draw_type_names) - (allow_replace_draw_nans ? 1 : 2), CachedPipeline::shader_replace_draw_type_names[(size_t)pipeline_pair->second->replace_draw_type], ImGuiSliderFlags_NoInput);
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                       ImGui::SetTooltip("Affects all the instances of this shader.\nNote that \"Draw Purple\" (Magenta) or \"Draw NaN\" might not always work, if it doesn't, it will skip the shader anyway. With compute shaders, the written area might not match at all.\nBe careful with NaNs as they can break temporal passes until the resolution is changed.");

                                    if (pipeline_pair->second->HasPixelShader())
                                    {
                                       ImGui::SliderInt("Shader Custom Depth/Stencil Type", reinterpret_cast<int*>(&pipeline_pair->second->custom_depth_stencil), 0, IM_ARRAYSIZE(CachedPipeline::shader_custom_depth_stencil_type_names) - 1, CachedPipeline::shader_custom_depth_stencil_type_names[(size_t)pipeline_pair->second->custom_depth_stencil], ImGuiSliderFlags_NoInput);
                                       if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                          ImGui::SetTooltip("Forces a custom depth and stencil state. For example, ignoring depth read tests, or forcing depth writes, meaning the affected polygons will draw on top of everything else.");
                                    }
                                 }

                                 {
                                    // Ensure the string has enough capacity for editing (e.g. 256 chars)
                                    if (pipeline_pair->second->custom_name.capacity() < 128)
                                       pipeline_pair->second->custom_name.resize(128);

                                    // ImGui::InputText modifies the buffer in-place
                                    if (ImGui::InputText("Shader Custom Name", pipeline_pair->second->custom_name.data(), pipeline_pair->second->custom_name.capacity()))
                                    {
                                       pipeline_pair->second->custom_name.resize(strlen(pipeline_pair->second->custom_name.c_str())); // Optional
                                    }
                                 }

                                 if (pipeline_pair->second->cloned && ImGui::Button("Unload"))
                                 {
                                    s_mutex_generic.unlock(); // Hack to avoid deadlocks with the device destroying pipelines // TODO: use "out_pipelines_to_destroy" instead (in all similar cases too)
                                    UnloadCustomShaders(device_data, { pipeline_handle }, false);
                                    s_mutex_generic.lock();
                                 }
                                 if (ImGui::Button(pipeline_pair->second->cloned ? "Recompile" : "Load"))
                                 {
                                    reload = true;
                                    recompile = true; // If this shader wasn't cloned, we'd need to compile it probably as it might not have already been compiled. If it was cloned, then our intent is to re-compile it anyway
                                 }
                                 if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                 {
                                    ImGui::SetTooltip("Recompile and/or load/unload the custom shader that replaces the original one.");
                                 }

                                 bool copy_shader_hash = false;
                                 bool copy_shader_hash_exclude_0x = true;
                                 copy_shader_hash |= ImGui::Button("Copy Shader Hash to Clipboard");
                                 ImGui::SameLine();
                                 if (ImGui::Button("0x")) // Add a small button to copy it with the hash, it depends on the usage whether we need it or not
                                 {
                                    copy_shader_hash = true;
                                    copy_shader_hash_exclude_0x = false;
                                 }
                                 if (copy_shader_hash)
                                 {
                                    const std::shared_lock lock(s_mutex_loading);
                                    std::string shader_hash = (pipeline_pair->second->shader_hashes.size() > 0) ? Shader::Hash_NumToStr(pipeline_pair->second->shader_hashes[0]) : "????????";
                                    if (!copy_shader_hash_exclude_0x)
                                       shader_hash = "0x" + shader_hash; // Somehow we need to add "0x" in front of it manually // DX11 specific behaviour
                                    System::CopyToClipboard(shader_hash);
                                 }

                                 if (custom_shader && custom_shader->is_hlsl && !custom_shader->file_path.empty() && ImGui::Button("Open hlsl in IDE"))
                                 {
                                    // You may need to specify the full path to "code.exe" if it's not in PATH.
                                    HINSTANCE ret_val = ShellExecuteA(nullptr, "open", "code", ("\"" + custom_shader->file_path.string() + "\"").c_str(), nullptr, SW_SHOWNORMAL); // TODO: instruct users on how to use this (add "code" path to VS Code).
                                    ASSERT_ONCE(ret_val > (HINSTANCE)32); // Unknown reason
                                 }

                                 // TODO: add opening the dumped shader file (e.g. "dumped_shaders")
                                 if (custom_shader && !custom_shader->file_path.empty() && ImGui::Button("Open in Explorer"))
                                 {
                                    System::OpenExplorerToFile(custom_shader->file_path);
                                 }

                                 bool debug_draw_shader_enabled = false; // Whether this shader/pipeline instance is the one we are draw debugging

                                 if (pipeline_pair->second->HasVertexShader() || pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                 {
                                    debug_draw_shader_enabled = debug_draw_shader_hash == pipeline_pair->second->shader_hashes[0];

                                    const auto& trace_draw_call_data = draw_call_data;

                                    int32_t target_instance = -1;
                                    // Automatically calculate the index of the instance of this pipeline run, to directly select it on selection (this works as long as the current scene draw calls match the ones in the trace)
                                    {
                                       target_instance = 0;
                                       for (int32_t i = 0; i < selected_index; i++)
                                       {
                                          if (pipeline_handle == cmd_list_data.trace_draw_calls_data.at(i).pipeline_handle && draw_call_data.thread_id == cmd_list_data.trace_draw_calls_data.at(i).thread_id)
                                             target_instance++;
                                       }

                                       debug_draw_shader_enabled &= debug_draw_pipeline_target_instance < 0 || debug_draw_pipeline_target_instance == target_instance;
                                    }

                                    bool has_any_resources = false;
                                    {
                                       for (UINT i = 0; i < std::size(trace_draw_call_data.srv_format); i++)
                                       {
                                          if (trace_draw_call_data.IsSRVValid(i)) has_any_resources = true; break;
                                       }
                                       for (UINT i = 0; i < std::size(trace_draw_call_data.uav_format); i++)
                                       {
                                          if (trace_draw_call_data.IsUAVValid(i)) has_any_resources = true; break;
                                       }
                                       for (UINT i = 0; i < std::size(trace_draw_call_data.rtv_format); i++)
                                       {
                                          if (trace_draw_call_data.IsRTVValid(i)) has_any_resources = true; break;
                                       }
                                       if (trace_draw_call_data.IsDSVValid()) has_any_resources = true;
                                    }

                                    // Note: yes, this can be done on vertex shaders too, as they might have resources!
                                    bool debug_draw_shader_just_enabled = false;
                                    // TODO: add a slider to scroll through all the draw calls quickly and show the RTVs etc, like Nsight. Or at least add Next, Prev buttons.
                                    if (has_any_resources && (debug_draw_shader_enabled ? ImGui::Button("Disable Debug Draw Shader Instance") : ImGui::Button("Debug Draw Shader Instance")))
                                    {
                                       ASSERT_ONCE(GetShaderDefineCompiledNumericalValue(DEVELOPMENT_HASH) >= 1); // Development flag is needed in shaders for this to output correctly
                                       ASSERT_ONCE(luma_data_cbuffer_index != -1); // Needed to pass data to the display composition shader
                                       ASSERT_ONCE(device_data.native_pixel_shaders[CompileTimeStringHash("Display Composition")]); // This shader is necessary to draw this debug stuff

                                       if (debug_draw_shader_enabled)
                                       {
                                          debug_draw_pipeline = 0;
                                          debug_draw_shader_hash = 0;
                                          debug_draw_shader_hash_string[0] = 0;
                                       }
                                       else
                                       {
                                          debug_draw_pipeline = pipeline_pair->first; // Note: this is probably completely useless at the moment as we don't store the index of the pipeline instance the user had selected (e.g. "debug_draw_pipeline_target_instance")
                                          debug_draw_shader_hash = pipeline_pair->second->shader_hashes[0];
                                          std::string new_debug_draw_shader_hash_string = Shader::Hash_NumToStr(debug_draw_shader_hash);
                                          if (new_debug_draw_shader_hash_string.size() <= HASH_CHARACTERS_LENGTH)
                                             strcpy(&debug_draw_shader_hash_string[0], new_debug_draw_shader_hash_string.c_str());
                                          else
                                             debug_draw_shader_hash_string[0] = 0;
                                          debug_draw_shader_just_enabled = true;
                                       }
                                       device_data.debug_draw_frozen_draw_state_stack.reset(); // We need to force reset this anyway otherwise it might store a pointer to a "DrawStateStack<DrawStateStackType::Compute>" or a "DrawStateStack<DrawStateStackType::FullGraphcis>" and we wouldn't know about
                                       device_data.debug_draw_texture = nullptr;
                                       device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                                       device_data.debug_draw_texture_size = {};
                                       debug_draw_pipeline_instance = 0;
#if 1 // We could also let the user settings persist if we wished so, but automatically setting them is usually better
                                       debug_draw_pipeline_target_instance = debug_draw_shader_enabled ? -1 : target_instance;
                                       debug_draw_pipeline_target_thread = (debug_draw_shader_enabled || debug_draw_pipeline_instance_filter_by_thread) ? std::thread::id() : draw_call_data.thread_id;
                                       const auto prev_debug_draw_mode = debug_draw_mode;
                                       if (prev_debug_draw_mode == DebugDrawMode::Depth)
                                       {
                                          debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                       }
                                       if (prev_debug_draw_mode == DebugDrawMode::Custom)
                                       {
                                          debug_draw_mip = 0;
                                       }
                                       debug_draw_mode = pipeline_pair->second->HasPixelShader() ? DebugDrawMode::RenderTarget : (pipeline_pair->second->HasComputeShader() ? DebugDrawMode::UnorderedAccessView : DebugDrawMode::ShaderResource); // Do it regardless of "debug_draw_shader_enabled"
                                       // Fall back on depth if there main RT isn't valid
                                       if (debug_draw_mode == DebugDrawMode::RenderTarget && trace_draw_call_data.rt_format[0] == DXGI_FORMAT_UNKNOWN && draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled)
                                       {
                                          debug_draw_mode = DebugDrawMode::Depth;
                                          debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                          debug_draw_mip = 0;
                                       }
                                       if (debug_draw_mode != prev_debug_draw_mode)
                                       {
                                          debug_draw_view_index = 0;
                                       }
                                       // Preserve the last SRV/UAV/RTV index if we are passing between draw calls, it should help (e.g. during gbuffers rendering)
                                       else if (debug_draw_shader_just_enabled)
                                       {
                                          if (debug_draw_mode == DebugDrawMode::RenderTarget)
                                          {
                                             if (debug_draw_view_index < 0 || debug_draw_view_index >= std::size(original_shader->rtvs) || !original_shader->rtvs[debug_draw_view_index])
                                                debug_draw_view_index = 0;
                                          }
                                          else if (debug_draw_mode == DebugDrawMode::ShaderResource)
                                          {
                                             if (debug_draw_view_index < 0 || debug_draw_view_index >= std::size(original_shader->srvs) || !original_shader->srvs[debug_draw_view_index])
                                                debug_draw_view_index = 0;
                                          }
                                          else if (debug_draw_mode == DebugDrawMode::UnorderedAccessView)
                                          {
                                             if (debug_draw_view_index < 0 || debug_draw_view_index >= std::size(original_shader->uavs) || !original_shader->uavs[debug_draw_view_index])
                                                debug_draw_view_index = 0;
                                          }
                                          else // DebugDrawMode::Depth
                                          {
                                             debug_draw_view_index = 0;
                                             debug_draw_mip = 0;
                                          }
                                       }
                                       //debug_draw_replaced_pass = false;
#endif

                                       debug_draw_shader_enabled = !debug_draw_shader_enabled;
                                    }
                                    // Show that it's failing to retrieve the texture if we can!
                                    if (!debug_draw_auto_clear_texture && has_any_resources && debug_draw_shader_enabled && !debug_draw_shader_just_enabled && device_data.debug_draw_texture.get() == nullptr)
                                    {
                                       ImGui::BeginDisabled(true);
                                       ImGui::SameLine();
                                       ImGui::SmallButton(ICON_FK_WARNING);
                                       ImGui::EndDisabled();
                                    }
                                    if (has_any_resources)
                                    {
                                       ImGui::SameLine();
                                       if (ImGui::Checkbox("Freeze Inputs", &debug_draw_freeze_inputs) && !debug_draw_freeze_inputs)
                                       {
                                          device_data.debug_draw_frozen_draw_state_stack.reset();
                                       }
                                    }

                                    bool track_buffer_enabled = track_buffer_pipeline != 0 && track_buffer_pipeline == pipeline_pair->first;
                                    track_buffer_enabled &= track_buffer_pipeline_target_instance < 0 || track_buffer_pipeline_target_instance == target_instance;
                                    if (track_buffer_enabled ? ImGui::Button("Disable Constant Buffer Tracking") : ImGui::Button("Enable Constant Buffer Tracking"))
                                    {
                                       if (!track_buffer_enabled)
                                       {
                                          track_buffer_pipeline = pipeline_pair->first;
                                          track_buffer_pipeline_target_instance = target_instance;
                                          track_buffer_index = 0;
                                       }
                                       else
                                       {
                                          track_buffer_pipeline = 0;
                                          track_buffer_pipeline_target_instance = -1;
                                       }
                                       track_buffer_enabled = !track_buffer_enabled;
                                    }

                                    if (track_buffer_enabled)
                                    {
                                       ImGui::SliderInt("Constant Buffer Tracked Index", &track_buffer_index, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1);

                                       if (!device_data.track_buffer_data.data.empty())
                                       {
                                          ImGui::NewLine();
                                          ImGui::Text("Tracked Constant Buffer:");
                                          ImGui::Text("Resource Hash: %s", device_data.track_buffer_data.hash.c_str());
                                          if (ImGui::BeginChild("TrackBufferScroll", ImVec2(0, 500), ImGuiChildFlags_Border))
                                          {
                                             // TODO: match with the shader assembly cbs etc (if the data is available)
                                             // TODO: add a matrix 4x4 view?
                                             if (ImGui::BeginTable("TrackBufferTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                                                ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                                                ImGui::TableSetupColumn("Float Value", ImGuiTableColumnFlags_WidthStretch);
                                                ImGui::TableSetupColumn("Int Value", ImGuiTableColumnFlags_WidthStretch);
                                                ImGui::TableHeadersRow();

                                                for (size_t i = 0; i < device_data.track_buffer_data.data.size(); ++i)
                                                {
                                                   float this_data = device_data.track_buffer_data.data[i];
                                                   ImGui::TableNextRow();
                                                   ImGui::TableSetColumnIndex(0);
                                                   static const char* components[] = { "x", "y", "z", "w" };
                                                   size_t group = i / 4; // Split index by float4
                                                   size_t comp_index = i % 4; // Print out the x/y/w/z identifier
                                                   ImGui::Text("%zu:%s", group, components[comp_index]);
                                                   // First print as float
                                                   ImGui::TableSetColumnIndex(1);
                                                   // I'm not sure whether the auto float printing handles nan/inf
                                                   if (std::isnan(this_data))
                                                   {
                                                      ImGui::Text("NaN");
                                                   }
                                                   else if (std::isinf(this_data))
                                                   {
                                                      ImGui::Text("Inf");
                                                   }
                                                   else
                                                   {
                                                      ImGui::Text("%.7f", this_data); // We need to show quite a bit of precision
                                                   }
                                                   // Then print as int (should work as uint/bool as well)
                                                   ImGui::TableSetColumnIndex(2);
                                                   ImGui::Text("%i", Math::AsInt(this_data));
                                                }
                                                ImGui::EndTable();
                                             }
                                          }
                                          ImGui::EndChild(); // TrackBufferScroll

                                          if (ImGui::Button("Copy Constant Buffer Data to Clipboard (float)"))
                                          {
                                             std::ostringstream oss;
                                             for (size_t i = 0; i < device_data.track_buffer_data.data.size(); ++i) {
                                                oss << device_data.track_buffer_data.data[i];
                                                if (i + 1 < device_data.track_buffer_data.data.size())
                                                   oss << '\n';
                                             }
                                             System::CopyToClipboard(oss.str());
                                          }
                                       }
                                    }
                                    // Hacky: clear the data here...
                                    else
                                    {
                                       device_data.track_buffer_data.hash.clear();
                                       device_data.track_buffer_data.data.clear();
                                    }
                                 }

                                 ImGui::NewLine();
                                 ImGui::Text("State Analysis:");
                                 if (ImGui::BeginChild("StateAnalysisScroll", ImVec2(0, -FLT_MIN), ImGuiChildFlags_Border)) // I prefer it without a separate scrolling box for now
                                 {
                                    bool is_first_draw = true;

                                    if (pipeline_pair->second->HasComputeShader())
                                    {
                                       ImGui::Text("Indirect: %s", draw_call_data.draw_dispatch_data.indirect ? "True" : "False");
                                       ImGui::Text("Dispatch Count: %ux%ux%u", draw_call_data.draw_dispatch_data.dispatch_count.x, draw_call_data.draw_dispatch_data.dispatch_count.y, draw_call_data.draw_dispatch_data.dispatch_count.z);
                                       is_first_draw = false;
                                    }
                                    else if (pipeline_pair->second->HasVertexShader())
                                    {
                                       ImGui::Text("Indirect: %s", draw_call_data.draw_dispatch_data.indirect ? "True" : "False");
                                       ImGui::NewLine();
                                       ImGui::Text("Vertex Count: %u", draw_call_data.draw_dispatch_data.vertex_count);
                                       ImGui::Text("Instance Count: %u", draw_call_data.draw_dispatch_data.instance_count);
                                       ImGui::Text("First Vertex: %u", draw_call_data.draw_dispatch_data.first_vertex);
                                       ImGui::Text("First Instance: %u", draw_call_data.draw_dispatch_data.first_instance);
                                       ImGui::Text("Index Count: %u", draw_call_data.draw_dispatch_data.index_count);
                                       ImGui::Text("First Index: %u", draw_call_data.draw_dispatch_data.first_index);
                                       ImGui::Text("Vertex Offset: %i", draw_call_data.draw_dispatch_data.vertex_offset);
                                       ImGui::Text("Indexed: %s", draw_call_data.draw_dispatch_data.indexed ? "True" : "False"); // TODO: do we need this? Is "index_count" enough? The functions would be different.

                                       auto GetPrimitiveTopologyName = [](D3D_PRIMITIVE_TOPOLOGY topology) -> const char*
                                          {
                                             switch (topology)
                                             {
                                             case D3D_PRIMITIVE_TOPOLOGY_UNDEFINED:         return "UNDEFINED";
                                             case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:         return "POINTLIST";
                                             case D3D_PRIMITIVE_TOPOLOGY_LINELIST:          return "LINELIST";
                                             case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:         return "LINESTRIP";
                                             case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:      return "TRIANGLELIST";
                                             case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:     return "TRIANGLESTRIP";
                                             case D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN:       return "TRIANGLEFAN";
                                             case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:      return "LINELIST ADJ";
                                             case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:     return "LINESTRIP ADJ";
                                             case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:  return "TRIANGLELIST ADJ";
                                             case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ: return "TRIANGLESTRIP ADJ";
                                             default:                                       return "UNKNOWN";
                                             }
                                          };
                                       ImGui::NewLine();
                                       ImGui::Text("Primitive Topology: %s", GetPrimitiveTopologyName(draw_call_data.primitive_topology));

                                       for (size_t i = 0; i < draw_call_data.vertex_buffer_hashes.size(); i++)
                                       {
                                          ImGui::NewLine();
                                          ImGui::Text("Vertex Buffer %u: %s", i, draw_call_data.vertex_buffer_hashes[i].c_str());
                                          ImGui::Text("Input Layout %u Format: %s", i, GetFormatNameSafe(draw_call_data.input_layouts_formats[i]));
                                       }

                                       ImGui::NewLine();
                                       ImGui::Text("Input Buffer: %s", draw_call_data.input_layout_hash.c_str());
                                       ImGui::Text("Input Buffer Format: %s", GetFormatNameSafe(draw_call_data.index_buffer_format));
                                       ImGui::Text("Input Buffer Offset: %u", draw_call_data.index_buffer_offset);

                                       ImGui::Text("Input Layout: %s", draw_call_data.index_buffer_hash.c_str());
                                       is_first_draw = false;
                                    }

                                    if (pipeline_pair->second->HasVertexShader() || pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                    {
                                       for (UINT i = 0; i < TraceDrawCallData::srvs_size; i++)
                                       {
                                          auto srv_format = draw_call_data.srv_format[i];
                                          if (srv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                                          {
                                             continue;
                                          }
                                          const bool non_referenced = srv_format == DXGI_FORMAT(-1);
                                          if (trace_ignore_non_bound_shader_referenced_resources && srv_format == DXGI_FORMAT(-1))
                                          {
                                             continue;
                                          }
                                          auto sr_format = draw_call_data.sr_format[i];
                                          auto sr_size = draw_call_data.sr_size[i];
                                          auto srv_size = draw_call_data.srv_size[i];
                                          auto sr_hash = draw_call_data.sr_hash[i];
                                          auto sr_type_name = draw_call_data.sr_type_name[i];
                                          auto sr_is_rt = draw_call_data.sr_is_rt[i];
                                          auto sr_is_ua = draw_call_data.sr_is_ua[i];

                                          ImGui::PushID(i);

                                          if (!is_first_draw) { ImGui::Text(""); };
                                          is_first_draw = false;
                                          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(245, 230, 140, 255)); // Faint Yellow
                                          ImGui::Text("SRV Index: %u", i);
                                          ImGui::PopStyleColor();
                                          if (non_referenced)
                                          {
                                             ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                             ImGui::Text("R Referenced but Not Bound");
                                             ImGui::PopStyleColor();
                                             continue;
                                          }
                                          ImGui::Text("R Hash: %s", sr_hash.c_str());
                                          if (!draw_call_data.sr_debug_name[i].empty())
                                             ImGui::Text("R Debug Name: %s", draw_call_data.sr_debug_name[i].c_str());
                                          ImGui::Text("R Type: %s", sr_type_name.c_str());
                                          if (GetFormatName(sr_format) != nullptr)
                                          {
                                             ImGui::Text("R Format: %s", GetFormatName(sr_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("R Format: %u", sr_format);
                                          }
                                          if (GetFormatName(srv_format) != nullptr)
                                          {
                                             ImGui::Text("RV Format: %s", GetFormatName(srv_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("RV Format: %u", srv_format);
                                          }
                                          ImGui::Text("R Size: %ux%ux%ux%u", sr_size.x, sr_size.y, sr_size.z, sr_size.w);
                                          ImGui::Text("RV Mip: %u", draw_call_data.srv_mip[i]);
                                          ImGui::Text("RV Size: %ux%ux%u", srv_size.x, srv_size.y, srv_size.z);
                                          ImGui::Text("R is RT: %s", sr_is_rt ? "True" : "False");
                                          ImGui::Text("R is UA: %s", sr_is_ua ? "True" : "False");
                                          bool upgraded = false;
                                          {
                                             const std::shared_lock lock(device_data.mutex);
                                             // TODO: store this information in the trace list, it might expire otherwise, or even be incorrect if ptrs were re-used
                                             for (auto upgraded_resource_pair : device_data.original_upgraded_resources_formats)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Direct Upgraded");

                                                   ImGui::Text("R Original Format: %s", GetFormatName(DXGI_FORMAT(upgraded_resource_pair.second)));

                                                   if (const auto it = device_data.original_upgraded_resource_views_formats.find(reinterpret_cast<uint64_t>(draw_call_data.srvs[i])); it != device_data.original_upgraded_resource_views_formats.end())
                                                   {
                                                      const auto& [native_resource, original_view_format] = it->second;
                                                      ASSERT_ONCE(native_resource == upgraded_resource_pair.first); // Uh!?

                                                      // If the game already tried to create a view in the upgraded format, it means it simply read the format from the upgraded texture,
                                                      // and thus we can assume the original format would have been the same as the original texture (or anyway the most obvious non typeless version of it)
                                                      DXGI_FORMAT adjusted_original_view_format = (DXGI_FORMAT(original_view_format) == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DXGI_FORMAT(upgraded_resource_pair.second) : DXGI_FORMAT(original_view_format);

                                                      ImGui::Text("RV Original Format: %s", GetFormatName(adjusted_original_view_format));
                                                      // TODO: if the native texture format is TYPELESS, don't send this warning? Alternatively keep track of how the resource was last used (with what view it was written to, if any), and base the state off of that,
                                                      // then check the current state of the backbuffer and whether it's currently holding linear or gamma space colors (we don't store that anywhere atm, given it's not that simple).
                                                      // We could also send a message in case the upgraded format was float and the original format was not linear, but that's kinda obvious already (that the current color encoding might not be the most optimal).
                                                      if (IsLinearFormat(DXGI_FORMAT(upgraded_resource_pair.second)) != IsLinearFormat(DXGI_FORMAT(adjusted_original_view_format)))
                                                      {
                                                         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                                         ImGui::Text("RV Gamma Change");
                                                         ImGui::PopStyleColor();
                                                      }
                                                   }

                                                   upgraded = true;
                                                
                                                   break;
                                                }
                                             }
                                             for (auto upgraded_resource_pair : device_data.original_resources_to_mirrored_upgraded_resources)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Indirect Upgraded");
                                                   upgraded = true;
                                                   break;
                                                }
                                             }
                                          }

                                          const bool is_highlighted_resource = highlighted_resource == sr_hash;
                                          if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                          {
                                             highlighted_resource = is_highlighted_resource ? "" : sr_hash;
                                          }

                                          // TODO: hide the button if the resource is a buffer
                                          if (debug_draw_shader_enabled && (debug_draw_mode != DebugDrawMode::ShaderResource || debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                                          {
                                             if (debug_draw_mode == DebugDrawMode::Depth)
                                             {
                                                debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                             }
                                             debug_draw_mode = DebugDrawMode::ShaderResource;
                                             debug_draw_view_index = i;
                                             debug_draw_mip = draw_call_data.srv_mip[i];
                                          }

                                          ImGui::PopID();
                                       }
                                    }

                                    if (pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                    {
                                       for (UINT i = 0; i < TraceDrawCallData::uavs_size; i++)
                                       {
                                          auto uav_format = draw_call_data.uav_format[i];
                                          if (uav_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                                          {
                                             continue;
                                          }
                                          const bool non_referenced = uav_format == DXGI_FORMAT(-1);
                                          if (trace_ignore_non_bound_shader_referenced_resources && uav_format == DXGI_FORMAT(-1))
                                          {
                                             continue;
                                          }
                                          auto ua_format = draw_call_data.ua_format[i];
                                          auto ua_size = draw_call_data.ua_size[i];
                                          auto uav_size = draw_call_data.uav_size[i];
                                          auto ua_hash = draw_call_data.ua_hash[i];
                                          auto ua_type_name = draw_call_data.ua_type_name[i];
                                          auto ua_is_rt = draw_call_data.ua_is_rt[i];

                                          ImGui::PushID(i + TraceDrawCallData::srvs_size); // Offset by the max amount of previous iterations from above

                                          if (!is_first_draw) { ImGui::Text(""); };
                                          is_first_draw = false;
                                          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 170, 230, 255)); // Faint Purple
                                          ImGui::Text("UAV Index: %u", i);
                                          ImGui::PopStyleColor();
                                          if (non_referenced)
                                          {
                                             ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                             ImGui::Text("R Referenced but Not Bound");
                                             ImGui::PopStyleColor();
                                             continue;
                                          }
                                          ImGui::Text("R Hash: %s", ua_hash.c_str());
                                          if (!draw_call_data.ua_debug_name[i].empty())
                                             ImGui::Text("R Debug Name: %s", draw_call_data.ua_debug_name[i].c_str());
                                          ImGui::Text("R Type: %s", ua_type_name.c_str());
                                          if (GetFormatName(ua_format) != nullptr)
                                          {
                                             ImGui::Text("R Format: %s", GetFormatName(ua_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("R Format: %u", ua_format);
                                          }
                                          if (GetFormatName(uav_format) != nullptr)
                                          {
                                             ImGui::Text("RV Format: %s", GetFormatName(uav_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("RV Format: %u", uav_format);
                                          }
                                          ImGui::Text("R Size: %ux%ux%ux%u", ua_size.x, ua_size.y, ua_size.z, ua_size.w);
                                          ImGui::Text("RV Mip: %u", draw_call_data.uav_mip[i]);
                                          ImGui::Text("RV Size: %ux%ux%u", uav_size.x, uav_size.y, uav_size.z);
                                          ImGui::Text("R is RT: %s", ua_is_rt ? "True" : "False");
                                          bool upgraded = false;
                                          {
                                             const std::shared_lock lock(device_data.mutex);
                                             for (auto upgraded_resource_pair : device_data.original_upgraded_resources_formats)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (ua_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Direct Upgraded");

                                                   ImGui::Text("R Original Format: %s", GetFormatName(DXGI_FORMAT(upgraded_resource_pair.second)));

                                                   if (const auto it = device_data.original_upgraded_resource_views_formats.find(reinterpret_cast<uint64_t>(draw_call_data.uavs[i])); it != device_data.original_upgraded_resource_views_formats.end())
                                                   {
                                                      const auto& [native_resource, original_view_format] = it->second;
                                                      ASSERT_ONCE(native_resource == upgraded_resource_pair.first); // Uh!?
                                                      DXGI_FORMAT adjusted_original_view_format = (DXGI_FORMAT(original_view_format) == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DXGI_FORMAT(upgraded_resource_pair.second) : DXGI_FORMAT(original_view_format);
                                                      ImGui::Text("RV Original Format: %s", GetFormatName(adjusted_original_view_format));
                                                      if (IsLinearFormat(DXGI_FORMAT(upgraded_resource_pair.second)) != IsLinearFormat(adjusted_original_view_format))
                                                      {
                                                         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                                         ImGui::Text("RV Gamma Change");
                                                         ImGui::PopStyleColor();
                                                      }
                                                   }

                                                   upgraded = true;

                                                   break;
                                                }
                                             }
                                             for (auto upgraded_resource_pair : device_data.original_resources_to_mirrored_upgraded_resources)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (ua_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Indirect Upgraded");
                                                   upgraded = true;
                                                   break;
                                                }
                                             }
                                          }

                                          const bool is_highlighted_resource = highlighted_resource == ua_hash;
                                          if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                          {
                                             highlighted_resource = is_highlighted_resource ? "" : ua_hash;
                                          }

                                          if (!upgraded && texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && ImGui::Button("Indirect Upgrade Resource Format (By Shader)"))
                                          {
                                             auto_texture_format_upgrade_shader_hashes[pipeline_pair->second->shader_hashes[0]] = std::pair{ std::vector<uint8_t>(), std::vector<uint8_t>{ uint8_t(i) } }; // DX11 logic
                                          }

                                          if (debug_draw_shader_enabled && (debug_draw_mode != DebugDrawMode::UnorderedAccessView || debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                                          {
                                             if (debug_draw_mode == DebugDrawMode::Depth)
                                             {
                                                debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                             }
                                             debug_draw_mode = DebugDrawMode::UnorderedAccessView;
                                             debug_draw_view_index = i;
                                             debug_draw_mip = draw_call_data.uav_mip[i];
                                          }

                                          bool is_redirection_target = pipeline_pair->second->redirect_data.target_type == CachedPipeline::RedirectData::RedirectTargetType::UAV && pipeline_pair->second->redirect_data.target_index == i;
                                          if (pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && is_redirection_target && ImGui::Button("Disable Copy"))
                                          {
                                             pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::None;
                                             pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::None;
                                             pipeline_pair->second->redirect_data.source_index = 0;
                                             pipeline_pair->second->redirect_data.target_index = 0;
                                             is_redirection_target = false;
                                          }
                                          if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::SRV || !is_redirection_target) && ImGui::Button("Copy from SRV"))
                                          {
                                             pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::SRV;
                                             pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::UAV;
                                             pipeline_pair->second->redirect_data.source_index = 0;
                                             pipeline_pair->second->redirect_data.target_index = i;
                                             is_redirection_target = true;
                                          }
                                          if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::UAV || !is_redirection_target) && ImGui::Button("Copy from UAV"))
                                          {
                                             pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::UAV;
                                             pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::UAV;
                                             pipeline_pair->second->redirect_data.source_index = 0;
                                             pipeline_pair->second->redirect_data.target_index = i;
                                             is_redirection_target = true;
                                          }
                                          if (is_redirection_target)
                                          {
                                             ImGui::SliderInt("Copy from View Index", &pipeline_pair->second->redirect_data.source_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT /*The largest allowed view count by type*/);
                                          }

                                          ImGui::PopID();
                                       }
                                    }

                                    if (pipeline_pair->second->HasPixelShader())
                                    {
                                       auto blend_desc = draw_call_data.blend_desc;

                                       for (UINT i = 0; i < TraceDrawCallData::rtvs_size; i++)
                                       {
                                          auto rtv_format = draw_call_data.rtv_format[i];
                                          if (rtv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                                          {
                                             continue;
                                          }
                                          const bool non_referenced = rtv_format == DXGI_FORMAT(-1);
                                          if (trace_ignore_non_bound_shader_referenced_resources && rtv_format == DXGI_FORMAT(-1))
                                          {
                                             continue;
                                          }
                                          auto rt_format = draw_call_data.rt_format[i];
                                          auto rt_size = draw_call_data.rt_size[i];
                                          auto rtv_size = draw_call_data.rtv_size[i];
                                          auto rt_hash = draw_call_data.rt_hash[i];
                                          auto rt_type_name = draw_call_data.rt_type_name[i];

                                          ImGui::PushID(i + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size); // Offset by the max amount of previous iterations from above

                                          if (!is_first_draw) { ImGui::Text(""); };
                                          is_first_draw = false;
                                          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 200, 255, 255)); // Faint Blue
                                          ImGui::Text("RTV Index: %u", i);
                                          ImGui::PopStyleColor();
                                          if (non_referenced)
                                          {
                                             ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                             ImGui::Text("R Referenced but Not Bound");
                                             ImGui::PopStyleColor();
                                             continue;
                                          }
                                          ImGui::Text("R Hash: %s", rt_hash.c_str());
                                          if (!draw_call_data.rt_debug_name[i].empty())
                                             ImGui::Text("R Debug Name: %s", draw_call_data.rt_debug_name[i].c_str());
                                          ImGui::Text("R Type: %s", rt_type_name.c_str());
                                          if (GetFormatName(rt_format) != nullptr)
                                          {
                                             ImGui::Text("R Format: %s", GetFormatName(rt_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("R Format: %u", rt_format);
                                          }
                                          if (GetFormatName(rtv_format) != nullptr)
                                          {
                                             ImGui::Text("RV Format: %s", GetFormatName(rtv_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("RV Format: %u", rtv_format);
                                          }
                                          ImGui::Text("R Size: %ux%ux%ux%u", rt_size.x, rt_size.y, rt_size.z, rt_size.w);
                                          ImGui::Text("RV Mip: %u", draw_call_data.rtv_mip[i]);
                                          ImGui::Text("RV Size: %ux%ux%u", rtv_size.x, rtv_size.y, rtv_size.z);
                                          bool upgraded = false;
                                          {
                                             const std::shared_lock lock(device_data.mutex);
                                             // TODO: this is missing the "R is UAV" print
                                             for (auto upgraded_resource_pair : device_data.original_upgraded_resources_formats)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Direct Upgraded");

                                                   ImGui::Text("R Original Format: %s", GetFormatName(DXGI_FORMAT(upgraded_resource_pair.second)));

                                                   // TODO: why does this flicker on and off in Deux Ex HR when it writes on the swapchain for material draws?
                                                   if (const auto it = device_data.original_upgraded_resource_views_formats.find(reinterpret_cast<uint64_t>(draw_call_data.rtvs[i])); it != device_data.original_upgraded_resource_views_formats.end())
                                                   {
                                                      const auto& [native_resource, original_view_format] = it->second;
                                                      ASSERT_ONCE(native_resource == upgraded_resource_pair.first); // Uh!?
                                                      DXGI_FORMAT adjusted_original_view_format = (DXGI_FORMAT(original_view_format) == DXGI_FORMAT_R16G16B16A16_FLOAT) ? DXGI_FORMAT(upgraded_resource_pair.second) : DXGI_FORMAT(original_view_format);
                                                      ImGui::Text("RV Original Format: %s", GetFormatName(adjusted_original_view_format));
                                                      if (IsLinearFormat(DXGI_FORMAT(upgraded_resource_pair.second)) != IsLinearFormat(adjusted_original_view_format))
                                                      {
                                                         ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                                         ImGui::Text("RV Gamma Change");
                                                         ImGui::PopStyleColor();
                                                      }
                                                   }

                                                   upgraded = true;

                                                   break;
                                                }
                                             }
                                             for (auto upgraded_resource_pair : device_data.original_resources_to_mirrored_upgraded_resources)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Indirect Upgraded");
                                                   upgraded = true;
                                                   break;
                                                }
                                             }
                                          }
                                          ImGui::Text("R Swapchain: %s", draw_call_data.rt_is_swapchain[i] ? "True" : "False"); // TODO: add this for compute shaders / UAVs toos

                                          // Blend mode
                                          {
                                             bool pop_text_style_color = false;
                                             // Print out invalid blend modes. We assume "R16G16B16A16_FLOAT" if "upgraded" is true (indirect upgrades).
                                             if (IsRGBAFormat(draw_call_data.rtv_format[i], true) && (upgraded || IsSignedFloatFormat(draw_call_data.rtv_format[i])) && IsBlendInverted(draw_call_data.blend_desc, 1, false, i))
                                             {
                                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 105, 0, 255)); // Orange
                                                pop_text_style_color = true;
                                             }

                                             const D3D11_RENDER_TARGET_BLEND_DESC& render_target_blend_desc = blend_desc.IndependentBlendEnable ? blend_desc.RenderTarget[i] : blend_desc.RenderTarget[0];
                                             // See "ui_data.blend_mode" for details on usage
                                             if (render_target_blend_desc.BlendEnable)
                                             {
                                                bool has_drawn_blend_rgb_text = false;

                                                if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR || render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR
                                                   || render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR || render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR)
                                                {
                                                   ImGui::Text("Blend RGB Mode: Blend Factor (Any)");
                                                   has_drawn_blend_rgb_text = true;
                                                }
                                                else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
                                                {
                                                   ImGui::Text("Blend RGB Mode: Zero (Override Color with Zero)");
                                                   has_drawn_blend_rgb_text = true;
                                                }

                                                if (!has_drawn_blend_rgb_text && render_target_blend_desc.BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
                                                {
                                                   if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Additive Color");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Additive Alpha");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Additive Alpha (Saturated)");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Premultiplied Alpha"); // The alpha was supposedly (but not necessarily) pre-multiplied in the rgb before the pixel shader output
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Straight Alpha");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Straight Alpha (Saturated)");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   // Often used for lighting, glow, or compositing effects where the destination alpha controls how much of the source contributes
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_DEST_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Reverse Premultiplied Alpha");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_DEST_COLOR && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Multiplicative Color");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   // It's enabled but it's as if it was disabled
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Disabled");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                }
                                                // This subtracts the source from the target
                                                else if (!has_drawn_blend_rgb_text && render_target_blend_desc.BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT)
                                                {
                                                   if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Subtractive Color");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Subtractive Alpha");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Subtractive Alpha (Saturated)");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Disabled");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                   else
                                                   {
                                                      ImGui::Text("Blend RGB Mode: Subtractive (Any)");
                                                      has_drawn_blend_rgb_text = true;
                                                   }
                                                }

                                                if (!has_drawn_blend_rgb_text)
                                                {
                                                   ImGui::Text("Blend RGB Mode: Unknown");
                                                   has_drawn_blend_rgb_text = true;
                                                }

                                                if ((render_target_blend_desc.RenderTargetWriteMask & (D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE)) == 0)
                                                {
                                                   ImGui::SameLine();
                                                   ImGui::Text(" (Ignored)");
                                                }

                                                // TODO: add "SampleMask" etc (stencil too, together with stencil debug draw, but that goes with depth)
                                                ImGui::Text("Blend RGB Mode Details: Src %s - Op %s - Dest %s", GetBlendName(render_target_blend_desc.SrcBlend), GetBlendOpName(render_target_blend_desc.BlendOp), GetBlendName(render_target_blend_desc.DestBlend));

                                                bool has_drawn_blend_a_text = false;

                                                // It's enabled but it's as if it was disabled
                                                if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR || render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR
                                                   || render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR || render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR)
                                                {
                                                   ImGui::Text("Blend A Mode: Blend Factor (Any)");
                                                   has_drawn_blend_a_text = true;
                                                }
                                                if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                                                {
                                                   ImGui::Text("Blend A Mode: Zero (Overwrite Alpha with Zero)");
                                                   has_drawn_blend_a_text = true;
                                                }

                                                if (!has_drawn_blend_a_text && render_target_blend_desc.BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
                                                {
                                                   if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                                   {
                                                      ImGui::Text("Blend A Mode: Standard Transparency");
                                                      has_drawn_blend_a_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                                                   {
                                                      ImGui::Text("Blend A Mode: Standard Transparency (Saturated)");
                                                      has_drawn_blend_a_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_DEST_ALPHA && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                                                   {
                                                      ImGui::Text("Blend A Mode: Multiplicative");
                                                      has_drawn_blend_a_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend A Mode: Additive");
                                                      has_drawn_blend_a_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                                                   {
                                                      ImGui::Text("Blend A Mode: Source Alpha (Overwrite Alpha, Blending Disabled)");
                                                      has_drawn_blend_a_text = true;
                                                   }
                                                   else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
                                                   {
                                                      ImGui::Text("Blend A Mode: Destination Alpha (Preserve Alpha)");
                                                      has_drawn_blend_a_text = true;
                                                   }
                                                }
                                                else if (!has_drawn_blend_a_text && render_target_blend_desc.BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT)
                                                {
                                                   ImGui::Text("Blend A Mode: Subtractive (Any)");
                                                   has_drawn_blend_a_text = true;
                                                }

                                                if (!has_drawn_blend_a_text)
                                                {
                                                   ImGui::Text("Blend A Mode: Unknown");
                                                   has_drawn_blend_a_text = true;
                                                }

                                                if ((render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA) == 0)
                                                {
                                                   ImGui::SameLine();
                                                   ImGui::Text(" (Ignored)");
                                                }

                                                ImGui::Text("Blend A Mode Details: Src %s - Op %s - Dest %s", GetBlendName(render_target_blend_desc.SrcBlendAlpha), GetBlendOpName(render_target_blend_desc.BlendOpAlpha), GetBlendName(render_target_blend_desc.DestBlendAlpha));
                                             }
                                             else
                                             {
                                                ImGui::Text("Blend Mode: Disabled");
                                             }

                                             if (pop_text_style_color)
                                             {
                                                ImGui::PopStyleColor();
                                             }

                                             // This applies even if blending is disabled!
                                             if ((render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALL) == D3D11_COLOR_WRITE_ENABLE_ALL)
                                             {
                                                ImGui::Text("Write Mask: All");
                                             }
                                             else if ((render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALL) == 0)
                                             {
                                                ImGui::Text("Write Mask: None");
                                             }
                                             else
                                             {
                                                ImGui::Text("Write Mask:");
                                                if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_RED)   ImGui::SameLine(), ImGui::Text(" R");
                                                if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_GREEN) ImGui::SameLine(), ImGui::Text(" G");
                                                if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_BLUE)  ImGui::SameLine(), ImGui::Text(" B");
                                                if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA) ImGui::SameLine(), ImGui::Text(" A");
                                             }
                                          }

                                          // Show even if all blend modes are disabled, given that it might still be useful to track its state
                                          ImGui::Text("Blend Factor: %f %f %f %f", draw_call_data.blend_factor[0], draw_call_data.blend_factor[1], draw_call_data.blend_factor[2], draw_call_data.blend_factor[3]);

                                          const bool is_highlighted_resource = highlighted_resource == rt_hash;
                                          if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                          {
                                             highlighted_resource = is_highlighted_resource ? "" : rt_hash;
                                          }

                                          if (!upgraded && texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && ImGui::Button("Indirect Upgrade Resource Format (By Shader)"))
                                          {
                                             auto_texture_format_upgrade_shader_hashes[pipeline_pair->second->shader_hashes[0]] = std::pair{ std::vector<uint8_t>{ uint8_t(i) }, std::vector<uint8_t>() }; // DX11 logic
                                          }

                                          if (debug_draw_shader_enabled && (debug_draw_mode != DebugDrawMode::RenderTarget || debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                                          {
                                             if (debug_draw_mode == DebugDrawMode::Depth)
                                             {
                                                debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                             }
                                             debug_draw_mode = DebugDrawMode::RenderTarget;
                                             debug_draw_view_index = i;
                                             debug_draw_mip = draw_call_data.rtv_mip[i];
                                          }

                                          bool is_redirection_target = pipeline_pair->second->redirect_data.target_type == CachedPipeline::RedirectData::RedirectTargetType::RTV && pipeline_pair->second->redirect_data.target_index == i;
                                          if (pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && is_redirection_target && ImGui::Button("Disable Copy"))
                                          {
                                             pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::None;
                                             pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::None;
                                             pipeline_pair->second->redirect_data.source_index = 0;
                                             pipeline_pair->second->redirect_data.target_index = 0;
                                             is_redirection_target = false;
                                          }
                                          if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::SRV || !is_redirection_target) && ImGui::Button("Copy from SRV"))
                                          {
                                             pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::SRV;
                                             pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::RTV;
                                             pipeline_pair->second->redirect_data.source_index = 0;
                                             pipeline_pair->second->redirect_data.target_index = i;
                                             is_redirection_target = true;
                                          }
                                          if ((pipeline_pair->second->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::UAV || !is_redirection_target) && ImGui::Button("Copy from UAV"))
                                          {
                                             pipeline_pair->second->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::UAV;
                                             pipeline_pair->second->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::RTV;
                                             pipeline_pair->second->redirect_data.source_index = 0;
                                             pipeline_pair->second->redirect_data.target_index = i;
                                             is_redirection_target = true;
                                          }
                                          if (is_redirection_target)
                                          {
                                             ImGui::SliderInt("Copy from View Index", &pipeline_pair->second->redirect_data.source_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT /*The largest allowed view count by type*/);
                                          }

                                          ImGui::PopID();
                                       }

                                       // Only print this when enabled given it's rare (it's MSAA transparency dithering basically)
                                       if (blend_desc.AlphaToCoverageEnable)
                                       {
                                          if (!is_first_draw) { ImGui::Text(""); };
                                          // Don't set "is_first_draw" to true to let depth printing have a space too
                                          ImGui::Text("Alpha to Coverage Enable: %s", blend_desc.AlphaToCoverageEnable ? "True" : "False");
                                       }

                                       if (!is_first_draw) { ImGui::Text(""); }; // No views drew before, skip space
                                       is_first_draw = false;
                                       ImGui::Text("Depth State: %s", TraceDrawCallData::depth_state_names[(size_t)draw_call_data.depth_state]);
                                       ImGui::Text("Stencil Enabled: %s", draw_call_data.stencil_enabled ? "True" : "False");

                                       if (draw_call_data.dsv_format != DXGI_FORMAT_UNKNOWN && draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled && draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Invalid)
                                       {
                                          // Note: "trace_ignore_non_bound_shader_referenced_resources" isn't implemented here

                                          ImGui::PushID(TraceDrawCallData::rtvs_size + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size); // Offset by the max amount of previous iterations from above

                                          ImGui::Text("");
                                          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(190, 160, 120, 255)); // Faint Brown
                                          ImGui::Text("Depth");
                                          ImGui::PopStyleColor();
                                          ImGui::Text("R Hash: %s", draw_call_data.ds_hash.c_str());
                                          if (!draw_call_data.ds_debug_name.empty())
                                             ImGui::Text("R Debug Name: %s", draw_call_data.ds_debug_name.c_str());
                                          if (GetFormatName(draw_call_data.ds_format) != nullptr)
                                          {
                                             ImGui::Text("R Format: %s", GetFormatName(draw_call_data.ds_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("R Format: %u", draw_call_data.ds_format);
                                          }
                                          if (GetFormatName(draw_call_data.dsv_format) != nullptr)
                                          {
                                             ImGui::Text("RV Format: %s", GetFormatName(draw_call_data.dsv_format));
                                          }
                                          else
                                          {
                                             ImGui::Text("RV Format: %u", draw_call_data.dsv_format);
                                          }
                                          ImGui::Text("R Size: %ux%u", draw_call_data.ds_size.x, draw_call_data.ds_size.y); // Should match all the Render Targets size
                                          {
                                             const std::shared_lock lock(device_data.mutex);
                                             for (uint64_t upgraded_resource : device_data.upgraded_resources)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                                if (draw_call_data.ds_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Direct Upgraded");
                                                   break;
                                                }
                                             }
                                             for (auto upgraded_resource_pair : device_data.original_resources_to_mirrored_upgraded_resources)
                                             {
                                                void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                                if (draw_call_data.ds_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                                {
                                                   ImGui::Text("R: Indirect Upgraded");
                                                   break;
                                                }
                                             }
                                          }

                                          const bool is_highlighted_resource = highlighted_resource == draw_call_data.ds_hash;
                                          if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                          {
                                             highlighted_resource = is_highlighted_resource ? "" : draw_call_data.ds_hash;
                                          }

                                          ImGui::PopID();
                                       }

                                       const bool has_valid_depth = draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled
                                          && draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Invalid;
                                       if (has_valid_depth && debug_draw_shader_enabled && debug_draw_mode != DebugDrawMode::Depth && ImGui::Button("Debug Draw Depth Resource"))
                                       {
                                          debug_draw_mode = DebugDrawMode::Depth;
                                          debug_draw_view_index = 0;
                                          debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                                          debug_draw_mip = 0;
                                       }

                                       ImGui::Text("");
                                       ImGui::Text("Scissors Enabled: %s", draw_call_data.scissors ? "True" : "False");
                                       ImGui::Text("Viewport 0: x: %s y:%s w: %s h: %s",
                                          std::to_string(draw_call_data.viewport_0.x).c_str(),
                                          std::to_string(draw_call_data.viewport_0.y).c_str(),
                                          std::to_string(draw_call_data.viewport_0.z).c_str(),
                                          std::to_string(draw_call_data.viewport_0.w).c_str());
                                    }

                                    if (pipeline_pair->second->HasVertexShader() || pipeline_pair->second->HasPixelShader() || pipeline_pair->second->HasComputeShader())
                                    {
                                       for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
                                       {
                                          if (int(draw_call_data.samplers_filter[i]) >= 0)
                                          {
                                             ImGui::PushID(i + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size + TraceDrawCallData::rtvs_size); // Offset by the max amount of previous iterations from above
                                             ImGui::Text("");
                                             ImGui::Text("Sampler Index: %u", i);
                                             ImGui::Text("Sampler Filter: %s", GetFilterName(draw_call_data.samplers_filter[i]));
                                             ImGui::Text("Sampler Address U: %s", GetTextureAddressModeName(draw_call_data.samplers_address_u[i]));
                                             ImGui::Text("Sampler Address V: %s", GetTextureAddressModeName(draw_call_data.samplers_address_v[i]));
                                             ImGui::Text("Sampler Address W: %s", GetTextureAddressModeName(draw_call_data.samplers_address_w[i]));
                                             ImGui::Text("Sampler Mip LOD bias: %f", draw_call_data.samplers_mip_lod_bias[i]);
                                             ImGui::PopID();
                                          }
                                       }

                                       for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
                                       {
                                          if (draw_call_data.cbs[i])
                                          {
                                             ImGui::PushID(i + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size + TraceDrawCallData::rtvs_size + D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT); // Offset by the max amount of previous iterations from above
                                             ImGui::Text("");
                                             ImGui::Text("CB Index: %u", i);
                                             ImGui::Text("CB Hash: %s", draw_call_data.cb_hash[i].c_str());
                                             const bool is_highlighted_resource = highlighted_resource == draw_call_data.cb_hash[i];
                                             if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                             {
                                                highlighted_resource = is_highlighted_resource ? "" : draw_call_data.cb_hash[i];
                                             }
                                             ImGui::PopID();
                                          }
                                       }
                                    }
                                 }
                                 ImGui::EndChild(); // StateAnalysisScroll
                              }
                              ImGui::EndChild(); // Settings and Info
                           }
                           lock.unlock(); // Needed to prevent "LoadCustomShaders()" from deadlocking, and anyway, there's no need to lock it beyond the for loop above

                           if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CopyResource
                              || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead
                              || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPUWrite
                              || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource
                              || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ClearResource
                              || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom)
                           {
                              if (ImGui::BeginChild("Settings and Info"))
                              {
                                 const bool has_source_resource = draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CopyResource
                                    || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead
                                    || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource;
                                 const bool has_target_resource = draw_call_data.type != TraceDrawCallData::TraceDrawCallType::CPURead
                                    && draw_call_data.type != TraceDrawCallData::TraceDrawCallType::BindResource;

                                 if (has_source_resource)
                                 {
                                    ImGui::PushID(0);
                                    auto sr_format = draw_call_data.sr_format[0];
                                    auto sr_size = draw_call_data.sr_size[0];
                                    auto sr_hash = draw_call_data.sr_hash[0];
                                    auto sr_type_name = draw_call_data.sr_type_name[0];
                                    ImGui::Text("Source R Hash: %s", sr_hash.c_str());
                                    ImGui::Text("Source R Type: %s", sr_type_name.c_str());
                                    if (GetFormatName(sr_format) != nullptr)
                                    {
                                       ImGui::Text("Source R Format: %s", GetFormatName(sr_format));
                                    }
                                    ImGui::Text("Source R Size: %ux%ux%ux%u", sr_size.x, sr_size.y, sr_size.z, sr_size.w);
                                    for (uint64_t upgraded_resource : device_data.upgraded_resources)
                                    {
                                       void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                       if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                       {
                                          ImGui::Text("Source R: Direct Upgraded");
                                          // TODO: add og resource format here
                                          break;
                                       }
                                    }
                                    for (auto upgraded_resource_pair : device_data.original_resources_to_mirrored_upgraded_resources)
                                    {
                                       void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                       if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                       {
                                          ImGui::Text("Source R: Indirect Upgraded");
                                          break;
                                       }
                                    }

                                    const bool is_highlighted_resource = highlighted_resource == sr_hash && (sr_type_name == "Buffer" || sr_format != DXGI_FORMAT_UNKNOWN); // Skip if invalid
                                    if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                    {
                                       highlighted_resource = is_highlighted_resource ? "" : sr_hash;
                                    }
                                    ImGui::PopID();
                                 }

                                 if (has_target_resource)
                                 {
                                    if (has_source_resource)
                                       ImGui::Text(""); // Empty line for spacing

                                    auto rt_format = draw_call_data.rt_format[0];
                                    auto rt_size = draw_call_data.rt_size[0];
                                    auto rt_hash = draw_call_data.rt_hash[0];
                                    auto rt_type_name = draw_call_data.rt_type_name[0];

                                    ImGui::PushID(1);
                                    ImGui::Text("Target R Hash: %s", rt_hash.c_str());
                                    ImGui::Text("Target R Type: %s", rt_type_name.c_str());
                                    if (GetFormatName(rt_format) != nullptr)
                                    {
                                       ImGui::Text("Target R Format: %s", GetFormatName(rt_format));
                                    }
                                    else
                                    {
                                       ImGui::Text("Target R Format: %u", rt_format);
                                    }
                                    ImGui::Text("Target R Size: %ux%ux%ux%u", rt_size.x, rt_size.y, rt_size.z, rt_size.w);
                                    for (uint64_t upgraded_resource : device_data.upgraded_resources)
                                    {
                                       void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                       if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                       {
                                          ImGui::Text("Target R: Direct Upgraded");
                                          break;
                                       }
                                    }
                                    for (auto upgraded_resource_pair : device_data.original_resources_to_mirrored_upgraded_resources)
                                    {
                                       void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                       if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                       {
                                          ImGui::Text("Target R: Indirect Upgraded");
                                          break;
                                       }
                                    }
#if 0 // TODO: implement for this case (and above)
                                    ImGui::Text("Target R Swapchain: %s", draw_call_data.rt_is_swapchain[0] ? "True" : "False");
#endif

                                    const bool is_highlighted_resource = highlighted_resource == rt_hash && (rt_type_name == "Buffer" || rt_format != DXGI_FORMAT_UNKNOWN); // Skip if invalid
                                    if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                    {
                                       highlighted_resource = is_highlighted_resource ? "" : rt_hash;
                                    }

                                    ImGui::PopID();
                                 }
                              }
                              ImGui::EndChild(); // Settings and Info
                           }

                           // We need to do this here or it'd deadlock due to "s_mutex_generic" trying to be locked in shared mod again
                           if (reload && pipeline_handle != 0)
                           {
                              LoadCustomShaders(device_data, { pipeline_handle }, recompile);
                           }
                        }
                     }

                     ImGui::EndTabItem(); // Settings
                  }

                  const bool open_disassembly_tab_item = ImGui::BeginTabItem("Disassembly");
                  static bool opened_disassembly_tab_item = false;
                  if (open_disassembly_tab_item)
                  {
                     static bool pending_disassembly_refresh = false;

                     static const char* asm_types[] = { "Default", "Live Patched", "Custom" };
                     static int asm_type = 0;
                     if (ImGui::SliderInt("Type", &asm_type, 0, std::size(asm_types) - 1, asm_types[asm_type]))
                     {
                        pending_disassembly_refresh = true;
                     }

                     static std::string disasm_string;
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace(cmd_list_data.mutex_trace);

                     bool refresh_disassembly = changed_selected || opened_disassembly_tab_item != open_disassembly_tab_item || pending_disassembly_refresh;
                     pending_disassembly_refresh = false;

                     if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= (selected_index + 1) && refresh_disassembly)
                     {
                        const auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                        const std::unique_lock lock(s_mutex_generic);
                        if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                        {
                           if (asm_type == 0 || asm_type == 1)
                           {
                              const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                              auto* cache = (!pipeline_pair->second->shader_hashes.empty() && shader_cache.contains(pipeline_pair->second->shader_hashes[0])) ? shader_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                              if (asm_type == 0 || !cache || !cache->live_patched_data)
                              {
                                 if (cache && cache->disasm.empty())
                                 {
                                    auto disasm_code = Shader::DisassembleShader(cache->data, cache->size);
                                    if (disasm_code.has_value())
                                    {
                                       cache->disasm.assign(disasm_code.value());
                                    }
                                    else
                                    {
                                       cache->disasm.assign("DISASSEMBLY FAILED");
                                    }
                                 }
                                 disasm_string.assign(cache ? cache->disasm : "");
                              }
                              else
                              {
                                 if (cache && cache->live_patched_disasm.empty())
                                 {
                                    auto disasm_code = Shader::DisassembleShader(cache->live_patched_data, cache->live_patched_size);
                                    if (disasm_code.has_value())
                                    {
                                       cache->live_patched_disasm.assign(disasm_code.value());
                                    }
                                    else
                                    {
                                       cache->live_patched_disasm.assign("DISASSEMBLY FAILED");
                                    }
                                 }
                                 disasm_string.assign(cache ? cache->live_patched_disasm : "");
                              }
                           }
                           else if (asm_type == 2)
                           {
                              const std::unique_lock lock_loading(s_mutex_loading);
                              auto* cache = (!pipeline_pair->second->shader_hashes.empty() && custom_shaders_cache.contains(pipeline_pair->second->shader_hashes[0])) ? custom_shaders_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                              if (cache && cache->disasm.empty())
                              {
                                 auto disasm_code = Shader::DisassembleShader(cache->code.data(), cache->code.size());
                                 if (disasm_code.has_value())
                                 {
                                    cache->disasm.assign(disasm_code.value());
                                 }
                                 else
                                 {
                                    cache->disasm.assign("DISASSEMBLY FAILED");
                                 }
                              }
                              disasm_string.assign(cache ? cache->disasm : "");
                           }
                        }
                        else
                        {
                           disasm_string.clear();
                        }
                     }
                     // Force a refresh for later on
                     else if (refresh_disassembly)
                     {
                        pending_disassembly_refresh = true;
                        disasm_string.clear();
                     }

                     if (ImGui::BeginChild("DisassemblyCode"))
                     {
                        ImGui::InputTextMultiline(
                           "##disassemblyCode",
                           disasm_string.data(),
                           disasm_string.length() + 1, // Add the null terminator
                           ImVec2(-FLT_MIN, -FLT_MIN),
                           ImGuiInputTextFlags_ReadOnly);
                     }
                     ImGui::EndChild(); // DisassemblyCode
                     ImGui::EndTabItem(); // Disassembly
                  }
                  opened_disassembly_tab_item = open_disassembly_tab_item;

                  ImGui::PushID("##LiveCodeTabItem");
                  const bool open_live_tab_item = ImGui::BeginTabItem("HLSL");
                  ImGui::PopID();
                  static bool opened_live_tab_item = false;
                  if (open_live_tab_item)
                  {
                     static bool inline_includes = false;
                     bool inline_includes_toggled = ImGui::Checkbox("Inline Includes", &inline_includes);

                     static std::string hlsl_string;
                     static bool hlsl_error = false;
                     static bool hlsl_warning = false;
                     CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                     const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                     static bool pending_live_code_refresh = false;
                     bool refresh_live_code = changed_selected || inline_includes_toggled || opened_live_tab_item != open_live_tab_item || refresh_cloned_pipelines || pending_live_code_refresh;
                     pending_live_code_refresh = false;
                     if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= (selected_index + 1) && refresh_live_code)
                     {
                        bool hlsl_set = false;
                        const auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;

                        const std::shared_lock lock(s_mutex_generic);
                        if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                           pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                        {
                           const auto pipeline = pipeline_pair->second;
                           const std::shared_lock lock_loading(s_mutex_loading);
                           const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;
                           // If the custom shader has a compilation error, print that, otherwise read the file text
                           if (custom_shader != nullptr && !custom_shader->compilation_errors.empty())
                           {
                              hlsl_string = custom_shader->compilation_errors;
                              hlsl_error = custom_shader->compilation_error;
                              hlsl_warning = !custom_shader->compilation_error;
                              hlsl_set = true;
                           }
                           else if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty())
                           {
                              if (inline_includes && !custom_shader->preprocessed_code.empty())
                              {
                                 // Remove line breaks as there's a billion of them in the preprocessed code
                                 std::istringstream in_stream(custom_shader->preprocessed_code);
                                 std::ostringstream out_stream;
                                 std::string line;
                                 unsigned int empty_lines_count = 0;
                                 while (std::getline(in_stream, line))
                                 {
                                    // Skip lines that begin line "#line n" (unless they also include the name of a header, wrapped around "), for some reason the concatenated source file includes many of these.
                                    // Also skip pragmas as they aren't particularly relevant when viewing the source in a tiny box (this is arguable!).
                                    // Also skip any empty line beyond 2 ones, as usually it's empty spaces (or tabs) generated by the source concatenation.
                                    bool is_meta_line = line.rfind("#line ", 0) == 0 && !line.contains("\"");
                                    bool is_pragma_line = line.rfind("#pragma ", 0) == 0;
                                    if (is_meta_line || is_pragma_line || line.empty() || std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); }))
                                    {
                                       empty_lines_count++;
                                    }
                                    else
                                    {
                                       empty_lines_count = 0;
                                    }

                                    if (!is_meta_line && !is_pragma_line && empty_lines_count <= 2)
                                    {
                                       out_stream << line << '\n';
                                    }
                                 }
                                 hlsl_string = out_stream.str();
                                 hlsl_error = false;
                                 hlsl_warning = false;
                                 hlsl_set = true;
                              }
                              else
                              {
                                 auto result = ReadTextFile(custom_shader->file_path);
                                 if (result.has_value())
                                 {
                                    hlsl_string.assign(result.value());
                                    hlsl_error = false;
                                    hlsl_warning = false;
                                    hlsl_set = true;
                                 }
                              }
                              if (!hlsl_set)
                              {
                                 hlsl_string.assign("FAILED TO READ FILE");
                                 hlsl_error = true;
                                 hlsl_warning = false;
                                 hlsl_set = true;
                              }
                           }
                        }

                        if (!hlsl_set)
                        {
                           hlsl_string.clear();
                           hlsl_error = false;
                           hlsl_warning = false;
                        }
                     }
                     else if (refresh_live_code)
                     {
                        hlsl_string.clear();
                        hlsl_error = false;
                        hlsl_warning = false;
                        pending_live_code_refresh = true;
                     }
                     opened_live_tab_item = open_live_tab_item;

                     if (ImGui::BeginChild("LiveCode"))
                     {
                        ImGui::PushStyleColor(ImGuiCol_Text, hlsl_error ? IM_COL32(255, 0, 0, 255) : (hlsl_warning ? IM_COL32(255, 165, 0, 255) : IM_COL32(255, 255, 255, 255))); // Red for Error, Orange for Warning, White for the rest
                        ImGui::InputTextMultiline(
                           "##liveCode",
                           hlsl_string.data(),
                           hlsl_string.length() + 1, // Add the null terminator
                           ImVec2(-FLT_MIN, -FLT_MIN),
                           ImGuiInputTextFlags_ReadOnly);
                        ImGui::PopStyleColor();
                     }
                     ImGui::EndChild(); // LiveCode
                     ImGui::EndTabItem(); // HLSL
                  }

                  ImGui::EndTabBar(); // ShadersCodeTab
               }
               ImGui::EndDisabled();
            }
            ImGui::EndChild(); // ShaderDetails

            ImGui::EndTabItem(); // Captured Commands
         }

#if !GRAPHICS_ANALYZER
         // Show even if "custom_shaders_cache" was empty
         if (ImGui::BeginTabItem("Custom Shaders"))
         {
            static int32_t shaders_selected_index = -1;

            const CachedCustomShader* selected_custom_shader = nullptr;

            if (ImGui::BeginChild("CustomShadersList", ImVec2(500, -FLT_MIN), ImGuiChildFlags_ResizeX))
            {
               if (ImGui::BeginListBox("##CustomShadersListBox", ImVec2(-FLT_MIN, -FLT_MIN)))
               {
                  int index = 0;
                  const std::shared_lock lock(s_mutex_loading);
                  // TODO: list these in alphabetical order instead of random order
                  for (const auto& custom_shader : custom_shaders_cache)
                  {
                     if (custom_shader.second == nullptr)
                     {
                        index++;
                        continue;
                     }

                     bool is_selected = shaders_selected_index == index;

                     auto text_color = IM_COL32(255, 255, 255, 255); // White

                     if (custom_shader.second->compilation_error)
                     {
                        text_color = IM_COL32(255, 0, 0, 255); // Red
                     }
                     else if (!custom_shader.second->compilation_errors.empty())
                     {
                        text_color = IM_COL32(255, 105, 0, 255); // Orange
                     }

                     ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                     ImGui::PushID(index);
                     ImGui::PushID(custom_shader.second->preprocessed_hash); // Avoid files by the same simplified name (we also shader files with multiple hashes) from conflicting
                     
                     std::string file_name = custom_shader.second->file_path.stem().string();
                     // Remove all the shader hashes and the shader model ("xs_n_n")
                     if (!custom_shader.second->is_luma_native)
                     {
                        size_t pos = file_name.find("0x");
                        if (pos != std::string::npos)
                           file_name.erase(pos);
                     }
                     else
                     {
                        size_t pos = file_name.find("."); // After the first "." there would (likely) be the shader model
                        if (pos != std::string::npos)
                           file_name.erase(pos);
                     }
                     if (file_name.ends_with('_'))
                     {
                        file_name.erase(file_name.length() - 1, 1);
                     }
                     if (file_name.size() == 0)
                     {
                        file_name = "NO NAME";
                        text_color = IM_COL32(255, 0, 0, 255); // Red
                     }

                     if (ImGui::Selectable(file_name.c_str(), is_selected))
                     {
                        shaders_selected_index = index;
                        is_selected = shaders_selected_index == index;
                     }
                     ImGui::PopID();
                     ImGui::PopID();
                     ImGui::PopStyleColor();

                     if (is_selected)
                     {
                        ImGui::SetItemDefaultFocus();
                        selected_custom_shader = custom_shader.second;
                     }

                     index++;
                  }
                  ImGui::EndListBox();
               }
            }
            ImGui::EndChild(); // CustomShadersList

            ImGui::SameLine();
            if (ImGui::BeginChild("##ShaderDetails", ImVec2(0, 0)))
            {
               ImGui::BeginDisabled(selected_custom_shader == nullptr);

               uint64_t pipeline_to_recompile = 0;

               // Make sure our selected shader is still in the "custom_shaders_cache" list, without blocking its mutex through the whole imgui rendering
               bool custom_shader_found = false;
               uint32_t shader_hash;
               std::string shader_hash_str;
               {
                  std::shared_lock lock(s_mutex_loading);
                  for (const auto& custom_shader : custom_shaders_cache)
                  {
                     if (selected_custom_shader != nullptr && custom_shader.second == selected_custom_shader)
                     {
                        custom_shader_found = true;
                        shader_hash = uint32_t(custom_shader.first);
                        shader_hash_str = Shader::Hash_NumToStr(shader_hash, true);
                        break;
                     }
                  }
               }

               if (custom_shader_found)
               {
                  // TODO: show more info here (e.g. in how many pipelines it's used, the disasm etc). Unify with the other shader views from the graphics captures list. Allow hiding the ones not used by this sub game (e.g. unity) or by the current scene

                  ImGui::Text("Full Path: %s", selected_custom_shader->file_path.string().c_str());

                  ImGui::Text("File Type: %s", selected_custom_shader->is_hlsl ? "hlsl" : "cso");
                  ImGui::Text("Luma Native: %s", selected_custom_shader->is_luma_native ? "True" : "False");

                  if (selected_custom_shader->is_luma_native)
                  {
                     for (const auto& native_shader_definition : native_shaders_definitions)
                     {
                        if (native_shader_definition.second.file_name == selected_custom_shader->definition.file_name
                           && native_shader_definition.second.type == selected_custom_shader->definition.type
                           && native_shader_definition.second.function_name == selected_custom_shader->definition.function_name
                           && native_shader_definition.second.defines_data == selected_custom_shader->definition.defines_data)
                        {
                           ImGui::Text("Luma Native Key: %u", native_shader_definition.first); // TODO: unfortunately at this point the name we address it by in code is lost, so we only have a hash left, which is kinda useless information
                        }
                     }
                  }

                  // Some shaders lack the definition, don't show ambiguous info
                  if (selected_custom_shader->definition.type != reshade::api::pipeline_subobject_type::unknown)
                  {
                     ImGui::Text("Shader Type: ");
                     ImGui::SameLine();
                     switch (selected_custom_shader->definition.type)
                     {
                     case reshade::api::pipeline_subobject_type::vertex_shader:   ImGui::Text("Vertex"); break;
                     case reshade::api::pipeline_subobject_type::geometry_shader: ImGui::Text("Geometry"); break;
                     case reshade::api::pipeline_subobject_type::pixel_shader:    ImGui::Text("Pixel"); break;
                     case reshade::api::pipeline_subobject_type::compute_shader:  ImGui::Text("Compute"); break;
                     default:                                                     ImGui::Text("Unknown"); break;
                     }
                  }

                  // It should fall back to "main" if this isn't define but we don't need to show it
                  if (selected_custom_shader->definition.function_name)
                     ImGui::Text("Custom Function Name: %s", selected_custom_shader->definition.function_name);

                  for (size_t i = 0; i < selected_custom_shader->definition.defines_data.size(); i++)
                  {
                     const auto& define_data = selected_custom_shader->definition.defines_data[i];

                     ImGui::NewLine();
                     ImGui::Text("Define %zu Name: %s", i, define_data.name.c_str());
                     ImGui::Text("Define %zu Value: %s", i, define_data.value.c_str());
                  }

                  if (!selected_custom_shader->is_luma_native)
                  {
                     ImGui::Text("Original Hash: %s", shader_hash_str.c_str());

                     {
                        int i = 0;
                        std::unique_lock lock(s_mutex_generic);
                        auto pipelines_pair = device_data.pipeline_caches_by_shader_hash.find(shader_hash);
                        if (pipelines_pair != device_data.pipeline_caches_by_shader_hash.end())
                        {
                           for (auto pipeline : pipelines_pair->second)
                           {
                              ASSERT_ONCE(pipeline->cloned);

                              ImGui::PushID(i);
                              ImGui::NewLine();
                              pipeline->custom_name.empty() ? ImGui::Text("Pipeline: %i", i) : ImGui::Text("Pipeline: %s", pipeline->custom_name.c_str());
                              if (pipeline->HasPixelShader() || pipeline->HasComputeShader())
                              {
                                 //ImGui::Checkbox("Allow Shader Replace Draw NaNs", &allow_replace_draw_nans); // Disabled as it's draw too many times
                                 ImGui::SliderInt("Shader Replace Draw Type", reinterpret_cast<int*>(&pipeline->replace_draw_type), 0, IM_ARRAYSIZE(CachedPipeline::shader_replace_draw_type_names) - (allow_replace_draw_nans ? 1 : 2), CachedPipeline::shader_replace_draw_type_names[(size_t)pipeline->replace_draw_type], ImGuiSliderFlags_NoInput);
                                 if (pipeline->HasPixelShader())
                                    ImGui::SliderInt("Shader Custom Depth/Stencil Type", reinterpret_cast<int*>(&pipeline->custom_depth_stencil), 0, IM_ARRAYSIZE(CachedPipeline::shader_custom_depth_stencil_type_names) - 1, CachedPipeline::shader_custom_depth_stencil_type_names[(size_t)pipeline->custom_depth_stencil], ImGuiSliderFlags_NoInput);
                              }
                              if (ImGui::Button("Unload"))
                              {
                                 s_mutex_generic.unlock(); // Hack to avoid deadlocks with the device destroying pipelines
                                 UnloadCustomShaders(device_data, { reinterpret_cast<uint64_t>(pipeline) }, false);
                                 s_mutex_generic.lock();
                              }
#if 0 // TODO: this still deadlocks!
                              if (ImGui::Button(pipeline->cloned ? "Recompile" : "Load"))
                              {
                                 pipeline_to_recompile = reinterpret_cast<uint64_t>(pipeline);
                              }
#endif
                              ImGui::PopID();

                              i++;
                           }
                        }
                     }

                     ImGui::NewLine();

                     if (ImGui::Button("Copy Shader Hash to Clipboard"))
                     {
                        System::CopyToClipboard(shader_hash_str);
                     }
                  }
                  else
                  {
                     ImGui::NewLine();
                  }

                  if (selected_custom_shader->is_hlsl && !selected_custom_shader->file_path.empty() && ImGui::Button("Open hlsl in IDE"))
                  {
                     HINSTANCE ret_val = ShellExecuteA(nullptr, "open", "code", ("\"" + selected_custom_shader->file_path.string() + "\"").c_str(), nullptr, SW_SHOWNORMAL);
                     ASSERT_ONCE(ret_val > (HINSTANCE)32); // Unknown reason
                  }

                  if (!selected_custom_shader->file_path.empty() && ImGui::Button("Open in Explorer"))
                  {
                     System::OpenExplorerToFile(selected_custom_shader->file_path);
                  }

                  if (!selected_custom_shader->compilation_errors.empty())
                  {
                     if (ImGui::BeginChild("Shader Compilation Errors", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar))
                     {
                        ImGui::PushTextWrapPos(0.0f); // Disable wrapping
                        ImGui::TextUnformatted(selected_custom_shader->compilation_errors.c_str());
                        ImGui::PopTextWrapPos();
                     }
                     ImGui::EndChild();
                  }
               }

               if (pipeline_to_recompile != 0)
               {
                  bool recompile = true;
                  LoadCustomShaders(device_data, { pipeline_to_recompile }, recompile);
               }

               ImGui::EndDisabled();
            }
            ImGui::EndChild(); // ShaderDetails

            ImGui::EndTabItem(); // Custom Shaders
         }

         if (ImGui::BeginTabItem("Upgraded Textures"))
         {
            static int32_t resources_selected_index = -1;

            com_ptr<ID3D11Resource> selected_resource;

            if (ImGui::BeginChild("TexturesList", ImVec2(500, -FLT_MIN), ImGuiChildFlags_ResizeX))
            {
               if (ImGui::BeginListBox("##TexturesListBox", ImVec2(-FLT_MIN, -FLT_MIN)))
               {
                  int index = 0;
                  const std::shared_lock lock(device_data.mutex); // Note: this is probably not 100% safe, as we don't keep the resources as a com ptr, DX might destroy them as we iterate the array, but this is debug code so, whatever!

                  std::unordered_set<uint64_t> upgraded_resources = device_data.upgraded_resources;
                  for (const auto& original_resource_to_mirrored_upgraded_resource : device_data.original_resources_to_mirrored_upgraded_resources)
                  {
                     upgraded_resources.insert(original_resource_to_mirrored_upgraded_resource.second);
                  }
                  for (const auto upgraded_resource : upgraded_resources)
                  {
                     if (upgraded_resource == 0)
                     {
                        index++;
                        continue;
                     }

                     com_ptr<ID3D11Resource> native_resource = reinterpret_cast<ID3D11Resource*>(upgraded_resource);

                     bool is_selected = resources_selected_index == index;

                     auto text_color = IM_COL32(255, 255, 255, 255); // White

                     bool direct_upgraded = device_data.upgraded_resources.contains(upgraded_resource); // TODO: add all resources (textures), including non upgraded ones, swapchain (we couldn't draw debug that one!) etc
                     if (direct_upgraded)
                     {
                        text_color = IM_COL32(0, 255, 0, 255); // Green
                     }
                     else // Indirect upgraded
                     {
                        text_color = IM_COL32(12, 255, 12, 255); // Some Green
                     }

                     std::string name = std::to_string(std::hash<void*>{}(native_resource.get()));

                     const bool is_highlighted_resource = highlighted_resource == name;
                     if (is_highlighted_resource)
                     {
                        text_color = IM_COL32(255, 0, 0, 255); // Red
                     }

                     ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                     ImGui::PushID(index);

                     std::optional<std::string> debug_name = GetD3DNameW(native_resource.get());
                     if (debug_name.has_value())
                     {
                        name = debug_name.value();
                     }

                     if (ImGui::Selectable(name.c_str(), is_selected))
                     {
                        resources_selected_index = index;
                        is_selected = resources_selected_index == index;
                     }
                     ImGui::PopID();
                     ImGui::PopStyleColor();

                     if (is_selected)
                     {
                        ImGui::SetItemDefaultFocus();
                        selected_resource = native_resource;
                     }

                     index++;
                  }
                  ImGui::EndListBox();
               }
            }
            ImGui::EndChild(); // TexturesList

            ImGui::SameLine();
            if (ImGui::BeginChild("##TexturesDetails", ImVec2(0, 0)))
            {
               if (selected_resource.get())
               {
                  // TODO: show more info here (e.g. in which shaders it has been used, the amount of times it's been used)

                  std::string hash = std::to_string(std::hash<void*>{}(selected_resource.get()));
                  ImGui::Text("Hash: %s", hash.c_str());

                  std::optional<std::string> debug_name = GetD3DNameW(selected_resource.get());
                  if (debug_name.has_value())
                  {
                     ImGui::Text("Debug Name: %s", debug_name.value().c_str());
                  }

                  ImGui::Text("Upgraded: %s", "True"); // If it's here, it's upgraded for now

                  bool debug_draw_resource_enabled = device_data.debug_draw_texture == selected_resource;
                  UINT extra_refs = 1; // Our current local ref.
                  if (debug_draw_resource_enabled) extra_refs++; // The debug draw ref
                  // Note: there possibly might be more, spread into render targets (actually they don't seem to add references?), SRVs etc, that we set ourselves, but it's hard, but it might actually be correct already.
                  // ReShade doesn't seem to keep textures with hard refences, instead they add private data with a destructor to them, to detect when they are being garbage collected, and anyway it doesn't see the resources we created.

                  ImGui::Text("Reference Count: %lu", selected_resource.ref_count() - (unsigned long)(extra_refs));

                  // Note: it's possible to use "ID3D11Resource::GetType()" instead of this
                  com_ptr<ID3D11Texture2D> selected_texture_2d;
                  selected_resource->QueryInterface(&selected_texture_2d);
                  com_ptr<ID3D11Texture3D> selected_texture_3d;
                  selected_resource->QueryInterface(&selected_texture_3d);
                  com_ptr<ID3D11Texture1D> selected_texture_1d;
                  selected_resource->QueryInterface(&selected_texture_1d);

                  DXGI_FORMAT selected_texture_format = DXGI_FORMAT_UNKNOWN;
                  uint4 selected_texture_size = { 1, 1, 1, 1 };
                  if (selected_texture_2d)
                  {
                     D3D11_TEXTURE2D_DESC texture_desc;
                     selected_texture_2d->GetDesc(&texture_desc);
                     selected_texture_format = texture_desc.Format;
                     selected_texture_size.x = texture_desc.Width;
                     selected_texture_size.y = texture_desc.Height;
                     selected_texture_size.z = texture_desc.ArraySize;
                     selected_texture_size.w = max(texture_desc.MipLevels, texture_desc.SampleDesc.Count);
                  }
                  else if (selected_texture_3d)
                  {
                     D3D11_TEXTURE3D_DESC texture_desc;
                     selected_texture_3d->GetDesc(&texture_desc);
                     selected_texture_format = texture_desc.Format;
                     selected_texture_size.x = texture_desc.Width;
                     selected_texture_size.y = texture_desc.Height;
                     selected_texture_size.z = texture_desc.Depth;
                     selected_texture_size.w = texture_desc.MipLevels;
                  }
                  else if (selected_texture_1d)
                  {
                     D3D11_TEXTURE1D_DESC texture_desc;
                     selected_texture_1d->GetDesc(&texture_desc);
                     selected_texture_format = texture_desc.Format;
                     selected_texture_size.x = texture_desc.Width;
                     selected_texture_size.y = texture_desc.ArraySize;
                     selected_texture_size.z = texture_desc.MipLevels;
                  }

                  if (GetFormatName(selected_texture_format) != nullptr)
                  {
                     ImGui::Text("Format: %s", GetFormatName(selected_texture_format));
                  }
                  else
                  {
                     ImGui::Text("Format: %u", selected_texture_format);
                  }

                  ImGui::Text("Size: %ux%ux%ux%u", selected_texture_size.x, selected_texture_size.y, selected_texture_size.z, selected_texture_size.w);

                  const bool is_highlighted_resource = highlighted_resource == hash;
                  if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                  {
                     highlighted_resource = is_highlighted_resource ? "" : hash;
                  }

                  if (debug_draw_resource_enabled ? ImGui::Button("Disable Debug Draw Texture") : ImGui::Button("Debug Draw Texture"))
                  {
                     ASSERT_ONCE(GetShaderDefineCompiledNumericalValue(DEVELOPMENT_HASH) >= 1); // Development flag is needed in shaders for this to output correctly
                     ASSERT_ONCE(device_data.native_pixel_shaders[CompileTimeStringHash("Display Composition")]); // This shader is necessary to draw this debug stuff

                     if (!debug_draw_resource_enabled) // Enabled
                     {
                        // Reset all the settings we don't need anymore, as they were for a different debug draw mode
                        debug_draw_pipeline = 0;
                        debug_draw_shader_hash = 0;
                        debug_draw_shader_hash_string[0] = 0;
                        debug_draw_pipeline_target_instance = -1;
                        debug_draw_pipeline_target_thread = std::thread::id();
                        if (debug_draw_mode == DebugDrawMode::Depth)
                        {
                           debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                        }
                        debug_draw_mode = DebugDrawMode::Custom;
                        debug_draw_freeze_inputs = false;
                        debug_draw_view_index = 0;
                        debug_draw_mip = 0;

                        // TODO: fix. As of now this is needed or the texture would be cleared every frame and then set again. We'd need to add a separate (temporary) non clear texture mode for it
                        debug_draw_auto_clear_texture = false;

                        device_data.debug_draw_frozen_draw_state_stack.reset();
                        device_data.debug_draw_texture = selected_resource.get();
                        device_data.debug_draw_texture_format = selected_texture_format;
                        device_data.debug_draw_texture_size = selected_texture_size;
                     }
                     else // Disabled
                     {
                        device_data.debug_draw_texture = nullptr;
                        device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                        device_data.debug_draw_texture_size = {};
                     }
                  }
               }
            }
            ImGui::EndChild(); // TexturesDetails

            ImGui::EndTabItem(); // Upgraded Textures
         }
#endif // !GRAPHICS_ANALYZER
#endif // DEVELOPMENT

         if (ImGui::BeginTabItem("Settings", nullptr, open_settings_tab ? ImGuiTabItemFlags_SetSelected : 0))
         {
            const std::unique_lock lock_reshade(s_mutex_reshade); // Lock the entire scope for extra safety, though we are mainly only interested in keeping "cb_luma_global_settings" safe

#if ENABLE_SR
            const char* selected_sr_user_type = nullptr;
            switch (sr_user_type)
            {
            case SR::UserType::None:
               selected_sr_user_type = "None"; break;
            case SR::UserType::Auto:
               selected_sr_user_type = "Auto"; break;
            case SR::UserType::DLSS_CNN:
               selected_sr_user_type = "DLSS (CNN Model)"; break;
            case SR::UserType::DLSS_TRANSFORMER:
               selected_sr_user_type = "DLSS (Transformer Model)"; break;
            case SR::UserType::FSR_3:
               selected_sr_user_type = "FSR 3"; break;
            }

            SR::Type sr_type = device_data.sr_type;

            SR::Type sr_auto_type = SR::Type::None;
            // Pick the first supported one (they are in order of priority)
            for (const auto& sr_implementation_instance : device_data.sr_implementations_instances)
            {
               if (sr_implementation_instance.second && sr_implementation_instance.second->is_supported)
               {
                  sr_auto_type = sr_implementation_instance.first;
                  break;
               }
            }

            // Note: if we reached here, it's guaranteed that the selected SR type is supported
            if (ImGui::BeginCombo("Super Resolution", selected_sr_user_type))
            {
               auto AddComboItem = [&](const char* name, SR::UserType local_sr_user_type, SR::Type local_sr_type, bool enabled)
                  {
                     if (!enabled)
                     {
                        // Grey out text
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        ImGui::Selectable(name, false, ImGuiSelectableFlags_Disabled);
                        ImGui::PopStyleColor();
                     }
                     else
                     {
                        const bool selected = sr_user_type == local_sr_user_type;
                        if (ImGui::Selectable(name, selected))
                        {
                           sr_user_type = local_sr_user_type;
                           int sr_user_type_i = int(sr_user_type);
                           reshade::set_config_value(runtime, NAME, "SRUserType", sr_user_type_i);

                           sr_type = local_sr_type;
                        }
                        if (selected)
                           ImGui::SetItemDefaultFocus();
                     }
                  };

               AddComboItem("None", SR::UserType::None, SR::Type::None, true);
               constexpr bool always_show_auto_sr = true; // It's the default value, so just always show it
               if (always_show_auto_sr || sr_auto_type != SR::Type::None)
                  AddComboItem("Auto", SR::UserType::Auto, sr_auto_type, !device_data.sr_implementations_instances.empty());
               AddComboItem("DLSS (CNN Model)", SR::UserType::DLSS_CNN, SR::Type::DLSS, device_data.sr_implementations_instances.contains(SR::Type::DLSS));
               AddComboItem("DLSS (Transformer Model)", SR::UserType::DLSS_TRANSFORMER, SR::Type::DLSS, device_data.sr_implementations_instances.contains(SR::Type::DLSS));
               AddComboItem("FSR 3", SR::UserType::FSR_3, SR::Type::FSR, device_data.sr_implementations_instances.contains(SR::Type::FSR));

               ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("This replaces the game's native AA and dynamic resolution scaling implementations.\n%sA tick will appear here when it's engaged and a warning will appear if it failed.\n\nSome Super Resolution techniques might only be supported on specific hardware.", sr_game_tooltip);
            }

            ImGui::SameLine();
            if (sr_type == SR::Type::None)
            {
               ImGui::PushID("Super Resolution Enabled");
               if (ImGui::SmallButton(ICON_FK_UNDO))
               {
                  sr_user_type = SR::UserType::Auto;
						int sr_user_type_i = int(sr_user_type);
                  reshade::set_config_value(runtime, NAME, "SRUserType", sr_user_type_i);

                  sr_type = sr_auto_type;
               }
               ImGui::PopID();
            }
            else
            {
               // Show that SR is engaged. Ignored if the game scene isn't rendering.
               // If SR currently can't run due to the user settings/state, or failed, show a warning.
               if (device_data.has_drawn_main_post_processing && sr_type != SR::Type::None /*&& IsModActive(device_data)*/)
               {
                  ImGui::PushID("Super Resolution Active");
                  ImGui::BeginDisabled();
                  ImGui::SmallButton((device_data.taa_detected && device_data.has_drawn_sr_imgui && !device_data.sr_suppressed) ? ICON_FK_OK : ICON_FK_WARNING);
                  ImGui::EndDisabled();
                  ImGui::PopID();
               }
               else
               {
                  const auto& style = ImGui::GetStyle();
                  ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                  size.x += style.FramePadding.x;
                  size.y += style.FramePadding.y;
                  ImGui::InvisibleButton("", ImVec2(size.x, size.y));
               }
            }

            // Init the new SR if the selection changed.
            // We wouldn't really need to do anything other than clearing "sr_output_color",
            // but to avoid wasting memory allocated by SR texture and other resources, clear it up once disabled.
            // Note that we keep these textures in memory if the user temporarily changed away from an AA method that supports SR, or if users unloaded shaders (there's no reason to, and it'd cause stutters).
            if (device_data.sr_type != sr_type)
            {
               auto* sr_instance_data = device_data.GetSRInstanceData();
               if (sr_instance_data)
               {
#if 0 // This would actually unload the previously selected SR DLL and all, making the game hitch, so it's better to just keep it in memory, especially because otherwise we need to check for compatibily again and load them again etc
                  sr_implementations[device_data.sr_type]->Deinit(sr_instance_data);
                  sr_instance_data = nullptr;
#endif
               }

               device_data.sr_type = sr_type;
               device_data.sr_suppressed = false;
               sr_instance_data = device_data.GetSRInstanceData(); // Take new instance data

               if (device_data.sr_type != SR::Type::None)
               {
                  ASSERT_ONCE(sr_instance_data && sr_implementations[device_data.sr_type]->HasInit(sr_instance_data)); // Should never happen
               }
               else
               {
                  device_data.sr_output_color = nullptr;
                  device_data.sr_exposure = nullptr;
                  device_data.sr_render_resolution_scale = 1.f; // Reset this to 1 when SR is toggled, even if dynamic resolution scaling is active (e.g. in Prey), we'll set it back to a low value if DRS is used again.
                  device_data.sr_scene_exposure = 1.f;
                  device_data.sr_scene_pre_exposure = 1.f;
                  game->CleanExtraSRResources(device_data);
               }
            }
#endif // ENABLE_SR

            auto ChangeDisplayMode = [&](DisplayModeType display_mode, bool enable_hdr_on_display = true, IDXGISwapChain3* swapchain = nullptr)
               {
                  int display_mode_i = int(display_mode);
                  reshade::set_config_value(runtime, NAME, "DisplayMode", display_mode_i);
                  cb_luma_global_settings.DisplayMode = display_mode;
                  OnDisplayModeChanged();
                  if (display_mode >= DisplayModeType::HDR)
                  {
                     if (enable_hdr_on_display)
                     {
                        Display::SetHDREnabled(game_window);
                        bool dummy_bool;
                        Display::IsHDRSupportedAndEnabled(game_window, dummy_bool, hdr_enabled_display, swapchain); // This should always succeed, so we don't fallback to SDR in case it didn't
                     }
                     if (!reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite) || cb_luma_global_settings.ScenePeakWhite <= 0.f)
                     {
                        cb_luma_global_settings.ScenePeakWhite = device_data.default_user_peak_white;
                     }
                     if (!reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_global_settings.ScenePaperWhite))
                     {
                        cb_luma_global_settings.ScenePaperWhite = default_paper_white;
                     }
                     if (!reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_global_settings.UIPaperWhite))
                     {
                        cb_luma_global_settings.UIPaperWhite = default_paper_white;
                     }
                     // Align all the parameters for the SDR on HDR mode (the game paper white can still be changed)
                     if (display_mode >= DisplayModeType::SDRInHDR)
                     {
                        // For now we don't default to 203 nits game paper white when changing to this mode
                        cb_luma_global_settings.UIPaperWhite = cb_luma_global_settings.ScenePaperWhite;
                        cb_luma_global_settings.ScenePeakWhite = cb_luma_global_settings.ScenePaperWhite; // No, we don't want "default_peak_white" here
                     }
                  }
                  else
                  {
                     cb_luma_global_settings.ScenePeakWhite = display_mode == DisplayModeType::SDR ? srgb_white_level : (display_mode >= DisplayModeType::SDRInHDR ? default_paper_white : default_peak_white);
                     cb_luma_global_settings.ScenePaperWhite = display_mode == DisplayModeType::SDR ? srgb_white_level : default_paper_white;
                     cb_luma_global_settings.UIPaperWhite = display_mode == DisplayModeType::SDR ? srgb_white_level : default_paper_white;
                  }
               };

            auto DrawScenePaperWhite = [&](bool has_separate_ui_paper_white = true)
               {
                  static const char* scene_paper_white_name = "Scene Paper White";
                  static const char* paper_white_name = "Paper White";
                  if (ImGui::SliderFloat(has_separate_ui_paper_white ? scene_paper_white_name : paper_white_name, &cb_luma_global_settings.ScenePaperWhite, srgb_white_level, 500.f, "%.f"))
                  {
                     cb_luma_global_settings.ScenePaperWhite = max(cb_luma_global_settings.ScenePaperWhite, 0.0);
                     reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_global_settings.ScenePaperWhite);
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("The \"average\" brightness of the game scene.\nChange this to your liking, just don't get too close to the peak white.\nHigher does not mean better (especially if you struggle to read UI text), the brighter the image is, the lower the dynamic range (contrast) is.\nThe in game settings brightness is best left at default.");
                  }
                  // Warnings
                  if (cb_luma_global_settings.ScenePaperWhite > cb_luma_global_settings.ScenePeakWhite)
                  {
                     ImGui::SameLine();
                     if (ImGui::SmallButton(ICON_FK_WARNING))
                     {
                        cb_luma_global_settings.ScenePaperWhite = default_paper_white;
                     }
                     if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                     {
                        ImGui::SetTooltip("Your Paper White setting is greater than your Peak White setting, the image will either look bad or broken.");
                     }
                  }
                  // Reset button
                  ImGui::SameLine();
                  if (cb_luma_global_settings.ScenePaperWhite != default_paper_white)
                  {
                     ImGui::PushID(has_separate_ui_paper_white ? scene_paper_white_name : paper_white_name);
                     if (ImGui::SmallButton(ICON_FK_UNDO))
                     {
                        cb_luma_global_settings.ScenePaperWhite = default_paper_white;
                        reshade::set_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_global_settings.ScenePaperWhite);
                     }
                     ImGui::PopID();
                  }
                  else
                  {
                     const auto& style = ImGui::GetStyle();
                     ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                     size.x += style.FramePadding.x;
                     size.y += style.FramePadding.y;
                     ImGui::InvisibleButton("", ImVec2(size.x, size.y));
                  }
               };

            {
               // Note: this is fast enough that we can check it every frame.
               Display::IsHDRSupportedAndEnabled(game_window, hdr_supported_display, hdr_enabled_display, device_data.GetMainNativeSwapchain().get());
            }

            DisplayModeType display_mode = cb_luma_global_settings.DisplayMode;
            int display_mode_max = 1;
            if (hdr_supported_display)
            {
#if DEVELOPMENT || TEST
               display_mode_max++; // Add "SDR in HDR for HDR" mode
#endif
            }
#if !HIDE_DISPLAY_MODE
            ImGui::BeginDisabled(!hdr_supported_display);
            if (ImGui::SliderInt("Display Mode", reinterpret_cast<int*>(&display_mode), 0, display_mode_max, display_mode_preset_strings[(size_t)display_mode], ImGuiSliderFlags_NoInput))
            {
               ChangeDisplayMode(display_mode, true, device_data.GetMainNativeSwapchain().get());
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Display Mode. Greyed out if HDR is not supported.\nThe HDR display calibration (peak white brightness) is retrieved from the OS (Windows 11 HDR user calibration or display EDID),\nonly adjust it if necessary.\nIt's suggested to only play the game in SDR while the display is in SDR mode (with gamma 2.2, not sRGB) (avoid SDR mode in HDR).");
            }
            ImGui::SameLine();
            // Show a reset button to enable HDR in the game if we are playing SDR in HDR
            if ((display_mode == DisplayModeType::SDR && hdr_enabled_display) || (display_mode >= DisplayModeType::HDR && !hdr_enabled_display))
            {
               ImGui::PushID("Display Mode");
               if (ImGui::SmallButton(ICON_FK_UNDO))
               {
                  display_mode = hdr_enabled_display ? DisplayModeType::HDR : DisplayModeType::SDR;
                  ChangeDisplayMode(display_mode, false, device_data.GetMainNativeSwapchain().get());
               }
               ImGui::PopID();
            }
            else
            {
               const auto& style = ImGui::GetStyle();
               ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
               size.x += style.FramePadding.x;
               size.y += style.FramePadding.y;
               ImGui::InvisibleButton("", ImVec2(size.x, size.y));
            }
#endif // !HIDE_DISPLAY_MODE || DISABLE_DISPLAY_COMPOSITION
            const bool mod_active = IsModActive(device_data);
            const bool has_separate_ui_paper_white = GetShaderDefineCompiledNumericalValue(UI_DRAW_TYPE_HASH) >= 1;
#if !HIDE_BRIGHTNESS_SETTINGS
            if (display_mode == DisplayModeType::HDR)
            {
               ImGui::BeginDisabled(!mod_active);
               // We should this even if "IsModActive()" is false
               if (ImGui::SliderFloat("Scene Peak White", &cb_luma_global_settings.ScenePeakWhite, 400.0, 10000.f, "%.f"))
               {
                  if (cb_luma_global_settings.ScenePeakWhite == device_data.default_user_peak_white)
                  {
                     reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f); // Store it as 0 to highlight that it's default (whatever the current or next display peak white is)
                  }
                  else
                  {
                     reshade::set_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite);
                  }
               }
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Set this to the brightest nits value your display (TV/Monitor) can emit.\nDirectly calibrating in Windows is suggested.");
               }
               ImGui::SameLine();
               if (cb_luma_global_settings.ScenePeakWhite != device_data.default_user_peak_white)
               {
                  ImGui::PushID("Scene Peak White");
                  if (ImGui::SmallButton(ICON_FK_UNDO))
                  {
                     cb_luma_global_settings.ScenePeakWhite = device_data.default_user_peak_white;
                     reshade::set_config_value(runtime, NAME, "ScenePeakWhite", 0.f);
                  }
                  ImGui::PopID();
               }
               else
               {
                  const auto& style = ImGui::GetStyle();
                  ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                  size.x += style.FramePadding.x;
                  size.y += style.FramePadding.y;
                  ImGui::InvisibleButton("", ImVec2(size.x, size.y));
               }
               ImGui::EndDisabled();
               DrawScenePaperWhite(has_separate_ui_paper_white && mod_active);
               constexpr bool supports_custom_ui_paper_white_scaling = true; // Currently all "post_process_space_define_index" modes support it (modify the tooltip otherwise)
               if (has_separate_ui_paper_white)
               {
                  ImGui::BeginDisabled(!supports_custom_ui_paper_white_scaling || !mod_active);
                  if (ImGui::SliderFloat("UI Paper White", supports_custom_ui_paper_white_scaling ? &cb_luma_global_settings.UIPaperWhite : &cb_luma_global_settings.ScenePaperWhite, srgb_white_level, 500.f, "%.f"))
                  {
                     cb_luma_global_settings.UIPaperWhite = max(cb_luma_global_settings.UIPaperWhite, 0.0);
                     reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_global_settings.UIPaperWhite);

                     // This is not safe to do, so let's rely on users manually setting this instead.
                     // Also note that this is a test implementation, it doesn't react to all places that change "cb_luma_global_settings.UIPaperWhite", and does not restore the user original value on exit.
#if 0
                     // This makes the game cursor have the same brightness as the game's UI
                     SetSDRWhiteLevel(game_window, std::clamp(cb_luma_global_settings.UIPaperWhite, 80.f, 480.f));
#endif
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("The peak brightness of the User Interface (with the exception of the 2D cursor, which is driven by the Windows SDR White Level).\nHigher does not mean better, change this to your liking.");
                  }
                  ImGui::SameLine();
                  if (cb_luma_global_settings.UIPaperWhite != default_paper_white)
                  {
                     ImGui::PushID("UI Paper White");
                     if (ImGui::SmallButton(ICON_FK_UNDO))
                     {
                        cb_luma_global_settings.UIPaperWhite = default_paper_white;
                        reshade::set_config_value(runtime, NAME, "UIPaperWhite", cb_luma_global_settings.UIPaperWhite);
                     }
                     ImGui::PopID();
                  }
                  else
                  {
                     const auto& style = ImGui::GetStyle();
                     ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                     size.x += style.FramePadding.x;
                     size.y += style.FramePadding.y;
                     ImGui::InvisibleButton("", ImVec2(size.x, size.y));
                  }
                  ImGui::EndDisabled();
               }
            }
            else if (display_mode >= DisplayModeType::SDRInHDR)
            {
               DrawScenePaperWhite(has_separate_ui_paper_white);
               cb_luma_global_settings.UIPaperWhite = cb_luma_global_settings.ScenePaperWhite;
               cb_luma_global_settings.ScenePeakWhite = cb_luma_global_settings.ScenePaperWhite;
            }
#endif // !DISABLE_DISPLAY_COMPOSITION
#if DEVELOPMENT
            // Print warnings if the OS gamma wasn't neutral (we don't want that in HDR!). This is extremely slow in some games (e.g. Lego City Undercover) (probably because they set a value, even if neutral) so it's behind a toggle.
            static bool check_gamma_ramp = false;
            ImGui::Checkbox("Check Gamma Ramp", &check_gamma_ramp);
            if (check_gamma_ramp)
            {
               {
                  bool neutral_gamma = true;

                  HDC hDC = GetDC(game_window); // Pass NULL to get the DC for the entire screen (NULL = desktop, primary display, the gamma ramp only ever applies to that apparently)
                  WORD gamma_ramp[3][256];
                  if (GetDeviceGammaRamp(hDC, gamma_ramp) == TRUE)
                  {
                     for (int i = 1; i < 255; i++)
                     {
                        neutral_gamma &= (gamma_ramp[0][i] == i * 257) && (gamma_ramp[1][i] == i * 257) && (gamma_ramp[2][i] == i * 257);
                     }
                  }
                  ReleaseDC(game_window, hDC);

                  if (!neutral_gamma)
                  {
                     ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Warning: Non Neutral Device Gamma Ramp");
                  }
               }

               if (auto native_swapchain = device_data.GetMainNativeSwapchain())
               {
                  bool neutral_gamma = true;

                  com_ptr<IDXGIOutput> output;
                  native_swapchain->GetContainingOutput(&output);
                  if (output)
                  {
                     DXGI_GAMMA_CONTROL gamma_control;
                     if (SUCCEEDED(output->GetGammaControl(&gamma_control)))
                     {
                        neutral_gamma &= gamma_control.Scale.Red == 1.0f;
                        neutral_gamma &= gamma_control.Scale.Green == 1.0f;
                        neutral_gamma &= gamma_control.Scale.Blue == 1.0f;
                        neutral_gamma &= gamma_control.Offset.Red == 0.0f;
                        neutral_gamma &= gamma_control.Offset.Green == 0.0f;
                        neutral_gamma &= gamma_control.Offset.Blue == 0.0f;
                        for (int i = 0; i < std::size(gamma_control.GammaCurve); ++i)
                        {
                           float value = i / ((float)std::size(gamma_control.GammaCurve) - 1.f);
                           neutral_gamma &= gamma_control.GammaCurve[i].Red == value;
                           neutral_gamma &= gamma_control.GammaCurve[i].Green == value;
                           neutral_gamma &= gamma_control.GammaCurve[i].Blue == value;
                        }
                     }

                     DXGI_OUTPUT_DESC output_desc = {};
                     output->GetDesc(&output_desc);

                     HMONITOR hMonitor = output_desc.Monitor;
                  }

                  if (!neutral_gamma)
                  {
                     ImGui::TextColored(ImVec4(1.0, 0.0, 0.0, 1.0), "Warning: Non Neutral Output Gamma Control");
                  }
               }
            }
#endif
            if (allow_disabling_gamma_ramp && ImGui::Button("Reset Gamma Ramp"))
            {
               // First do it the old way, given that games like Bioshock 2 do it. This seemengly works in windowed mode too, and applies to HDR.
               {
                  HDC hDC = GetDC(game_window); // Pass NULL to get the DC for the entire screen (NULL = desktop, primary display, the gamma ramp only ever applies to that apparently)
                  WORD gamma_ramp[3][256];

#if 0 // Analyze the gamma ramp to see if it's not neutral (now we do it above)
                  if (GetDeviceGammaRamp(hDC, gamma_ramp) == TRUE)
                  {
#if 1 // Make sure the gamma was absolutely neutral
                     for (int i = 1; i < 255; i++)
                        ASSERT_ONCE_MSG((gamma_ramp[0][i] == i * 257) && (gamma_ramp[1][i] == i * 257) && (gamma_ramp[2][i] == i * 257), "Gamma ramp was not neutral");
#elif 1
                     double gamma_ramp_averages[3] = {};
                     for (int i = 1; i < 255; i++) // Ignore the values at the edges, they should ideally map to min and max and they'd cause nan with our log formula (we could write an alternative formula in case)
                     {
                        float i_double = i / 255.0;

                        double ramp_temp_value_r = gamma_ramp[0][i] / 65535.0;
                        double ramp_temp_value_g = gamma_ramp[1][i] / 65535.0;
                        double ramp_temp_value_b = gamma_ramp[2][i] / 65535.0;

                        // Note that these will break (go to inf) if the second value is either 0 or 1
                        gamma_ramp_averages[0] += std::log(i_double) / std::log(ramp_temp_value_r);
                        gamma_ramp_averages[1] += std::log(i_double) / std::log(ramp_temp_value_g);
                        gamma_ramp_averages[2] += std::log(i_double) / std::log(ramp_temp_value_b);
                     }
                     double gamma_ramp_average = (gamma_ramp_averages[0] + gamma_ramp_averages[1] + gamma_ramp_averages[2]) / 3.0 / 254.0; // Ignore the values at the edges in the normalization too
                     ASSERT_ONCE_MSG(Math::AlmostEqual(gamma_ramp_average, 1.0, 0.001), "Gamma ramp was not neutral");
#else
                     // Split into 3 sections to know shadow, midtones and highlights gamma
                     double gamma_ramp_averages1[3] = {};
                     double gamma_ramp_averages2[3] = {};
                     double gamma_ramp_averages3[3] = {};
                     for (int i = 1; i <= 84; i++)
                     {
                        float i_double = i / 255.0;
                        double ramp_temp_value_r = gamma_ramp[0][i] / 65535.0;
                        double ramp_temp_value_g = gamma_ramp[1][i] / 65535.0;
                        double ramp_temp_value_b = gamma_ramp[2][i] / 65535.0;
                        gamma_ramp_averages1[0] += std::log(i_double) / std::log(ramp_temp_value_r);
                        gamma_ramp_averages1[1] += std::log(i_double) / std::log(ramp_temp_value_g);
                        gamma_ramp_averages1[2] += std::log(i_double) / std::log(ramp_temp_value_b);
                     }
                     for (int i = 85; i <= 169; i++)
                     {
                        float i_double = i / 255.0;
                        double ramp_temp_value_r = gamma_ramp[0][i] / 65535.0;
                        double ramp_temp_value_g = gamma_ramp[1][i] / 65535.0;
                        double ramp_temp_value_b = gamma_ramp[2][i] / 65535.0;
                        gamma_ramp_averages2[0] += std::log(i_double) / std::log(ramp_temp_value_r);
                        gamma_ramp_averages2[1] += std::log(i_double) / std::log(ramp_temp_value_g);
                        gamma_ramp_averages2[2] += std::log(i_double) / std::log(ramp_temp_value_b);
                     }
                     for (int i = 170; i <= 254; i++)
                     {
                        float i_double = i / 255.0;
                        double ramp_temp_value_r = gamma_ramp[0][i] / 65535.0;
                        double ramp_temp_value_g = gamma_ramp[1][i] / 65535.0;
                        double ramp_temp_value_b = gamma_ramp[2][i] / 65535.0;
                        gamma_ramp_averages3[0] += std::log(i_double) / std::log(ramp_temp_value_r);
                        gamma_ramp_averages3[1] += std::log(i_double) / std::log(ramp_temp_value_g);
                        gamma_ramp_averages3[2] += std::log(i_double) / std::log(ramp_temp_value_b);
                     }
                     double gamma_ramp_average1 = (gamma_ramp_averages1[0] + gamma_ramp_averages1[1] + gamma_ramp_averages1[2]) / 254.0;
                     double gamma_ramp_average2 = (gamma_ramp_averages2[0] + gamma_ramp_averages2[1] + gamma_ramp_averages2[2]) / 254.0;
                     double gamma_ramp_average3 = (gamma_ramp_averages3[0] + gamma_ramp_averages3[1] + gamma_ramp_averages3[2]) / 254.0;
                     double gamma_ramp_average = (gamma_ramp_average1 + gamma_ramp_average2 + gamma_ramp_average3) / 3.0;
                     ASSERT_ONCE_MSG(Math::AlmostEqual(gamma_ramp_average, 1.0, 0.001), "Gamma ramp was not neutral");
#endif
                  }
#endif

                  for (int i = 0; i < 256; i++)
                  {
#if 0
                     constexpr double neutral_gamma = 1.0;
                     double ramp_temp_value = pow(i / 255.0, 1.0 / neutral_gamma);
                     WORD ramp_value = (WORD)(ramp_temp_value * 65535.0 + 0.5);
#else // Pure neutral value
                     WORD ramp_value = i * 257; // 0..255 -> 0..65535 (because 65535/255==257)
#endif

                     gamma_ramp[0][i] = ramp_value; // Red
                     gamma_ramp[1][i] = ramp_value; // Green
                     gamma_ramp[2][i] = ramp_value; // Blue
                  }
                  ASSERT_ONCE(SetDeviceGammaRamp(hDC, gamma_ramp) == TRUE); // Passing in nullpt won't work...

                  ReleaseDC(game_window, hDC);
               }

               // Second, do it the more modern way (this way seems to only work in SDR or any FSE mode)
               // Third, do it in another weird way (that was probably never used by games)
               if (auto native_swapchain = device_data.GetMainNativeSwapchain())
               {
                  com_ptr<IDXGIOutput> output;
                  native_swapchain->GetContainingOutput(&output);
                  if (output)
                  {
#if 1 // Disable it directly
                     ASSERT_ONCE(output->SetGammaControl(nullptr));
#else // Set it to neutral (not really needed, we have no idea how this works in HDR)
                     DXGI_GAMMA_CONTROL gamma_control;
                     ASSERT_ONCE(output->GetGammaControl(&gamma_control));

                     // Set scale to default (1, 1, 1)
                     gamma_control.Scale.Red = 1.0f;
                     gamma_control.Scale.Green = 1.0f;
                     gamma_control.Scale.Blue = 1.0f;
                     // Set offset to default (0, 0, 0)
                     gamma_control.Offset.Red = 0.0f;
                     gamma_control.Offset.Green = 0.0f;
                     gamma_control.Offset.Blue = 0.0f;

                     // Create a simple gamma curve (linear in this example)
                     for (int i = 0; i < std::size(gamma_control.GammaCurve); ++i)
                     {
                        float value = i / ((float)std::size(gamma_control.GammaCurve) - 1.f);
                        gamma_control.GammaCurve[i].Red = value;
                        gamma_control.GammaCurve[i].Green = value;
                        gamma_control.GammaCurve[i].Blue = value;
                     }

                     ASSERT_ONCE(output->SetGammaControl(&gamma_control));
#endif

                     DXGI_OUTPUT_DESC output_desc = {};
                     if (SUCCEEDED(output->GetDesc(&output_desc)))
                     {
                        HMONITOR hMonitor = output_desc.Monitor;
                        if (hMonitor)
                        {
                           // Step 3: Enumerate physical monitors for this HMONITOR
                           DWORD num_monitors = 0;
                           if (GetNumberOfPhysicalMonitorsFromHMONITOR(hMonitor, &num_monitors) || num_monitors == 0)
                           {
                              std::vector<PHYSICAL_MONITOR> physical_monitors(num_monitors);
                              if (GetPhysicalMonitorsFromHMONITOR(hMonitor, num_monitors, physical_monitors.data()))
                              {
                                 for (DWORD i = 0; i < num_monitors; ++i)
                                 {
                                    SetMonitorBrightness(physical_monitors[i].hPhysicalMonitor, 50);
                                 }

                                 DestroyPhysicalMonitors(num_monitors, physical_monitors.data());
                              }
                           }
                        }

                     }
                  }
               }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("Some games changed the Windows gamma ramp/control, which has undefined behaviour in HDR and either way is not wanted, because it changes the appearance of the game, and because it prevents us from properly display mapping to the display peak.");
            }

            game->DrawImGuiSettings(device_data);

#if DEVELOPMENT || TEST
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Test Settings"))
            {
               ImGui::Checkbox("Redirect ReShade Effects toggle to Display Mode toggle (HDR<->SDR in HDR)", &reshade_effects_toggle_to_display_mode_toggle);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("When using the ReShade Effects toggle keyboard shortcut, toggle the Display Mode instead of of enabling/disabling our custom shaders.");
               }

               ImGui::SliderInt("Test Index", &test_index, 0, 25);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Slider to quickly disable or change certain features of the mod for testing purposes.");
               }

               ImGui::TreePop();
            }
#endif // DEVELOPMENT || TEST

#if DEVELOPMENT
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Developer Settings"))
            {
               static std::string DevSettingsNames[CB::LumaDevSettings::SettingsNum];
               bool any_dev_setting_non_default = false;
               for (size_t i = 0; i < CB::LumaDevSettings::SettingsNum; i++)
               {
                  // These strings need to persist
                  if (DevSettingsNames[i].empty())
                  {
                     DevSettingsNames[i] = "Developer Setting " + std::to_string(i + 1);
                  }
                  ImGui::PushID(DevSettingsNames[i].c_str());
                  float& value = cb_luma_global_settings.DevSettings[i];
                  float& min_value = cb_luma_dev_settings_min_value[i];
                  float& max_value = cb_luma_dev_settings_max_value[i];
                  float& default_value = cb_luma_dev_settings_default_value[i];
                  if (cb_luma_dev_settings_edit_mode) // Reduce it to make space for others
                  {
                     ImGui::SetNextItemWidth(ImGui::CalcTextSize("1.1111111111").x);
                  }
                  ImGui::SliderFloat((cb_luma_dev_settings_edit_mode || cb_luma_dev_settings_names[i].empty()) ? DevSettingsNames[i].c_str() : cb_luma_dev_settings_names[i].c_str(), &value, min_value, max_value);
                  ImGui::SameLine();
                  if (value != default_value)
                  {
                     any_dev_setting_non_default = true;
                     if (ImGui::SmallButton(ICON_FK_UNDO))
                     {
                        value = default_value;
                     }
                  }
                  else
                  {
                     const auto& style = ImGui::GetStyle();
                     ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                     size.x += style.FramePadding.x;
                     size.y += style.FramePadding.y;
                     ImGui::InvisibleButton("", ImVec2(size.x, size.y));
                  }
                  // TODO: serialize these on ReShade so we can get rid of the code to reflect these from shaders?
                  if (cb_luma_dev_settings_edit_mode)
                  {
                     ImGui::SameLine();
                     ImGui::SetNextItemWidth(ImGui::CalcTextSize("1.11111").x);
                     if (ImGui::InputFloat("Default", &default_value, 0.f, 0.f, "%.4f", ImGuiInputTextFlags_CharsScientific))
                        cb_luma_dev_settings_set_from_code = true;
                     ImGui::SameLine();
                     ImGui::SetNextItemWidth(ImGui::CalcTextSize("1.11111").x);
                     if (ImGui::InputFloat("Min", &min_value, 0.f, 0.f, "%.4f", ImGuiInputTextFlags_CharsScientific))
                        cb_luma_dev_settings_set_from_code = true;
                     ImGui::SameLine();
                     ImGui::SetNextItemWidth(ImGui::CalcTextSize("1.11111").x);
                     if (ImGui::InputFloat("Max", &max_value, 0.f, 0.f, "%.4f", ImGuiInputTextFlags_CharsScientific))
                        cb_luma_dev_settings_set_from_code = true;
                     {
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(ImGui::CalcTextSize("     CUSTOM NAME TEST     ").x);

                        // Ensure the string has enough capacity for editing
                        if (cb_luma_dev_settings_names[i].capacity() < 64)
                           cb_luma_dev_settings_names[i].resize(64);

                        // ImGui::InputText modifies the buffer in-place
                        if (ImGui::InputText("Name", cb_luma_dev_settings_names[i].data(), cb_luma_dev_settings_names[i].capacity()))
                        {
                           cb_luma_dev_settings_names[i].resize(strlen(cb_luma_dev_settings_names[i].c_str())); // Optional
                           cb_luma_dev_settings_set_from_code = true;
                        }
                     }
                  }
                  ImGui::PopID();
               }
               if ((any_dev_setting_non_default || cb_luma_dev_settings_edit_mode) && ImGui::Button("Reset All Developer Settings"))
               {
                  // Reset all settings if we are in edit mode!
                  if (cb_luma_dev_settings_edit_mode)
                  {
                     cb_luma_dev_settings_default_value = CB::LumaDevSettings(0.f);
                     cb_luma_dev_settings_min_value = CB::LumaDevSettings(0.f);
                     cb_luma_dev_settings_max_value = CB::LumaDevSettings(1.f);
                     cb_luma_dev_settings_names = {};
                  }

                  for (size_t i = 0; i < CB::LumaDevSettings::SettingsNum; i++)
                  {
                     cb_luma_global_settings.DevSettings[i] = cb_luma_dev_settings_default_value[i];
                  }

                  // cb_luma_dev_settings_set_from_code = false; // We could do this too but it's optional
               }
               if (ImGui::Checkbox("Developer Settings: Edit Mode", &cb_luma_dev_settings_edit_mode))
               {
                  // Clear strings that had empty reserved characters for ImGUI editing
                  if (!cb_luma_dev_settings_edit_mode)
                  {
                     for (size_t i = 0; i < CB::LumaDevSettings::SettingsNum; i++)
                     {
                        bool is_effectively_empty = cb_luma_dev_settings_names[i].find_first_not_of(" \t\n\r") == std::string::npos;
                        if (is_effectively_empty)
                           cb_luma_dev_settings_names[i].clear(); // TODO: this doesn't work!!!
                     }
                  }
               }

               ImGui::NewLine();
               ImGui::SliderInt("Tank Performance (Frame Sleep MS)", &frame_sleep_ms, 0, 100);
               ImGui::SliderInt("Tank Performance (Frame Sleep Interval)", &frame_sleep_interval, 1, 30);

               ImGui::NewLine();
               bool changed_gamma = ImGui::SliderFloat("Custom SDR Gamma", &custom_sdr_gamma, 1.f, 3.f);
               changed_gamma |= DrawResetButton<float, false>(custom_sdr_gamma, 0.f, nullptr, runtime);
               if (changed_gamma)
               {
                  defines_need_recompilation = true;
               }

               ImGui::NewLine();
               ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_NoUndoRedo;
               if (ImGui::InputTextWithHint("Debug Draw Shader Hash", "12345678", debug_draw_shader_hash_string, HASH_CHARACTERS_LENGTH + 1, text_flags))
               {
                  try
                  {
                     if (strlen(debug_draw_shader_hash_string) != HASH_CHARACTERS_LENGTH)
                     {
                        throw std::invalid_argument("Shader Hash has invalid length");
                     }
                     debug_draw_shader_hash = Shader::Hash_StrToNum(&debug_draw_shader_hash_string[0]);
                  }
                  catch (const std::exception& e)
                  {
                     debug_draw_shader_hash = 0;
                  }
                  // Keep the pipeline ptr if we are simply clearing the hash // TODO: why???
                  if (debug_draw_shader_hash != 0)
                  {
                     debug_draw_pipeline = 0;
                  }

                  device_data.debug_draw_frozen_draw_state_stack.reset();
                  device_data.debug_draw_texture = nullptr;
                  device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                  device_data.debug_draw_texture_size = {};
               }
               ImGui::SameLine();
               // Allow it to go through if we have a custom debug draw texture set
               bool debug_draw_enabled = debug_draw_shader_hash != 0 || debug_draw_pipeline != 0 || (debug_draw_mode == DebugDrawMode::Custom && device_data.debug_draw_texture.get()); // It wants to be shown (if it manages!)
               // Show the reset button for both conditions or they could get stuck
               if (debug_draw_enabled)
               {
                  ImGui::PushID("Debug Draw");
                  if (ImGui::SmallButton(ICON_FK_UNDO))
                  {
                     debug_draw_shader_hash_string[0] = 0;
                     debug_draw_shader_hash = 0;
                     debug_draw_pipeline = 0;

                     device_data.debug_draw_frozen_draw_state_stack.reset();
                     device_data.debug_draw_texture = nullptr;
                     device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                     device_data.debug_draw_texture_size = {};
                  }
                  ImGui::PopID();
               }
               else
               {
                  const auto& style = ImGui::GetStyle();
                  ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
                  size.x += style.FramePadding.x;
                  size.y += style.FramePadding.y;
                  ImGui::InvisibleButton("", ImVec2(size.x, size.y));
               }
               if (debug_draw_enabled)
               {
                  if (debug_draw_mode != DebugDrawMode::Custom)
                  {
                     auto prev_debug_draw_mode = debug_draw_mode;
                     if (ImGui::SliderInt("Debug Draw Mode", &(int&)debug_draw_mode, int(DebugDrawMode::Custom) + 1, IM_ARRAYSIZE(debug_draw_mode_strings) - 1, debug_draw_mode_strings[(size_t)debug_draw_mode], ImGuiSliderFlags_NoInput))
                     {
                        // Make sure to reset it to 0 when we change mode, depth only supports 1 texture etc
                        debug_draw_view_index = 0;
                        // Automatically toggle some settings
                        if (debug_draw_mode == DebugDrawMode::Depth)
                        {
                           debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                           debug_draw_mip = 0;
                        }
                        else if (prev_debug_draw_mode == DebugDrawMode::Depth)
                        {
                           debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                        }
                     }
                     if (ImGui::Checkbox("Debug Draw: Freeze Inputs", &debug_draw_freeze_inputs))
                     {
                        device_data.debug_draw_frozen_draw_state_stack.reset();
                     }
                  }
                  if (debug_draw_mode == DebugDrawMode::RenderTarget)
                  {
                     ImGui::SliderInt("Debug Draw: Render Target Index", &debug_draw_view_index, 0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT - 1);
                  }
                  else if (debug_draw_mode == DebugDrawMode::UnorderedAccessView)
                  {
                     ImGui::SliderInt("Debug Draw: Unordered Access View", &debug_draw_view_index, 0, device_data.uav_max_count - 1);
                  }
                  else if (debug_draw_mode == DebugDrawMode::ShaderResource)
                  {
                     ImGui::SliderInt("Debug Draw: Pixel Shader Resource Index", &debug_draw_view_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1);
                  }
                  const int max_mip = GetTextureMaxMipLevels(D3D11_REQ_TEXTURE1D_U_DIMENSION) - 1;
                  ImGui::SliderInt("Debug Draw: Texture Mip", &debug_draw_mip, 0, max_mip);
                  ImGui::Checkbox("Debug Draw: Allow Drawing Replaced Pass", &debug_draw_replaced_pass);
                  ImGui::SliderInt("Debug Draw: Target Pipeline Instance", &debug_draw_pipeline_target_instance, -1, 100); // In case the same pipeline was run more than once by the game, we can pick one to print
                  if (ImGui::Checkbox("Debug Draw: Filter Target Pipeline Instance By Thread", &debug_draw_pipeline_instance_filter_by_thread) && !debug_draw_pipeline_instance_filter_by_thread)
                  {
                     debug_draw_pipeline_target_thread = std::thread::id();
                  }
                  bool debug_draw_fullscreen = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Fullscreen) != 0;
                  bool debug_draw_rend_res_scale = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale) != 0;
                  bool debug_draw_red_only = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::RedOnly) != 0;
                  bool debug_draw_show_alpha = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::ShowAlpha) != 0;
                  bool debug_draw_premultiply_alpha = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha) != 0;
                  bool debug_draw_invert_colors = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::InvertColors) != 0;
                  bool debug_draw_linear_to_gamma = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::LinearToGamma) != 0;
                  bool debug_draw_gamma_to_linear = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear) != 0;
                  bool debug_draw_flip_y = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::FlipY) != 0;
                  bool debug_draw_abs = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Abs) != 0;
                  bool debug_draw_saturate = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Saturate) != 0;
                  bool debug_draw_background_passthrough = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::BackgroundPassthrough) != 0;
                  bool debug_draw_zoom_4x = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Zoom4x) != 0;
                  bool debug_draw_bilinear = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Bilinear) != 0;
                  bool debug_draw_srgb = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::SRGB) != 0;
                  bool debug_draw_tonemap = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Tonemap) != 0;
                  bool debug_draw_uv_to_pixel_space = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::UVToPixelSpace) != 0;
                  bool debug_draw_denormalize = (debug_draw_options & (uint32_t)DebugDrawTextureOptionsMask::Denormalize) != 0;

                  // TODO: do these in a template function to shorten the code
                  if (ImGui::Checkbox("Debug Draw Options: Fullscreen", &debug_draw_fullscreen))
                  {
                     if (debug_draw_fullscreen)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Fullscreen;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Fullscreen;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Render Resolution Scale", &debug_draw_rend_res_scale))
                  {
                     if (debug_draw_rend_res_scale)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RenderResolutionScale;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Background Passthrough", &debug_draw_background_passthrough))
                  {
                     if (debug_draw_background_passthrough)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::BackgroundPassthrough;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::BackgroundPassthrough;
                     }
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("True: Background passes through the edges\nFalse: forces Black outside of the debugged Texture range");
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Show Alpha", &debug_draw_show_alpha))
                  {
                     if (debug_draw_show_alpha)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::ShowAlpha;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::ShowAlpha;
                     }
                  }
                  ImGui::BeginDisabled(debug_draw_show_alpha); // Alpha takes over red in shaders, so disable red if alpha is on
                  if (ImGui::Checkbox("Debug Draw Options: Red Only", &debug_draw_red_only))
                  {
                     if (debug_draw_red_only)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
                     }
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Shows textures with only one channel (e.g. red, alpha, depth) as grey-scale instead of in red.");
                  }
                  ImGui::EndDisabled();
                  if (ImGui::Checkbox("Debug Draw Options: Premultiply Alpha", &debug_draw_premultiply_alpha))
                  {
                     if (debug_draw_premultiply_alpha)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::PreMultiplyAlpha;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Invert Colors", &debug_draw_invert_colors))
                  {
                     if (debug_draw_invert_colors)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::InvertColors;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::InvertColors;
                     }
                  }
                  ImGui::Checkbox("Debug Draw Options: Auto Gamma", &debug_draw_auto_gamma);
                  if (!debug_draw_auto_gamma)
                  {
                     // Draw this first as it's much more likely to be needed
                     if (ImGui::Checkbox("Debug Draw Options: Gamma to Linear", &debug_draw_gamma_to_linear))
                     {
                        if (debug_draw_gamma_to_linear)
                        {
                           debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                        }
                        else
                        {
                           debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::GammaToLinear;
                        }
                     }
                     if (ImGui::Checkbox("Debug Draw Options: Linear to Gamma", &debug_draw_linear_to_gamma))
                     {
                        if (debug_draw_linear_to_gamma)
                        {
                           debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                        }
                        else
                        {
                           debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::LinearToGamma;
                        }
                     }
                  }
                  ImGui::BeginDisabled((debug_draw_options & ((uint32_t)DebugDrawTextureOptionsMask::LinearToGamma | (uint32_t)DebugDrawTextureOptionsMask::GammaToLinear)) == 0);
                  if (ImGui::Checkbox("Debug Draw Options: sRGB Encode/Decode", &debug_draw_srgb))
                  {
                     if (debug_draw_srgb)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::SRGB;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::SRGB;
                     }
                  }
                  ImGui::EndDisabled();
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Force sRGB \"Gamma\" conversions (as opposed to a generic 2.2 power gamma)");
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Flip Y", &debug_draw_flip_y))
                  {
                     if (debug_draw_flip_y)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::FlipY;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::FlipY;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Abs", &debug_draw_abs))
                  {
                     if (debug_draw_abs)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Abs;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Abs;
                     }
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Useful for Motion Vectors or debugging film grain etc");
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Saturate", &debug_draw_saturate))
                  {
                     if (debug_draw_saturate)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Saturate;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Saturate;
                     }
                  }
                  ImGui::BeginDisabled(debug_draw_saturate || !IsFloatFormat(device_data.debug_draw_texture_format));
                  if (debug_draw_saturate || !IsFloatFormat(device_data.debug_draw_texture_format))
                  {
                     debug_draw_tonemap = false; // Make sure to show it as false if it's disabled (though this might be confusing too, as the setting in the restored later)
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Tonemap", &debug_draw_tonemap))
                  {
                     if (debug_draw_tonemap)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Tonemap;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Tonemap;
                     }
                  }
                  ImGui::EndDisabled();
                  if (ImGui::Checkbox("Debug Draw Options: UV to Pixel Space", &debug_draw_uv_to_pixel_space))
                  {
                     if (debug_draw_uv_to_pixel_space)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::UVToPixelSpace;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::UVToPixelSpace;
                     }
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Useful for Motion Vectors");
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Denormalize", &debug_draw_denormalize))
                  {
                     if (debug_draw_denormalize)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Denormalize;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Denormalize;
                     }
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Converts from 0 to +1 back to -1 to +1, useful for Motion Vectors or other encoded textures");
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Zoom 4x", &debug_draw_zoom_4x))
                  {
                     if (debug_draw_zoom_4x)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Zoom4x;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Zoom4x;
                     }
                  }
                  if (ImGui::Checkbox("Debug Draw Options: Bilinear Sampling", &debug_draw_bilinear))
                  {
                     if (debug_draw_bilinear)
                     {
                        debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::Bilinear;
                     }
                     else
                     {
                        debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Bilinear;
                     }
                  }
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Bilinear instead of Nearest Neighbor");
                  }
                  if (device_data.debug_draw_texture || debug_draw_auto_clear_texture)
                  {
                     if (GetFormatName(device_data.debug_draw_texture_format) != nullptr)
                     {
                        ImGui::Text("Debug Draw Info: Texture (View) Format: %s", GetFormatName(device_data.debug_draw_texture_format));
                     }
                     else
                     {
                        ImGui::Text("Debug Draw Info: Texture (View) Format: %u", device_data.debug_draw_texture_format);
                     }
                     ImGui::Text("Debug Draw Info: Texture Size: %ux%ux%ux%u", device_data.debug_draw_texture_size.x, device_data.debug_draw_texture_size.y, device_data.debug_draw_texture_size.z, device_data.debug_draw_texture_size.w);
                  }
                  if (ImGui::Checkbox("Debug Draw: Auto Clear Texture", &debug_draw_auto_clear_texture)) // Is it persistent or not (in case the target texture stopped being found on newer frames). We could also "freeze" it and stop updating it, but we don't need that for now.
                  {
                     device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
                     device_data.debug_draw_texture_size = {};
                  }
               }

#if !GRAPHICS_ANALYZER // Graphics Analyzer doesn't want upgrades
               ImGui::NewLine();
               // Requires a change in resolution to (~fully) apply (no texture cloning yet)
               if (swapchain_format_upgrade_type > TextureFormatUpgradesType::None)
               {
                  if (swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled ? ImGui::Button("Disable Swapchain Upgrade") : ImGui::Button("Enable Swapchain Upgrade"))
                  {
                     swapchain_format_upgrade_type = swapchain_format_upgrade_type == TextureFormatUpgradesType::AllowedEnabled ? TextureFormatUpgradesType::AllowedDisabled : TextureFormatUpgradesType::AllowedEnabled;
                  }
               }
               if (texture_format_upgrades_type > TextureFormatUpgradesType::None)
               {
                  if (texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled ? ImGui::Button("Disable Texture Format Upgrades") : ImGui::Button("Enable Texture Format Upgrades"))
                  {
                     texture_format_upgrades_type = texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled ? TextureFormatUpgradesType::AllowedDisabled : TextureFormatUpgradesType::AllowedEnabled;
                  }
                  //if (texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled) // TODO: hide only the ones that would be restricted by this being disabled, I think most would at least continue to work (at least without clearing what they previously upgraded)
                  {
                     ImGui::Checkbox("Enable Indirect Texture Format Upgrades", &enable_indirect_texture_format_upgrades);
                     ImGui::Checkbox("Enable Automatic Indirect Texture Format Upgrades", &enable_automatic_indirect_texture_format_upgrades);
                     ImGui::Checkbox("Ignore Indirect Upgraded Textures", &ignore_indirect_upgraded_textures);
                     if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                     {
                        ImGui::SetTooltip("Indirect Texture Upgrades will possibly still happen, but they will not be bound, allowing you to quickly toggle between the game textures and the upgraded ones");
                     }
                     
                     // TODO: add a button to also re-create all of them live (we'd need some sort of tracking for that, or to temporarily white list all PS/CS shaders to automatically upgrade textures, or actually, we can just read the source texture and re-upgrade it if we keep them in the list)
                     std::unordered_map<uint64_t, uint64_t> original_resource_views_to_mirrored_upgraded_resource_views;
                     std::unordered_map<uint64_t, uint64_t> original_resources_to_mirrored_upgraded_resources;
                     std::shared_lock lock_device_read(device_data.mutex);
                     if (!device_data.original_resources_to_mirrored_upgraded_resources.empty() && ImGui::Button("Clear Indirect Upgraded Textures"))
                     {
                        lock_device_read.unlock();
                        {
                           std::unique_lock lock_device_write(device_data.mutex);
                           original_resource_views_to_mirrored_upgraded_resource_views = device_data.original_resource_views_to_mirrored_upgraded_resource_views;
                           original_resources_to_mirrored_upgraded_resources = device_data.original_resources_to_mirrored_upgraded_resources;
                           device_data.original_resource_views_to_mirrored_upgraded_resource_views.clear();
                           device_data.original_resources_to_mirrored_upgraded_resources.clear();
                        }
                        for (const auto& original_resource_view_to_mirrored_upgraded_resource_view : original_resource_views_to_mirrored_upgraded_resource_views)
                        {
                           runtime->get_device()->destroy_resource_view({ original_resource_view_to_mirrored_upgraded_resource_view.second });
                        }
                        for (const auto& original_resource_to_mirrored_upgraded_resource : original_resources_to_mirrored_upgraded_resources)
                        {
                           runtime->get_device()->destroy_resource({ original_resource_to_mirrored_upgraded_resource.second });
                        }
                     }
                     else if (device_data.original_resources_to_mirrored_upgraded_resources.empty())
                     {
                        ASSERT_ONCE(device_data.original_resource_views_to_mirrored_upgraded_resource_views.empty());
                     }
                  }
               }
               if (prevent_fullscreen_state ? ImGui::Button("Allow Fullscreen State") : ImGui::Button("Disallow Fullscreen State"))
               {
                  prevent_fullscreen_state = !prevent_fullscreen_state;
               }

               if (ImGui::SliderInt("Luma Settings CBuffer Index", reinterpret_cast<int*>(&luma_settings_cbuffer_index), 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1))
               {
                  ASSERT_ONCE(luma_settings_cbuffer_index == -1 || luma_settings_cbuffer_index != luma_data_cbuffer_index); // Can't be equal!
                  Shader::defines_need_recompilation = true;
               }
               if (ImGui::SliderInt("Luma Data CBuffer Index", reinterpret_cast<int*>(&luma_data_cbuffer_index), -1, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1))
               {
                  ASSERT_ONCE(luma_data_cbuffer_index == -1 || luma_settings_cbuffer_index != luma_data_cbuffer_index); // Can't be equal!
                  Shader::defines_need_recompilation = true;
               }

               if (ImGui::Button("Attempt Make Window Borderless"))
               {
                  CenterWindowAndRemoveBorders();
               }

               if (ImGui::Button("Attempt Resize Window (DANGEROUS)"))
               {
                  RECT rect;
                  if (GetWindowRect(game_window, &rect))
                  {
                     LONG width = rect.right - rect.left;
                     LONG height = rect.bottom - rect.top;

                     // Decrease the window by 1, to attempt trigger a swapchain resize event (avoids it setting it beyond the screen)
                     LONG new_width = width > 1 ? (width - 1) : (width + 1);
                     LONG new_height = height > 1 ? (height - 1) : (height + 1);

                     SetWindowPos(game_window, nullptr,
                        rect.left, rect.top,
                        new_width, new_height,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);

                     SetWindowPos(game_window, nullptr,
                        rect.left, rect.top,
                        width, height,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
                  }
               }

               // This will probably hang/crash the game
               if (ImGui::Button("Attempt Resize Swapchain (DANGEROUS)"))
               {
                  // Note: unsafe!
                  auto ThreadFunc = [&]() {
                     UINT width = UINT(device_data.output_resolution.x + 0.5);
                     UINT height = UINT(device_data.output_resolution.y + 0.5);

                     // Decrease the window by 1, to attempt trigger a swapchain resize event (avoids it setting it beyond the screen)
                     UINT new_width = width > 1 ? (width - 1) : (width + 1);
                     UINT new_height = height > 1 ? (height - 1) : (height + 1);

                     // Replacing the swapchain texture live might crash the game anyway, if it cached ptrs to the swapchain buffers.
                     UINT swap_chain_flags = 0;
                     // We already set these on swapchain creation, so maintain them. Usually there aren't any other (game original) flags to maintain. Ideally we'd fish these out of the current swapchain desc
                     swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
                     if (prevent_fullscreen_state) // TODO: test this
                     {
                        swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
                        swap_chain_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
                     }
                     else
                     {
                        swap_chain_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
                     }

                     com_ptr<IDXGISwapChain3> native_swapchain;

                     {
                        const std::shared_lock lock(device_data.mutex);

                        native_swapchain = device_data.GetMainNativeSwapchain();

                        SwapchainData& swapchain_data = *(*device_data.swapchains.begin())->get_private_data<SwapchainData>();

                        {
                           const std::shared_lock lock_swapchain(swapchain_data.mutex);
                           // Before resizing the swapchain, we need to make sure any of its resources/views are not bound to any state (at least from our side, we can't control the game side here)
                           if (!swapchain_data.display_composition_rtvs.empty())
                           {
                              ID3D11Device* native_device = (ID3D11Device*)(runtime->get_device()->get_native());
                              com_ptr<ID3D11DeviceContext> primary_command_list;
                              native_device->GetImmediateContext(&primary_command_list);
                              com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                              com_ptr<ID3D11DepthStencilView> depth_stencil_view;
                              primary_command_list->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &depth_stencil_view);
                              bool rts_changed = false;
                              for (size_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
                              {
                                 for (auto& display_composition_rtv : swapchain_data.display_composition_rtvs)
                                 {
                                    if (rtvs[i].get() != nullptr && rtvs[i].get() == display_composition_rtv.get())
                                    {
                                       rtvs[i] = nullptr;
                                       rts_changed = true;
                                    }
                                    display_composition_rtv = nullptr;
                                 }
                              }
                              if (rts_changed)
                              {
                                 ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(rtvs[0]);
                                 primary_command_list->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());
                              }
                           }
                        }

                        native_swapchain->ResizeBuffers(0, new_width, new_height, DXGI_FORMAT_UNKNOWN, swap_chain_flags);
                     }

                     native_swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swap_chain_flags);
                     };


                  std::thread thread(ThreadFunc);
                  // Detach it so it runs independently from within the "Present" call
                  thread.detach();
               }

               // TODO: test etc
               ImGui::NewLine();
               // Can be useful to force pause/unpause the game
               if (ImGui::Button("Fake Focus Loss Event"))
               {
#if 1
                  SetForegroundWindow(GetDesktopWindow());
                  SetForegroundWindow(game_window);
                  SetFocus(game_window);
#else // These won't work
                  SendMessage(game_window, WM_KILLFOCUS, (WPARAM)NULL, 0); // Tell the window it lost keyboard focus
                  SendMessage(game_window, WM_ACTIVATE, WA_INACTIVE, (LPARAM)NULL); // Also notify it is inactive
#endif
               }
               if (ImGui::Button("Fake Focus Gain Event"))
               {
                  SendMessage(game_window, WM_SETFOCUS, 0, 0);
                  SendMessage(game_window, WM_ACTIVATE, WA_ACTIVE, 0);
               }

               ImGui::NewLine();
               if (enable_ui_separation ? ImGui::Button("Disable Separate UI Drawing and Composition") : ImGui::Button("Enable Separate UI Drawing and Composition"))
               {
                  enable_ui_separation = !enable_ui_separation;

                  device_data.ui_texture = nullptr;
                  device_data.ui_texture_rtv = nullptr;
                  device_data.ui_texture_srv = nullptr;
                  if (enable_ui_separation)
                  {
                     if (auto native_swapchain = device_data.GetMainNativeSwapchain())
                     {
                        com_ptr<ID3D11Texture2D> back_buffer;
                        native_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
                        ID3D11Device* native_device = (ID3D11Device*)runtime->get_device()->get_native();
                        device_data.ui_texture = CloneTexture<ID3D11Texture2D>(native_device, back_buffer.get(), ui_separation_format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, D3D11_BIND_UNORDERED_ACCESS, true, false, nullptr);
                        native_device->CreateRenderTargetView(device_data.ui_texture.get(), nullptr, &device_data.ui_texture_rtv);
                        native_device->CreateShaderResourceView(device_data.ui_texture.get(), nullptr, &device_data.ui_texture_srv);
                     }
                  }
               }
               if (enable_ui_separation)
                  ImGui::Checkbox("Hide Gameplay UI", &hide_ui);
#endif

               game->DrawImGuiDevSettings(device_data);

               if (enable_samplers_upgrade)
               {
                  ImGui::NewLine();
                  bool samplers_changed = ImGui::SliderInt("Texture Samplers Upgrade Mode", &samplers_upgrade_mode, 0, 7);
                  samplers_changed |= ImGui::SliderInt("Texture Samplers Upgrade Mode - 2", &samplers_upgrade_mode_2, 0, 6);
                  ImGui::Checkbox("Custom Texture Samplers Mip LOD Bias", &custom_texture_mip_lod_bias_offset);
                  if (samplers_upgrade_mode > 0 && custom_texture_mip_lod_bias_offset)
                  {
                     const std::unique_lock lock_samplers(s_mutex_samplers);
                     samplers_changed |= ImGui::SliderFloat("Texture Samplers Mip LOD Bias", &device_data.texture_mip_lod_bias_offset, -8.f, +8.f);
                  }
                  if (samplers_changed)
                  {
                     std::unique_lock lock_samplers(s_mutex_samplers);
                     // Re-create them (they will be nullptr and not be replaced in case "samplers_upgrade_mode" was 0)
                     for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
                     {
                        ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                        lock_samplers.unlock(); // Avoids deadlocks with the device (note: not 100% safe but this is dev code)
                        D3D11_SAMPLER_DESC native_desc;
                        native_sampler->GetDesc(&native_desc);
                        com_ptr<ID3D11SamplerState> custom_sampler = CreateCustomSampler(device_data, (ID3D11Device*)runtime->get_device()->get_native(), native_desc);
                        lock_samplers.lock();
                        samplers_handle.second[device_data.texture_mip_lod_bias_offset] = custom_sampler;
                     }
                  }
               }

               ImGui::TreePop();
            }
#endif // DEVELOPMENT

				ImGui::EndTabItem(); // Settings
         }

#if DEVELOPMENT // Use the proper technical name of what this is
         if (ImGui::BeginTabItem("Shader Defines"))
#else // User friendly name (users don't need to understand what shader defines are)
         if (ImGui::BeginTabItem("Advanced Settings"))
#endif
         {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               const char* head = auto_recompile_defines ? "" : "Reload shaders after changing these for the changes to apply (and save).\n";
               const char* tail = "Some settings are only editable in debug modes, and only apply if the \"DEVELOPMENT\" flag is turned on.\nDo not change unless you know what you are doing.";
               ImGui::SetTooltip("Shader Defines: %s%s", head, tail);
            }

            const std::unique_lock lock_shader_defines(s_mutex_shader_defines);

            bool shader_defines_changed = false;

            // Show reset button
            {
               bool is_default = true;
               for (uint32_t i = 0; i < shader_defines_data.size() && is_default; i++)
               {
                  is_default = shader_defines_data[i].IsDefault() && !shader_defines_data[i].IsCustom();
               }
               ImGui::BeginDisabled(is_default);
               ImGui::PushID("Advanced Settings: Reset Defines");
               static const std::string reset_button_title = std::string(ICON_FK_UNDO) + std::string(" Reset");
               if (ImGui::Button(reset_button_title.c_str()))
               {
                  // Remove all newly added settings
                  ShaderDefineData::RemoveCustomData(shader_defines_data);

                  // Reset the rest to default
                  ShaderDefineData::Reset(shader_defines_data);

                  shader_defines_changed = true;
               }
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Resets the defines to their default value");
               }
               ImGui::PopID();
               ImGui::EndDisabled();
            }
#if DEVELOPMENT || TEST
            // Show restore button (basically "undo")
            //if (!auto_recompile_defines) // We could do this, but it's better to just always grey it out in that case
            {
               bool needs_compilation = defines_need_recompilation;
               for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++)
               {
                  needs_compilation |= shader_defines_data[i].NeedsCompilation();
               }
               ImGui::BeginDisabled(!needs_compilation);
               ImGui::SameLine();
               ImGui::PushID("Advanced Settings: Restore Defines");
               static const std::string restore_button_title = std::string(ICON_FK_UNDO) + std::string(" Restore");
               if (ImGui::Button(restore_button_title.c_str()))
               {
                  ShaderDefineData::Restore(shader_defines_data);
                  shader_defines_changed = true;
               }
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Restores the defines to the last compiled values, undoing any changes that haven't been applied");
               }
               ImGui::PopID();
               ImGui::EndDisabled();
            }
#endif

#if DEVELOPMENT || TEST
            ImGui::BeginDisabled(shader_defines_data.empty() || !shader_defines_data[shader_defines_data.size() - 1].IsCustom());
            ImGui::SameLine();
            ImGui::PushID("Advanced Settings: Remove Define");
            static const std::string remove_button_title = std::string(ICON_FK_MINUS) + std::string(" Remove");
            if (ImGui::Button(remove_button_title.c_str()))
            {
               shader_defines_data.pop_back();
               defines_count--;
               shader_defines_changed = true;
            }
            ImGui::PopID();
            ImGui::EndDisabled();

            ImGui::BeginDisabled(shader_defines_data.size() >= MAX_SHADER_DEFINES);
            ImGui::SameLine();
            ImGui::PushID("Advanced Settings: Add Define");
            static const std::string add_button_title = std::string(ICON_FK_PLUS) + std::string(" Add");
            if (ImGui::Button(add_button_title.c_str()))
            {
               // We don't default the value to 0 here, we leave it blank
               shader_defines_data.emplace_back();
               shader_defines_changed = true; // Probably not necessary in this case but ...
            }
            ImGui::PopID();
            ImGui::EndDisabled();
#endif

#if DEVELOPMENT || TEST // Always true in publishing mode
            // Auto Compile Button
            {
               ImGui::SameLine();
               ImGui::PushID("Advanced Settings: Auto Compile");
               ImGui::Checkbox("Auto Compile", &auto_recompile_defines);
               if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("Automatically re-compile instantly when you change settings, at the possible cost of a small stutter");
               }
               ImGui::PopID();
            }
#endif

#if 0 // We simply add a "*" next to the reload shaders button now instead
            // Show when the defines are "dirty" (shaders need recompile)
            {
               bool needs_compilation = defines_need_recompilation;
               for (uint32_t i = 0; i < shader_defines_data.size() && !needs_compilation; i++)
               {
                  needs_compilation |= shader_defines_data[i].NeedsCompilation();
               }
               if (needs_compilation)
               {
                  ImGui::SameLine();
                  ImGui::PushID("Advanced Settings: Defines Dirty");
                  ImGui::BeginDisabled();
                  ImGui::SmallButton(ICON_FK_REFRESH); // Note: we don't want to modify "needs_load_shaders" here, there's another button for that
                  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                  {
                     ImGui::SetTooltip("Recompile shaders needed to apply the changed settings");
                  }
                  ImGui::EndDisabled();
                  ImGui::PopID();
               }
            }
#endif

            bool force_dev_name = false;
#if DEVELOPMENT
            force_dev_name = true;
#endif
            uint8_t longest_shader_define_name_length = 0;
#if 1 // Enables automatic sizing
            for (uint32_t i = 0; i < shader_defines_data.size(); i++)
            {
               const char* shader_define_name = shader_defines_data[i].editable_data.GetName();
               longest_shader_define_name_length = max(longest_shader_define_name_length, strlen(shader_define_name));
            }
            longest_shader_define_name_length += 1; // Add an extra space to avoid it looking too crammed and lagging by one frame
#else
            uint8_t longest_shader_define_name_length = SHADER_DEFINES_MAX_NAME_LENGTH - 1; // Remove the null termination
#endif
            for (uint32_t i = 0; i < shader_defines_data.size(); i++)
            {
               // Don't render empty text fields that couldn't be filled due to them not being editable
               bool disabled = false;
               if (!shader_defines_data[i].IsNameEditable() && !shader_defines_data[i].IsValueEditable())
               {
#if !DEVELOPMENT && !TEST
                  if (shader_defines_data[i].IsCustom())
                  {
                     continue;
                  }
#endif
                  disabled = true;
                  ImGui::BeginDisabled();
               }

               bool show_tooltip = false;

               ImGui::PushID(shader_defines_data[i].name_hint.data());
               ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsNoBlank;
               if (!shader_defines_data[i].IsNameEditable())
               {
                  flags |= ImGuiInputTextFlags_ReadOnly;
               }
               // All characters should (roughly) have the same length
               ImGui::SetNextItemWidth(ImGui::CalcTextSize("0").x * longest_shader_define_name_length);
               // ImGUI doesn't work with std::string data, it seems to need c style char arrays.
               bool name_edited = false;
               if (force_dev_name || shader_defines_data[i].IsNameEditable())
               {
                  name_edited = ImGui::InputTextWithHint("", shader_defines_data[i].name_hint.data(), shader_defines_data[i].editable_data.GetName(), std::size(shader_defines_data[i].editable_data.name) /*SHADER_DEFINES_MAX_NAME_LENGTH*/, flags);
               }
               else
               {
                  std::string user_facing_name = Shader::NameToTitleCase(shader_defines_data[i].editable_data.GetName());

                  // Read only InputText with temporary buffer, to make sure the alignment is right and matches the branch above
                  char user_facing_name_buffer[SHADER_DEFINES_MAX_NAME_LENGTH]{};
                  strncpy(user_facing_name_buffer, user_facing_name.c_str(), sizeof(user_facing_name_buffer) - 1);

                  ImGui::InputText("", user_facing_name_buffer, sizeof(user_facing_name_buffer), ImGuiInputTextFlags_ReadOnly );
               }
               show_tooltip |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
               ImGui::PopID();

               // TODO: fix this, it doesn't seem to work
               auto ModulateValueText = [](ImGuiInputTextCallbackData* data) -> int
                  {
#if 0
                     if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
                     {
                        if (data->Buf[0] == '\0')
                        {
                           // SHADER_DEFINES_MAX_VALUE_LENGTH
#if 0 // Better implementation (actually resets to default when the text was cleaned (invalid value)) (space and - can also be currently written to in the value text field)
                           data->Buf[0] = shader_defines_data[i].default_data.value[0];
                           data->Buf[1] = shader_defines_data[i].default_data.value[1];
#else
                           data->Buf[0] == '0';
                           data->Buf[1] == '\0';
#endif
                           data->BufDirty = true;
                        };
                     };
#endif
                     return 0;
                  };

               ImGui::SameLine();
               ImGui::PushID(shader_defines_data[i].value_hint.data());
               flags = ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_NoUndoRedo | ImGuiInputTextFlags_CallbackEdit;
               if (!shader_defines_data[i].IsValueEditable())
               {
                  flags |= ImGuiInputTextFlags_ReadOnly;
               }
               ImGui::SetNextItemWidth(ImGui::CalcTextSize("00").x);
               bool value_edited = false;
               if (shader_defines_data[i].max_value == 0)
               {
                  value_edited = ImGui::InputTextWithHint("", shader_defines_data[i].value_hint.data(), shader_defines_data[i].editable_data.GetValue(), std::size(shader_defines_data[i].editable_data.value) /*SHADER_DEFINES_MAX_VALUE_LENGTH*/, flags, ModulateValueText);
               }
               else
               {
                  // 0-255 as it's char/uint8
                  static char items[256][4]; // enough for "255\0"
                  static const char* items_ptrs[256];
                  static bool items_initialized = false;
                  if (!items_initialized)
                  {
                     items_initialized = true;
                     for (int i = 0; i < 256; i++)
                     {
                        snprintf(items[i], sizeof(items[i]), "%d", i);
                        items_ptrs[i] = items[i];
                     }
                  }

                  // Draw it as a checkbox if the only possible values are 0 and 1
                  // TODO: some defines might still prefer a drop down list? Maybe we could fix that by defining their values names as a char array
                  if (shader_defines_data[i].max_value == 1)
                  {
                     bool value = shader_defines_data[i].editable_data.GetNumericalValue() != 0;
                     if (ImGui::Checkbox("", &value))
                     {
                        shader_defines_data[i].editable_data.value[0] = static_cast<char>('0' + (value ? 1 : 0));
                        value_edited = true;
                     }
                  }
                  else
                  {  // Compute enough width for one number plus the dropdown arrow and padding
                     float text_width = ImGui::CalcTextSize("00").x;
                     float arrow_width = ImGui::GetFrameHeight(); // Roughly the width of the arrow button
                     float padding = ImGui::GetStyle().ItemInnerSpacing.x; // Some padding

                     ImGui::SetNextItemWidth(text_width + arrow_width + padding); // Should be enough for a combo with a 0-9 range
                     int value = shader_defines_data[i].editable_data.GetNumericalValue();
                     if (ImGui::Combo("", &value, items_ptrs, shader_defines_data[i].max_value + 1))
                     {
                        shader_defines_data[i].editable_data.value[0] = static_cast<char>('0' + value);
                        value_edited = true;
                     }
                  }
               }
               show_tooltip |= ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
               // Avoid having empty values unless the default value also was empty. This is a worse implementation of the "ImGuiInputTextFlags_CallbackEdit" above, which we can't get to work.
               // If the value was empty to begin with, we leave it, to avoid confusion.
               if (value_edited && shader_defines_data[i].IsValueEmpty())
               {
                  // SHADER_DEFINES_MAX_VALUE_LENGTH
                  shader_defines_data[i].editable_data.value[0] = shader_defines_data[i].default_data.value[0];
                  shader_defines_data[i].editable_data.value[1] = shader_defines_data[i].default_data.value[1];
#if 0 // This would only appear for 1 frame at the moment
                  if (show_tooltip)
                  {
                     ImGui::SetTooltip(shader_defines_data[i].value_hint.c_str());
                     show_tooltip = false;
                  }
#endif
               }
#if 0 // Disabled for now as this is not very user friendly and could accidentally happen if two defines start with the same name.
               // Reset the define name if it matches another one
               if (name_edited && ShaderDefineData::ContainsName(shader_defines_data, shader_defines_data[i].editable_data.GetName(), i))
               {
                  shader_defines_data[i].Clear();
               }
#endif
               ImGui::PopID();

               if (disabled)
               {
                  ImGui::EndDisabled();
               }

               if (name_edited || value_edited)
               {
                  shader_defines_changed = true;
               }

               if (show_tooltip && shader_defines_data[i].IsNameDefault() && shader_defines_data[i].HasTooltip())
               {
                  ImGui::SetTooltip(shader_defines_data[i].GetTooltip());
               }
            }

            if (shader_defines_changed)
            {
               game->OnShaderDefinesChanged();
            }

            ImGui::EndTabItem();
         }

#if DEVELOPMENT || TEST
         if (ImGui::BeginTabItem("Info"))
         {
            std::string text;

            {
               const std::shared_lock lock(device_data.mutex);

               for (auto back_buffer : device_data.back_buffers)
               {
                  ImGui::Text("Swapchain Format: ", "");

                  reshade::api::resource resource;
                  resource.handle = back_buffer;
                  const reshade::api::resource_desc resource_desc = runtime->get_device()->get_resource_desc(resource);
                  std::ostringstream oss;
                  oss << resource_desc.texture.format;
                  text = oss.str();
                  ImGui::Text(text.c_str(), "");
                  ImGui::NewLine();
               }

#if !GRAPHICS_ANALYZER
               ImGui::Text("Direct Upgraded Textures: ", "");
               text = std::to_string((int)device_data.upgraded_resources.size());
               ImGui::Text(text.c_str(), "");

               ImGui::Text("Indirect Upgraded Textures: ", "");
               text = std::to_string((int)device_data.original_resources_to_mirrored_upgraded_resources.size());
               ImGui::Text(text.c_str(), "");

               ImGui::NewLine();

               ImGui::Text("Upgraded Samplers: ", "");
               text = std::to_string((int)device_data.custom_sampler_by_original_sampler.size());
               ImGui::Text(text.c_str(), "");
#endif // !GRAPHICS_ANALYZER
            }

#if !GRAPHICS_ANALYZER
            ImGui::NewLine();
            ImGui::Text("Render Resolution: ", "");
            text = std::to_string((int)device_data.render_resolution.x) + " " + std::to_string((int)device_data.render_resolution.y);
            ImGui::Text(text.c_str(), "");
#endif // !GRAPHICS_ANALYZER

            ImGui::NewLine();
            ImGui::Text("Output Resolution: ", "");
            text = std::to_string((int)device_data.output_resolution.x) + " " + std::to_string((int)device_data.output_resolution.y);
            ImGui::Text(text.c_str(), "");

#if ENABLE_SR
            if (device_data.sr_type != SR::Type::None)
            {
               ImGui::NewLine();
               ImGui::Text("SR Target Resolution Scale: ", "");
               text = std::to_string(device_data.sr_render_resolution_scale);
               ImGui::Text(text.c_str(), "");

               if (IsModActive(device_data))
               {
                  ImGui::NewLine();
                  ImGui::Text("SR Scene Pre Exposure: ", "");
                  text = std::to_string(device_data.sr_scene_pre_exposure);
                  ImGui::Text(text.c_str(), "");
               }
            }
#endif // ENABLE_SR

            // Useful for x86 processes with limited space
            {
               MEMORYSTATUSEX status;
               status.dwLength = sizeof(status);
               if (GlobalMemoryStatusEx(&status))
               {
                  ImGui::NewLine();
                  unsigned long long avail_mb = status.ullAvailVirtual / (1024 * 1024);
                  ImGui::Text("Process Available Memory: %llu MB", avail_mb);
               }
            }

            game->PrintImGuiInfo(device_data);

            ImGui::EndTabItem(); // Info
         }
#endif // DEVELOPMENT || TEST

         if (ImGui::BeginTabItem("About"))
         {
            game->PrintImGuiAbout();

            ImGui::NewLine();
            static const std::string version = "Version: " + std::to_string(Globals::VERSION);
            ImGui::Text(version.c_str());

            static std::string development_state = "Finished"; // "ModDevelopmentState::Finished" by default
            switch (Globals::DEVELOPMENT_STATE)
            {
            case Globals::ModDevelopmentState::NonFunctional: development_state = "Non Functional"; break;
            case Globals::ModDevelopmentState::WorkInProgress: development_state = "Work in Progress"; break;
            case Globals::ModDevelopmentState::Playable: development_state = "Playable"; break;
            }
            ImGui::Text("Development State: %s", development_state.c_str());
#if TEST
            ImGui::Text("TEST Enabled");
#endif
#if DEVELOPMENT
            ImGui::Text("DEVELOPMENT Enabled");
#endif

            ImGui::EndTabItem(); // About
         }

         ImGui::EndTabBar(); // TabBar
      }
   }
#pragma optimize("", on) // Restore the previous state
} // namespace

// "async" means that this is not called from the dll loading function ("DLL_PROCESS_ATTACH"), so we can do threading related stuff in the function
void Init(bool async)
{
   has_init = true;

   for (int i = 0; i < shader_defines_data.size(); i++)
   {
      shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].default_data.GetName()))] = i;
   }

   Shader::InitShaderCompiler();

   game->OnInit(async);

   assert(luma_settings_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
   assert(luma_data_cbuffer_index < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT); // Not necessary for custom shaders unless used, but necessary for the final luma display composition shader (unless we forced that one to use cb0 or something for the buffer)
   assert(luma_settings_cbuffer_index == -1 || luma_data_cbuffer_index == -1 || luma_settings_cbuffer_index != luma_data_cbuffer_index); // Can't be equal!

   cb_luma_global_settings.DisplayMode = DisplayModeType::HDR; // Default to HDR in case we had no prior config, it will be automatically disabled if the current display doesn't support it (when the swapchain is created, which should be guaranteed to be after)
   cb_luma_global_settings.ScenePeakWhite = default_peak_white;
   cb_luma_global_settings.ScenePaperWhite = default_paper_white;
   cb_luma_global_settings.UIPaperWhite = default_paper_white;
   cb_luma_global_settings.SRType = 0; // We can't set this to >0 until we verified SR engaged correctly and is running

   // Note: these shouldn't load any dlls etc, they are just creating an empty class
#if ENABLE_NGX
   sr_implementations[SR::Type::DLSS] = std::make_unique<NGX::DLSS>();
#endif
#if ENABLE_FIDELITY_SK
   sr_implementations[SR::Type::FSR] = std::make_unique<FidelityFX::FSR>();
#endif

   // Load settings
   [[maybe_unused]] bool delete_old_shaders = false;
   {
      if (async)
         s_mutex_reshade.lock();

      bool do_shader_defines_reset = false;

      reshade::api::effect_runtime* runtime = nullptr;
      uint32_t config_version = Globals::VERSION;
      reshade::get_config_value(runtime, NAME, "Version", config_version);
      if (config_version != Globals::VERSION)
      {
         if (config_version < Globals::VERSION)
         {
            if (async)
               s_mutex_loading.lock();
            // NOTE: put behaviour to load previous versions into new ones here
            CleanShadersCache(); // Force recompile shaders, just for extra safety (theoretically changes are auto detected through the preprocessor, but we can't be certain). We don't need to change the last config serialized shader defines.
            delete_old_shaders = true;
            do_shader_defines_reset = true;
            if (async)
               s_mutex_loading.unlock();
         }
         else if (config_version > Globals::VERSION)
         {
            reshade::log::message(reshade::log::level::warning, "Luma: trying to load a config from a newer version of the mod, loading might have unexpected results");
         }
         reshade::set_config_value(runtime, NAME, "Version", Globals::VERSION);
      }

#if DEVELOPMENT
      std::string shaders_path_str;
      shaders_path_str.resize(256);
      size_t shaders_path_str_size = shaders_path_str.capacity();
      if (reshade::get_config_value(runtime, NAME, "ShadersPath", shaders_path_str.data(), &shaders_path_str_size))
      {
         shaders_path_str.resize(shaders_path_str_size);
         custom_shaders_path = std::filesystem::path(shaders_path_str);
      }
#endif

#if ENABLE_SR
      int sr_user_type_i = int(sr_user_type);
      reshade::get_config_value(runtime, NAME, "SRUserType", sr_user_type_i); // This will be reset later if not compatible
      sr_user_type = SR::UserType(sr_user_type_i);
#endif
      int display_mode_i = int(cb_luma_global_settings.DisplayMode);
      reshade::get_config_value(runtime, NAME, "DisplayMode", display_mode_i);
      cb_luma_global_settings.DisplayMode = DisplayModeType(display_mode_i);
#if !DEVELOPMENT && !TEST // Don't allow "SDR in HDR for HDR" mode (there's no strong reason not to, but it avoids permutations exposed to users)
      if (cb_luma_global_settings.DisplayMode >= DisplayModeType::SDRInHDR)
      {
         cb_luma_global_settings.DisplayMode = DisplayModeType::SDR;
      }
#endif

      // If we read an invalid value from the config, reset it
      if (reshade::get_config_value(runtime, NAME, "ScenePeakWhite", cb_luma_global_settings.ScenePeakWhite) && cb_luma_global_settings.ScenePeakWhite <= 0.f)
      {
         if (async)
            s_mutex_device.lock_shared(); // This is not completely safe as the write to "default_user_peak_white" isn't protected by this mutex but it's fine, it shouldn't have been written yet when we get here
         cb_luma_global_settings.ScenePeakWhite = global_devices_data.empty() ? default_peak_white : global_devices_data[0]->default_user_peak_white;
         hdr_display_mode_pending_auto_peak_white_calibration = global_devices_data.empty(); // Re-adjust it later when we can
         if (async)
            s_mutex_device.unlock_shared();
      }
      reshade::get_config_value(runtime, NAME, "ScenePaperWhite", cb_luma_global_settings.ScenePaperWhite);
      reshade::get_config_value(runtime, NAME, "UIPaperWhite", cb_luma_global_settings.UIPaperWhite);
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::SDR)
      {
         cb_luma_global_settings.ScenePeakWhite = srgb_white_level;
         cb_luma_global_settings.ScenePaperWhite = srgb_white_level;
         cb_luma_global_settings.UIPaperWhite = srgb_white_level;
      }
      else if (cb_luma_global_settings.DisplayMode >= DisplayModeType::SDRInHDR)
      {
         cb_luma_global_settings.UIPaperWhite = cb_luma_global_settings.ScenePaperWhite;
         cb_luma_global_settings.ScenePeakWhite = cb_luma_global_settings.ScenePaperWhite;
      }

      if (async)
         s_mutex_shader_defines.lock(); // Note: do we need the mutex here the whole time?
      ShaderDefineData::Load(shader_defines_data, NAME_ADVANCED_SETTINGS, runtime);
      if (do_shader_defines_reset)
      {
         ShaderDefineData::Reset(shader_defines_data);
      }

      game->LoadConfigs();
		ASSERT_ONCE(global_devices_data.empty()); // There should be no devices yet, otherwise we need to set "cb_luma_global_settings_dirty" to true! Hence why the "precompile_custom_shaders" code below seems to make no sense?

      OnDisplayModeChanged();

      game->OnShaderDefinesChanged();

      if (async)
      {
         s_mutex_shader_defines.unlock();
         s_mutex_reshade.unlock();
      }
   }

   shaders_path = GetShadersRootPath(); // Needs to be done after "custom_shaders_path" was set

   // Delete old shaders from previous version
#if DEVELOPMENT && defined(SOLUTION_DIR) && (!defined(REMOTE_BUILD) || !REMOTE_BUILD)
   // Development shader folder would always be up to date
#else
   if (delete_old_shaders && !old_shader_file_names.empty())
   {
      std::filesystem::path game_shaders_path = shaders_path / Globals::GAME_NAME;
      // No need to create the directory here if it didn't already exist
      if (std::filesystem::is_directory(game_shaders_path))
      {
         for (const auto& entry : std::filesystem::directory_iterator(game_shaders_path))
         {
            if (entry.is_regular_file() && old_shader_file_names.contains(entry.path().filename().string()))
            {
               std::filesystem::remove(entry);
            }
         }
      }
   }
#endif

#if ALLOW_SHADERS_DUMPING // Needs to be done after "custom_shaders_path" was set
   // Add all the shaders we have already dumped to the dumped list to avoid live re-dumping them
   dumped_shaders.clear();
   std::set<std::filesystem::path> dumped_shaders_paths;
   shaders_dump_path = shaders_path / Globals::GAME_NAME / (std::string("Dump") + (sub_game_shaders_appendix.empty() ? "" : " ") + sub_game_shaders_appendix); // We dump in the game specific folder
   // No need to create the directory here if it didn't already exist
   if (std::filesystem::is_directory(shaders_dump_path))
   {
      if (async)
         s_mutex_dumping.lock();
      for (const auto& entry : std::filesystem::directory_iterator(shaders_dump_path))
      {
         if (!entry.is_regular_file()) continue;
         const auto& entry_path = entry.path();
         bool is_cso = entry_path.extension() == ".cso";
         bool is_meta = entry_path.extension() == ".meta";
         if (!is_cso && !is_meta) continue;
         const auto& entry_strem_string = entry_path.stem().string();
         if (entry_strem_string.starts_with("0x") && entry_strem_string.length() >= 2 + HASH_CHARACTERS_LENGTH)
         {
            const std::string shader_hash_string = entry_strem_string.substr(2, HASH_CHARACTERS_LENGTH);
            uint32_t shader_hash;
            try
            {
               shader_hash = Shader::Hash_StrToNum(shader_hash_string);
            }
            catch (const std::exception& e)
            {
               continue;
            }

            if (is_meta)
            {
#if DEVELOPMENT
               dumped_shaders_meta_paths[shader_hash] = entry_path;
#endif
               continue;
            }
            // "is_cso"

            bool duplicate = dumped_shaders.contains(shader_hash);
#if DEVELOPMENT
            ASSERT_ONCE(!duplicate); // We have a duplicate shader dumped, cancel here to avoid deleting it
#endif
            if (duplicate) // Not really needed anymore, as this fixes a legacy behaviour
            {
               for (const auto& prev_entry_path : dumped_shaders_paths)
               {
                  if (prev_entry_path.string().contains(shader_hash_string))
                  {
                     // Delete the old version if it's shorter in name (e.g. it might have missed the "ps_5_0" appendix, or simply missing a name we manually appended to it)
                     if (prev_entry_path.string().length() < entry_path.string().length())
                     {
                        if (std::filesystem::remove(prev_entry_path))
                        {
                           duplicate = false;
                           break;
                        }
                     }
                     // Delete the new version
                     else
                     {
                        if (std::filesystem::remove(entry_path))
                        {
                           break;
                        }
                     }
                  }
               }
            }
            if (!duplicate)
            {
               dumped_shaders.emplace(shader_hash);
               dumped_shaders_paths.emplace(entry_path);
            }
         }
      }
      if (async)
         s_mutex_dumping.unlock();
   }
#endif // ALLOW_SHADERS_DUMPING

   {
      // Assume that the shader defines loaded from config match the ones the current pre-compiled shaders have (or, simply use the defaults otherwise)
      if (async)
         s_mutex_shader_defines.lock();
      ShaderDefineData::OnCompilation(shader_defines_data);
      shader_defines_data_index.clear();
      for (uint32_t i = 0; i < shader_defines_data.size(); i++)
      {
         shader_defines_data_index[string_view_crc32(std::string_view(shader_defines_data[i].compiled_data.GetName()))] = i;
      }
      if (async)
         s_mutex_shader_defines.unlock();
   }

   // Pre-load all shaders to minimize the wait before replacing them after they are found in game ("auto_load"),
   // and to fill the list of shaders we customized, so we can know which ones we need replace on the spot.
   if (async && precompile_custom_shaders)
   {
      thread_auto_compiling_running = true;
      static std::binary_semaphore async_shader_compilation_semaphore{ 0 };
      thread_auto_compiling = std::thread([]
         {
            // We need to lock this mutex for the whole async shader loading, so that if the game starts loading shaders (from another thread), we can already see if we have a custom version and live load it ("live_load"), otherwise the "custom_shaders_cache" list would be incomplete
            const std::unique_lock lock_loading(s_mutex_loading);
            // This is needed to make sure this thread locks "s_mutex_loading" before any other function could
            async_shader_compilation_semaphore.release();
            CompileCustomShaders(nullptr, true);
            const std::shared_lock lock_device(s_mutex_device);
            // Create custom device shaders if the device has already been created before custom shaders were loaded on boot, independently of "block_draw_until_device_custom_shaders_creation".
            // Note that this might be unsafe if "global_devices_data" was already being destroyed in "OnDestroyDevice()" (I'm not sure you can create device resources anymore at that point).
            // TODO: can this even ever happen? Having devices already created/added here? I don't think so... however, "async_shader_compilation_semaphore" below will hang in some games
            for (auto global_device_data : global_devices_data)
            {
               const std::unique_lock lock_shader_objects(s_mutex_shader_objects);
               if (!global_device_data->created_native_shaders)
               {
                  CreateDeviceNativeShaders(*global_device_data, nullptr, false);
               }
            }
            thread_auto_compiling_running = false;
         });
      async_shader_compilation_semaphore.acquire();
   }
}

// This can't be called on "DLL_PROCESS_DETACH" as it needs a multi threaded environment
void Uninit()
{
   if (thread_auto_dumping.joinable())
   {
      thread_auto_dumping.join();
   }
   if (thread_auto_compiling.joinable())
   {
      thread_auto_compiling.join();
   }
   {
      const std::shared_lock lock(s_mutex_device);
      for (auto global_device_data : global_devices_data)
      {
         if (global_device_data->thread_auto_loading.joinable())
         {
            global_device_data->thread_auto_loading.join();
         }
      }
   }

#if 0 // Unload any dll. For now we keep it loaded forever because we might load a custom version of DX shader compiler DLLs, and sometimes ReShade or ReShade addons load and unload multiple times on boot, so we wouldn't want to add any weight to further loads
   Shader::UnInitShaderCompiler();
#endif

   has_init = false;
}

#ifndef RESHADE_EXTERNS
// This is called immediately after the main function ("DllMain") gets "DLL_PROCESS_ATTACH" if this dll/addon is loaded directly by ReShade, unless the "LoadFromDllMain" config is enabled in ReShade
extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
   bool inline_dll_load = false;
   reshade::get_config_value(nullptr, "ADDON", "LoadFromDllMain", inline_dll_load);
   bool async = !inline_dll_load;
   Init(async);
   return true;
}
extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
   Uninit();
}
#endif

// This is a static library so this "main" function won't ever be automatically called, it needs to be manually hooked.
BOOL APIENTRY CoreMain(HMODULE h_module, DWORD fdw_reason, LPVOID lpv_reserved)
{
   switch (fdw_reason)
   {
   // Note: this dll should support being loaded more than once (included being unloaded in the middle of execution).
   // ReShade loads addons when the game creates a DirectX device, this usually only happens once on boot under normal circumstances (e.g. Prey), but can happen multiple times (e.g. Dishonored 2, Unity games, ...).
   case DLL_PROCESS_ATTACH:
   {
      // Optimization (we don't need DLL_THREAD_ATTACH)
      DisableThreadLibraryCalls(h_module);

// Some games like to crash or have input issues if a debugger is present on boot, so make it optional
#if (DEVELOPMENT || _DEBUG) && !defined(DISABLE_AUTO_DEBUGGER)
      LaunchDebugger(NAME);
#endif // DEVELOPMENT

      std::filesystem::path file_path = System::GetModulePath(h_module);
      if (file_path.extension() == ".addon" || file_path.extension() == ".addon64")
      {
         asi_loaded = false;
      }
      else
      {
         // Just to make sure, if we got loaded then it's probably fine either way
         assert(file_path.extension() == ".dll" || file_path.extension() == ".asi");
      }

      bool load_failed = false;

#if !DISABLE_RESHADE
      // Register the ReShade addon.
      // We simply cancel everything else if reshade is not present or failed to register,
      // we could still load the native plugin,
      const bool reshade_addon_register_succeeded = reshade::register_addon(h_module);
      if (!reshade_addon_register_succeeded) load_failed = true;
#endif // !DISABLE_RESHADE

      std::filesystem::path shader_compiler_path = file_path.parent_path();
      shader_compiler_path.append("msvcp140.dll");
      if (std::filesystem::is_regular_file(shader_compiler_path))
      {
         const std::string warn_message = "Your game has a custom version of \"msvcp140.dll\" in its path, please remove it and restart the game or Luma will possibly crash (there are no other side effects).\nWould you like to close the game now?";
         auto ret = MessageBoxA(NULL, warn_message.c_str(), NAME, MB_SETFOREGROUND | MB_YESNO);
         if (ret == IDYES)
         {
            exit(0);
         }
      }

      // We give the game code the opportunity to do something before rejecting the dll load
      game->OnLoad(file_path, load_failed);

#if !DEVELOPMENT && !TEST
      if (Globals::DEVELOPMENT_STATE == Globals::ModDevelopmentState::NonFunctional || Globals::DEVELOPMENT_STATE == Globals::ModDevelopmentState::WorkInProgress)
      {
         const std::string warn_message = "You are playing a mod that is either non functional or non finished. Proceed at your own risk.\nReporting issues is generally not necessary unless you were asked for testing.";
         MessageBoxA(NULL, warn_message.c_str(), NAME, MB_SETFOREGROUND);
		}
#endif

#if DISABLE_RESHADE
      if (!asi_loaded) return FALSE;
#endif // DISABLE_RESHADE

      if (load_failed)
      {
         return FALSE;
      }

#if DISABLE_RESHADE
      if (asi_loaded) return TRUE;
#endif // DISABLE_RESHADE

      reshade::register_event<reshade::addon_event::create_device>(OnCreateDevice);
      reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::register_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
      reshade::register_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::register_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
      reshade::register_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
      reshade::register_event<reshade::addon_event::create_pipeline>(OnCreatePipeline);
#endif
      reshade::register_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
      reshade::register_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);

      reshade::register_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

      reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargetsAndDepthStencil);

      reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
      reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);

      if (texture_format_upgrades_type > TextureFormatUpgradesType::None)
      {
         reshade::register_event<reshade::addon_event::init_resource>(OnInitResource);
         reshade::register_event<reshade::addon_event::create_resource>(OnCreateResource);
         reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      }
#if DEVELOPMENT || GAME_PREY
      else
      {
         reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      }
#endif
      if (texture_format_upgrades_type > TextureFormatUpgradesType::None || swapchain_format_upgrade_type > TextureFormatUpgradesType::None)
      {
         reshade::register_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
         reshade::register_event<reshade::addon_event::init_resource_view>(OnInitResourceView);
         reshade::register_event<reshade::addon_event::destroy_resource_view>(OnDestroyResourceView);
      }

      reshade::register_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
#if RESHADE_API_VERSION >= 18
      reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
#endif
#endif // DEVELOPMENT
#if !DEVELOPMENT
      if (texture_format_upgrades_type > TextureFormatUpgradesType::None || swapchain_format_upgrade_type > TextureFormatUpgradesType::None)
#endif
      {
         reshade::register_event<reshade::addon_event::map_texture_region>(OnMapTextureRegion);
         reshade::register_event<reshade::addon_event::unmap_texture_region>(OnUnmapTextureRegion);
         reshade::register_event<reshade::addon_event::update_texture_region>(OnUpdateTextureRegion);
      }

      reshade::register_event<reshade::addon_event::clear_depth_stencil_view>(OnClearDepthStancilView);
      reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
      reshade::register_event<reshade::addon_event::clear_unordered_access_view_uint>(OnClearUnorderedAccessViewUInt);
      reshade::register_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccessViewFloat);

      reshade::register_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::register_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
      reshade::register_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);

      reshade::register_event<reshade::addon_event::draw>(OnDraw);
      reshade::register_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::register_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

      if (enable_samplers_upgrade)
      {
         reshade::register_event<reshade::addon_event::init_sampler>(OnInitSampler);
         reshade::register_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
      }

      reshade::register_event<reshade::addon_event::reset_command_list>(OnResetCommandList);
#if DEVELOPMENT
      reshade::register_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);
#endif // DEVELOPMENT
      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(OnExecuteSecondaryCommandList);

      reshade::register_event<reshade::addon_event::present>(OnPresent);

      reshade::register_event<reshade::addon_event::reshade_present>(OnReShadePresent);

#if DEVELOPMENT || TEST // Currently Dev only as we don't need the average user to compare the mod on/off
      reshade::register_event<reshade::addon_event::reshade_set_effects_state>(OnReShadeSetEffectsState);
      reshade::register_event<reshade::addon_event::reshade_reloaded_effects>(OnReShadeReloadedEffects);
#endif // DEVELOPMENT

      reshade::register_overlay(NAME, OnRegisterOverlay);

      break;
   }
   case DLL_PROCESS_DETACH:
   {
#if DEVELOPMENT
      if (game_window_original_proc && game_window != NULL && IsWindow(game_window))
      {
         SetWindowLongPtr(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game_window_original_proc));
         game_window_original_proc = nullptr;
      }
      if (game_window_proc_hook)
      {
         UnhookWindowsHookEx(game_window_proc_hook);
         game_window_proc_hook = nullptr;
      }
#endif

      // Automatically destroy this if it was instanced by a game implementation
      if (game != &default_game)
      {
         delete game;
         game = nullptr;
      }

      reshade::unregister_event<reshade::addon_event::create_device>(OnCreateDevice);
      reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::unregister_event<reshade::addon_event::create_swapchain>(OnCreateSwapchain);
      reshade::unregister_event<reshade::addon_event::init_swapchain>(OnInitSwapchain);
      reshade::unregister_event<reshade::addon_event::destroy_swapchain>(OnDestroySwapchain);
      reshade::unregister_event<reshade::addon_event::set_fullscreen_state>(OnSetFullscreenState);

#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
      reshade::unregister_event<reshade::addon_event::create_pipeline>(OnCreatePipeline);
#endif
      reshade::unregister_event<reshade::addon_event::init_pipeline>(OnInitPipeline);
      reshade::unregister_event<reshade::addon_event::destroy_pipeline>(OnDestroyPipeline);

      reshade::unregister_event<reshade::addon_event::bind_pipeline>(OnBindPipeline);

      reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(OnBindRenderTargetsAndDepthStencil);

      reshade::unregister_event<reshade::addon_event::init_command_list>(OnInitCommandList);
      reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);

      if (texture_format_upgrades_type > TextureFormatUpgradesType::None)
      {
         reshade::unregister_event<reshade::addon_event::init_resource>(OnInitResource);
         reshade::unregister_event<reshade::addon_event::create_resource>(OnCreateResource);
         reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      }
#if DEVELOPMENT || GAME_PREY
      else
      {
         reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      }
#endif
      if (texture_format_upgrades_type > TextureFormatUpgradesType::None || swapchain_format_upgrade_type > TextureFormatUpgradesType::None)
      {
         reshade::unregister_event<reshade::addon_event::create_resource_view>(OnCreateResourceView);
         reshade::unregister_event<reshade::addon_event::init_resource_view>(OnInitResourceView);
         reshade::unregister_event<reshade::addon_event::destroy_resource_view>(OnDestroyResourceView);
      }

      reshade::unregister_event<reshade::addon_event::push_descriptors>(OnPushDescriptors);
#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
#if RESHADE_API_VERSION >= 18
      reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(OnUpdateBufferRegionCommand);
#endif
#endif // DEVELOPMENT
#if !DEVELOPMENT
      if (texture_format_upgrades_type > TextureFormatUpgradesType::None || swapchain_format_upgrade_type > TextureFormatUpgradesType::None)
#endif
      {
         reshade::unregister_event<reshade::addon_event::map_texture_region>(OnMapTextureRegion);
         reshade::unregister_event<reshade::addon_event::unmap_texture_region>(OnUnmapTextureRegion);
         reshade::unregister_event<reshade::addon_event::update_texture_region>(OnUpdateTextureRegion);
      }

      reshade::unregister_event<reshade::addon_event::clear_depth_stencil_view>(OnClearDepthStancilView);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
      reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_uint>(OnClearUnorderedAccessViewUInt);
      reshade::unregister_event<reshade::addon_event::clear_unordered_access_view_float>(OnClearUnorderedAccessViewFloat);

      reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopyResource);
      reshade::unregister_event<reshade::addon_event::copy_texture_region>(OnCopyTextureRegion);
      reshade::unregister_event<reshade::addon_event::resolve_texture_region>(OnResolveTextureRegion);

      reshade::unregister_event<reshade::addon_event::draw>(OnDraw);
      reshade::unregister_event<reshade::addon_event::dispatch>(OnDispatch);
      reshade::unregister_event<reshade::addon_event::draw_indexed>(OnDrawIndexed);
      reshade::unregister_event<reshade::addon_event::draw_or_dispatch_indirect>(OnDrawOrDispatchIndirect);

      if (enable_samplers_upgrade)
      {
         reshade::unregister_event<reshade::addon_event::init_sampler>(OnInitSampler);
         reshade::unregister_event<reshade::addon_event::destroy_sampler>(OnDestroySampler);
      }

      reshade::unregister_event<reshade::addon_event::reset_command_list>(OnResetCommandList);
#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::execute_command_list>(OnExecuteCommandList);
#endif // DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(OnExecuteSecondaryCommandList);

      reshade::unregister_event<reshade::addon_event::present>(OnPresent);

      reshade::unregister_event<reshade::addon_event::reshade_present>(OnReShadePresent);

#if DEVELOPMENT || TEST
      reshade::unregister_event<reshade::addon_event::reshade_set_effects_state>(OnReShadeSetEffectsState);
      reshade::unregister_event<reshade::addon_event::reshade_reloaded_effects>(OnReShadeReloadedEffects);
#endif // DEVELOPMENT

      reshade::unregister_overlay(NAME, OnRegisterOverlay);

      reshade::unregister_addon(h_module);

      // In case our threads are still not joined, detach them and safely do a busy loop
      // until they finished running, so we don't risk them reading/writing to stale memory.
      // This could cause a bit of wait, especially if we just booted the game and shaders are still compiling,
      // but there's no nice and clear alternatively really.
      // This is needed because DLL loading/unloading is completely single threaded and isn't
      // able to join threads (though "thread.detach()" somehow seems to work).
      // Note that there's no need to call "Uninit()" here, independently on whether we are asi or ReShade loaded.
      if (thread_auto_dumping.joinable())
      {
         thread_auto_dumping.detach();
         while (thread_auto_dumping_running) {}
      }
      if (thread_auto_compiling.joinable())
      {
         thread_auto_compiling.detach();
         while (thread_auto_compiling_running) {}
      }
      // We can't lock "s_mutex_device" here, but we also know that if this ptr is valid, then there's no other thread able to run now and change it.
      // ReShade is unloaded when the last device is destroyed so we should have already received an event to clear this thread anyway.
      for (auto global_device_data : global_devices_data)
      {
         if (global_device_data->thread_auto_loading.joinable())
         {
            global_device_data->thread_auto_loading.detach();
            while (global_device_data->thread_auto_loading_running) {}
         }
      }

      break;
   }
   }

   return TRUE;
}
