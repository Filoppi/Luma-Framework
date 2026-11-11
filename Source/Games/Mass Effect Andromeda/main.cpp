// Luma anti-aliasing mod for Mass Effect: Andromeda (Frostbite 3, D3D11).
// Injects DLSS, FSR or XeSS, as AA or upscaling (in-game AA = TAA; upscaling runs the native AO at the output size), replaces
// FXAA with SMAA (in-game AA = FXAA), and fixes the game's HDR10 output in its presents (see Shaders/.../Includes/Common.hlsl).

#define GAME_MASS_EFFECT_ANDROMEDA 1

// Frostbite input quirks: any message box (e.g. dev asserts) permanently breaks the game's
// input, and focus-loss handling interferes too.
#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define AVOID_INPUT_LOSS 1

// FSR 4 and XeSS create a D3D12 device through the DX11->DX12 interop, which Core has to ignore
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1 // replaces the game's FXAA pass (FXAA AA mode) with SMAA
#define ENABLE_RCAS 1 // optional sharpening of the SMAA or DLSS/FSR output (Core's "RCAS PS")
// Every pass Luma reissues itself (the DOF-variant resolve run natively first, the output-sized post passes, the upscaled
// tonemap and the draws after it, the tonemap's RCAS) calls the game's draw through "original_draw_dispatch_func": Core only
// provides it outside DEVELOPMENT with this set (without it they all silently fall back to native in Test/Publishing).
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
// SMAA's area texture without the U-shape smoothing (see "ReplaceFXAAWithSMAA")
#include "SMAA_AreaTex_SharpU.h"
#include <d3d11_1.h>  // ID3D11DeviceContext1 (bound-range CB queries)
#include <shellapi.h> // ShellExecuteA for the About-tab link buttons (system("start") hangs in exclusive fullscreen)
#include <bit>        // std::countr_zero (an AO pass's mode bit)
#include <deque>
#include "..\..\External\NVAPI\nvapi.h" // types only (the HDR call is hooked, not linked)

// A Luma native shader ("native_*_shaders") by name, null until compiled. Looked up with "find": "operator[]" would insert on a miss,
// mutating a map Core's draw helpers read concurrently.
template <typename ShaderMap>
static auto FindShader(const ShaderMap& shaders, uint32_t name)
{
   const auto it = shaders.find(name);
   return (it != shaders.end() ? it->second.get() : nullptr);
}

// The size of the texture behind a view (0x0 without one)
static uint2 GetViewTextureSize(ID3D11View* view)
{
   uint4 size;
   DXGI_FORMAT format;
   GetResourceInfo(view, size, format);
   return uint2{size.x, size.y};
}

// Whether Core's SMAA passes ("DrawSMAA()") are compiled
static bool HasSMAAShaders(const DeviceData& device_data)
{
   const auto& pixel_shaders = device_data.native_pixel_shaders;
   const auto& vertex_shaders = device_data.native_vertex_shaders;
   return FindShader(pixel_shaders, "SMAA Edge Detection PS"_h) && FindShader(pixel_shaders, "SMAA Blending Weight Calculation PS"_h) &&
          FindShader(pixel_shaders, "SMAA Neighborhood Blending PS"_h) && FindShader(vertex_shaders, "SMAA Edge Detection VS"_h) &&
          FindShader(vertex_shaders, "SMAA Blending Weight Calculation VS"_h) && FindShader(vertex_shaders, "SMAA Neighborhood Blending VS"_h);
}

// Persisted setting widgets: the control, its ini save, a tooltip (also shown while disabled) and Core's reset button.
// Returns whether the value was saved (the edit ended, or it was reset).
static bool DrawSettingSlider(const char* label, const char* key, float* value, float default_value, float min_value, float max_value, const char* tooltip, const char* format = "%.3f", ImGuiSliderFlags flags = 0)
{
   ImGui::SliderFloat(label, value, min_value, max_value, format, flags);
   // Saved once the edit ends, not on every drag step
   bool save = ImGui::IsItemDeactivatedAfterEdit();
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("%s", tooltip);
   }
   save |= DrawResetButton<float, false>(*value, default_value, key);
   if (save)
   {
      reshade::set_config_value(nullptr, NAME, key, *value);
   }
   return save;
}

// A bool shown as a checkbox. "forced": shown as "forced_value" and disabled while another feature replaces it, keeping the saved choice.
static void DrawSettingCheckbox(const char* label, const char* key, bool* value, bool default_value, const char* tooltip, bool forced = false, bool forced_value = false)
{
   bool shown = (forced ? forced_value : *value);
   ImGui::BeginDisabled(forced);
   if (ImGui::Checkbox(label, &shown))
   {
      *value = shown;
      reshade::set_config_value(nullptr, NAME, key, *value);
   }
   ImGui::EndDisabled();
   if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("%s", tooltip);
   }
   if (forced)
   {
      DrawResetButton<bool, false>(shown, shown, key); // Only its spacer, the saved choice can't be reset from here
   }
   else
   {
      DrawResetButton(*value, default_value, key);
   }
}

// Core hides its display settings while its display composition is disabled (see "force_disable_display_composition"), but the HDR fix
// reads the display mode, peak and paper whites. This switches the display mode as Core's own Display Mode slider does: HDR (optionally
// turning the display's HDR on) loads the saved peak and paper whites, SDR sets them to the SDR white level. A subset of Core's: no
// link to the OS reference white, no HDR10+ GAMING switch, no SDR in HDR mode. Kept with composition disabled because in SDR mode
// Core's composition always draws its gamma correction on this game's non-linear swapchain.
static void ChangeDisplayMode(DeviceData& device_data, DisplayModeType display_mode, bool enable_hdr_on_display)
{
   reshade::set_config_value(nullptr, NAME, "DisplayMode", int(display_mode));
   cb_luma_global_settings.DisplayMode = display_mode;
   OnDisplayModeChanged();
   if (display_mode == DisplayModeType::HDR)
   {
      if (enable_hdr_on_display)
      {
         Display::SetHDREnabled(game_window);
         bool hdr_supported = false;
         Display::IsHDRSupportedAndEnabled(game_window, hdr_supported, hdr_enabled_display, device_data.GetMainNativeSwapchain().get());
      }
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
   device_data.cb_luma_global_settings_dirty = true;
}

// Core's display settings (hidden by "force_disable_display_composition"): the display mode, and the HDR fix's peak and paper whites
static void DrawDisplaySettings(DeviceData& device_data)
{
   int display_mode = (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR ? 1 : 0);
   ImGui::BeginDisabled(!hdr_supported_display);
   if (ImGui::SliderInt("Display Mode", &display_mode, 0, 1, display_mode_preset_strings[display_mode], ImGuiSliderFlags_NoInput))
   {
      ChangeDisplayMode(device_data, DisplayModeType(display_mode), true);
   }
   ImGui::EndDisabled();
   if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   {
      ImGui::SetTooltip("Display Mode. Greyed out if HDR is not supported.\nHDR also needs the game's own HDR option on.");
   }
   // Shown when the mode doesn't match the display's HDR: matches the mode to the display, without toggling the display
   if (DrawResetButton<int, false>(display_mode, hdr_enabled_display ? 1 : 0, "DisplayMode"))
   {
      ChangeDisplayMode(device_data, DisplayModeType(display_mode), false);
   }
   if (cb_luma_global_settings.DisplayMode != DisplayModeType::HDR)
   {
      return;
   }
   ImGui::BeginDisabled(!IsModActive(device_data));
   // Saved as 0 at the default, as Core does: it then follows the display's peak
   if (DrawSettingSlider("Scene Peak White", "ScenePeakWhite", &cb_luma_global_settings.ScenePeakWhite, device_data.default_user_peak_white, 400.f, 10000.f,
          "Set this to the brightest nits value your display (TV/Monitor) can emit.\nDirectly calibrating in Windows is suggested.", "%.f") &&
       cb_luma_global_settings.ScenePeakWhite == device_data.default_user_peak_white)
   {
      reshade::set_config_value(nullptr, NAME, "ScenePeakWhite", 0.f);
   }
   DrawSettingSlider("Scene Paper White", "ScenePaperWhite", &cb_luma_global_settings.ScenePaperWhite, default_paper_white, srgb_white_level, 500.f,
      "The \"average\" brightness of the game scene.\nHigher does not mean better, the brighter the image is, the lower the dynamic range (contrast) is.", "%.f");
   DrawSettingSlider("UI Paper White", "UIPaperWhite", &cb_luma_global_settings.UIPaperWhite, default_paper_white, srgb_white_level, 500.f,
      "The peak brightness of the User Interface.\nHigher does not mean better, change this to your liking.", "%.f");
   ImGui::EndDisabled();
}

// TAA color-resolve CS, the DLSS/FSR injection point. The game ships it as 4 logic variants x 2 tile sizes (32x16 for
// warp-32/NVIDIA, 8x8 for wave-64/AMD and Intel) and picks the tile by GPU at runtime, so each vendor dispatches another hash
// of the same pass (each pair is instruction-identical, e.g. 0x70E49B83 (8x8) == 0xD7E13B2A (32x16)): the whole set is hooked,
// one perm alone silently no-ops SR on the other vendors. Wider variants with extra t5/t6 SRVs and u4/u5 UAVs aren't hooked
// until confirmed to be this resolve, not a temporal SSR/AO pass.
static const ShaderHashesList shader_hashes_taa_resolve = {
   .compute_shaders = {
      0xD7E13B2A,
      0x70E49B83, // variant A  (32x16 / 8x8)
      0xFE348A4C,
      0x174F06D1, // variant B
      0x789DECF6,
      0x3D06A19E, // variant C
      0x960B6C89,
      0x1986BDD0, // variant D
      // E family: the same quality ladder without the t2 mask input and u0 mask output (96% identical body, tile twins
      // identical), used by the main menu and loading background. Its I/O is a subset of A-D (t0/t1/t3/t4 in, u2/u3 out).
      0x1C0D65CC,
      0x3DD7FD62, // variant E-A (32x16 / 8x8)
      0x40918D68,
      0xCEF1D745, // variant E-B
      0xCFB58943,
      0xA1AA9997, // variant E-C
      0x2F3D250F,
      0x633550DF, // variant E-D
   },
};
// DOF variant of the resolve (dialogue, cutscenes): it also filters the DOF CoC temporally (t5/t6 r16f current and history in,
// u4/u5 out; the DOF setup CS reads u4 right after). Cancelling it would leave u4/u5 stale and break the bokeh, so it runs
// natively first and SR then overwrites only its u2/u3 color.
static const ShaderHashesList shader_hashes_taa_resolve_dof = {
   .compute_shaders = {
      0x42871661,
      0xA280FBF8, // variant DOF-A (32x16 / 8x8)
      0x6514D8F7,
      0x34C459FC, // variant DOF-B
      0x631EF4A0,
      0xEBA90095, // variant DOF-C
      0x65882783,
      0x3BEAC6F3, // variant DOF-D
      // E + DOF: the E family with the CoC filter (no t2 mask / u0), never seen drawn; found by its bindings.
      // Same handling: its t0/t1/t3/u2/u3 are the ones SR reads and writes.
      0x86CD4399,
      0xAD22261B, // (32x16 / 8x8)
      0xF17DBBA3,
      0x44EF3967,
      0x814F8264,
      0x275B1D73,
      0x233592F5,
      0xDF0033AB,
   },
};

static constexpr uint32_t kFXAAHash = 0x5B81D1F2;    // FXAA PS, replaced with SMAA
static constexpr uint32_t kGbufferVS_A = 0xC089424D; // gbuffer VS that binds the main camera CB at VS slot 2
static constexpr uint32_t kGbufferVS_B = 0xFF93953D; // (reliable jitter capture point)
// PS that turns the D24 reverse-Z depth into the game's linear view depth (r32_float, metres; near / device Z), the SMAA
// predication source. Drawn twice per frame, in gameplay and the main menu alike; the second write is the final depth.
static constexpr uint32_t kLinearDepthHash = 0xDE1C9EB9;
// Native AO ("HBAO Full" = interleaved GTAO, all before the resolve, at the render size): the deinterleave splits depth (times
// cb0[5].x) and normals into 4x4 layers of a quarter size, the horizon pass traces them and writes the full-size AO target
// (r8g8b8a8: .x AO, .yzw bent normal, read by the tiled lighting), then a depth-aware blur X into a temporary and Y back.
static constexpr uint32_t kAODeinterleaveHash = 0xAFD03D17;
static constexpr uint32_t kAOHorizonHash = 0x3DC1C671;
static constexpr uint32_t kAOBlurXHash = 0xA93EB2C5;
static constexpr uint32_t kAOBlurYHash = 0xC4844743;
// HBAO (the menu's non-Full HBAO): the same blurs at half size after a depth-only deinterleave and its own horizon (same
// interface), then a depth-aware 2x upsample into the engine's AO target
static constexpr uint32_t kAOHalfDeinterleaveHash = 0xBC7C85BE;
static constexpr uint32_t kAOHalfHorizonHash = 0x758910F2;
static constexpr uint32_t kAOUpsampleHash = 0x4B94634E;
// SSAO: pixel shaders at half size (the AO, a horizontal and a vertical blur), then the same kind of upsample (R8)
static constexpr uint32_t kSSAOHash = 0xF93F1662;
static constexpr uint32_t kSSAOBlurXHash = 0xB654AC8B;
static constexpr uint32_t kSSAOBlurYHash = 0x0722BC95;
static constexpr uint32_t kSSAOUpsampleHash = 0x795E2F51;
enum class AOMode : uint8_t
{
   HBAOFull,
   HBAO, // The menu's non-Full HBAO
   SSAO
};
constexpr uint8_t AOModeBit(AOMode mode)
{
   return uint8_t(1u << uint8_t(mode));
}
// The passes "RunOutputSizedAOPass" runs at the output size: the modes each belongs to (the CS blurs serve both HBAO modes),
// the mode it starts (it decides for the frame) and the modes it ends (then scaled down into the engine's AO target)
struct OutputSizedAOPass
{
   uint32_t hash = 0;
   uint8_t modes = 0;
   uint8_t first_of = 0;
   uint8_t last_of = 0;
   UINT cb_rows = 0; // Its dcl_constantbuffer size
   bool pixel = false;
   uint32_t runs_as = 0; // A quality permutation with the interface and constants of this pass (else its own hash)
};
static constexpr uint8_t kHBAOFullBit = AOModeBit(AOMode::HBAOFull);
static constexpr uint8_t kHBAOBit = AOModeBit(AOMode::HBAO);
static constexpr uint8_t kSSAOBit = AOModeBit(AOMode::SSAO);
static constexpr OutputSizedAOPass kOutputSizedAOPasses[] = {
   {.hash = kAODeinterleaveHash, .modes = kHBAOFullBit, .first_of = kHBAOFullBit, .cb_rows = 24},
   {.hash = kAOHorizonHash, .modes = kHBAOFullBit, .cb_rows = 8},
   {.hash = kAOBlurXHash, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 2},
   {.hash = kAOBlurYHash, .modes = kHBAOFullBit | kHBAOBit, .last_of = kHBAOFullBit, .cb_rows = 2},
   {.hash = kAOHalfDeinterleaveHash, .modes = kHBAOBit, .first_of = kHBAOBit, .cb_rows = 6},
   {.hash = kAOHalfHorizonHash, .modes = kHBAOBit, .cb_rows = 8},
   {.hash = kAOUpsampleHash, .modes = kHBAOBit, .last_of = kHBAOBit, .cb_rows = 1},
   {.hash = kSSAOHash, .modes = kSSAOBit, .first_of = kSSAOBit, .cb_rows = 6, .pixel = true},
   {.hash = kSSAOBlurXHash, .modes = kSSAOBit, .cb_rows = 4, .pixel = true},
   {.hash = kSSAOBlurYHash, .modes = kSSAOBit, .cb_rows = 4, .pixel = true},
   {.hash = kSSAOUpsampleHash, .modes = kSSAOBit, .last_of = kSSAOBit, .cb_rows = 1},
   // Quality permutations (shader dump census): horizons with other step and direction counts, blurs with another kernel
   // (window 101 instead of 103), upsamples. The deinterleave lookalike 0x735582C6 (c0/c5.y read) stays native: a first pass
   // the table doesn't know leaves the whole frame's AO at the render size.
   {.hash = 0x8F27ABBC, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 8, .runs_as = kAOHorizonHash},
   {.hash = 0xE6B00EB6, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 8, .runs_as = kAOHorizonHash},
   {.hash = 0xCD9F40EA, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 8, .runs_as = kAOHorizonHash},
   {.hash = 0x3CDF3475, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 8, .runs_as = kAOHorizonHash},
   {.hash = 0x26724A38, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 2, .runs_as = kAOBlurXHash},
   {.hash = 0xC161C7BF, .modes = kHBAOFullBit | kHBAOBit, .cb_rows = 2, .runs_as = kAOBlurXHash},
   {.hash = 0x91E7D397, .modes = kHBAOFullBit | kHBAOBit, .last_of = kHBAOFullBit, .cb_rows = 2, .runs_as = kAOBlurYHash},
   {.hash = 0xCB371E7D, .modes = kHBAOFullBit | kHBAOBit, .last_of = kHBAOFullBit, .cb_rows = 2, .runs_as = kAOBlurYHash},
   {.hash = 0x4E82236F, .modes = kHBAOBit | kSSAOBit, .last_of = kHBAOBit | kSSAOBit, .cb_rows = 1, .runs_as = kAOUpsampleHash},
   {.hash = 0x7DF12F5D, .modes = kHBAOBit | kSSAOBit, .last_of = kHBAOBit | kSSAOBit, .cb_rows = 1, .runs_as = kAOUpsampleHash},
};
// Tonemap PS (a 33^3 LUT after a PQ shaping or a 32^3 one after an SDR shaping, also writes the luma for FXAA): its RT0 is
// display-encoded and bounded, the FXAA pass' input, and only UI and the final encode follow it. RCAS runs on it under DLSS/FSR;
// upscaling, it draws at the output size. Every permutation in the shader dump (census: the LUT shaping and the luma): {PQ | 4
// SDR shapings} x {luma at RT1 | in RT0's alpha} x post quality x {distortion warp | chromatic aberration} x {radial lens warp} x
// {film grain | screen overlay}; the bloom is t2 or t3.
static const ShaderHashesList shader_hashes_tonemap = {
   .pixel_shaders = {0xB6A91712, 0x376C116B, 0xE3D57A10, 0xEB91AB31, 0x339025EE, 0x71562FF9, 0x18AC2B1A, 0x66BE1F36, 0x18F31608, 0xF8A12BF4, 0x62D3752D, 0xA42A680A,
      0x0C6BB88C, 0x0FEEC7CA, 0x13AD94B8, 0x23293C1D, 0x312C9FC2, 0x48148C49, 0x6071CFCE, 0x71677DB7, 0x75D88114, 0x77867D5B, 0x7DF2AAD4, 0x85A01ECD,
      0x87E726AA, 0x95DED713, 0xA4484C9D, 0xAFAA1401, 0xB7063CB6, 0xD4DAE5C3, 0xE60E2385, 0xF96E5DEC, 0x07CB39D2, 0x0C1A7388, 0x16C8E34C, 0x38A6FC4F,
      0x39DAD2F8, 0x3F438316, 0x513EBF50, 0x53AE0ABA, 0x550BDE9C, 0x60876A69, 0x678EC571, 0x6E3E7622, 0x701B6924, 0x8C333515, 0x8D596A79, 0x91DD4F7C,
      0x93F57093, 0x96A4573A, 0x97AF86B4, 0x992597D0, 0xA26DC150, 0xA304AFDF, 0xA4123D0B, 0xA822C123, 0xAEBDCDE5, 0xB1530A4B, 0xC4CA5450, 0xCBD6D4ED,
      0xD7108AE9, 0xE22BF9E4, 0xF1B20524, 0xF2CDD044,
      // The same 64 with an SDR shaping into a 32^3 LUT (filmic + sRGB, gamma 2.2, two log curves) instead of the PQ one
      0x006F5E61, 0x0072F8D2, 0x007F73D3, 0x014028A1, 0x0409DC30, 0x0448B213, 0x04960F36, 0x05DF24FA, 0x0693A91F, 0x0AE79C24, 0x0B939EF3, 0x0BAB9305,
      0x0D7028AA, 0x0E9CB7D7, 0x0FF4B088, 0x1255DA1F, 0x1259F9C1, 0x1373156D, 0x1395D6D1, 0x163DC793, 0x163FB625, 0x164B85CB, 0x16A04764, 0x187B575F,
      0x18FAAC0B, 0x1A18A319, 0x1AF6CAD6, 0x1B7ABE73, 0x1B7F5F65, 0x1B9502CF, 0x1CC82BE5, 0x1D04C3F5, 0x1D22B4EC, 0x1D4AA5C0, 0x1D87AD42, 0x1F2CFB3A,
      0x20165658, 0x2112B3E5, 0x218F4E00, 0x22E3FF9F, 0x23D93C93, 0x246CBCF0, 0x25F130CC, 0x27E283A0, 0x2AC1030E, 0x2C0EF2BD, 0x2C783D46, 0x2DDD0509,
      0x2F68CBB3, 0x2FAD5D31, 0x2FBDD104, 0x31DCCDC2, 0x3221BB38, 0x3226F201, 0x3284B166, 0x328BBB59, 0x32B001FB, 0x331BF91B, 0x33FE4872, 0x341A41EB,
      0x35A0C016, 0x363356AF, 0x3756D9A2, 0x384B721B, 0x39AFF254, 0x3A9F303E, 0x3AD0F8E7, 0x3BCC0F88, 0x3CD6CB86, 0x3E164067, 0x3E1F14E6, 0x3E2F754B,
      0x3E77ABE2, 0x40073197, 0x4111126F, 0x42AE611A, 0x43E36203, 0x442A9940, 0x4565FFD4, 0x45DFAC51, 0x461BDB68, 0x464E0FEE, 0x4689F332, 0x46A1FC8E,
      0x47055661, 0x49D9C7EB, 0x49FD82E9, 0x4A2B141C, 0x4A3B3A69, 0x4B0FE250, 0x4DDCA16D, 0x4EB1F1EE, 0x4FE81586, 0x501E1E73, 0x5320F05D, 0x533CB37E,
      0x53F6C6AD, 0x540B4A6F, 0x54304EE6, 0x5447EEEC, 0x562A2D15, 0x56F10E1A, 0x57918A44, 0x58DE7D97, 0x58F851B7, 0x593480E2, 0x59CA307B, 0x5CA9D777,
      0x5E2E55DE, 0x5F3762CF, 0x62A7DEAA, 0x63CE07A8, 0x6500E605, 0x658741C4, 0x66616248, 0x69F90409, 0x6BC5274C, 0x6BD139DF, 0x6CCF3706, 0x6D027FAD,
      0x6E18FA92, 0x6E401C44, 0x6F9CD761, 0x6FD4584F, 0x70103F5B, 0x714A20B2, 0x725E5ACE, 0x754E8427, 0x75556D1D, 0x774B9E93, 0x787B368E, 0x7A75D35A,
      0x7A84A689, 0x7E291A9A, 0x86428251, 0x894220F0, 0x89993B33, 0x89AAB1DC, 0x8A1C958B, 0x8B500F0A, 0x8B674872, 0x8B8CFA8D, 0x8BFC8417, 0x8C5D3712,
      0x8D173FA8, 0x8D24A337, 0x8E610E3C, 0x8F540120, 0x8F6BD040, 0x8F777225, 0x907173D1, 0x93C2681F, 0x970E289A, 0x977943EC, 0x97846417, 0x983FF1AF,
      0x98A6D78D, 0x99C44C64, 0x9B851419, 0x9CB6B59B, 0x9D587860, 0x9EFB0918, 0x9F994B58, 0xA04A5313, 0xA0754060, 0xA09F94D0, 0xA11E6DB1, 0xA22B149A,
      0xA76F5420, 0xA7B31C2A, 0xA8143FB3, 0xA8C66DC2, 0xA9E54F1B, 0xAAEB94C1, 0xABA5FE8A, 0xAC92E0FF, 0xADD03661, 0xAF8860FB, 0xB0BC18E6, 0xB0C1E8BB,
      0xB18789C7, 0xB1F188E6, 0xB511F77D, 0xB576F538, 0xB82254E5, 0xB9C13DB0, 0xB9EB4190, 0xBA10D64E, 0xBB8080A4, 0xBC520955, 0xBC9D58A1, 0xBD5A5513,
      0xBF36C8DD, 0xC02B7D37, 0xC0583078, 0xC1A0AAA0, 0xC260204C, 0xC2AD3993, 0xC2C2DCFD, 0xC5B31BC0, 0xC600F967, 0xC678D3B7, 0xC7F37E1E, 0xC836ED3F,
      0xC84396A4, 0xC85C3B19, 0xC955FB2C, 0xCC4BA163, 0xCD3B5BF7, 0xCFCC047A, 0xD02F5FA7, 0xD0C112CD, 0xD187D2D0, 0xD19AEB12, 0xD241423B, 0xD49BAAAE,
      0xD54F22CA, 0xD63AC035, 0xD83047A3, 0xD84FB858, 0xD97D0DC0, 0xDB056906, 0xDB4826F1, 0xDB952F3E, 0xDC61AD05, 0xDD6A8CDD, 0xDDF2A9DA, 0xDFCC1B96,
      0xE0C7A8D4, 0xE2C767FB, 0xE3CBFE7C, 0xE3CEE786, 0xE534333E, 0xE5641232, 0xE740CBA8, 0xE9295138, 0xE9DA3A76, 0xEB91D898, 0xEC13C15C, 0xEC644E95,
      0xECA15A18, 0xED829F26, 0xEE2DDF04, 0xEE5797B9, 0xEEB91C1B, 0xEFBB961B, 0xF156264A, 0xF222DD6D, 0xF286116E, 0xF6E6D93A, 0xF6F5D783, 0xF730E758,
      0xF988DD5A, 0xFBAB6080, 0xFBB7739A, 0xFE1211CB},
};
// Motion blur, upscaling: the whole chain runs at the output size (see "DrawOutputSizedMotionBlurPass"). Tile max passes (a
// column and a row pass per stage, one or two stages by resolution; the first stage reads the motion vectors), the tile
// neighbourhood, the velocity buffer, then the gather: {20 px tiles} x {step per tap} x {Karis weights}, the 20 px permutations replaced
// to read the tile size from cb0[0].y like the others (see "MotionBlur_0x04678ABA_0x05953C5C_0x812EA2D8_0x9D5D747F").
static constexpr uint32_t kMBTileColumnsFromMVHash = 0xEF28CAAC;
static constexpr uint32_t kMBTileRowsFromMVHash = 0x4C0E6DE6;
static constexpr uint32_t kMBTileColumnsHash = 0x47305294;
static constexpr uint32_t kMBTileRowsHash = 0xFDCCD82A;
static constexpr uint32_t kMBTileNeighbourhoodHash = 0x63700321;
static constexpr uint32_t kMBVelocityHash = 0x2DDB2655;
static const ShaderHashesList shader_hashes_motion_blur = {
   .pixel_shaders = {kMBTileColumnsFromMVHash, kMBTileRowsFromMVHash, kMBTileColumnsHash, kMBTileRowsHash, kMBTileNeighbourhoodHash, kMBVelocityHash, 0xB5AEF3E2, 0x812EA2D8, 0x5DADB8F2,
      0x04678ABA, 0xF502F7C9, 0x05953C5C, 0x298A15CE, 0x9D5D747F},
};
// Depth of field (Frostbite sprite bokeh), upscaling: the whole chain after the resolve runs at the output size (see
// "DrawOutputSizedDoFPass"). A small-CoC blur along x then y, the sprite build into append buffers, the sprite binning, the bokeh
// sprites into layers, the layer upsamples, the composite. Every permutation in the shader dump (opcode census).
// The blur: one row per axis and window border (the next power of two of its radius, cb0[3].x, which the engine keeps at every
// resolution: the native 4K one is the 1080p one)
struct DoFBlurPass
{
   uint32_t hash;
   bool columns;
};
static constexpr DoFBlurPass kDoFBlurPasses[] = {
   {0x25A74FB4, false},
   {0xC5A152D9, false},
   {0xFB0AE8E9, false},
   {0xDB44B3C5, false},
   {0x282B5286, false},
   {0xF3FDCB87, false},
   {0xAE03BF02, false},
   {0xEDEFF8A4, false},
   {0xA76DB70F, true},
   {0x49A9B7F9, true},
   {0x9CBA3C1D, true},
   {0x15E9C5A5, true},
   {0x0E052B80, true},
   {0xCCB52708, true},
   {0x6661DBE1, true},
   {0x52C75ECC, true},
};
// The sprite build: 6 or 3 buckets, with or without the far layers' overflow bucket (u7), at full or half resolution ("half": a
// thread per 2x2 pixels, with a depth input t1 for the 2x2 reduction), with or without the 2x2 sprite merge (group shared memory)
struct DoFSpriteBuildPass
{
   uint32_t hash;
   bool half;
};
static constexpr DoFSpriteBuildPass kDoFSpriteBuildPasses[] = {
   {0x1B4FC37F, false},
   {0x381D24CA, false},
   {0x8E607FE0, false},
   {0x8EB3F435, true},
   {0x5F5C22F7, true},
   {0xD9BD89E1, true},
   {0x8ECC3E07, false},
   {0x52FDC5EA, false},
   {0xAB8003D0, false},
   {0x048A6A90, true},
   {0xEBA9BB3B, true},
   {0x0E6B40E3, true},
};
static constexpr uint32_t kDoFSpriteBinHash = 0x82B58778;
static constexpr uint32_t kDoFSpriteHash = 0xEBF62A30;
static constexpr uint32_t kDoFLayerResampleHash = 0x30F1A5E7; // Also the bloom's 1x1: a layer resample only between the sprites and the composite
// The composite, by the layers it reads (near t12, far array t10, the extra layers t4/t5/t7/t8)
static const ShaderHashesList shader_hashes_dof_composite = {
   .pixel_shaders = {0x4E197292, 0xBF21B4E5, 0x212369F4, 0xCE52ACCB, 0x69A9A8E5, 0x45A42EF5, 0x967D9C96, 0x44AF10E2},
};
// Bloom, upscaling: the whole chain at the output size (see "BuildOutputSizedBloom"). The engine blurs the post chain's last target
// into that target's own mips, each level a column pass from the previous mip into a temporary then a row pass into the mip: the
// first level by its own pair, the next ones by a variant per axis, a fixed tap halving or any ratio (weights from cb0[1].xy). Then
// the last mip into a 1x1 ("kDoFLayerResampleHash") and an upsample per mip up to mip 1, the tonemap's t3.
static constexpr uint32_t kBloomPyramidHashes[] = {
   0x643373FF, // The first level's columns
   0x836ADD97, // The first level's rows
   0xC8442A34, // Columns, halving
   0xE87BB0B1, // Rows, halving
   0xC705D213, // Columns, any ratio
   0xC00AAFAD, // Rows, any ratio
};
static constexpr uint32_t kBloomUpsampleHash = 0x8744747A; // mip * cb0[3] + previous * cb0[4], cb0[3] = 1 / the upsample count
// The scanner overlay, upscaling: at the output size (see "GetOutputSizedDepthStencil"). Into a render-sized RGBA8 overlay (the
// tonemap's t3, point sampled): the scanned objects' meshes (depth test against the scene's depth, the outline marking stencil bit
// 128, the fill testing it; the aimed object's fill is another permutation), then their glow: a fill where the scene's stencil
// bit 1 is set, a blur (see "kPostBlurPasses"), added back where the stencil is clear ("kDoFLayerResampleHash").
static const ShaderHashesList shader_hashes_scanner = {
   // Outline, fill, the aimed object's fill, glow mask, then the fills of the other highlight colors
   .pixel_shaders = {0xB73F1E71, 0x19BB0BE5, 0xC3C08F2B, 0x2EF4225E, 0x7E05C06D, 0x91B007B8, 0xF764A98B},
};
// Frostbite's separable blurs (shader dump census), one axis each: c0.xy the texel size, the taps at a multiple of it or at uv
// offsets in the .x / .z of the next rows ("offsets", a bit per row and component from c1.x: x, z, x, z...), the weights in .y /
// .w. The engine keeps their px at every resolution (the scanner's glow: -1.458 / 0.486 px at 0.5 and 1.0), so at the output
// size the texel size and the offsets follow the output's texels.
struct PostBlurPass
{
   uint32_t hash;
   bool columns;
   uint16_t offsets;
};
static constexpr PostBlurPass kPostBlurPasses[] = {
   {0x34F810BB, true, 0b1},
   {0xE0D762DA, false, 0b1},
   {0xC5688D54, true, 0b11},
   {0xDB18CC73, false, 0b11},
   {0xA6A01019, true, 0b111},
   {0x60592A06, false, 0b111},
   {0xB12E894F, true, 0b1111},
   {kSSAOBlurYHash, false, 0b1111}, // Also the SSAO blur, before the upscaler
   {0xC363180A, true, 0b1111111},
   {0x413B1833, false, 0b1111111},
   {0xDFF23CB3, true, 0b111111111111111},
   {0x54497978, false, 0b111111111111111},
   {0x01742927, true, 0},
   {0x33BEEB32, false, 0},
   {0x533E5D8A, true, 0},
   {0x3C389EA3, false, 0},
   {0x809824ED, true, 0},
   {0x031BB878, false, 0},
   {kSSAOBlurXHash, true, 0}, // Also the SSAO blur, before the upscaler
   {0x0595EAF9, false, 0},
   {0x35692B77, true, 0},
   {0x8ACBB228, false, 0},
   {0xC04D7A74, true, 0},
   {0x806DCA42, false, 0},
};
// The pixel inputs of the post passes Luma maps to their twins (the depth of field composite reads t0..t12)
static constexpr UINT kPostMaxInputs = 13;
// DLSS far_plane stand-in: MEA's reverse-Z projection has no finite far plane, and DLSS only uses it to linearize depth.
static constexpr float kCamFar = 100000.f;
// Presents after which SMAA's and RCAS's resources go once they stopped running
static constexpr uint32_t smaa_idle_release_frames = 600;

// --- User settings (ReShade config: loaded in LoadConfigs, before the device exists, saved on UI change) ---
static constexpr bool kDefaultSmaaEnable = true;
static constexpr float kDefaultRcasSharpness = 0.f;       // RCAS on SMAA or DLSS/FSR, off by default
static constexpr bool kDefaultSmaaPredication = true;     // predicate SMAA on geometry, using the game's linear view depth
static constexpr float kDefaultSmaaPredTolerance = 0.02f; // plane deviation counted as a full edge, as a fraction of view depth
static constexpr bool kDefaultDisableTaaSharpening = false;
static constexpr bool kDefaultImproveTaaJitter = true;
// DEVELOPMENT only: both always on otherwise. The peak and paper white levels are Core's (ScenePeakWhite, ScenePaperWhite,
// UIPaperWhite).
static constexpr bool kDefaultHDRFix = true;
static constexpr bool kDefaultHDRGammaCorrection = true;
static constexpr bool kDefaultVideoAutoHDR = true;
static constexpr float kDefaultVideoAutoHDRBoost = 0.5f;
static constexpr bool kDefaultDithering = true;
static constexpr float kDefaultExposure = 1.f;
static constexpr float kDefaultContrast = 1.f;
static constexpr float kDefaultSaturation = 1.f;
static constexpr float kDefaultHighlightDechroma = 0.f;
static constexpr float kDefaultBloomIntensity = 1.f;
static constexpr float kDefaultVignetteIntensity = 1.f;
static constexpr float kDefaultFilmGrainIntensity = 1.f;
static constexpr float kDefaultChromaticAberrationIntensity = 1.f;
static bool g_smaa_enable = kDefaultSmaaEnable;
static float g_rcas_sharpness = kDefaultRcasSharpness;
static bool g_smaa_predication = kDefaultSmaaPredication;
static float g_smaa_pred_tolerance = kDefaultSmaaPredTolerance;
static bool g_disable_taa_sharpening = kDefaultDisableTaaSharpening;
static bool g_improve_taa_jitter = kDefaultImproveTaaJitter;
static bool g_hdr_fix = kDefaultHDRFix;
static bool g_hdr_gamma_correction = kDefaultHDRGammaCorrection;
static bool g_video_auto_hdr = kDefaultVideoAutoHDR;
static float g_video_auto_hdr_boost = kDefaultVideoAutoHDRBoost;
static bool g_dithering = kDefaultDithering;
static float g_exposure = kDefaultExposure;
static float g_contrast = kDefaultContrast;
static float g_saturation = kDefaultSaturation;
static float g_highlight_dechroma = kDefaultHighlightDechroma;
static float g_bloom_intensity = kDefaultBloomIntensity;
static float g_vignette_intensity = kDefaultVignetteIntensity;
static float g_film_grain_intensity = kDefaultFilmGrainIntensity;
static float g_chromatic_aberration_intensity = kDefaultChromaticAberrationIntensity;
static bool g_hide_ui = false; // Session-only, never persisted: a stuck "on" would look like a broken HUD
// The FMV YUV to RGB decode into the UI layer: in a frame it drew, the loading present composes the UI layer as a movie (RenoDX's
// MEA mod keys its video flag on the same hash). Set on any context, moved into LumaSettings on the immediate one.
static constexpr uint32_t kVideoDecodeHash = 0x7ED07F45;
static std::atomic<bool> g_video_decoded = false;
// DEV knob: Halton (2, 3) jitter while SR runs (else the game's vanilla correlated multi-jittered sequence)
static bool g_halton_jitter = true;
// DEV knob: the Halton phase count "Fix Native TAA Jitter" gives the game's own TAA. Measured on its resolve (static camera,
// vs the vanilla 389 point table), second scene: 4 phases flicker -37%, sharpness +42%; 8 -35%/+13%; 16 -25%/+10%.
static int g_halton_jitter_native_phases = 4;

// Frostbite's "WorldRender" settings container (reflected class WorldRenderSettings, 0x4E0 bytes). Field offsets from its
// TypeInfo field table. The TAA setup reads these every frame and rebuilds its jitter sequence when they change (the engine
// writes fields of this container at runtime itself), so writing them is the engine's own path, no code patch.
static constexpr size_t kWrsJitterCount = 0x2E4;          // uint32: sequence length (vanilla 389)
static constexpr size_t kWrsPostSharpeningAmount = 0x2E8; // float: the TAA resolve's sharpen, its cb0[19].w (vanilla 0.5)
static constexpr size_t kWrsJitterUseCmj = 0x478;         // bool: the jitter table below, else the engine's own Halton (2, 3) from index 0
#if DEVELOPMENT
// DEV permutation census: MCP knobs force engine modes (-1 = the game's), the counters show the engine's values. Enum values = the
// order of the enum member records in the memdump (verified live): PostProcessAAMode None 0, FxaaLow 1, FxaaMedium 2, FxaaHigh 3,
// FxaaCompute 4, FxaaComputeExtreme 5, Smaa1x 6, SmaaT2x 7, TemporalAA 8. (The AO method isn't here: it's GlobalPostProcessSettings,
// container "PostProcess", and the menu's AO quality is the AO preset values, not the method.)
static constexpr size_t kWrsPostProcessAntialiasingMode = 0x2E0; // enum PostProcessAAMode
static int g_dev_engine_aa_mode = -1;
static int g_dev_engine_resample_mode = -1;
static uint32_t g_dev_engine_aa_mode_value = 0;
static uint32_t g_dev_engine_resample_mode_value = 0;
#endif
struct WorldRenderSettingsValues
{
   uint8_t jitter_use_cmj = 0;
   uint32_t jitter_count = 0;
   float post_sharpening_amount = 0.f;
};
static uint8_t* g_world_render_settings = nullptr;                     // Render thread only (OnPresent)
static WorldRenderSettingsValues g_world_render_settings_vanilla = {}; // Valid while "g_world_render_settings" is set
static bool g_world_render_settings_rejected = false;                  // The container failed the layout check: not this build

// Frostbite's "Render" settings container (reflected class GameRenderSettings, 0x110 bytes): the engine's render scale, applied
// to the whole frame up to the tonemap from the next frame on (no reload). The in-game Resolution Scale writes the scale and
// Custom. While an upscaler draws, Luma's render scale replaces the user's; otherwise the container follows the game.
static constexpr size_t kGrsResolutionScaleGame = 0x20; // float: the scale, read in Custom mode only
static constexpr size_t kGrsResolutionScaleMode = 0x2C; // enum: Disabled 0, Auto720p 1, Auto900p 2 (vanilla), Auto1080p 3, Custom 4
// enum: the present's upscale filter. Point 0, Linear 1, Bicubic 2, Lanczos 3, LanczosSeparable 4, BicubicSharp 5 (vanilla),
// BicubicSharpSeparable 6. Upscaling, the present reads an output-sized image at the size it believes is the render size: a
// single bilinear tap at the pixel centre is the only filter that stays 1:1 then.
static constexpr size_t kGrsRenderScaleResampleMode = 0x78;
// float nits: the in-game HDR calibration ("HDR10 Peak Value"), the grade LUT's display mapping shoulder (10000 = none). The HDR
// fix rolls off to the display's peak itself, so it holds this at 10000 (else the highlights are compressed twice).
static constexpr size_t kGrsDisplayMappingHdr10PeakLuma = 0x8C;
static constexpr float kNoHdr10PeakNits = 10000.f;
static constexpr int kResolutionScaleModeCustom = 4;
static constexpr int kResampleModeLinear = 1;
struct GameRenderSettingsValues
{
   float resolution_scale_game = 1.f;
   int resolution_scale_mode = 0;
   int render_scale_resample_mode = 0;
};
static uint8_t* g_game_render_settings = nullptr;                    // Render thread only (OnPresent)
static GameRenderSettingsValues g_game_render_settings_vanilla = {}; // The game's own, re-read every present Luma's aren't in
static bool g_game_render_settings_written = false;                  // Luma's values are in the container
static bool g_game_render_settings_rejected = false;                 // The container failed the layout check: not this build
static float g_game_hdr10_peak_vanilla = kNoHdr10PeakNits;           // The user's calibration, kept while Luma's is in
static bool g_game_hdr10_peak_written = false;                       // Luma's peak is in the container

static constexpr float kMinRenderScale = 0.5f;
static float g_render_scale = 1.f; // "RenderScale": applies only while an upscaler draws

// --- Live dev knobs (DEV overlay and MCP, not saved). Default signs are the stable trail-free set:
// MV flip X+Y, jitter flip Y; jitter flip X shakes. ---
static bool g_mv_flip_x = true;      // -> MV X scale = -0.5*W
static bool g_mv_flip_y = true;      // -> MV Y scale = +0.5*H
static float g_mv_scale_mult = 1.f;  // 0.25..4
static bool g_jitter_flip_x = false; // jitter X = -clipX*0.5*W
static bool g_jitter_flip_y = true;  // jitter Y = +clipY*0.5*H (removes shimmer)
static bool g_mv_jittered = false;
static bool g_ao_output_size = true; // Upscaling: the native AO at the output size (see "RunOutputSizedAOPass")

#if DEVELOPMENT
// What the TAA and FXAA hooks did in a frame, for the MCP "luma_dev_values" tool ("last" = the last complete frame).
// MEA_COUNT also notes the outcome on the draw or dispatch in a running "luma_trace_capture".
struct FrameCounters
{
   uint32_t taa_resolves = 0;            // hooked TAA resolve dispatches
   uint32_t taa_resolves_deferred = 0;   // on a deferred context: SR can't run there, native TAA
   uint32_t dof_resolves = 0;            // DOF variant: native resolve run first, then SR
   uint32_t camera_gbuffer = 0;          // camera captured at the gbuffer VS
   uint32_t camera_scene_vs = 0;         // camera captured at another scene VS (main menu), keyed to the scene size
   uint32_t camera_probe = 0;            // camera found by the fallback probe at the resolve
   uint32_t camera_misses = 0;           // no camera at the resolve: native TAA
   uint32_t handoff_cs = 0;              // SR output copied through the game's UAVs (copy CS)
   uint32_t handoff_incompatible = 0;    // no hand-off into u2 (copy CS not ready, or u2 of another size): native TAA
   uint32_t u2_r11g11b10 = 0;            // u2 is r11g11b10_float (the game's alternate "Buffer Format")
   uint32_t sr_draws = 0;                // SR drew and replaced the resolve
   uint32_t sr_failures = 0;             // output creation or Draw failed: SR suppressed until re-picked
   uint32_t fxaa_draws = 0;              // the game's FXAA pass
   uint32_t smaa_draws = 0;              // SMAA replaced it
   uint32_t rcas_draws = 0;              // RCAS ran (on SMAA's output or the SR tonemap's)
   uint32_t tonemaps_upscaled = 0;       // upscaling: the tonemap drew at the output size from the upscaler's output
   uint32_t motion_blurs_upscaled = 0;   // upscaling: the motion blur gather drew at the output size
   uint32_t post_pass_failures = 0;      // upscaling: a post pass couldn't run at the output size (drawn natively)
   uint32_t dof_composites_upscaled = 0; // upscaling: the depth of field composite drew at the output size
   uint32_t bloom_pyramids_upscaled = 0; // upscaling: the bloom pyramid was built at the output size
   uint32_t blooms_upscaled = 0;         // upscaling: the bloom's last upsample drew at the output size
   uint32_t scanner_passes_upscaled = 0; // upscaling: scanner overlay passes drawn at the output size
   uint32_t post_blurs_upscaled = 0;     // upscaling: separable blurs (see "kPostBlurPasses") drawn at the output size

   uint32_t twin_draws = 0;                  // upscaling: unrouted draws into a target Luma drew at the output size, moved to its twin
   uint32_t structure_counts_redirected = 0; // upscaling: CopyStructureCount read a Luma append buffer
   uint32_t tonemaps_stretched = 0;          // upscaling: the tonemap drew natively (a render-sized pass ran first), stretched at the first read
   uint32_t tonemap_redirects = 0;           // upscaling: draws after the tonemap moved to Luma's output-sized target
   uint32_t ao_output_size_passes = 0;       // upscaling: native AO passes run at the output size
   uint32_t ao_output_size_aborts = 0;       // upscaling: a later AO pass failed, it and the rest skipped (stale AO)
};
static FrameCounters g_counters_this_frame;
static FrameCounters g_counters_last_frame;
#define MEA_COUNT(counter) (++g_counters_this_frame.counter)
#else
#define MEA_COUNT(counter) \
   do                      \
   {                       \
   } while (false)
#endif

struct MassEffectAndromedaGameDeviceData final : public GameDeviceData
{
   // --- Camera state captured once per frame from the per-view camera CB (CPU map pointer + bound offset) ---
   bool cam_valid_this_frame = false;
   // The CPU map pointers of the large dynamic CB rings this frame (see "OnMapBufferRegion"), read by bound offset with no GPU
   // readback. Mapped on both the immediate and Frostbite worker threads, hence the mutex. Reset each present.
   struct MapRec
   {
      uint64_t handle = 0;
      const void* data = nullptr; // nullptr = banned (seen DISCARD-mapped)
      uint64_t size = 0;
   };
   struct MapCache
   {
      std::shared_mutex mutex;
      static constexpr int kMaxMaps = 16;
      MapRec recs[kMaxMaps] = {};
      int count = 0;
#if DEVELOPMENT || TEST
      bool logged_full = false; // one-shot kMaxMaps cap warning
#endif

      // Caller holds "mutex"
      int Find(uint64_t handle) const
      {
         for (int i = 0; i < count; ++i)
         {
            if (recs[i].handle == handle)
               return i;
         }
         return -1;
      }
   };
   MapCache map_cache;
   // The immediate context's ID3D11DeviceContext1 (bound-range CB queries), queried once. Non-owning: the device owns its
   // immediate context for its whole lifetime.
   ID3D11DeviceContext1* immediate_context1 = nullptr;
   // Where the camera CB was last found by the resolve's probe, to avoid probing all 28 combos every frame
   struct CameraSlot
   {
      bool compute = false;
      UINT slot = 0;
   };
   std::optional<CameraSlot> cam_probe_slot;
   float cam_jitter_clip_x = 0.f; // raw [6].z
   float cam_jitter_clip_y = 0.f; // raw [7].z
   float cam_near = 0.06f;        // [8].w
   float cam_proj_m11 = 0.f;      // [7].y (vertical FOV for FSR)
   // The last hooked resolve's scene size: keys the camera capture at draws other than the gbuffer pass (0 = none yet)
   uint2 scene_size = {};

#if ENABLE_SR
   // --- SR (the output texture is Core's device_data.sr_output_color; DLSS/FSR write there, we copy into u2/u3) ---
   ComPtr<ID3D11ShaderResourceView> sr_output_srv; // t0 of the format-converting hand-off CS
   // A new output texture: DLSS needs its feature recreated after the first draw into it (see "ReplaceTAAResolve")
   bool sr_output_recreated = false;
   // No upscaler picked (see "CleanExtraSRResources"): Luma's upscaling resources go at the next present
   std::atomic<bool> release_sr_resources = false;
   // Latched at present: an upscaler is picked and hasn't failed. Fixed for the whole frame.
   bool sr_active = false;
   uint32_t taa_resolve_frame = UINT32_MAX; // The last frame with a TAA resolve SR can replace (the render scale needs one)

   // --- Upscaling (the upscaler drew above the render size): the frame from the tonemap on at the output size. The engine's
   // tonemap target stays render sized; Luma's "upscaled_scene" replaces it for every later draw that reads or writes it (the
   // letterbox bars, the present). All but the textures are this frame's, reset at present. ---
   // NVIDIA: every pass after the upscaler runs at the output size. An engine resource of this frame maps to its output-sized
   // "twin": u2 to the upscaler's output, the depth and motion vectors to point stretched copies, a redirected pass's target to
   // Luma's. Later passes read the twins; the tonemap draws from its scene's twin. None = not upscaling this frame.
   struct OutputTwin
   {
      ComPtr<ID3D11Resource> engine;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11Resource> luma;           // The twin's resource when Luma owns it (a target or buffer to write)
      ComPtr<ID3D11UnorderedAccessView> uav; // A buffer twin's append view, a texture twin's typed view
      bool engine_current = false;           // The engine resource has this frame's content too (a stretched or rescaled copy)
   };
   std::vector<OutputTwin> output_twins;
   // The redirected passes' targets and the depth of field's append buffers, reused by description across frames (deques: their
   // addresses stay valid). "in_use" is this frame's.
   struct PooledTarget
   {
      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11RenderTargetView> rtv;
      ComPtr<ID3D11UnorderedAccessView> uav;
      bool in_use = false;
   };
   std::deque<PooledTarget> output_targets;
   struct PooledBuffer
   {
      ComPtr<ID3D11Buffer> buffer;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11UnorderedAccessView> uav;
      UINT elements = 0;
      bool in_use = false;
   };
   std::deque<PooledBuffer> output_buffers;
   ComPtr<ID3D11Buffer> post_cb;    // The redirected passes' patched b0
   ComPtr<ID3D11Buffer> tonemap_cb; // The tonemap's b0 with the effect sliders (see "WrapTonemapEffects")
   // The motion blur's first tile stage stride this frame (engine, output)
   uint32_t mb_stride = 0;
   uint32_t mb_output_stride = 0;
   // The depth of field's layer upsamples run between its sprites and its composite this frame (the same PS also downsamples the
   // bloom from the composite's mips, which have a twin too)
   bool dof_layers_open = false;
   // A post pass failed at the output size: the rest of the frame's post runs natively (see "FallBackToRenderSize")
   bool post_fallback = false;
   // The bloom at the output size: the engine's pyramid shaders (by "kBloomPyramidHashes", kept from their first dispatch), Luma's
   // pyramid (every mip, a view of each) and this frame's engine target it stands in for (none = the bloom runs natively)
   ComPtr<ID3D11ComputeShader> bloom_shaders[std::size(kBloomPyramidHashes)];
   ComPtr<ID3D11Texture2D> bloom_pyramid;
   ComPtr<ID3D11ShaderResourceView> bloom_pyramid_srv;
   std::vector<ComPtr<ID3D11ShaderResourceView>> bloom_mip_srvs;
   std::vector<ComPtr<ID3D11UnorderedAccessView>> bloom_mip_uavs;
   ComPtr<ID3D11Resource> bloom_scene;
   // The scene's depth-stencil at the output size (see "GetOutputSizedDepthStencil"), the engine's it holds this frame (none = not
   // filled yet), and the resample's depth-stencil states (depth, then a stencil bit each)
   ComPtr<ID3D11Texture2D> output_depth_stencil;
   ComPtr<ID3D11DepthStencilView> output_depth_stencil_dsv;
   ComPtr<ID3D11Resource> output_depth_stencil_engine;
   ComPtr<ID3D11DepthStencilState> resample_depth_stencil_states[9];
   bool scanner_open = false; // The scanner's overlay drew at the output size this frame
#if DEVELOPMENT
   // The last depth of field composite's inputs (by slot) and output twins, for MCP readback ("dof.t<slot>", "dof.out")
   ComPtr<ID3D11ShaderResourceView> dof_debug_inputs[kPostMaxInputs];
   ComPtr<ID3D11ShaderResourceView> dof_debug_output;
#endif
   ComPtr<ID3D11Resource> tonemap_target; // The engine's tonemap target, once the tonemap drew
   // The tonemap drew into "upscaled_scene" from its scene's twin. Else (a render-sized pass ran between: DOF) it drew the
   // engine's target at the render size, stretched into "upscaled_scene" at the first read.
   bool tonemap_upscaled = false;
   bool upscaled_scene_filled = false;
   ComPtr<ID3D11Texture2D> upscaled_scene;
   ComPtr<ID3D11ShaderResourceView> upscaled_scene_srv;
   ComPtr<ID3D11RenderTargetView> upscaled_scene_rtv;

   void ReleaseUpscaledScene()
   {
      upscaled_scene_rtv.reset();
      upscaled_scene_srv.reset();
      upscaled_scene.reset();
      output_targets.clear();
      output_buffers.clear();
      post_cb.reset();
      ReleaseBloomPyramid();
      output_depth_stencil_dsv.reset();
      output_depth_stencil.reset();
   }

   void ReleaseBloomPyramid()
   {
      bloom_mip_uavs.clear();
      bloom_mip_srvs.clear();
      bloom_pyramid_srv.reset();
      bloom_pyramid.reset();
   }

   const OutputTwin* FindOutputTwinRecord(ID3D11Resource* engine) const
   {
      // The latest: a resource the engine reuses within the frame gets a new twin
      for (auto twin = output_twins.rbegin(); twin != output_twins.rend(); ++twin)
      {
         if (twin->engine.get() == engine)
            return &*twin;
      }
      return nullptr;
   }
   OutputTwin* FindOutputTwinRecord(ID3D11Resource* engine)
   {
      return const_cast<OutputTwin*>(std::as_const(*this).FindOutputTwinRecord(engine));
   }
   ID3D11ShaderResourceView* FindOutputTwin(ID3D11Resource* engine) const
   {
      const OutputTwin* const twin = FindOutputTwinRecord(engine);
      return twin ? twin->srv.get() : nullptr;
   }

   // --- Native AO at the output size while upscaling (see "RunOutputSizedAOPass"), all at "size" ---
   struct OutputSizedAO
   {
      uint32_t frame = UINT32_MAX;        // The frame its first pass ran at the output size: the later ones follow
      uint32_t broken_frame = UINT32_MAX; // A later pass failed that frame: the rest are skipped
      uint2 size = {};
      uint2 render_size = {};         // The engine's AO target size that frame
      AOMode mode = AOMode::HBAOFull; // HBAO/SSAO: the passes before the upsample run at "size" / 2
      // Inputs stretched (point) to the output size: linear depth (r32: the Full deinterleave's and blurs' t0, the upsample's
      // t1), normals and material ids (Full only)
      ComPtr<ID3D11Texture2D> depth, normals, material;
      ComPtr<ID3D11ShaderResourceView> depth_srv, normals_srv, material_srv;
      ComPtr<ID3D11RenderTargetView> depth_rtv, normals_rtv, material_rtv;
      // HBAO/SSAO only, at "size" / 2: the linear depth (the deinterleave's, SSAO's, blurs' and upsample's) and the AO before
      // the upsample
      ComPtr<ID3D11Texture2D> half_depth, half_result;
      ComPtr<ID3D11ShaderResourceView> half_depth_srv, half_result_srv;
      ComPtr<ID3D11RenderTargetView> half_depth_rtv, half_result_rtv;
      ComPtr<ID3D11UnorderedAccessView> half_result_uav;
      // The 16 deinterleaved layers (depth, normals; HBAO only), the AO target at the output size and the blur's temporary (at
      // the passes' size)
      ComPtr<ID3D11Texture2D> layer_depth, layer_normals, result, blur;
      ComPtr<ID3D11ShaderResourceView> layer_depth_srv, layer_normals_srv, result_srv, blur_srv;
      ComPtr<ID3D11RenderTargetView> blur_rtv;
      ComPtr<ID3D11UnorderedAccessView> layer_depth_uav, layer_normals_uav, result_uav, blur_uav;
      ComPtr<ID3D11Buffer> cb;              // A pass's constants, copied from the engine's and patched
      ComPtr<ID3D11Resource> engine_target; // The engine's AO target, and the view Luma scales the result down into
      ComPtr<ID3D11RenderTargetView> engine_target_rtv;
   };
   OutputSizedAO ao;
#endif

#if ENABLE_SMAA || ENABLE_SR
   // The game's linear view depth (the RT of "kLinearDepthHash", keyed by its resource): SMAA predication's source and the
   // half AO's depth. A capture from an earlier frame (the pass didn't draw) is not used.
   ComPtr<ID3D11Resource> scene_depth;
   ComPtr<ID3D11ShaderResourceView> srv_scene_depth;
   uint32_t scene_depth_frame = UINT32_MAX;
#endif

#if ENABLE_SMAA
   // --- SMAA (replaces the game's FXAA pass in FXAA AA mode) ---
   ComPtr<ID3D11Buffer> cb_smaa_metrics; // float4(1/W,1/H,W,H) at output res + float4(predication scale,0,0,0), at VS+PS b1
   uint2 smaa_metrics_size = {};
   float smaa_metrics_pred_scale = -1.f;
   // Predication: "scene_depth" turned into an edge-ness mask by the depth-extract CS
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_tolerance = -1.f;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   uint2 pred_size = {};

   void ReleasePredication()
   {
      cb_pred.reset();
      uav_pred.reset();
      srv_pred.reset();
      tex_pred.reset();
   }
   // RCAS's input: SMAA's output, or the tonemap's under DLSS/FSR (both display-encoded)
   ComPtr<ID3D11Texture2D> tex_rcas_input;
   ComPtr<ID3D11RenderTargetView> tex_rcas_input_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_rcas_input_srv;
   uint2 rcas_input_size = {};

   void ReleaseRCAS()
   {
      tex_rcas_input_rtv.reset();
      tex_rcas_input_srv.reset();
      tex_rcas_input.reset();
   }
   // The frames SMAA and RCAS last ran, for "smaa_idle_release_frames"
   uint32_t smaa_frame = 0;
   uint32_t sharpen_frame = 0;
#endif
};

// The game engages HDR with NVAPI's legacy "NvAPI_Disp_HdrColorControl", which only reaches the display in exclusive fullscreen.
// Core blocks that (flip model, borderless), so DWM composed the PQ backbuffer as sRGB. The call is swallowed and its mode
// becomes the swapchain's DXGI color space (the display's HDR stays with Windows).
namespace NvapiHdr
{
   constexpr uint32_t kHdrColorControlId = 0x351DA224; // nvapi_interface.h
   // ReShade's DXGI proxy stores itself in the native swapchain's private data under its class UUID (dxgi_swapchain.cpp)
   constexpr GUID kReShadeSwapchainProxy = {0x1f445f9f, 0x9887, 0x4c4c, {0x90, 0x55, 0x4e, 0x3b, 0xad, 0xaf, 0xcc, 0xa8}};
   using HdrColorControlFunc = NvAPI_Status(__cdecl*)(NvU32 display_id, NV_HDR_COLOR_DATA* hdr_color_data);
   static safetyhook::InlineHook hdr_color_control_hook;
   static std::atomic<int> requested_hdr = -1;   // -1 = the game never set it
   static bool display_hdr_enabled_once = false; // Render thread only (OnPresent)
   static std::shared_mutex native_swapchain_mutex;
   static IDXGISwapChain* native_swapchain = nullptr; // the game owns it, cleared on destroy

   static DXGI_COLOR_SPACE_TYPE ColorSpace(int hdr)
   {
      return (hdr != 0 ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
   }

   static NvAPI_Status __cdecl HdrColorControlDetour(NvU32 display_id, NV_HDR_COLOR_DATA* hdr_color_data)
   {
      if (hdr_color_data == nullptr || hdr_color_data->cmd != NV_HDR_CMD_SET)
      {
         return hdr_color_control_hook.original<HdrColorControlFunc>()(display_id, hdr_color_data);
      }
      const int hdr = (hdr_color_data->hdrMode != NV_HDR_MODE_OFF ? 1 : 0);
      if (requested_hdr.exchange(hdr) == hdr)
      {
         return NVAPI_OK;
      }
      com_ptr<IDXGISwapChain> swapchain;
      {
         const std::shared_lock lock(native_swapchain_mutex);
         swapchain = native_swapchain;
      }
      // Through ReShade's proxy, which re-inits its runtime (and so our "OnInitSwapchain", hence no lock held here) with the new
      // color space, else its effects keep the SDR one. Before the swapchain exists, "OnInitSwapchain" applies it.
      HRESULT hr = S_FALSE;
      IDXGISwapChain3* proxy = nullptr;
      UINT proxy_size = sizeof(proxy);
      if (swapchain != nullptr && SUCCEEDED(swapchain->GetPrivateData(kReShadeSwapchainProxy, &proxy_size, &proxy)) && proxy != nullptr)
      {
         hr = proxy->SetColorSpace1(ColorSpace(hdr));
      }
      reshade::log::message(SUCCEEDED(hr) ? reshade::log::level::info : reshade::log::level::warning,
         std::format("[MEA HDR] NvAPI_Disp_HdrColorControl set mode {} (swallowed), SetColorSpace1 hr 0x{:08X}", static_cast<int>(hdr_color_data->hdrMode), static_cast<uint32_t>(hr)).c_str());
      return NVAPI_OK;
   }

   // Off the loader lock only ("async" init): loads nvapi64.dll, absent without an NVIDIA driver
   static void Install()
   {
      const HMODULE nvapi = LoadLibraryW(L"nvapi64.dll");
      const auto query_interface = (nvapi != nullptr ? reinterpret_cast<void*(__cdecl*)(uint32_t)>(GetProcAddress(nvapi, "nvapi_QueryInterface")) : nullptr);
      void* const hdr_color_control = (query_interface != nullptr ? query_interface(kHdrColorControlId) : nullptr);
      if (hdr_color_control == nullptr)
      {
         return;
      }
      // Enabled once stored, so the detour never calls an empty trampoline
      auto hook = safetyhook::InlineHook::create(hdr_color_control, reinterpret_cast<void*>(&HdrColorControlDetour), safetyhook::InlineHook::StartDisabled);
      if (hook)
      {
         hdr_color_control_hook = std::move(*hook);
      }
      const bool installed = Hooks::SetEnabled(hdr_color_control_hook, true);
      reshade::log::message(installed ? reshade::log::level::info : reshade::log::level::warning, std::format("[MEA HDR] NvAPI_Disp_HdrColorControl hook installed: {}", installed).c_str());
   }

   static void Uninstall()
   {
      hdr_color_control_hook = {};
   }

   // Before ReShade's runtime init, which reads the color space from the SKID private data (as its proxy writes it)
   static void OnInitSwapchain(reshade::api::swapchain* swapchain)
   {
      auto* const native = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
      {
         const std::unique_lock lock(native_swapchain_mutex);
         native_swapchain = native;
      }
      const int hdr = requested_hdr.load();
      com_ptr<IDXGISwapChain3> native3;
      if (hdr < 0 || FAILED(native->QueryInterface(&native3)))
      {
         return;
      }
      const DXGI_COLOR_SPACE_TYPE color_space = ColorSpace(hdr);
      const HRESULT hr = native3->SetColorSpace1(color_space);
      if (SUCCEEDED(hr))
      {
         constexpr GUID SKID_SwapChainColorSpace = {0x18b57e4, 0x1493, 0x4953, {0xad, 0xf2, 0xde, 0x6d, 0x99, 0xcc, 0x5, 0xe5}};
         native3->SetPrivateData(SKID_SwapChainColorSpace, sizeof(color_space), &color_space);
      }
      reshade::log::message(SUCCEEDED(hr) ? reshade::log::level::info : reshade::log::level::warning, std::format("[MEA HDR] swapchain init: SetColorSpace1({}) hr 0x{:08X}", static_cast<int>(color_space), static_cast<uint32_t>(hr)).c_str());
   }

   static void OnDestroySwapchain(reshade::api::swapchain* swapchain, bool resize)
   {
      const std::unique_lock lock(native_swapchain_mutex);
      if (native_swapchain == reinterpret_cast<IDXGISwapChain*>(swapchain->get_native()))
      {
         native_swapchain = nullptr;
      }
   }
} // namespace NvapiHdr

class MassEffectAndromeda final : public Game
{
   static MassEffectAndromedaGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<MassEffectAndromedaGameDeviceData*>(device_data.game);
   }

   // A texture with a SRV, plus a RTV and/or a UAV when asked. False on any failure (the caller resets what was made).
   static bool CreateTexture(ID3D11Device* native_device, DXGI_FORMAT format, uint2 size, UINT bind_flags, ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv,
      ID3D11RenderTargetView** rtv = nullptr, ID3D11UnorderedAccessView** uav = nullptr, UINT array_size = 1)
   {
      const CD3D11_TEXTURE2D_DESC desc(format, size.x, size.y, array_size, 1, bind_flags);
      if (FAILED(native_device->CreateTexture2D(&desc, nullptr, texture)) || FAILED(native_device->CreateShaderResourceView(*texture, nullptr, srv)))
         return false;
      if (rtv && FAILED(native_device->CreateRenderTargetView(*texture, nullptr, rtv)))
         return false;
      return !uav || SUCCEEDED(native_device->CreateUnorderedAccessView(*texture, nullptr, uav));
   }

   static ID3D11DeviceContext1* GetImmediateContext1(ID3D11DeviceContext* immediate_context, MassEffectAndromedaGameDeviceData* gd)
   {
      if (!gd->immediate_context1)
      {
         ComPtr<ID3D11DeviceContext1> context1;
         if (SUCCEEDED(immediate_context->QueryInterface(context1.put())))
         {
            gd->immediate_context1 = context1.get();
         }
      }
      return gd->immediate_context1;
   }

#if ENABLE_SMAA
   // RCAS on a display-encoded pass (SMAA, or the tonemap under DLSS/FSR): the pass draws into "tex_rcas_input" and Core's
   // "DrawRCAS()" writes the pass' own target. False when a piece is missing (shaders still compiling, a failed creation): the pass
   // then draws straight to its target, or the image would be lost. True counts as an RCAS use for "smaa_idle_release_frames".
   static bool PrepareRCASInput(ID3D11Device* native_device, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd, uint2 size)
   {
      if (g_rcas_sharpness <= 0.f || !PrepareRCAS(native_device, device_data))
         return false;
      if (!gd->tex_rcas_input || gd->rcas_input_size != size)
      {
         gd->ReleaseRCAS();
         if (CreateTexture(native_device, DXGI_FORMAT_R16G16B16A16_FLOAT, size, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd->tex_rcas_input.put(),
                gd->tex_rcas_input_srv.put(), gd->tex_rcas_input_rtv.put()))
         {
            gd->rcas_input_size = size;
         }
      }
      if (!gd->tex_rcas_input_rtv || !gd->tex_rcas_input_srv)
         return false;
      gd->sharpen_frame = cb_luma_global_settings.FrameIndex;
      return true;
   }
#endif

   static bool CreateImmutableCB(ID3D11Device* device, const void* data, UINT size, ComPtr<ID3D11Buffer>* out)
   {
      out->reset();
      const CD3D11_BUFFER_DESC bd(size, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_IMMUTABLE);
      const D3D11_SUBRESOURCE_DATA initial_data = {.pSysMem = data};
      return SUCCEEDED(device->CreateBuffer(&bd, &initial_data, out->put()));
   }

   // The per-view camera CB bound at (cs, slot), read from its cached CPU map pointer: when valid, its jitter, projection and
   // near plane go into "gd". Validation: the signature [5] == (0, 0, 0, 1) and [9].z == -1; with "expected_size", [1].xy must
   // also match the render size (rejects other views). Without it the signature alone accepts (the gbuffer VS is reliably the
   // main view). Caller MUST hold gd->map_cache.mutex (shared is enough).
   static bool TryStoreCamera(ID3D11DeviceContext1* ctx1, MassEffectAndromedaGameDeviceData* gd, bool cs, UINT slot, const uint2* expected_size)
   {
      ID3D11Buffer* cb = nullptr;
      UINT fc = 0, nc = 0;
      if (cs)
      {
         ctx1->CSGetConstantBuffers1(slot, 1, &cb, &fc, &nc);
      }
      else
      {
         ctx1->VSGetConstantBuffers1(slot, 1, &cb, &fc, &nc);
      }
      if (!cb)
         return false;
      const uint64_t h = reinterpret_cast<uint64_t>(cb);
      const uint32_t off = fc * 16u;
      cb->Release(); // only the handle is needed, as the map cache key
      for (int i = 0; i < gd->map_cache.count; ++i)
      {
         const auto& rec = gd->map_cache.recs[i];
         if (rec.handle != h || rec.data == nullptr || (uint64_t)off + 160u > rec.size)
            continue; // need [off .. off+10 float4]; nullptr = banned (seen DISCARD-mapped, so the pointer is unsafe)
         const float* r = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(rec.data) + off);
         if (expected_size && (fabsf(r[1 * 4 + 0] - (float)expected_size->x) > 1.f || fabsf(r[1 * 4 + 1] - (float)expected_size->y) > 1.f))
            return false; // [1].xy == res
         if (fabsf(r[5 * 4 + 0]) > 1e-3f || fabsf(r[5 * 4 + 1]) > 1e-3f || fabsf(r[5 * 4 + 2]) > 1e-3f || fabsf(r[5 * 4 + 3] - 1.f) > 1e-3f)
            return false; // [5]==(0,0,0,1)
         if (fabsf(r[9 * 4 + 2] + 1.f) > 1e-2f)
            return false; // [9].z == -1 (reverse-Z m32)
         gd->cam_proj_m11 = r[7 * 4 + 1];
         gd->cam_jitter_clip_x = r[6 * 4 + 2];
         gd->cam_jitter_clip_y = r[7 * 4 + 2];
         gd->cam_near = r[8 * 4 + 3];
         gd->cam_valid_this_frame = true;
         return true;
      }
      return false;
   }

   // Caches the CPU map pointer of every large (>= 64 KB) WRITE_NO_OVERWRITE dynamic CB (the ring holding the camera and the
   // constants of the AO and post passes Luma rescales among them), deduped by handle: a NO_OVERWRITE allocation stays
   // committed, so reading after Unmap is safe. A DISCARD map bans a tracked handle (data = nullptr, record kept): the driver
   // may free the old allocation, and a buffer the game ever discard-maps isn't the always-NO_OVERWRITE ring, so caching it
   // again would re-arm a use-after-free (the wide camera slot probe crashed on it in menus, where UI CBs are DISCARD-cycled
   // every frame). OnDestroyResource drops the record, which also lifts the ban for a handle the runtime recycles.
   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      if (!device_data.game)
         return;
      auto& gd = GetGameDeviceData(device_data);
      const bool discard = access == reshade::api::map_access::write_discard;
      if (!discard && (access != reshade::api::map_access::write_only || data == nullptr || *data == nullptr))
         return; // NO_OVERWRITE (cached) or DISCARD (bans) only
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      D3D11_BUFFER_DESC bd = {};
      buffer->GetDesc(&bd);
      if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 || bd.Usage != D3D11_USAGE_DYNAMIC)
         return;
      if (bd.ByteWidth < 65536)
         return; // camera ring buffer is ~1MB; skip small per-draw CBs
      {
         // The ring maps the same pointer every time: nothing to write (nor for a banned handle), and no exclusive lock against
         // the readers
         const std::shared_lock read_lock(gd.map_cache.mutex);
         const int i = gd.map_cache.Find(resource.handle);
         if (i >= 0 && (gd.map_cache.recs[i].data == nullptr || (!discard && gd.map_cache.recs[i].data == *data)))
            return;
      }
      const std::unique_lock lock(gd.map_cache.mutex);
      if (const int i = gd.map_cache.Find(resource.handle); i >= 0) // dedupe by handle, keep the latest pointer
      {
         if (discard)
         {
            gd.map_cache.recs[i].data = nullptr; // ban, see the function's comment
         }
         else if (gd.map_cache.recs[i].data != nullptr) // banned handles stay banned until destroyed
         {
            gd.map_cache.recs[i].data = *data;
            gd.map_cache.recs[i].size = bd.ByteWidth;
         }
         return;
      }
      if (discard)
         return;
      if (gd.map_cache.count < MassEffectAndromedaGameDeviceData::MapCache::kMaxMaps)
      {
         gd.map_cache.recs[gd.map_cache.count++] = {.handle = resource.handle, .data = *data, .size = bd.ByteWidth};
      }
#if DEVELOPMENT || TEST
      else if (!gd.map_cache.logged_full) // cap hit: the camera handle may be dropped this frame (SR falls back to native TAA)
      {
         gd.map_cache.logged_full = true;
         reshade::log::message(reshade::log::level::warning, "MEA: map cache cap (kMaxMaps) hit — raise it if camera capture starts missing.");
      }
#endif
   }

#if ENABLE_SR
   // ReShade reports CopyStructureCount as a buffer region copy from the counted UAV's buffer: from an engine append buffer with a
   // Luma twin this frame (the depth of field's sprites, see "DrawOutputSizedDoFPass"), the count comes from the twin
   static bool OnCopyBufferRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, [[maybe_unused]] uint64_t source_offset, reshade::api::resource dest, uint64_t dest_offset,
      [[maybe_unused]] uint64_t size)
   {
      DeviceData* device_data = cmd_list->get_device()->get_private_data<DeviceData>();
      if (device_data == nullptr || !device_data->game)
         return false;
      auto* const native_device_context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         return false; // The twins are the immediate context's
      auto& gd = GetGameDeviceData(*device_data);
      const auto* const twin = gd.FindOutputTwinRecord(reinterpret_cast<ID3D11Resource*>(source.handle));
      if (!twin || !twin->uav)
         return false;
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
      twin->uav->GetDesc(&uav_desc);
      if (uav_desc.ViewDimension != D3D11_UAV_DIMENSION_BUFFER)
         return false;
      native_device_context->CopyStructureCount(reinterpret_cast<ID3D11Buffer*>(dest.handle), UINT(dest_offset), twin->uav.get());
      [[maybe_unused]] CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
      MEA_COUNT(structure_counts_redirected);
      return true;
   }

   // An engine clear of a target with a Luma twin this frame clears the twin too (a transient target reused for accumulation)
   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, [[maybe_unused]] const reshade::api::rect* rects)
   {
      DeviceData* device_data = cmd_list->get_device()->get_private_data<DeviceData>();
      if (device_data == nullptr || !device_data->game || rect_count != 0)
         return false;
      auto* const native_device_context = reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native());
      if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         return false;
      auto& gd = GetGameDeviceData(*device_data);
      if (gd.output_twins.empty())
         return false;
      auto* const engine_rtv = reinterpret_cast<ID3D11RenderTargetView*>(rtv.handle);
      com_ptr<ID3D11Resource> engine;
      engine_rtv->GetResource(&engine);
      const auto* const twin = gd.FindOutputTwinRecord(engine.get());
      if (!twin || !twin->luma)
         return false;
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      engine_rtv->GetDesc(&rtv_desc);
      com_ptr<ID3D11Device> native_device;
      native_device_context->GetDevice(&native_device);
      ComPtr<ID3D11RenderTargetView> twin_rtv;
      if (SUCCEEDED(native_device->CreateRenderTargetView(twin->luma.get(), &rtv_desc, twin_rtv.put())))
      {
         native_device_context->ClearRenderTargetView(twin_rtv.get(), color);
      }
      return false; // The engine's target is cleared as well
   }
#endif

   // Drop the record of a destroyed buffer: its cached map pointer is dead, and the runtime may recycle the
   // handle for a brand-new buffer (which must not inherit a stale pointer or a DISCARD ban).
   static void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      DeviceData* device_data = device->get_private_data<DeviceData>();
      if (device_data == nullptr || !device_data->game)
         return;
      auto& gd = GetGameDeviceData(*device_data);
      const std::unique_lock lock(gd.map_cache.mutex);
      if (const int i = gd.map_cache.Find(resource.handle); i >= 0)
      {
         gd.map_cache.recs[i] = gd.map_cache.recs[--gd.map_cache.count];
      }
   }

   // An engine settings container by name ("WorldRender", "Render"...), null until the engine made it, or on an unknown build. The
   // settings manager and its lookup come from the function that creates "WorldRender": sub rsp, 0x48; mov rcx, [rip+settings_manager];
   // lea rdx, [rip+"WorldRender"]; call get_container; test rax, rax; jnz; lea r8d, [rax+0x10]; lea rcx, [rip+?]; mov edx, 0x4E0 (the
   // class size)
   static uint8_t* FindSettingsContainer(const char* name)
   {
      using GetContainer = uint8_t* (*)(void* manager, const char* name);
      struct Locator
      {
         void* const* manager = nullptr;
         GetContainer get_container = nullptr;
      };
      static const Locator locator = []
      {
         // clang-format off
         constexpr std::array<System::BytePattern, 40> pattern = {{
            0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x8D, 0x15, System::ANY, System::ANY,
            System::ANY, System::ANY, 0xE8, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x85, 0xC0, 0x75, 0x6E, 0x44, 0x8D, 0x40, 0x10,
            0x48, 0x8D, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY, 0xBA
         }};
         // clang-format on
         const std::vector<std::byte*> matches = System::ScanModuleForPattern(pattern);
         if (matches.size() != 1)
            return Locator{};
         const uint8_t* function = reinterpret_cast<const uint8_t*>(matches[0]);
         if (std::memcmp(function + 0x28, "\xE0\x04\x00\x00", 4) != 0)
            return Locator{};
         const auto rip_target = [&](size_t operand, size_t next)
         { return function + next + *reinterpret_cast<const int32_t*>(function + operand); };
         if (std::strcmp(reinterpret_cast<const char*>(rip_target(0x0E, 0x12)), "WorldRender") != 0)
            return Locator{};
         return Locator{
            .manager = reinterpret_cast<void* const*>(rip_target(0x07, 0x0B)),
            .get_container = reinterpret_cast<GetContainer>(const_cast<uint8_t*>(rip_target(0x13, 0x17))),
         };
      }();
      if (!locator.get_container || *locator.manager == nullptr)
         return nullptr;
      return locator.get_container(*locator.manager, name);
   }

   // The engine's jitter table, a vector of float2 pixel offsets in [-0.5, 0.5] that it fills with a correlated multi-jittered
   // sequence of "TemporalAAJitterCount" points, rebuilt only when the count changes. Found through its lookup, which every
   // frame returns table[frame % size]: mov r9, [rip+begin]; mov r8, [rip+end]; mov eax, edx; xor edx, edx; sub r8, r9;
   // sar r8, 3; div r8d; mov rax, [r9+rdx*8]; mov [rcx], rax; mov rax, rcx; ret
   struct JitterTable
   {
      float* const* begin = nullptr;
      float* const* end = nullptr;
   };
   static const JitterTable& FindJitterTable()
   {
      static const JitterTable table = []
      {
         // clang-format off
         constexpr std::array<System::BytePattern, 39> pattern = {{
            0x4C, 0x8B, 0x0D, System::ANY, System::ANY, System::ANY, System::ANY, 0x4C, 0x8B, 0x05, System::ANY, System::ANY, System::ANY, System::ANY,
            0x8B, 0xC2, 0x33, 0xD2, 0x4D, 0x2B, 0xC1, 0x49, 0xC1, 0xF8, 0x03, 0x41, 0xF7, 0xF0, 0x49, 0x8B, 0x04, 0xD1, 0x48, 0x89, 0x01, 0x48,
            0x8B, 0xC1, 0xC3
         }};
         // clang-format on
         const std::vector<std::byte*> matches = System::ScanModuleForPattern(pattern);
         if (matches.size() != 1)
            return JitterTable{};
         const uint8_t* function = reinterpret_cast<const uint8_t*>(matches[0]);
         const auto rip_target = [&](size_t operand, size_t next)
         { return reinterpret_cast<float* const*>(function + next + *reinterpret_cast<const int32_t*>(function + operand)); };
         return JitterTable{.begin = rip_target(0x03, 0x07), .end = rip_target(0x0A, 0x0E)};
      }();
      return table;
   }

   static void WriteWorldRenderSettings(const WorldRenderSettingsValues& values)
   {
      uint8_t* settings = g_world_render_settings;
      settings[kWrsJitterUseCmj] = values.jitter_use_cmj;
      std::memcpy(settings + kWrsJitterCount, &values.jitter_count, sizeof(values.jitter_count));
      std::memcpy(settings + kWrsPostSharpeningAmount, &values.post_sharpening_amount, sizeof(values.post_sharpening_amount));
   }

   // The game's TAA settings for this frame, from the user's choices: while SR runs, a jitter table of the SR's phase count filled
   // with Luma's Halton (2, 3) (DLSS/FSR want one; the vanilla 389 point table isn't, and the engine's own Halton starts at index
   // 0, a corner sample with a -0.11 pixel mean in y over 8 phases), and no resolve sharpen when turned off
   static void UpdateWorldRenderSettings(DeviceData& device_data)
   {
      if (!g_world_render_settings)
      {
         if (g_world_render_settings_rejected)
            return;
         g_world_render_settings = FindSettingsContainer("WorldRender");
         if (!g_world_render_settings)
            return;
         WorldRenderSettingsValues vanilla = {.jitter_use_cmj = g_world_render_settings[kWrsJitterUseCmj]};
         std::memcpy(&vanilla.jitter_count, g_world_render_settings + kWrsJitterCount, sizeof(vanilla.jitter_count));
         std::memcpy(&vanilla.post_sharpening_amount, g_world_render_settings + kWrsPostSharpeningAmount, sizeof(vanilla.post_sharpening_amount));
         // A layout check: a bool, a sane sequence length and sharpen amount, else this isn't the build the offsets come from
         if (vanilla.jitter_use_cmj > 1 || vanilla.jitter_count == 0 || vanilla.jitter_count > 4096 || !(vanilla.post_sharpening_amount >= 0.f && vanilla.post_sharpening_amount <= 16.f))
         {
            g_world_render_settings = nullptr;
            g_world_render_settings_rejected = true;
            return;
         }
         g_world_render_settings_vanilla = vanilla;
      }
      WorldRenderSettingsValues values = g_world_render_settings_vanilla;
#if ENABLE_SR
      const bool sr_active = GetGameDeviceData(device_data).sr_active;
      if (sr_active ? g_halton_jitter : g_improve_taa_jitter)
      {
         const SR::InstanceData* sr_instance_data = device_data.GetSRInstanceData();
         values.jitter_use_cmj = 1;
         values.jitter_count = uint32_t((sr_active && sr_instance_data) ? (std::max)(sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data), 1) : g_halton_jitter_native_phases);
         // The engine rebuilds the table at the new count during the next frame: the points go in from the present after it
         const JitterTable& table = FindJitterTable();
         if (table.begin && *table.begin && size_t(*table.end - *table.begin) == size_t(values.jitter_count) * 2)
         {
            for (uint32_t i = 0; i < values.jitter_count; ++i)
            {
               (*table.begin)[i * 2] = SR::HaltonSequence(i, 2);
               (*table.begin)[i * 2 + 1] = SR::HaltonSequence(i, 3);
            }
         }
      }
#endif
      if (g_disable_taa_sharpening)
      {
         values.post_sharpening_amount = 0.f;
      }
      WriteWorldRenderSettings(values);
   }

   static GameRenderSettingsValues ReadGameRenderSettings(const uint8_t* settings)
   {
      GameRenderSettingsValues values;
      std::memcpy(&values.resolution_scale_game, settings + kGrsResolutionScaleGame, sizeof(values.resolution_scale_game));
      std::memcpy(&values.resolution_scale_mode, settings + kGrsResolutionScaleMode, sizeof(values.resolution_scale_mode));
      std::memcpy(&values.render_scale_resample_mode, settings + kGrsRenderScaleResampleMode, sizeof(values.render_scale_resample_mode));
      return values;
   }

   static void WriteGameRenderSettings(const GameRenderSettingsValues& values)
   {
      std::memcpy(g_game_render_settings + kGrsResolutionScaleGame, &values.resolution_scale_game, sizeof(values.resolution_scale_game));
      std::memcpy(g_game_render_settings + kGrsResolutionScaleMode, &values.resolution_scale_mode, sizeof(values.resolution_scale_mode));
      std::memcpy(g_game_render_settings + kGrsRenderScaleResampleMode, &values.render_scale_resample_mode, sizeof(values.render_scale_resample_mode));
   }

   // Luma's render scale while an upscaler draws (it upscales whatever the engine renders below the output size), else the
   // game's own settings, put back once
   static void UpdateGameRenderSettings(bool upscaling)
   {
      if (!g_game_render_settings)
      {
         if (g_game_render_settings_rejected)
            return;
         g_game_render_settings = FindSettingsContainer("Render");
         if (!g_game_render_settings)
            return;
         const GameRenderSettingsValues vanilla = ReadGameRenderSettings(g_game_render_settings);
         float hdr10_peak = 0.f;
         std::memcpy(&hdr10_peak, g_game_render_settings + kGrsDisplayMappingHdr10PeakLuma, sizeof(hdr10_peak));
         // A layout check: known enums, a sane scale and peak, else this isn't the build the offsets come from
         if (uint32_t(vanilla.resolution_scale_mode) > uint32_t(kResolutionScaleModeCustom) || uint32_t(vanilla.render_scale_resample_mode) > 6 ||
             !(vanilla.resolution_scale_game > 0.f && vanilla.resolution_scale_game <= 4.f) || !(hdr10_peak >= 1.f && hdr10_peak <= kNoHdr10PeakNits))
         {
            g_game_render_settings = nullptr;
            g_game_render_settings_rejected = true;
            return;
         }
         reshade::log::message(reshade::log::level::info, std::format("[MEA HDR] game HDR10 peak (calibration) {} nits", hdr10_peak).c_str());
      }

      if (g_hdr_fix && cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         // A value other than Luma's is the user's (the game writes it when the calibration slider moves)
         float hdr10_peak = 0.f;
         std::memcpy(&hdr10_peak, g_game_render_settings + kGrsDisplayMappingHdr10PeakLuma, sizeof(hdr10_peak));
         if (hdr10_peak != kNoHdr10PeakNits)
         {
            g_game_hdr10_peak_vanilla = hdr10_peak;
         }
         std::memcpy(g_game_render_settings + kGrsDisplayMappingHdr10PeakLuma, &kNoHdr10PeakNits, sizeof(kNoHdr10PeakNits));
         g_game_hdr10_peak_written = true;
      }
      else if (std::exchange(g_game_hdr10_peak_written, false))
      {
         std::memcpy(g_game_render_settings + kGrsDisplayMappingHdr10PeakLuma, &g_game_hdr10_peak_vanilla, sizeof(g_game_hdr10_peak_vanilla));
      }
      if (upscaling)
      {
         WriteGameRenderSettings({.resolution_scale_game = g_render_scale, .resolution_scale_mode = kResolutionScaleModeCustom, .render_scale_resample_mode = kResampleModeLinear});
         g_game_render_settings_written = true;
         return;
      }
      if (std::exchange(g_game_render_settings_written, false))
      {
         WriteGameRenderSettings(g_game_render_settings_vanilla);
      }
      g_game_render_settings_vanilla = ReadGameRenderSettings(g_game_render_settings);
   }

   // Camera capture (jitter/near/FOV) at a scene VS draw, once per frame (retries on the next draw if this one's camera map
   // hasn't landed). Immediate only keeps "cam_valid_this_frame" single-threaded. Here, not at the compute TAA dispatch: by
   // then VS slot 2 holds another view's camera. The gameplay gbuffer VS identifies the main view by hash. Scenes without it
   // (the main menu's forward-shaded planet) bind the same per-view CB at VS b2: any draw whose b2 passes the layout
   // signature with [1].xy = the last resolve's scene size is the main view (shadow and half-res views have their own sizes).
   static void CaptureCamera(ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, MassEffectAndromedaGameDeviceData* gd,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      const bool gbuffer = original_shader_hashes.Contains(kGbufferVS_A, reshade::api::shader_stage::vertex) ||
                           original_shader_hashes.Contains(kGbufferVS_B, reshade::api::shader_stage::vertex);
      if (!gbuffer && gd->scene_size.x == 0)
         return;
      ID3D11DeviceContext1* context1 = GetImmediateContext1(native_device_context, gd);
      if (!context1)
         return;
      const std::shared_lock lock(gd->map_cache.mutex);
      // Signature-only at the gbuffer VS (the hash identifies the main view), else keyed to the scene size
      if (!TryStoreCamera(context1, gd, false, 2, (gbuffer ? nullptr : &gd->scene_size)))
         return;
      if (gbuffer)
      {
         MEA_COUNT(camera_gbuffer);
      }
      else
      {
         MEA_COUNT(camera_scene_vs);
      }
   }

#if ENABLE_SMAA || ENABLE_SR
   // "scene_depth", captured at its writer (its readers vary with the settings). Immediate only, as the passes that read it: a
   // capture recorded on a worker thread would race them.
   static void CaptureLinearDepth(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MassEffectAndromedaGameDeviceData* gd)
   {
      ComPtr<ID3D11RenderTargetView> depth_rtv;
      native_device_context->OMGetRenderTargets(1, depth_rtv.put(), nullptr);
      if (!depth_rtv)
         return;
      D3D11_RENDER_TARGET_VIEW_DESC depth_rtv_desc = {};
      depth_rtv->GetDesc(&depth_rtv_desc);
      if (depth_rtv_desc.Format != DXGI_FORMAT_R32_FLOAT || depth_rtv_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D)
         return;
      // A smaller view's this frame doesn't replace the scene's (a 1920x1080 secondary camera after the 4K scene, measured live:
      // SMAA, at the end of the frame, lost its predication to the size check)
      if (gd->scene_depth_frame == cb_luma_global_settings.FrameIndex && gd->srv_scene_depth &&
          GetViewTextureSize(depth_rtv.get()).x < GetViewTextureSize(gd->srv_scene_depth.get()).x)
         return;
      ComPtr<ID3D11Resource> depth_resource;
      depth_rtv->GetResource(depth_resource.put());
      if (depth_resource.get() != gd->scene_depth.get())
      {
         gd->srv_scene_depth.reset();
         gd->scene_depth = depth_resource;
         const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R32_FLOAT, depth_rtv_desc.Texture2D.MipSlice, 1);
         native_device->CreateShaderResourceView(depth_resource.get(), &srv_desc, gd->srv_scene_depth.put());
      }
      gd->scene_depth_frame = cb_luma_global_settings.FrameIndex;
   }
#endif

#if ENABLE_SMAA
   // The FXAA pass (in-game FXAA AA mode) replaced with SMAA (with predication, then RCAS when on). Mutually exclusive with the
   // TAA path: FXAA mode has no TAA dispatch, and vice versa.
   static DrawOrDispatchOverrideType ReplaceFXAAWithSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data,
      DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd)
   {
      ComPtr<ID3D11ShaderResourceView> srv_color;
      native_device_context->PSGetShaderResources(0, 1, srv_color.put()); // t0 = display-encoded color
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr); // output (separate resource)
      // The real RTV size (not the swapchain's): it follows the in-game Resolution Scale
      const uint2 size = GetViewTextureSize(rtv.get());
      if (!srv_color || size.x == 0 || size.y == 0)
         return DrawOrDispatchOverrideType::None;
      gd->smaa_frame = cb_luma_global_settings.FrameIndex;

      // Predication: the scale and the mask fall back together (2.0 without a mask raises the threshold frame-wide).
      // The CS maps texels 1:1, so the depth must be this frame's and of the FXAA target's size.
      auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("MEA Depth Extract CS"));
      bool pred_ok = g_smaa_predication && pred_cs != nullptr && gd->srv_scene_depth && gd->scene_depth_frame == cb_luma_global_settings.FrameIndex &&
                     GetViewTextureSize(gd->srv_scene_depth.get()) == size;
      if (pred_ok)
      {
         if (!gd->cb_pred || gd->pred_tolerance != g_smaa_pred_tolerance)
         {
            const float pred_params[4] = {g_smaa_pred_tolerance, 0.f, 0.f, 0.f};
            if (CreateImmutableCB(native_device, pred_params, sizeof(pred_params), std::addressof(gd->cb_pred)))
            {
               gd->pred_tolerance = g_smaa_pred_tolerance;
            }
         }
         if (!gd->tex_pred || gd->pred_size != size)
         {
            gd->uav_pred.reset();
            gd->srv_pred.reset();
            gd->tex_pred.reset();
            if (CreateTexture(native_device, DXGI_FORMAT_R16_FLOAT, size, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd->tex_pred.put(), gd->srv_pred.put(), nullptr,
                   gd->uav_pred.put()))
            {
               gd->pred_size = size;
            }
         }
         pred_ok = gd->cb_pred && gd->uav_pred && gd->srv_pred;
      }

      const float pred_scale = (pred_ok ? 2.f : 1.f);
      if (!gd->cb_smaa_metrics || gd->smaa_metrics_size != size || gd->smaa_metrics_pred_scale != pred_scale)
      {
         const float metrics[8] = {1.f / (float)size.x, 1.f / (float)size.y, (float)size.x, (float)size.y, pred_scale, 0.f, 0.f, 0.f};
         if (CreateImmutableCB(native_device, metrics, sizeof(metrics), std::addressof(gd->cb_smaa_metrics)))
         {
            gd->smaa_metrics_size = size;
            gd->smaa_metrics_pred_scale = pred_scale;
         }
      }
      if (!gd->cb_smaa_metrics)
         return DrawOrDispatchOverrideType::None;

      // With sharpening on, SMAA renders into "tex_rcas_input" and RCAS writes the final RTV
      const bool do_sharpen = PrepareRCASInput(native_device, device_data, gd, size);

      // Linear view depth -> plane-deviation edge-ness (R16F); see Luma_MEA_DepthExtract.hlsl
      if (pred_ok)
      {
         DrawStateStack<DrawStateStackType::Compute> pred_state;
         pred_state.Cache(native_device_context, device_data.uav_max_count);
         ID3D11ShaderResourceView* pred_srv = gd->srv_scene_depth.get();
         ID3D11UnorderedAccessView* pred_uav = gd->uav_pred.get();
         ID3D11Buffer* pred_cb = gd->cb_pred.get();
         native_device_context->CSSetShaderResources(0, 1, &pred_srv);
         native_device_context->CSSetUnorderedAccessViews(0, 1, &pred_uav, nullptr);
         native_device_context->CSSetConstantBuffers(0, 1, &pred_cb);
         native_device_context->CSSetShader(pred_cs, nullptr, 0);
         native_device_context->Dispatch((size.x + 7) / 8, (size.y + 7) / 8, 1);
         pred_state.Restore(native_device_context);
      }

      // MEA's FXAA input is already display-encoded, so it is used as both color and gamma
      ID3D11RenderTargetView* smaa_target = (do_sharpen ? gd->tex_rcas_input_rtv.get() : rtv.get());
      // Core creates its area texture at the first draw; ours goes in first, without the U-shape smoothing (see "SMAA_AreaTex_SharpU.h")
      if (auto& area_tex_srv = device_data.managed_resources.shader_resource_views["smaa_area_tex"_h]; !area_tex_srv)
      {
         std::vector<unsigned char> area_tex(std::begin(areaTexBytes), std::end(areaTexBytes));
         for (const auto& [offset, value] : areaTexSharpUPatch)
         {
            area_tex[offset] = value;
         }
         const D3D11_TEXTURE2D_DESC area_tex_desc = {
            .Width = AREATEX_WIDTH,
            .Height = AREATEX_HEIGHT,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R8G8_UNORM,
            .SampleDesc = {.Count = 1},
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
         };
         const D3D11_SUBRESOURCE_DATA area_tex_data = {.pSysMem = area_tex.data(), .SysMemPitch = AREATEX_PITCH};
         if (com_ptr<ID3D11Texture2D> texture; SUCCEEDED(native_device->CreateTexture2D(&area_tex_desc, &area_tex_data, &texture)))
         {
            native_device->CreateShaderResourceView(texture.get(), nullptr, area_tex_srv.put());
         }
      }
      DrawSMAA(native_device, native_device_context, device_data, smaa_target, srv_color.get(), srv_color.get(), (pred_ok ? gd->srv_pred.get() : nullptr), gd->cb_smaa_metrics.get());
      MEA_COUNT(smaa_draws);

      if (do_sharpen)
      {
         MEA_COUNT(rcas_draws);
         DrawRCAS(native_device_context, device_data, gd->tex_rcas_input_srv.get(), rtv.get(), g_rcas_sharpness);
      }

      device_data.has_drawn_main_post_processing = true;
      return DrawOrDispatchOverrideType::Replaced; // cancel native FXAA
   }
#endif // ENABLE_SMAA

#if ENABLE_SMAA && ENABLE_SR
   // RCAS on top of DLSS/FSR, on the tonemap's output (see "shader_hashes_tonemap"): the tonemap draws into RCAS's input (RT1,
   // the FXAA luma, stays the game's), then RCAS writes the tonemap's own target
   static DrawOrDispatchOverrideType DrawTonemapWithRCAS(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data,
      DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd, const std::function<void()>& original_draw)
   {
      com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
      com_ptr<ID3D11DepthStencilView> dsv;
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
      const uint2 size = (rtvs[0] ? GetViewTextureSize(rtvs[0].get()) : uint2{});
      if (size.x == 0 || size.y == 0 || !PrepareRCASInput(native_device, device_data, gd, size))
         return DrawOrDispatchOverrideType::None;
      ID3D11RenderTargetView* tonemap_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         tonemap_rtvs[i] = rtvs[i].get();
      }
      tonemap_rtvs[0] = gd->tex_rcas_input_rtv.get();
      native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, tonemap_rtvs, dsv.get());
      original_draw();
      native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv.get());
      MEA_COUNT(rcas_draws);
      DrawRCAS(native_device_context, device_data, gd->tex_rcas_input_srv.get(), rtvs[0].get(), g_rcas_sharpness);
      return DrawOrDispatchOverrideType::Replaced;
   }
#endif

#if ENABLE_SR
   // Luma's output-sized stand-in for the engine's tonemap target, in its format
   static bool EnsureUpscaledScene(ID3D11Device* native_device, MassEffectAndromedaGameDeviceData* gd, uint2 size, DXGI_FORMAT format)
   {
      if (gd->upscaled_scene)
      {
         D3D11_TEXTURE2D_DESC desc;
         gd->upscaled_scene->GetDesc(&desc);
         if (desc.Width == size.x && desc.Height == size.y && desc.Format == format)
            return true;
      }
      gd->ReleaseUpscaledScene();
      if (CreateTexture(native_device, format, size, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd->upscaled_scene.put(), gd->upscaled_scene_srv.put(),
             gd->upscaled_scene_rtv.put()))
         return true;
      gd->ReleaseUpscaledScene();
      return false;
   }

   struct OutputTargetOptions
   {
      UINT array_size = 1;
      bool unordered_access = false;
      bool clear = true; // False when a full overwrite follows
   };
   // A pooled target for this frame (see "output_targets"): an unused one of that description, else an unused one recreated (the
   // pool never outgrows a frame's need), else a new one. Cleared to 0 by default: the passes that accumulate into it expect the
   // engine's clear.
   static MassEffectAndromedaGameDeviceData::PooledTarget* AcquireOutputTarget(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MassEffectAndromedaGameDeviceData* gd,
      DXGI_FORMAT format, uint2 size, const OutputTargetOptions& options = {})
   {
      const UINT array_size = options.array_size;
      const bool unordered_access = options.unordered_access;
      MassEffectAndromedaGameDeviceData::PooledTarget* spare = nullptr;
      MassEffectAndromedaGameDeviceData::PooledTarget* found = nullptr;
      for (auto& target : gd->output_targets)
      {
         if (target.in_use)
            continue;
         D3D11_TEXTURE2D_DESC desc;
         target.texture->GetDesc(&desc);
         if (desc.Format == format && desc.Width == size.x && desc.Height == size.y && desc.ArraySize == array_size && (!unordered_access || target.uav))
         {
            found = &target;
            break;
         }
         spare = &target;
      }
      if (!found)
      {
         found = (spare ? spare : &gd->output_targets.emplace_back());
         *found = {};
         const UINT bind_flags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | (unordered_access ? D3D11_BIND_UNORDERED_ACCESS : 0);
         if (!CreateTexture(native_device, format, size, bind_flags, found->texture.put(), found->srv.put(), found->rtv.put(), unordered_access ? found->uav.put() : nullptr, array_size))
         {
            *found = {};
            return nullptr;
         }
      }
      found->in_use = true;
      if (options.clear)
      {
         constexpr FLOAT black[4] = {};
         native_device_context->ClearRenderTargetView(found->rtv.get(), black);
      }
      return found;
   }

   // A pooled structured buffer for this frame (see "output_buffers") of "elements" elements of "stride" bytes, its UAV with the
   // engine's append / counter flags
   static MassEffectAndromedaGameDeviceData::PooledBuffer* AcquireOutputBuffer(ID3D11Device* native_device, MassEffectAndromedaGameDeviceData* gd, UINT elements, UINT stride, UINT uav_flags)
   {
      MassEffectAndromedaGameDeviceData::PooledBuffer* spare = nullptr;
      for (auto& buffer : gd->output_buffers)
      {
         if (buffer.in_use)
            continue;
         D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
         buffer.uav->GetDesc(&uav_desc);
         if (buffer.elements == elements && uav_desc.Buffer.Flags == uav_flags)
         {
            buffer.in_use = true;
            return &buffer;
         }
         spare = &buffer;
      }
      MassEffectAndromedaGameDeviceData::PooledBuffer* const created = (spare ? spare : &gd->output_buffers.emplace_back());
      *created = {};
      const CD3D11_BUFFER_DESC desc(elements * stride, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride);
      const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(D3D11_SRV_DIMENSION_BUFFER, DXGI_FORMAT_UNKNOWN, 0, elements);
      const CD3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc(D3D11_UAV_DIMENSION_BUFFER, DXGI_FORMAT_UNKNOWN, 0, elements, 0, uav_flags);
      if (FAILED(native_device->CreateBuffer(&desc, nullptr, created->buffer.put())) || FAILED(native_device->CreateShaderResourceView(created->buffer.get(), &srv_desc, created->srv.put())) ||
          FAILED(native_device->CreateUnorderedAccessView(created->buffer.get(), &uav_desc, created->uav.put())))
      {
         *created = {};
         return nullptr;
      }
      created->elements = elements;
      created->in_use = true;
      return created;
   }

   // The output-sized twin of an engine render target, as a view like the engine's (an array slice too): the existing twin when an
   // earlier pass drew into it this frame (the depth of field's layers accumulate) and it has that size, else a new cleared one
   static ComPtr<ID3D11RenderTargetView> GetOutputTargetView(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MassEffectAndromedaGameDeviceData* gd,
      ID3D11RenderTargetView* engine_rtv, uint2 size)
   {
      com_ptr<ID3D11Resource> engine;
      engine_rtv->GetResource(&engine);
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      engine_rtv->GetDesc(&rtv_desc);
      ID3D11Resource* luma = nullptr;
      if (auto* twin = gd->FindOutputTwinRecord(engine.get()); twin && twin->luma)
      {
         D3D11_TEXTURE2D_DESC twin_desc;
         static_cast<ID3D11Texture2D*>(twin->luma.get())->GetDesc(&twin_desc);
         if (twin_desc.Width == size.x && twin_desc.Height == size.y)
         {
            luma = twin->luma.get();
            twin->engine_current = false; // Drawn into again: the engine's copy, if restored, is behind
         }
      }
      if (!luma)
      {
         D3D11_TEXTURE2D_DESC engine_desc;
         static_cast<ID3D11Texture2D*>(engine.get())->GetDesc(&engine_desc);
         auto* const target = AcquireOutputTarget(native_device, native_device_context, gd, rtv_desc.Format, size, {.array_size = engine_desc.ArraySize});
         if (!target)
            return {};
         luma = target->texture.get();
         gd->output_twins.push_back({.engine = engine.get(), .srv = target->srv.get(), .luma = luma});
      }
      ComPtr<ID3D11RenderTargetView> view;
      native_device->CreateRenderTargetView(luma, &rtv_desc, view.put());
      return view;
   }

   // The first "row_count" rows of a pass's b0: a window of the engine's dynamic ring, read through its CPU map pointer
   static bool ReadEngineConstants(ID3D11DeviceContext1* context1, MassEffectAndromedaGameDeviceData* gd, bool pixel, float (*rows)[4], UINT row_count)
   {
      com_ptr<ID3D11Buffer> engine_cb;
      UINT first_constant = 0;
      UINT num_constants = 0;
      if (pixel)
      {
         context1->PSGetConstantBuffers1(0, 1, &engine_cb, &first_constant, &num_constants);
      }
      else
      {
         context1->CSGetConstantBuffers1(0, 1, &engine_cb, &first_constant, &num_constants);
      }
      if (!engine_cb)
         return false;
      const uint64_t engine_cb_handle = reinterpret_cast<uint64_t>(engine_cb.get()); // The map cache key
      const std::shared_lock lock(gd->map_cache.mutex);
      const int i = gd->map_cache.Find(engine_cb_handle);
      if (i < 0 || gd->map_cache.recs[i].data == nullptr || uint64_t(first_constant + row_count) * 16 > gd->map_cache.recs[i].size)
         return false;
      std::memcpy(rows, reinterpret_cast<const uint8_t*>(gd->map_cache.recs[i].data) + first_constant * 16, row_count * 16);
      return true;
   }

   // "source" scaled into "target" of "size" by Luma's Scale shaders, the pipeline state kept. False while they aren't compiled.
   static bool DrawScaled(ID3D11DeviceContext* native_device_context, DeviceData& device_data, ID3D11SamplerState* sampler, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target,
      uint2 size)
   {
      auto* const scale_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Scale VS"));
      auto* const scale_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Scale PS"));
      if (scale_vs == nullptr || scale_ps == nullptr)
         return false;
      DrawStateStack<DrawStateStackType::FullGraphics> scale_state;
      scale_state.Cache(native_device_context, device_data.uav_max_count);
      DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), sampler, scale_vs, scale_ps, source, target, size.x,
         size.y);
      scale_state.Restore(native_device_context);
      return true;
   }

   // A render-sized input made before the upscaler (depth, motion vectors) point stretched to the output size (NVIDIA: nearest), as
   // its twin. Depth views (typeless with an X8 part) aren't renderable: their twin is r32 float.
   static ID3D11ShaderResourceView* GetStretchedTwin(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd,
      ID3D11ShaderResourceView* engine_srv, uint2 output_size)
   {
      com_ptr<ID3D11Resource> resource;
      engine_srv->GetResource(&resource);
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
      engine_srv->GetDesc(&srv_desc);
      UINT support = 0;
      const bool renderable = SUCCEEDED(native_device->CheckFormatSupport(srv_desc.Format, &support)) && (support & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
      auto* const stretched = AcquireOutputTarget(native_device, native_device_context, gd, renderable ? srv_desc.Format : DXGI_FORMAT_R32_FLOAT, output_size, {.clear = false});
      if (!stretched || !DrawScaled(native_device_context, device_data, device_data.sampler_state_point.get(), engine_srv, stretched->rtv.get(), output_size))
         return nullptr;
      gd->output_twins.push_back({.engine = resource.get(), .srv = stretched->srv.get(), .luma = stretched->texture.get(), .engine_current = true});
      return stretched->srv.get();
   }

   static constexpr UINT kPostMaxRows = 9; // The redirected passes' b0 rows (the widest blur's, see "kPostBlurPasses")
   static bool EnsurePostConstants(ID3D11Device* native_device, MassEffectAndromedaGameDeviceData* gd)
   {
      if (gd->post_cb)
         return true;
      const CD3D11_BUFFER_DESC cb_desc(kPostMaxRows * 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
      return SUCCEEDED(native_device->CreateBuffer(&cb_desc, nullptr, gd->post_cb.put()));
   }
   static bool UploadPostConstants(ID3D11DeviceContext* native_device_context, MassEffectAndromedaGameDeviceData* gd, const float (*rows)[4])
   {
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(native_device_context->Map(gd->post_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
         return false;
      std::memcpy(mapped.pData, rows, kPostMaxRows * 16);
      native_device_context->Unmap(gd->post_cb.get(), 0);
      return true;
   }

   // The effect sliders on the tonemap, any of its 64 permutations (the same b0 rows in all, from the disassembly): c1.xyz film
   // grain, c5.xyz exposure (before the grade LUT), c8.xyz bloom, c11.w vignette strength (pow(saturate(1 - r^2 c11.w), c10.z):
   // 0 = none, where scaling the exponent would give log(0) * 0), c12 chromatic aberration (each channel's uv scale around the
   // centre, 0.5 = no split). Its draw is wrapped to bind a patched copy of b0 and rebind the engine's after, so whichever
   // path draws the tonemap uses it. False at the defaults.
   static bool WrapTonemapEffects(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MassEffectAndromedaGameDeviceData* gd, std::function<void()>* draw)
   {
      if (g_exposure == 1.f && g_bloom_intensity == 1.f && g_vignette_intensity == 1.f && g_film_grain_intensity == 1.f && g_chromatic_aberration_intensity == 1.f)
         return false;
      ID3D11DeviceContext1* const context1 = GetImmediateContext1(native_device_context, gd);
      if (!context1)
         return false;
      com_ptr<ID3D11Buffer> engine_cb;
      UINT first_constant = 0;
      UINT num_constants = 0;
      context1->PSGetConstantBuffers1(0, 1, &engine_cb, &first_constant, &num_constants);
      constexpr UINT row_count = 14; // The widest permutation's
      float rows[row_count][4] = {};
      if (!engine_cb || !ReadEngineConstants(context1, gd, true, rows, row_count))
         return false;
      for (UINT i = 0; i < 3; i++)
      {
         rows[1][i] *= g_film_grain_intensity;
         rows[5][i] *= g_exposure;
         rows[8][i] *= g_bloom_intensity;
      }
      rows[11][3] *= g_vignette_intensity;
      for (float& scale : rows[12])
      {
         scale = 0.5f + (scale - 0.5f) * g_chromatic_aberration_intensity;
      }
      if (!gd->tonemap_cb)
      {
         const CD3D11_BUFFER_DESC cb_desc(row_count * 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
         if (FAILED(native_device->CreateBuffer(&cb_desc, nullptr, gd->tonemap_cb.put())))
            return false;
      }
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(native_device_context->Map(gd->tonemap_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
         return false;
      std::memcpy(mapped.pData, rows, sizeof(rows));
      native_device_context->Unmap(gd->tonemap_cb.get(), 0);
      // Run within this draw callback, while the engine's buffer is still bound
      *draw = [context1, cb = gd->tonemap_cb.get(), engine = engine_cb.get(), first_constant, num_constants, original = std::move(*draw)]
      {
         context1->PSSetConstantBuffers(0, 1, &cb);
         original();
         context1->PSSetConstantBuffers1(0, 1, &engine, &first_constant, &num_constants);
      };
      return true;
   }
   // The header of the post passes' b0, c0 = (W u32, H u32, 1 / W, 1 / H) and c1 = (W u32, H u32, 1 / W, aspect), at the output
   // size as the engine's at native. c1 is the pass' thread grid: half the size for the half resolution sprite builds, which
   // place a sprite at (id + 0.5) x c1.z x 2 - 1 in NDC (a full-size c1 there squeezes the bokeh into the top left quarter).
   static void PatchPostHeader(float (*rows)[4], uint2 output_size)
   {
      const uint32_t grid_divisor = (std::max)(uint32_t(std::lround(float(std::bit_cast<uint32_t>(rows[0][0])) / float((std::max)(std::bit_cast<uint32_t>(rows[1][0]), 1u)))), 1u);
      const uint2 grid_size = {(output_size.x + grid_divisor - 1) / grid_divisor, (output_size.y + grid_divisor - 1) / grid_divisor};
      rows[0][0] = std::bit_cast<float>(output_size.x);
      rows[0][1] = std::bit_cast<float>(output_size.y);
      rows[0][2] = 1.f / float(output_size.x);
      rows[0][3] = 1.f / float(output_size.y);
      rows[1][0] = std::bit_cast<float>(grid_size.x);
      rows[1][1] = std::bit_cast<float>(grid_size.y);
      rows[1][2] = 1.f / float(grid_size.x);
   }
   static uint2 ScaleSize(uint2 size, float ratio)
   {
      return {(std::max)(uint32_t(float(size.x) * ratio + 0.5f), 1u), (std::max)(uint32_t(float(size.y) * ratio + 0.5f), 1u)};
   }
   static uint2 MipSize(uint2 size, UINT mip)
   {
      return {(std::max)(size.x >> mip, 1u), (std::max)(size.y >> mip, 1u)};
   }

   // The scene's depth-stencil (D24S8) at the output size (DLSS checklist POST-4: every depth a pass after the upscaler tests), point
   // stretched once per frame at its first user (see "Luma_MEA_ResampleDepthStencil"): the depth, then each stencil bit. Later
   // passes write into it as they would into the engine's (the scanner's outline marks bit 128).
   static ID3D11DepthStencilView* GetOutputSizedDepthStencil(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, ID3D11DepthStencilView* engine_dsv, uint2 output_size)
   {
      com_ptr<ID3D11Resource> engine;
      engine_dsv->GetResource(&engine);
      if (engine.get() == gd->output_depth_stencil_engine.get() && gd->output_depth_stencil_dsv)
         return gd->output_depth_stencil_dsv.get();
      auto* const scale_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Scale VS"));
      auto* const resample_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("MEA Resample Depth Stencil PS"));
      D3D11_TEXTURE2D_DESC engine_desc;
      static_cast<ID3D11Texture2D*>(engine.get())->GetDesc(&engine_desc);
      if (scale_vs == nullptr || resample_ps == nullptr || engine_desc.Format != DXGI_FORMAT_R24G8_TYPELESS || (engine_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 ||
          engine_desc.SampleDesc.Count != 1 || !EnsurePostConstants(native_device, gd))
         return nullptr;

      D3D11_TEXTURE2D_DESC desc = {};
      if (gd->output_depth_stencil)
      {
         gd->output_depth_stencil->GetDesc(&desc);
      }
      if (desc.Width != output_size.x || desc.Height != output_size.y)
      {
         gd->output_depth_stencil_dsv.reset();
         gd->output_depth_stencil.reset();
         const CD3D11_TEXTURE2D_DESC texture_desc(DXGI_FORMAT_R24G8_TYPELESS, output_size.x, output_size.y, 1, 1, D3D11_BIND_DEPTH_STENCIL);
         const CD3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc(D3D11_DSV_DIMENSION_TEXTURE2D, DXGI_FORMAT_D24_UNORM_S8_UINT);
         if (FAILED(native_device->CreateTexture2D(&texture_desc, nullptr, gd->output_depth_stencil.put())) ||
             FAILED(native_device->CreateDepthStencilView(gd->output_depth_stencil.get(), &dsv_desc, gd->output_depth_stencil_dsv.put())))
         {
            gd->output_depth_stencil_dsv.reset();
            gd->output_depth_stencil.reset();
            return nullptr;
         }
      }
      if (!gd->resample_depth_stencil_states[0])
      {
         for (UINT i = 0; i < std::size(gd->resample_depth_stencil_states); i++)
         {
            CD3D11_DEPTH_STENCIL_DESC state_desc(D3D11_DEFAULT);
            state_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            if (i != 0)
            {
               state_desc.DepthEnable = FALSE;
               state_desc.StencilEnable = TRUE;
               state_desc.StencilReadMask = 0;
               state_desc.StencilWriteMask = UINT8(1u << (i - 1));
               state_desc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_REPLACE, D3D11_COMPARISON_ALWAYS};
               state_desc.BackFace = state_desc.FrontFace;
            }
            if (FAILED(native_device->CreateDepthStencilState(&state_desc, gd->resample_depth_stencil_states[i].put())))
            {
               gd->resample_depth_stencil_states[0].reset();
               return nullptr;
            }
         }
      }
      const CD3D11_SHADER_RESOURCE_VIEW_DESC depth_desc(D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 0, 1);
      const CD3D11_SHADER_RESOURCE_VIEW_DESC stencil_desc(D3D11_SRV_DIMENSION_TEXTURE2D, DXGI_FORMAT_X24_TYPELESS_G8_UINT, 0, 1);
      ComPtr<ID3D11ShaderResourceView> depth_srv;
      ComPtr<ID3D11ShaderResourceView> stencil_srv;
      if (FAILED(native_device->CreateShaderResourceView(engine.get(), &depth_desc, depth_srv.put())) ||
          FAILED(native_device->CreateShaderResourceView(engine.get(), &stencil_desc, stencil_srv.put())))
         return nullptr;

      DrawStateStack<DrawStateStackType::FullGraphics> resample_state;
      resample_state.Cache(native_device_context, device_data.uav_max_count);
      ID3D11DepthStencilView* const dsv = gd->output_depth_stencil_dsv.get();
      native_device_context->OMSetRenderTargets(0, nullptr, dsv); // The engine's depth leaves the output first: it's an input now
      native_device_context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.f, 0);
      ID3D11ShaderResourceView* const inputs[2] = {depth_srv.get(), stencil_srv.get()};
      native_device_context->PSSetShaderResources(0, 2, inputs);
      ID3D11Buffer* const cb = gd->post_cb.get();
      native_device_context->PSSetConstantBuffers(0, 1, &cb);
      native_device_context->OMSetBlendState(device_data.default_blend_state.get(), nullptr, 0xFFFFFFFF);
      native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
      native_device_context->IASetInputLayout(nullptr);
      native_device_context->RSSetState(nullptr);
      SetViewportFullscreen(native_device_context, output_size);
      native_device_context->VSSetShader(scale_vs, nullptr, 0);
      native_device_context->PSSetShader(resample_ps, nullptr, 0);
      const uint2 render_size = {engine_desc.Width, engine_desc.Height};
      bool resampled = true;
      for (UINT i = 0; resampled && i < std::size(gd->resample_depth_stencil_states); i++)
      {
         const float rows[kPostMaxRows][4] = {{ float(render_size.x) / float(output_size.x),
            float(render_size.y) / float(output_size.y),
            std::bit_cast<float>(i == 0 ? 0u : (1u << (i - 1))) }};
         resampled = UploadPostConstants(native_device_context, gd, rows);
         if (resampled)
         {
            native_device_context->OMSetDepthStencilState(gd->resample_depth_stencil_states[i].get(), 0xFF);
            native_device_context->Draw(4, 0);
         }
      }
      resample_state.Restore(native_device_context);
      if (!resampled)
         return nullptr;
      gd->output_depth_stencil_engine = engine.get();
      return dsv;
   }

   // An engine post pixel pass at the output size (NVIDIA: every pass after the upscaler), into its target's twin of "size" (see
   // "GetOutputTargetView"). It reads the twins of its inputs; a render-sized input without one is stretched first (see
   // "GetStretchedTwin"); a depth or stencil test uses the scene's depth-stencil at the output size (see "GetOutputSizedDepthStencil").
   // "patch_constants" gets the first "row_count" rows of its b0 and its inputs' sizes; 0 rows keeps the engine's constants (size
   // agnostic passes).
   static bool DrawOutputSizedPostPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd,
      const std::function<void()>& original_draw, uint2 size, UINT row_count = 0, const std::function<void(float (*rows)[4], const uint2* input_sizes)>& patch_constants = {})
   {
      ID3D11DeviceContext1* const context1 = GetImmediateContext1(native_device_context, gd);
      com_ptr<ID3D11RenderTargetView> rtv;
      com_ptr<ID3D11DepthStencilView> engine_dsv;
      native_device_context->OMGetRenderTargets(1, &rtv, &engine_dsv);
      float rows[kPostMaxRows][4] = {};
      // The Scale shaders stretch inputs and downscale results into the engine's targets: no chain without them
      if (!context1 || !FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Scale VS")) || !FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Scale PS")) || !rtv || row_count > kPostMaxRows || (row_count != 0 && !ReadEngineConstants(context1, gd, true, rows, row_count)) ||
          !EnsurePostConstants(native_device, gd))
         return false;

      const uint2 render_size = {uint32_t(device_data.render_resolution.x), uint32_t(device_data.render_resolution.y)};
      const uint2 output_size = GetViewTextureSize(gd->sr_output_srv.get());
      constexpr UINT input_count = kPostMaxInputs;
      com_ptr<ID3D11ShaderResourceView> srvs[input_count];
      native_device_context->PSGetShaderResources(0, input_count, &srvs[0]);
      ID3D11ShaderResourceView* inputs[input_count] = {};
      uint2 input_sizes[input_count] = {};
      for (UINT i = 0; i < input_count; i++)
      {
         if (!srvs[i])
            continue;
         com_ptr<ID3D11Resource> resource;
         srvs[i]->GetResource(&resource);
         ID3D11ShaderResourceView* input = gd->FindOutputTwin(resource.get());
         if (!input && GetViewTextureSize(srvs[i].get()) == render_size)
         {
            input = GetStretchedTwin(native_device, native_device_context, device_data, gd, srvs[i].get(), output_size);
            if (!input)
               return false;
         }
         inputs[i] = (input ? input : srvs[i].get());
         input_sizes[i] = GetViewTextureSize(inputs[i]);
      }

      const ComPtr<ID3D11RenderTargetView> target = GetOutputTargetView(native_device, native_device_context, gd, rtv.get(), size);
      if (!target)
         return false;
      ID3D11DepthStencilView* depth_stencil = nullptr;
      if (engine_dsv)
      {
         com_ptr<ID3D11DepthStencilState> depth_stencil_state;
         UINT stencil_ref = 0;
         native_device_context->OMGetDepthStencilState(&depth_stencil_state, &stencil_ref);
         D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = {};
         if (depth_stencil_state)
         {
            depth_stencil_state->GetDesc(&depth_stencil_desc);
         }
         if (depth_stencil_desc.DepthEnable || depth_stencil_desc.StencilEnable)
         {
            depth_stencil = (size == output_size ? GetOutputSizedDepthStencil(native_device, native_device_context, device_data, gd, engine_dsv.get(), output_size) : nullptr);
            if (!depth_stencil)
               return false;
         }
      }
      if (row_count != 0)
      {
         patch_constants(rows, input_sizes);
         if (!UploadPostConstants(native_device_context, gd, rows))
            return false;
      }

      DrawStateStack<DrawStateStackType::FullGraphics> pass_state;
      pass_state.Cache(native_device_context, device_data.uav_max_count);
      if (row_count != 0)
      {
         // PS only: the VS reads none, or its own (the bloom's: its target's texel size, its triangles still cover the viewport)
         ID3D11Buffer* const cb = gd->post_cb.get();
         native_device_context->PSSetConstantBuffers(0, 1, &cb);
      }
      native_device_context->PSSetShaderResources(0, input_count, inputs);
      ID3D11RenderTargetView* const target_view = target.get();
      native_device_context->OMSetRenderTargets(1, &target_view, depth_stencil);
      SetViewportFullscreen(native_device_context, size);
      const D3D11_RECT scissor = {0, 0, LONG(size.x), LONG(size.y)}; // The engine's passes may scissor to the render size
      native_device_context->RSSetScissorRects(1, &scissor);
      original_draw();
      pass_state.Restore(native_device_context);
      return true;
   }

   // A motion blur pass while upscaling (see "shader_hashes_motion_blur"), at the output size with its size constants patched.
   // The output tiles are the engine's own at the output height, so the blur keeps the native length limit (2 tiles). The
   // engine picks its strides from the height alone, at any aspect (measured live at 16:9 and 21:9, see "tile_strides").
   // Below 900 it runs one stage of h / 36 px (776: 21, 864: 24). From 900 it runs two, and the tile is their product
   // (1080: 5 x 6, 2160: 9 x 7).
   // Constants (from the disassembly): tile passes c0.w motion scale, c1.y stride, c1.z the source texel's area in px, c2.xy
   // source size; neighbourhood c0.xy size, c1.x tile px, c1.z tile px squared, c2.xy tile counts; velocity c0.x tile px, c0.zw
   // size, c1.x shift; gather c0.y tile px, c2 = (size, tile counts), c1.w velocity shift. The gather's target is also
   // downscaled into the engine's for the passes that still read it at the render size (see "DownscaleIntoEngineTarget").
   static bool DrawOutputSizedMotionBlurPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd,
      uint32_t hash, const std::function<void()>& original_draw)
   {
      const uint2 render_size = {uint32_t(device_data.render_resolution.x), uint32_t(device_data.render_resolution.y)};
      const uint2 output_size = GetViewTextureSize(gd->sr_output_srv.get());
      if (render_size.y == 0)
         return false;
      const float ratio = float(output_size.y) / float(render_size.y);
      const auto ceil_div = [](uint32_t value, uint32_t divisor)
      { return (value + divisor - 1) / divisor; };

      // The first and second stage strides. A single stage is returned as (tile px, 1).
      const auto tile_strides = [](uint32_t height)
      {
         if (const uint32_t tile = height / 36; tile <= 24)
            return uint2{tile, 1u};
         const uint32_t first = height / 252 + 1;
         return uint2{first, ((height + first - 1) / first + 18) / 36};
      };
      const uint2 engine_strides = tile_strides(render_size.y);
      const uint2 output_strides = tile_strides(output_size.y);
      const uint32_t output_tile = output_strides.x * output_strides.y;

      bool drawn = false;
      switch (hash)
      {
      case kMBTileColumnsFromMVHash:
      case kMBTileRowsFromMVHash:
      case kMBTileColumnsHash:
      case kMBTileRowsHash:
      {
         // A column pass reduces x by its stride, a row pass y
         const bool columns = (hash == kMBTileColumnsFromMVHash || hash == kMBTileColumnsHash);
         com_ptr<ID3D11ShaderResourceView> source_srv;
         native_device_context->PSGetShaderResources(0, 1, &source_srv);
         if (!source_srv)
            return false;
         com_ptr<ID3D11Resource> source;
         source_srv->GetResource(&source);
         ID3D11ShaderResourceView* const source_twin = gd->FindOutputTwin(source.get());
         // The first stage's source is the motion vectors (stretched to the output size by the pass), the second's a twin
         const uint2 source_size = (source_twin ? GetViewTextureSize(source_twin) : output_size);
         uint32_t stride = 0;
         {
            ID3D11DeviceContext1* const context1 = GetImmediateContext1(native_device_context, gd);
            float engine_rows[2][4] = {};
            if (!context1 || !ReadEngineConstants(context1, gd, true, engine_rows, 2))
               return false;
            stride = std::bit_cast<uint32_t>(engine_rows[1][1]);
         }
         // The first stage reduces an axis still at the output size (its rows pass is 0xFDCCD82A too)
         const bool first_stage = (columns ? source_size.x == output_size.x : source_size.y == output_size.y);
         uint32_t output_stride = 0;
         if (first_stage && stride == engine_strides.x)
         {
            // A single engine stage stays single at the output size, and covers the whole output tile
            output_stride = (engine_strides.y == 1 ? output_tile : output_strides.x);
         }
         else if (!first_stage && stride == engine_strides.y)
         {
            output_stride = output_strides.y;
         }
         if (output_stride == 0)
         {
            FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false); // Not the measured formula
            return false;
         }
         if (first_stage)
         {
            gd->mb_stride = stride;
            gd->mb_output_stride = output_stride;
         }
         // The source texel's px per axis (1, the first stage's stride, the tile) at the output over the engine's: c1.z is its area
         const auto texel_scale = [&](uint32_t source, uint32_t full)
         {
            if (source == full)
               return 1.f;
            if (source == ceil_div(full, gd->mb_output_stride))
               return float(gd->mb_output_stride) / float((std::max)(gd->mb_stride, 1u));
            return float(output_tile) / float(engine_strides.x * engine_strides.y);
         };
         const float variance_scale = texel_scale(source_size.x, output_size.x) * texel_scale(source_size.y, output_size.y);
         const uint2 size = (columns ? uint2{ceil_div(source_size.x, output_stride), source_size.y} : uint2{source_size.x, ceil_div(source_size.y, output_stride)});
         drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, size, 3, [&](float (*cb)[4], const uint2* input_sizes)
            {
               cb[1][1] = std::bit_cast<float>(output_stride);
               cb[1][2] *= variance_scale;
               cb[2][0] = float(input_sizes[0].x);
               cb[2][1] = float(input_sizes[0].y); });
         break;
      }
      case kMBTileNeighbourhoodHash:
      {
         com_ptr<ID3D11ShaderResourceView> tiles_srv;
         native_device_context->PSGetShaderResources(0, 1, &tiles_srv);
         if (!tiles_srv)
            return false;
         com_ptr<ID3D11Resource> tiles;
         tiles_srv->GetResource(&tiles);
         ID3D11ShaderResourceView* const tiles_twin = gd->FindOutputTwin(tiles.get());
         if (!tiles_twin)
            return false;
         drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, GetViewTextureSize(tiles_twin), 3, [&](float (*cb)[4], const uint2* input_sizes)
            {
               const float tile = float(output_tile);
               cb[1][2] = tile * tile;
               cb[0][0] = float(output_size.x);
               cb[0][1] = float(output_size.y);
               cb[1][0] = tile;
               cb[2][0] = float(input_sizes[0].x);
               cb[2][1] = float(input_sizes[0].y); });
         break;
      }
      case kMBVelocityHash:
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (!rtv)
            return false;
         // The velocity buffer is at the size >> c1.x: the same shift at the output size
         const uint2 engine_size = GetViewTextureSize(rtv.get());
         const uint2 size = ScaleSize(engine_size, ratio);
         drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, size, 2, [&](float (*cb)[4], [[maybe_unused]] const uint2* input_sizes)
            {
               cb[0][0] = float(output_tile);
               cb[0][2] = float(output_size.x);
               cb[0][3] = float(output_size.y); });
         break;
      }
      default: // The gather
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (!rtv)
            return false;
         drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, output_size, 4, [&](float (*cb)[4], const uint2* input_sizes)
            {
               cb[0][1] = std::bit_cast<float>(output_tile);
               cb[2][0] = float(output_size.x);
               cb[2][1] = float(output_size.y);
               cb[2][2] = float(input_sizes[3].x);
               cb[2][3] = float(input_sizes[3].y); });
         if (!drawn)
            break;
         DownscaleIntoEngineTarget(native_device_context, device_data, gd, rtv.get());
         MEA_COUNT(motion_blurs_upscaled);
         break;
      }
      }
      if (!drawn)
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false);
      }
      return drawn;
   }

   // A pass's output-sized result also downscaled into its engine target, for the passes that still read it at the render size by
   // UV: the engine's own bloom chain, the fallback when Luma's can't run (see "BuildOutputSizedBloom")
   static void DownscaleIntoEngineTarget(ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd, ID3D11RenderTargetView* engine_rtv)
   {
      com_ptr<ID3D11Resource> engine;
      engine_rtv->GetResource(&engine);
      auto* const twin = gd->FindOutputTwinRecord(engine.get());
      if (twin && DrawScaled(native_device_context, device_data, device_data.sampler_state_linear.get(), twin->srv.get(), engine_rtv, GetViewTextureSize(engine_rtv)))
      {
         twin->engine_current = true;
      }
   }

   // A pass about to draw natively while upscaling (a fallback, or one no route knows): its inputs Luma drew at the output size in
   // place of the engine (twins whose engine texture isn't current) are scaled into their engine textures first, once. A plain render
   // target by "DownscaleIntoEngineTarget"; an array (the depth of field's far layers) slice by slice, and a texture that can't be a
   // render target through its UAV (see "Luma_MEA_ScaleArray"). Append buffers can't be restored.
   static void RestoreEngineInputs(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd, bool compute)
   {
      if (std::none_of(gd->output_twins.begin(), gd->output_twins.end(), [](const MassEffectAndromedaGameDeviceData::OutputTwin& twin)
             { return twin.luma && !twin.engine_current; }))
         return;
      constexpr UINT input_count = kPostMaxInputs;
      com_ptr<ID3D11ShaderResourceView> srvs[input_count];
      if (compute)
      {
         native_device_context->CSGetShaderResources(0, input_count, &srvs[0]);
      }
      else
      {
         native_device_context->PSGetShaderResources(0, input_count, &srvs[0]);
      }
      auto* const scale_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Scale VS"));
      auto* const scale_array_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("MEA Scale Array PS"));
      auto* const scale_array_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("MEA Scale Array CS"));
      // Binding an engine texture as a target unbinds it from the pass' inputs: both stages are restored after
      std::optional<DrawStateStack<DrawStateStackType::FullGraphics>> graphics_state;
      std::optional<DrawStateStack<DrawStateStackType::Compute>> compute_state;
      for (const auto& srv : srvs)
      {
         if (!srv)
            continue;
         com_ptr<ID3D11Resource> resource;
         srv->GetResource(&resource);
         auto* const twin = gd->FindOutputTwinRecord(resource.get());
         if (!twin || !twin->luma || twin->engine_current)
            continue;
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
         srv->GetDesc(&srv_desc);
         const bool array = (srv_desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
         if (!array && srv_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
            continue; // A buffer (the sprites' append buffers)
         D3D11_TEXTURE2D_DESC desc;
         static_cast<ID3D11Texture2D*>(resource.get())->GetDesc(&desc);
         const bool render_target = (desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
         if (!graphics_state)
         {
            graphics_state.emplace().Cache(native_device_context, device_data.uav_max_count);
            compute_state.emplace().Cache(native_device_context, device_data.uav_max_count);
         }
         const UINT mip = (array ? srv_desc.Texture2DArray.MostDetailedMip : srv_desc.Texture2D.MostDetailedMip);
         if (!array && render_target)
         {
            const CD3D11_RENDER_TARGET_VIEW_DESC rtv_desc(D3D11_RTV_DIMENSION_TEXTURE2D, srv_desc.Format, mip);
            ComPtr<ID3D11RenderTargetView> rtv;
            if (SUCCEEDED(native_device->CreateRenderTargetView(resource.get(), &rtv_desc, rtv.put())))
            {
               DownscaleIntoEngineTarget(native_device_context, device_data, gd, rtv.get());
            }
            continue;
         }
         // Slice by slice (one for a plain texture viewed as an array)
         D3D11_SHADER_RESOURCE_VIEW_DESC twin_srv_desc;
         twin->srv->GetDesc(&twin_srv_desc);
         const uint2 size = MipSize({desc.Width, desc.Height}, mip);
         const UINT first_slice = (array ? srv_desc.Texture2DArray.FirstArraySlice : 0);
         const UINT slice_count = (array ? srv_desc.Texture2DArray.ArraySize : 1);
         bool restored = scale_vs != nullptr && scale_array_ps != nullptr && scale_array_cs != nullptr && (render_target || (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0) &&
                         EnsurePostConstants(native_device, gd);
         for (UINT slice = first_slice; restored && slice < first_slice + slice_count; slice++)
         {
            const CD3D11_SHADER_RESOURCE_VIEW_DESC slice_srv_desc(D3D11_SRV_DIMENSION_TEXTURE2DARRAY, twin_srv_desc.Format, 0, 1, slice, 1);
            ComPtr<ID3D11ShaderResourceView> source;
            const float rows[kPostMaxRows][4] = {{ std::bit_cast<float>(slice),
               0.f,
               float(size.x),
               float(size.y) }};
            restored = SUCCEEDED(native_device->CreateShaderResourceView(twin->luma.get(), &slice_srv_desc, source.put())) && UploadPostConstants(native_device_context, gd, rows);
            if (!restored)
               break;
            ID3D11Buffer* const cb = gd->post_cb.get();
            ID3D11SamplerState* const sampler = device_data.sampler_state_linear.get();
            ID3D11ShaderResourceView* const source_view = source.get();
            if (render_target)
            {
               const CD3D11_RENDER_TARGET_VIEW_DESC rtv_desc(D3D11_RTV_DIMENSION_TEXTURE2DARRAY, srv_desc.Format, mip, slice, 1);
               ComPtr<ID3D11RenderTargetView> rtv;
               restored = SUCCEEDED(native_device->CreateRenderTargetView(resource.get(), &rtv_desc, rtv.put()));
               if (!restored)
                  break;
               native_device_context->PSSetConstantBuffers(0, 1, &cb);
               DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), sampler, scale_vs, scale_array_ps, source_view,
                  rtv.get(), size.x, size.y);
            }
            else
            {
               const CD3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc(D3D11_UAV_DIMENSION_TEXTURE2DARRAY, srv_desc.Format, mip, slice, 1);
               ComPtr<ID3D11UnorderedAccessView> uav;
               restored = SUCCEEDED(native_device->CreateUnorderedAccessView(resource.get(), &uav_desc, uav.put()));
               if (!restored)
                  break;
               ID3D11UnorderedAccessView* const no_uav = nullptr;
               native_device_context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr); // UAV first: an SRV still bound as UAV gets nulled
               native_device_context->CSSetShader(scale_array_cs, nullptr, 0);
               native_device_context->CSSetConstantBuffers(0, 1, &cb);
               native_device_context->CSSetSamplers(0, 1, &sampler);
               native_device_context->CSSetShaderResources(0, 1, &source_view);
               ID3D11UnorderedAccessView* const target_view = uav.get();
               native_device_context->CSSetUnorderedAccessViews(0, 1, &target_view, nullptr);
               native_device_context->Dispatch((size.x + 7) / 8, (size.y + 7) / 8, 1);
               ID3D11ShaderResourceView* const no_srv = nullptr;
               native_device_context->CSSetShaderResources(0, 1, &no_srv);
               native_device_context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr);
            }
         }
         twin->engine_current = restored;
      }
      if (graphics_state)
      {
         compute_state->Restore(native_device_context);
         graphics_state->Restore(native_device_context);
      }
   }

   // A post pass failed at the output size while upscaling (DLSS checklist POST-9: no half-redirected chain): it and the rest of the frame's
   // post run natively at the render size ("post_fallback"), on engine resources, its inputs restored (see "RestoreEngineInputs"). A
   // depth of field failing after its sprite build loses that frame's bokeh (its append buffers can't be restored).
   static void FallBackToRenderSize(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, bool compute)
   {
      MEA_COUNT(post_pass_failures);
      gd->post_fallback = true;
      RestoreEngineInputs(native_device, native_device_context, device_data, gd, compute);
   }

   // The depth of field's CoC (signed px, the DOF resolve's output) at the output size, in output px: upsampled guided by the depth of
   // field's color at both sizes (see "Luma_MEA_ScaleCoC") and scaled by output / render. The twin of the engine's CoC, made by the
   // first pass that reads it.
   static ID3D11ShaderResourceView* GetOutputSizedCoC(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd,
      ID3D11ShaderResourceView* engine_coc, ID3D11ShaderResourceView* render_color, ID3D11ShaderResourceView* output_color, uint2 output_size, float ratio)
   {
      com_ptr<ID3D11Resource> engine;
      engine_coc->GetResource(&engine);
      if (ID3D11ShaderResourceView* const twin = gd->FindOutputTwin(engine.get()))
         return twin;
      auto* const scale_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Scale VS"));
      auto* const scale_coc_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("MEA Scale CoC PS"));
      if (scale_vs == nullptr || scale_coc_ps == nullptr || !EnsurePostConstants(native_device, gd))
         return nullptr;
      auto* const target = AcquireOutputTarget(native_device, native_device_context, gd, DXGI_FORMAT_R16_FLOAT, output_size, {.clear = false});
      if (!target)
         return nullptr;
      const uint2 render_size = GetViewTextureSize(engine_coc);
      float rows[kPostMaxRows][4] = {{ float(render_size.x) / float(output_size.x),
         float(render_size.y) / float(output_size.y),
         ratio }};
      if (!UploadPostConstants(native_device_context, gd, rows))
         return nullptr;
      DrawStateStack<DrawStateStackType::FullGraphics> coc_state;
      coc_state.Cache(native_device_context, device_data.uav_max_count);
      ID3D11Buffer* const cb = gd->post_cb.get();
      native_device_context->PSSetConstantBuffers(0, 1, &cb);
      ID3D11ShaderResourceView* const guides[2] = {render_color, output_color};
      native_device_context->PSSetShaderResources(1, 2, guides);
      // DrawCustomPixelShader does NOT restore state
      DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, scale_vs, scale_coc_ps, engine_coc,
         target->rtv.get(), output_size.x, output_size.y);
      coc_state.Restore(native_device_context);
      gd->output_twins.push_back({.engine = engine.get(), .srv = target->srv.get(), .luma = target->texture.get(), .engine_current = true});
      return target->srv.get();
   }

   // A depth of field pass while upscaling (see "kDoFBlurPasses"), at the output size. Every pass binds the same b0 header
   // (live, 652x388): c0 = (W u32, H u32, 1 / W, 1 / H), c1 = (W u32, H u32, 1 / W, aspect), c2 = (1, 1, 9, 17: the CoC of the
   // half and quarter layers), c3 = (10: the small blur's radius and the sprites' CoC floor, 0, 1: the CoC merge tolerance, 0.2),
   // c5 = (8 far layers u32, 2: their CoC step, 3.5, 0.3: their CoC margin). Only the sizes change: at native 4K the engine
   // keeps every px constant of 1080p and scales the CoC alone (21.6 vs 10.8 px, measured live).
   // The sprites go to Luma's append buffers (one element per output pixel, as the engine's per render pixel), whose counts the
   // engine's CopyStructureCount reads (see "OnCopyBufferRegion"). The bokeh sprites are in NDC: their draws only change targets.
   static bool DrawOutputSizedDoFPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, uint32_t hash, const std::function<void()>& original_draw)
   {
      const uint2 render_size = {uint32_t(device_data.render_resolution.x), uint32_t(device_data.render_resolution.y)};
      const uint2 output_size = GetViewTextureSize(gd->sr_output_srv.get());
      if (render_size.y == 0)
         return false;
      const float ratio = float(output_size.y) / float(render_size.y);

      ID3D11DeviceContext1* const context1 = GetImmediateContext1(native_device_context, gd);
      if (!context1)
         return false;

      const auto* const blur = std::find_if(std::begin(kDoFBlurPasses), std::end(kDoFBlurPasses), [hash](const DoFBlurPass& pass)
         { return pass.hash == hash; });
      const auto* const build = std::find_if(std::begin(kDoFSpriteBuildPasses), std::end(kDoFSpriteBuildPasses), [hash](const DoFSpriteBuildPass& pass)
         { return pass.hash == hash; });
      const bool blur_pass = blur != std::end(kDoFBlurPasses);
      const bool build_pass = build != std::end(kDoFSpriteBuildPasses);

      bool drawn = false;
      if (blur_pass || build_pass || hash == kDoFSpriteBinHash)
      {
         constexpr UINT srv_count = 12;
         constexpr UINT uav_count = 8;
         com_ptr<ID3D11ShaderResourceView> srvs[srv_count];
         native_device_context->CSGetShaderResources(0, srv_count, &srvs[0]);
         com_ptr<ID3D11UnorderedAccessView> uavs[uav_count];
         native_device_context->CSGetUnorderedAccessViews(0, uav_count, &uavs[0]);
         float rows[kPostMaxRows][4] = {};
         if (!ReadEngineConstants(context1, gd, false, rows, 6) || !EnsurePostConstants(native_device, gd))
            return false;

         ID3D11ShaderResourceView* inputs[srv_count] = {};
         for (UINT i = 0; i < srv_count; i++)
         {
            if (!srvs[i])
               continue;
            com_ptr<ID3D11Resource> resource;
            srvs[i]->GetResource(&resource);
            inputs[i] = gd->FindOutputTwin(resource.get());
            if (!inputs[i] && i == 9 && srvs[0] && inputs[0])
            {
               // The CoC's first reader (the x blur or the build) also reads the color (t0) at both sizes: its upsample guide
               inputs[i] = GetOutputSizedCoC(native_device, native_device_context, device_data, gd, srvs[i].get(), srvs[0].get(), inputs[0], output_size, ratio);
            }
            else if (!inputs[i] && i != 9 && GetViewTextureSize(srvs[i].get()) == render_size)
            {
               inputs[i] = GetStretchedTwin(native_device, native_device_context, device_data, gd, srvs[i].get(), output_size); // The half build's depth (t1)
            }
            if (!inputs[i])
               return false; // Every input is this frame's post (color, CoC, the x pass, the sprites) or stretched
         }
         ID3D11UnorderedAccessView* outputs[uav_count] = {};
         UINT initial_counts[uav_count];
         for (UINT i = 0; i < uav_count; i++)
         {
            initial_counts[i] = UINT(-1);
            if (!uavs[i])
               continue;
            outputs[i] = uavs[i].get(); // The sprite counters (u6) stay the engine's
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            uavs[i]->GetDesc(&uav_desc);
            com_ptr<ID3D11Resource> resource;
            uavs[i]->GetResource(&resource);
            if (uav_desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D)
            {
               auto* const target = AcquireOutputTarget(native_device, native_device_context, gd, uav_desc.Format, output_size, {.unordered_access = true});
               if (!target)
                  return false;
               outputs[i] = target->uav.get();
               gd->output_twins.push_back({.engine = resource.get(), .srv = target->srv.get(), .luma = target->texture.get(), .uav = target->uav.get()});
            }
            else if (uav_desc.ViewDimension == D3D11_UAV_DIMENSION_BUFFER && uav_desc.Format == DXGI_FORMAT_UNKNOWN) // Structured: the sprite buckets
            {
               // The binning appends to the buckets that already have a twin this frame and fills the far layers' own, which the build
               // doesn't write. A bucket without a twin must never be left to the engine: its buffer holds the sprites of the last
               // frame the engine built natively, which its sprite draws would add again (a long gone object kept in the bokeh).
               const auto* const twin = (build_pass ? nullptr : gd->FindOutputTwinRecord(resource.get()));
               if (twin && twin->uav)
               {
                  outputs[i] = twin->uav.get();
               }
               else
               {
                  D3D11_BUFFER_DESC buffer_desc;
                  static_cast<ID3D11Buffer*>(resource.get())->GetDesc(&buffer_desc);
                  const UINT elements = UINT(std::ceil(float(uav_desc.Buffer.NumElements) * float(output_size.x) * float(output_size.y) / (float(render_size.x) * float(render_size.y))));
                  auto* const buffer = AcquireOutputBuffer(native_device, gd, elements, buffer_desc.StructureByteStride, uav_desc.Buffer.Flags);
                  if (!buffer)
                     return false;
                  gd->output_twins.push_back({.engine = resource.get(), .srv = buffer->srv.get(), .luma = buffer->buffer.get(), .uav = buffer->uav.get()});
                  outputs[i] = buffer->uav.get();
                  initial_counts[i] = 0; // Every bucket starts the frame empty, as the engine's build leaves it
               }
            }
         }

         PatchPostHeader(rows, output_size);
         if (!UploadPostConstants(native_device_context, gd, rows))
            return false;
         DrawStateStack<DrawStateStackType::Compute> compute_state;
         compute_state.Cache(native_device_context, device_data.uav_max_count);
         ID3D11Buffer* const cb = gd->post_cb.get();
         native_device_context->CSSetConstantBuffers(0, 1, &cb);
         ID3D11UnorderedAccessView* const no_uavs[uav_count] = {};
         native_device_context->CSSetUnorderedAccessViews(0, uav_count, no_uavs, nullptr); // UAVs first: an SRV still bound as UAV gets nulled
         native_device_context->CSSetShaderResources(0, srv_count, inputs);
         native_device_context->CSSetUnorderedAccessViews(0, uav_count, outputs, initial_counts);
         if (blur_pass && blur->columns)
         {
            native_device_context->Dispatch(output_size.x, (output_size.y + 127) / 128, 1);
         }
         else if (blur_pass)
         {
            native_device_context->Dispatch((output_size.x + 127) / 128, output_size.y, 1);
         }
         else if (build_pass)
         {
            const UINT group_pixels = (build->half ? 32 : 16);
            native_device_context->Dispatch((output_size.x + group_pixels - 1) / group_pixels, (output_size.y + group_pixels - 1) / group_pixels, 1);
         }
         else
         {
            original_draw(); // The binning strides over the buckets: its dispatch doesn't depend on the size
         }
         compute_state.Restore(native_device_context);
         drawn = true;
      }
      else if (hash == kDoFSpriteHash)
      {
         // The sprites (VS t3) of a bucket into a layer (out, out / 2, out / 4, or a slice of the far layers' array)
         com_ptr<ID3D11ShaderResourceView> sprites_srv;
         native_device_context->VSGetShaderResources(3, 1, &sprites_srv);
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (!sprites_srv || !rtv)
            return false;
         com_ptr<ID3D11Resource> sprites;
         sprites_srv->GetResource(&sprites);
         ID3D11ShaderResourceView* const sprites_twin = gd->FindOutputTwin(sprites.get());
         if (!sprites_twin)
            return false;
         const uint2 size = ScaleSize(GetViewTextureSize(rtv.get()), ratio);
         const ComPtr<ID3D11RenderTargetView> target = GetOutputTargetView(native_device, native_device_context, gd, rtv.get(), size);
         if (!target)
            return false;
         DrawStateStack<DrawStateStackType::FullGraphics> sprite_state;
         sprite_state.Cache(native_device_context, device_data.uav_max_count);
         native_device_context->VSSetShaderResources(3, 1, &sprites_twin);
         ID3D11RenderTargetView* const target_view = target.get();
         native_device_context->OMSetRenderTargets(1, &target_view, nullptr);
         SetViewportFullscreen(native_device_context, size);
         original_draw();
         sprite_state.Restore(native_device_context);
         gd->dof_layers_open = true;
         drawn = true;
      }
      else if (hash == kDoFLayerResampleHash)
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (!rtv)
            return false;
         drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, ScaleSize(GetViewTextureSize(rtv.get()), ratio));
      }
      else // The composite
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         gd->dof_layers_open = false;
         if (!rtv)
            return false;
         // A permutation without the blur reads the CoC first here: guided by its color (t0) too
         com_ptr<ID3D11ShaderResourceView> color_srv;
         native_device_context->PSGetShaderResources(0, 1, &color_srv);
         com_ptr<ID3D11ShaderResourceView> coc_srv;
         native_device_context->PSGetShaderResources(9, 1, &coc_srv);
         if (color_srv && coc_srv)
         {
            com_ptr<ID3D11Resource> color;
            color_srv->GetResource(&color);
            ID3D11ShaderResourceView* const color_twin = gd->FindOutputTwin(color.get());
            if (!color_twin || !GetOutputSizedCoC(native_device, native_device_context, device_data, gd, coc_srv.get(), color_srv.get(), color_twin, output_size, ratio))
               return false;
         }
         drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, output_size, 6, [&](float (*cb)[4], [[maybe_unused]] const uint2* input_sizes)
            { PatchPostHeader(cb, output_size); });
#if DEVELOPMENT
         if (drawn)
         {
            com_ptr<ID3D11ShaderResourceView> debug_srvs[kPostMaxInputs];
            native_device_context->PSGetShaderResources(0, kPostMaxInputs, &debug_srvs[0]);
            for (UINT i = 0; i < kPostMaxInputs; i++)
            {
               gd->dof_debug_inputs[i].reset();
               if (!debug_srvs[i])
                  continue;
               com_ptr<ID3D11Resource> resource;
               debug_srvs[i]->GetResource(&resource);
               gd->dof_debug_inputs[i] = gd->FindOutputTwin(resource.get());
            }
            com_ptr<ID3D11Resource> target;
            rtv->GetResource(&target);
            gd->dof_debug_output = gd->FindOutputTwin(target.get());
         }
#endif
         if (drawn)
         {
            DownscaleIntoEngineTarget(native_device_context, device_data, gd, rtv.get());
            MEA_COUNT(dof_composites_upscaled);
         }
      }
      if (!drawn)
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, (blur_pass || build_pass || hash == kDoFSpriteBinHash));
      }
      return drawn;
   }

   // The bloom pyramid at the output size, at the engine's first pyramid pass while upscaling. Luma's pyramid becomes the twin of
   // the engine's target (the pass' t0): mip 0 copied from that target's twin, each next mip L blurred from mip L - 1 by the
   // engine's shaders with the constants its native build has at these sizes (measured live at 0.5 and 1.0). For S the size of
   // mip L - 1 and D of mip L: the columns into a temporary of (D.x, S.y), then the rows into mip L; c0.zw = the input's texel
   // size, c1 = (S.x / D.x, S.y / D.y for the rows else 1, the pass' output size u32), c2 = (the input's mip u32, S u32). The
   // variant is the one the engine picks for the axis' ratio. An axis that doesn't shrink gets no pass, the other one writes the
   // mip (the engine leaves that last mip stale). The engine's own passes still run after it, at the render size.
   static bool BuildOutputSizedBloom(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd)
   {
      com_ptr<ID3D11ShaderResourceView> scene_srv;
      native_device_context->CSGetShaderResources(0, 1, &scene_srv);
      if (!scene_srv || !EnsurePostConstants(native_device, gd))
         return false;
      for (const auto& shader : gd->bloom_shaders)
      {
         if (!shader)
            return false; // Not dispatched by the engine yet
      }
      com_ptr<ID3D11Resource> scene;
      scene_srv->GetResource(&scene);
      ID3D11ShaderResourceView* const scene_twin = gd->FindOutputTwin(scene.get());
      if (!scene_twin)
         return false;
      com_ptr<ID3D11Resource> twin;
      scene_twin->GetResource(&twin);
      D3D11_TEXTURE2D_DESC twin_desc;
      static_cast<ID3D11Texture2D*>(twin.get())->GetDesc(&twin_desc);
      const uint2 output_size = {twin_desc.Width, twin_desc.Height};

      D3D11_TEXTURE2D_DESC pyramid_desc = {};
      if (gd->bloom_pyramid)
      {
         gd->bloom_pyramid->GetDesc(&pyramid_desc);
      }
      if (pyramid_desc.Width != output_size.x || pyramid_desc.Height != output_size.y || pyramid_desc.Format != twin_desc.Format)
      {
         gd->ReleaseBloomPyramid();
         const CD3D11_TEXTURE2D_DESC desc(twin_desc.Format, output_size.x, output_size.y, 1, 0, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS); // Every mip
         bool created = SUCCEEDED(native_device->CreateTexture2D(&desc, nullptr, gd->bloom_pyramid.put())) &&
                        SUCCEEDED(native_device->CreateShaderResourceView(gd->bloom_pyramid.get(), nullptr, gd->bloom_pyramid_srv.put()));
         if (created)
         {
            gd->bloom_pyramid->GetDesc(&pyramid_desc);
         }
         for (UINT mip = 0; created && mip < pyramid_desc.MipLevels; mip++)
         {
            const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(D3D11_SRV_DIMENSION_TEXTURE2D, pyramid_desc.Format, mip, 1);
            const CD3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc(D3D11_UAV_DIMENSION_TEXTURE2D, pyramid_desc.Format, mip);
            created = SUCCEEDED(native_device->CreateShaderResourceView(gd->bloom_pyramid.get(), &srv_desc, gd->bloom_mip_srvs.emplace_back().put())) &&
                      SUCCEEDED(native_device->CreateUnorderedAccessView(gd->bloom_pyramid.get(), &uav_desc, gd->bloom_mip_uavs.emplace_back().put()));
         }
         if (!created)
         {
            gd->ReleaseBloomPyramid();
            return false;
         }
      }
      native_device_context->CopySubresourceRegion(gd->bloom_pyramid.get(), 0, 0, 0, 0, twin.get(), 0, nullptr);

      // One pass of "bloom_shaders[shader]", "input" into "output" of "size", for the level from "source_size"
      const auto blur = [&](size_t shader, ID3D11ShaderResourceView* input, uint2 input_size, ID3D11UnorderedAccessView* output, uint2 size, uint2 source_size, float ratio_y)
      {
         // c2.x: mip 0 of the input, Luma's views are per mip
         const float rows[kPostMaxRows][4] = {{0.f, 0.f, 1.f / float(input_size.x), 1.f / float(input_size.y)},
            {float(source_size.x) / float(size.x), ratio_y, std::bit_cast<float>(size.x), std::bit_cast<float>(size.y)},
            { 0.f,
               std::bit_cast<float>(source_size.x),
               std::bit_cast<float>(source_size.y) }};
         if (!UploadPostConstants(native_device_context, gd, rows))
            return false;
         ID3D11UnorderedAccessView* const no_uav = nullptr;
         native_device_context->CSSetUnorderedAccessViews(0, 1, &no_uav, nullptr); // UAV first: an SRV still bound as UAV gets nulled
         native_device_context->CSSetShaderResources(0, 1, &input);
         native_device_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
         native_device_context->CSSetShader(gd->bloom_shaders[shader].get(), nullptr, 0);
         native_device_context->Dispatch((size.x + 7) / 8, (size.y + 7) / 8, 1);
         return true;
      };
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      compute_state.Cache(native_device_context, device_data.uav_max_count);
      ID3D11Buffer* const cb = gd->post_cb.get();
      native_device_context->CSSetConstantBuffers(0, 1, &cb);
      bool built = true;
      for (UINT mip = 1; built && mip < pyramid_desc.MipLevels; mip++)
      {
         const uint2 source_size = MipSize(output_size, mip - 1);
         const uint2 size = MipSize(output_size, mip);
         const bool columns = size.x != source_size.x;
         const bool rows = size.y != source_size.y;
         ID3D11ShaderResourceView* input = gd->bloom_mip_srvs[mip - 1].get();
         uint2 input_size = source_size;
         if (columns)
         {
            const uint2 columns_size = {size.x, source_size.y};
            ID3D11UnorderedAccessView* output = gd->bloom_mip_uavs[mip].get();
            ID3D11ShaderResourceView* temporary_srv = nullptr;
            if (rows)
            {
               auto* const temporary = AcquireOutputTarget(native_device, native_device_context, gd, pyramid_desc.Format, columns_size, {.unordered_access = true, .clear = false});
               if (!temporary)
               {
                  built = false;
                  break;
               }
               output = temporary->uav.get();
               temporary_srv = temporary->srv.get();
            }
            const size_t shader = (mip == 1 ? 0 : (source_size.x == size.x * 2 ? 2 : 4));
            built = blur(shader, input, input_size, output, columns_size, source_size, 1.f);
            input = temporary_srv;
            input_size = columns_size;
         }
         if (built && rows)
         {
            const size_t shader = (mip == 1 ? 1 : (source_size.y == size.y * 2 ? 3 : 5));
            built = blur(shader, input, input_size, gd->bloom_mip_uavs[mip].get(), size, source_size, float(source_size.y) / float(size.y));
         }
      }
      compute_state.Restore(native_device_context);
      if (!built)
         return false;
      gd->output_twins.push_back({.engine = scene.get(), .srv = gd->bloom_pyramid_srv.get(), .luma = gd->bloom_pyramid.get(), .engine_current = true});
      gd->bloom_scene = scene.get();
      MEA_COUNT(bloom_pyramids_upscaled);
      return true;
   }

   // The bloom's 1x1 ("kDoFLayerResampleHash") and upsamples while upscaling, after "BuildOutputSizedBloom" (false: a pass reading
   // another target). Each draws into the output pyramid's mip that is its engine target's counted from the last mip, from Luma's
   // pyramid, the header patched (see "PatchPostHeader"). The upsample's weight c3.xyz is 1 / the upsample count (one per mip from
   // the second last to mip 1; live 1 / 9 at 1080p, 1 / 10 at 4K), rescaled to the output's. Over twice the render size the output
   // has a mip more: the upsample into the engine's mip 1 is then followed by the same draw into each extra mip, reading the
   // previous result. The tonemap reads the last, the latest twin of the engine's target.
   static bool DrawOutputSizedBloomPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, bool upsample, const std::function<void()>& original_draw)
   {
      com_ptr<ID3D11ShaderResourceView> scene_srv;
      native_device_context->PSGetShaderResources(0, 1, &scene_srv);
      if (!scene_srv)
         return false;
      com_ptr<ID3D11Resource> scene;
      scene_srv->GetResource(&scene);
      if (scene.get() != gd->bloom_scene.get())
         return false;

      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      D3D11_TEXTURE2D_DESC scene_desc;
      static_cast<ID3D11Texture2D*>(scene.get())->GetDesc(&scene_desc);
      D3D11_TEXTURE2D_DESC pyramid_desc;
      gd->bloom_pyramid->GetDesc(&pyramid_desc);
      const uint2 scene_size = {scene_desc.Width, scene_desc.Height};
      const uint2 output_size = {pyramid_desc.Width, pyramid_desc.Height};
      UINT mip = 1;
      while (rtv && mip < scene_desc.MipLevels && !(MipSize(scene_size, mip) == GetViewTextureSize(rtv.get())))
      {
         mip++;
      }
      if (!rtv || mip == scene_desc.MipLevels)
         return false; // Another pass reading the scene, into a target of no mip's size
      if (scene_desc.MipLevels < 3 || pyramid_desc.MipLevels < scene_desc.MipLevels)
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false);
         return false;
      }
      const UINT extra_mips = pyramid_desc.MipLevels - scene_desc.MipLevels;
      const float weight_scale = float(scene_desc.MipLevels - 2) / float(pyramid_desc.MipLevels - 2);
      bool drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, MipSize(output_size, mip + extra_mips), upsample ? 5 : 4,
         [&](float (*cb)[4], [[maybe_unused]] const uint2* input_sizes)
         {
            PatchPostHeader(cb, output_size);
            if (upsample)
            {
               cb[3][0] *= weight_scale;
               cb[3][1] *= weight_scale;
               cb[3][2] *= weight_scale;
            }
         });
      if (drawn && upsample && mip == 1)
      {
         com_ptr<ID3D11Resource> engine_target;
         rtv->GetResource(&engine_target);
         ID3D11Buffer* const cb = gd->post_cb.get(); // Still the patched constants
         for (UINT output_mip = extra_mips; drawn && output_mip >= 1; output_mip--)
         {
            ID3D11ShaderResourceView* const inputs[2] = {gd->bloom_pyramid_srv.get(), gd->FindOutputTwin(engine_target.get())};
            const uint2 size = MipSize(output_size, output_mip);
            const ComPtr<ID3D11RenderTargetView> target = GetOutputTargetView(native_device, native_device_context, gd, rtv.get(), size);
            drawn = target != nullptr;
            if (!drawn)
               break;
            DrawStateStack<DrawStateStackType::FullGraphics> upsample_state;
            upsample_state.Cache(native_device_context, device_data.uav_max_count);
            native_device_context->PSSetConstantBuffers(0, 1, &cb);
            native_device_context->PSSetShaderResources(0, 2, inputs);
            ID3D11RenderTargetView* const target_view = target.get();
            native_device_context->OMSetRenderTargets(1, &target_view, nullptr);
            SetViewportFullscreen(native_device_context, size);
            const D3D11_RECT scissor = {0, 0, LONG(size.x), LONG(size.y)};
            native_device_context->RSSetScissorRects(1, &scissor);
            original_draw();
            upsample_state.Restore(native_device_context);
         }
         if (drawn)
         {
            MEA_COUNT(blooms_upscaled);
         }
      }
      if (!drawn)
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false);
      }
      return drawn;
   }

   // A scanner overlay pass while upscaling (see "shader_hashes_scanner"), into its render-sized target's twin at the output
   // size (false: a target of another size). The meshes, the glow mask and the glow's add test the scene's depth-stencil
   // there. The add ("kDoFLayerResampleHash") is routed here once the overlay ran ("scanner_open").
   static bool DrawOutputSizedScannerPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, const std::function<void()>& original_draw)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      const uint2 render_size = {uint32_t(device_data.render_resolution.x), uint32_t(device_data.render_resolution.y)};
      if (!rtv || !(GetViewTextureSize(rtv.get()) == render_size))
         return false;
      if (!DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, GetViewTextureSize(gd->sr_output_srv.get())))
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false);
         return false;
      }
      gd->scanner_open = true;
      MEA_COUNT(scanner_passes_upscaled);
      return true;
   }

   // A blur of "kPostBlurPasses" while upscaling, on an input Luma drew at the output size (false: another effect's, at the render
   // size): into its render-sized target's twin, at the engine's px in output texels
   static bool DrawOutputSizedBlurPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, const PostBlurPass& blur, const std::function<void()>& original_draw)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      com_ptr<ID3D11ShaderResourceView> input_srv;
      native_device_context->PSGetShaderResources(0, 1, &input_srv);
      const uint2 render_size = {uint32_t(device_data.render_resolution.x), uint32_t(device_data.render_resolution.y)};
      if (!rtv || !input_srv || !(GetViewTextureSize(rtv.get()) == render_size))
         return false;
      com_ptr<ID3D11Resource> input;
      input_srv->GetResource(&input);
      if (!gd->FindOutputTwin(input.get()))
         return false;
      const uint2 output_size = GetViewTextureSize(gd->sr_output_srv.get());
      const float texel_scale = (blur.columns ? float(render_size.x) / float(output_size.x) : float(render_size.y) / float(output_size.y));
      const UINT row_count = 1 + (UINT(std::bit_width(uint32_t(blur.offsets))) + 1) / 2;
      const bool drawn = DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, output_size, row_count, [&](float (*cb)[4], [[maybe_unused]] const uint2* input_sizes)
         {
            cb[0][0] = 1.f / float(output_size.x);
            cb[0][1] = 1.f / float(output_size.y);
            for (UINT i = 0; i < 16; i++)
            {
               if ((blur.offsets & (1u << i)) != 0)
               {
                  cb[1 + i / 2][(i % 2) * 2] *= texel_scale;
               }
            } });
      if (!drawn)
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false);
         return false;
      }
      MEA_COUNT(post_blurs_upscaled);
      return true;
   }

   // Any other draw before the tonemap into a target Luma drew at the output size this frame (a permutation no route lists): into the
   // twin at its size, the engine's constants kept. Else it would land in the engine's target, which nothing reads anymore.
   static bool DrawIntoOutputTwin(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, const std::function<void()>& original_draw)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      if (!rtv)
         return false;
      com_ptr<ID3D11Resource> target;
      rtv->GetResource(&target);
      const auto* const twin = gd->FindOutputTwinRecord(target.get());
      if (!twin || !twin->luma || twin->engine_current)
         return false;
      D3D11_TEXTURE2D_DESC twin_desc;
      static_cast<ID3D11Texture2D*>(twin->luma.get())->GetDesc(&twin_desc);
      if (!DrawOutputSizedPostPass(native_device, native_device_context, device_data, gd, original_draw, {twin_desc.Width, twin_desc.Height}))
      {
         FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, gd, false);
         return false;
      }
      MEA_COUNT(twin_draws);
      return true;
   }

   // The tonemap while upscaling. Reading a target with a twin (u2 or the motion blur's), it draws from the twin into
   // "upscaled_scene" at the output size; RT1, the FXAA luma, is left out (all targets must share a size, and FXAA doesn't
   // run with TAA). Else (false, or the post fell back) it draws natively at the render size and is stretched at the first read.
   static bool DrawUpscaledTonemap(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, const std::function<void()>& original_draw)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      com_ptr<ID3D11ShaderResourceView> scene_srv;
      native_device_context->PSGetShaderResources(0, 1, &scene_srv);
      if (!rtv || !scene_srv)
         return false;
      rtv->GetResource(gd->tonemap_target.put());
      com_ptr<ID3D11Resource> scene;
      scene_srv->GetResource(&scene);
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      rtv->GetDesc(&rtv_desc);
      const uint2 size = GetViewTextureSize(gd->sr_output_srv.get());
      ID3D11ShaderResourceView* const upscaled_color = gd->FindOutputTwin(scene.get());
      if (gd->post_fallback || !upscaled_color || !EnsureUpscaledScene(native_device, gd, size, rtv_desc.Format))
      {
         RestoreEngineInputs(native_device, native_device_context, device_data, gd, false); // The bloom
         return false;
      }
#if ENABLE_SMAA
      const bool rcas = PrepareRCASInput(native_device, device_data, gd, size);
#else
      constexpr bool rcas = false;
#endif
      DrawStateStack<DrawStateStackType::SimpleGraphics> tonemap_state;
      tonemap_state.Cache(native_device_context, device_data.uav_max_count);
      native_device_context->PSSetShaderResources(0, 1, &upscaled_color);
      // The other inputs with a twin too: the bloom (t2 or t3 by permutation) once it ran at the output size (see
      // "DrawOutputSizedBloomPass")
      constexpr UINT input_count = 6;
      com_ptr<ID3D11ShaderResourceView> srvs[input_count];
      native_device_context->PSGetShaderResources(0, input_count, &srvs[0]);
      for (UINT i = 1; i < input_count; i++)
      {
         if (!srvs[i])
            continue;
         com_ptr<ID3D11Resource> resource;
         srvs[i]->GetResource(&resource);
         if (ID3D11ShaderResourceView* const twin = gd->FindOutputTwin(resource.get()))
         {
            native_device_context->PSSetShaderResources(i, 1, &twin);
         }
      }
      ID3D11RenderTargetView* const target = (rcas ? gd->tex_rcas_input_rtv.get() : gd->upscaled_scene_rtv.get());
      native_device_context->OMSetRenderTargets(1, &target, nullptr);
      SetViewportFullscreen(native_device_context, size);
      original_draw();
      tonemap_state.Restore(native_device_context);
#if ENABLE_SMAA
      if (rcas)
      {
         MEA_COUNT(rcas_draws);
         DrawRCAS(native_device_context, device_data, gd->tex_rcas_input_srv.get(), gd->upscaled_scene_rtv.get(), g_rcas_sharpness);
      }
#endif
      gd->tonemap_upscaled = true;
      gd->upscaled_scene_filled = true;
      MEA_COUNT(tonemaps_upscaled);
      return true;
   }

   // A draw after the tonemap that reads or writes the engine's tonemap target uses "upscaled_scene" instead: the letterbox bars
   // draw into it at the output size, the present reads it (stretched first when the tonemap drew natively at the render size)
   static bool RedirectTonemapTarget(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, const std::function<void()>& original_draw)
   {
      constexpr UINT read_slots = 8;
      com_ptr<ID3D11ShaderResourceView> srvs[read_slots];
      native_device_context->PSGetShaderResources(0, read_slots, &srvs[0]);
      int read_slot = -1;
      for (UINT i = 0; i < read_slots; i++)
      {
         if (!srvs[i])
            continue;
         com_ptr<ID3D11Resource> resource;
         srvs[i]->GetResource(&resource);
         if (resource.get() == gd->tonemap_target.get())
         {
            read_slot = int(i);
            break;
         }
      }
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      com_ptr<ID3D11Resource> written;
      if (rtv)
      {
         rtv->GetResource(&written);
      }
      // Drawn natively, the target stays the engine's until it's read
      const bool writes = gd->tonemap_upscaled && written.get() == gd->tonemap_target.get();
      if (read_slot < 0 && !writes)
         return false;

      const uint2 size = GetViewTextureSize(gd->sr_output_srv.get());
      if (read_slot >= 0 && !gd->upscaled_scene_filled)
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
         srvs[read_slot]->GetDesc(&srv_desc);
         if (!EnsureUpscaledScene(native_device, gd, size, srv_desc.Format) ||
             !DrawScaled(native_device_context, device_data, device_data.sampler_state_linear.get(), srvs[read_slot].get(), gd->upscaled_scene_rtv.get(), size))
            return false;
         gd->upscaled_scene_filled = true;
         MEA_COUNT(tonemaps_stretched);
      }

      DrawStateStack<DrawStateStackType::SimpleGraphics> redirect_state;
      redirect_state.Cache(native_device_context, device_data.uav_max_count);
      if (read_slot >= 0)
      {
         ID3D11ShaderResourceView* const upscaled_scene = gd->upscaled_scene_srv.get();
         native_device_context->PSSetShaderResources(UINT(read_slot), 1, &upscaled_scene);
      }
      if (writes)
      {
         // The draw's viewports and scissors, from the render size to the output size
         UINT viewports_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
         D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
         native_device_context->RSGetViewports(&viewports_count, viewports);
         UINT scissors_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
         D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
         native_device_context->RSGetScissorRects(&scissors_count, scissors);
         const float scale_x = float(size.x) / float(gd->scene_size.x);
         const float scale_y = float(size.y) / float(gd->scene_size.y);
         for (UINT i = 0; i < viewports_count; i++)
         {
            viewports[i].TopLeftX *= scale_x;
            viewports[i].TopLeftY *= scale_y;
            viewports[i].Width *= scale_x;
            viewports[i].Height *= scale_y;
         }
         for (UINT i = 0; i < scissors_count; i++)
         {
            scissors[i] = {.left = LONG(scissors[i].left * scale_x), .top = LONG(scissors[i].top * scale_y), .right = LONG(std::ceil(scissors[i].right * scale_x)), .bottom = LONG(std::ceil(scissors[i].bottom * scale_y))};
         }
         native_device_context->RSSetViewports(viewports_count, viewports);
         native_device_context->RSSetScissorRects(scissors_count, scissors);
         ID3D11RenderTargetView* const target = gd->upscaled_scene_rtv.get();
         native_device_context->OMSetRenderTargets(1, &target, nullptr);
      }
      MEA_COUNT(tonemap_redirects);
      original_draw();
      redirect_state.Restore(native_device_context);
      return true;
   }
#endif

#if ENABLE_SR
   // The native AO at the output size while upscaling. The engine scales its AO radius with the render size, but the horizon
   // steps and the bilateral blur work in render pixels: below 100% every tap covers more of the world and the occlusion spreads
   // (+4.3% at 50%). Luma runs the same shaders on output-sized copies of their inputs (HBAO and SSAO: half output-sized, then
   // their upsample to the output size), with the constants they get at 100% (the render-size ones scaled by output/render),
   // and scales the result down into the engine's AO target. The first pass decides for the frame; false = the pass runs
   // natively. After it, a pass that can't run is skipped, never run natively: it would read inputs no pass wrote this frame
   // (the engine's AO target keeps the last frame's instead).
   static bool RunOutputSizedAOPass(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data, DeviceData& device_data,
      MassEffectAndromedaGameDeviceData* gd, const OutputSizedAOPass& pass)
   {
      const uint32_t frame = cb_luma_global_settings.FrameIndex;
      const bool first_pass = (pass.first_of != 0);
      if (first_pass)
      {
         if (!g_ao_output_size || !gd->sr_active)
            return false;
      }
      else if (gd->ao.frame != frame)
      {
         return false;
      }
      else if (gd->ao.broken_frame == frame)
      {
         return true;
      }
      const auto fail = [&]
      {
         if (first_pass)
            return false;
         gd->ao.broken_frame = frame;
         MEA_COUNT(ao_output_size_aborts);
         return true;
      };
      if (!first_pass && (pass.modes & AOModeBit(gd->ao.mode)) == 0)
         return fail(); // A pass of another AO mode

      ID3D11DeviceContext1* const context1 = GetImmediateContext1(native_device_context, gd);
      auto* const scale_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Scale VS"));
      auto* const scale_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Scale PS"));
      auto* const ssao_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("MEA SSAO VS"));
      if (!context1 || scale_vs == nullptr || scale_ps == nullptr || ssao_vs == nullptr)
         return fail();
      const auto draw_scaled = [&](ID3D11SamplerState* sampler, ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target, uint2 target_size)
      {
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), sampler, scale_vs, scale_ps, source, target,
            target_size.x, target_size.y);
      };
      com_ptr<ID3D11ShaderResourceView> srvs[3];
      if (pass.pixel)
      {
         native_device_context->PSGetShaderResources(0, 3, &srvs[0]);
      }
      else
      {
         native_device_context->CSGetShaderResources(0, 3, &srvs[0]);
      }

      const AOMode mode = (first_pass ? AOMode(std::countr_zero(pass.first_of)) : gd->ao.mode);
      const uint2 size = (first_pass ? uint2{uint32_t(device_data.output_resolution.x), uint32_t(device_data.output_resolution.y)} : gd->ao.size);
      const bool half = (mode != AOMode::HBAOFull);
      const uint2 pass_size = (half ? uint2{size.x / 2, size.y / 2} : size);
      const uint2 layer_size = {(pass_size.x + 3) / 4, (pass_size.y + 3) / 4};
      uint2 render_size = gd->ao.render_size;
      if (first_pass)
      {
         // HBAO/SSAO: the engine's input is its half depth (a 2x2 min); the full linear depth it was made from replaces it.
         // SSAO's t0 is its 4x4 noise.
         const bool has_inputs = (mode == AOMode::HBAOFull ? (srvs[0] && srvs[1] && srvs[2]) : (gd->srv_scene_depth && gd->scene_depth_frame == frame && (mode != AOMode::SSAO || srvs[0])));
         if (!has_inputs)
            return false;
         render_size = GetViewTextureSize(mode == AOMode::HBAOFull ? srvs[0].get() : gd->srv_scene_depth.get());
         if (render_size.x == 0 || render_size.x >= size.x || render_size.y >= size.y)
            return false;
         if (gd->ao.size != size || gd->ao.mode != mode)
         {
            gd->ao = {};
            constexpr UINT rt_bind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            constexpr UINT uav_bind = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            constexpr UINT rt_uav_bind = rt_bind | D3D11_BIND_UNORDERED_ACCESS;
            const CD3D11_BUFFER_DESC cb_desc(32 * 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
            // SSAO is single channel (R8 targets), HBAO carries a bent normal in .yzw
            const DXGI_FORMAT ao_format = (mode == AOMode::SSAO ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM);
            bool created = CreateTexture(native_device, DXGI_FORMAT_R32_FLOAT, size, rt_bind, gd->ao.depth.put(), gd->ao.depth_srv.put(), gd->ao.depth_rtv.put()) &&
                           CreateTexture(native_device, ao_format, size, uav_bind, gd->ao.result.put(), gd->ao.result_srv.put(), nullptr, gd->ao.result_uav.put()) &&
                           CreateTexture(native_device, ao_format, pass_size, rt_uav_bind, gd->ao.blur.put(), gd->ao.blur_srv.put(), gd->ao.blur_rtv.put(), gd->ao.blur_uav.put()) &&
                           SUCCEEDED(native_device->CreateBuffer(&cb_desc, nullptr, gd->ao.cb.put()));
            if (mode != AOMode::SSAO)
            {
               created = created && CreateTexture(native_device, DXGI_FORMAT_R16_FLOAT, layer_size, uav_bind, gd->ao.layer_depth.put(), gd->ao.layer_depth_srv.put(), nullptr, gd->ao.layer_depth_uav.put(), 16) &&
                         CreateTexture(native_device, DXGI_FORMAT_R8G8_SNORM, layer_size, uav_bind, gd->ao.layer_normals.put(), gd->ao.layer_normals_srv.put(), nullptr, gd->ao.layer_normals_uav.put(), 16);
            }
            if (half)
            {
               created = created && CreateTexture(native_device, DXGI_FORMAT_R32_FLOAT, pass_size, rt_bind, gd->ao.half_depth.put(), gd->ao.half_depth_srv.put(), gd->ao.half_depth_rtv.put()) &&
                         CreateTexture(native_device, ao_format, pass_size, rt_uav_bind, gd->ao.half_result.put(), gd->ao.half_result_srv.put(), gd->ao.half_result_rtv.put(), gd->ao.half_result_uav.put());
            }
            else
            {
               created = created && CreateTexture(native_device, DXGI_FORMAT_R10G10B10A2_UNORM, size, rt_bind, gd->ao.normals.put(), gd->ao.normals_srv.put(), gd->ao.normals_rtv.put()) &&
                         CreateTexture(native_device, DXGI_FORMAT_R8_UNORM, size, rt_bind, gd->ao.material.put(), gd->ao.material_srv.put(), gd->ao.material_rtv.put());
            }
            if (!created)
            {
               gd->ao = {};
               return false;
            }
            gd->ao.size = size;
            gd->ao.mode = mode;
         }
      }
      const float scale = float(size.x) / float(render_size.x);
      const uint32_t hash = (pass.runs_as != 0 ? pass.runs_as : pass.hash);

      constexpr UINT max_rows = 32;
      float rows[max_rows][4] = {};
      if (!ReadEngineConstants(context1, gd, pass.pixel, rows, pass.cb_rows))
         return fail();

      ID3D11ShaderResourceView* pass_srvs[3] = {srvs[0].get(), srvs[1].get(), srvs[2].get()};
      ID3D11UnorderedAccessView* pass_uavs[2] = {};
      ID3D11RenderTargetView* pass_rtv = nullptr; // The pixel passes' target
      UINT groups[3] = {1, 1, 1};
      switch (hash)
      {
      case kAODeinterleaveHash:
      {
         // Point stretch: depth must not blend across edges, the material id indexes a normal basis table
         DrawStateStack<DrawStateStackType::FullGraphics> stretch_state;
         stretch_state.Cache(native_device_context, device_data.uav_max_count);
         ID3D11RenderTargetView* const targets[3] = {gd->ao.depth_rtv.get(), gd->ao.normals_rtv.get(), gd->ao.material_rtv.get()};
         for (UINT i = 0; i < 3; i++)
         {
            draw_scaled(device_data.sampler_state_point.get(), srvs[i].get(), targets[i], size);
         }
         stretch_state.Restore(native_device_context);
         rows[5][0] /= scale; // The depth scale, inversely proportional to the size
         pass_srvs[0] = gd->ao.depth_srv.get();
         pass_srvs[1] = gd->ao.normals_srv.get();
         pass_srvs[2] = gd->ao.material_srv.get();
         pass_uavs[0] = gd->ao.layer_depth_uav.get();
         pass_uavs[1] = gd->ao.layer_normals_uav.get();
         // 16x16 threads per group, one 4x4 block of each layer (the engine adds a group)
         groups[0] = (size.x + 15) / 16 + 1;
         groups[1] = (size.y + 15) / 16 + 1;
         break;
      }
      case kAOHalfDeinterleaveHash:
      case kSSAOHash:
      {
         // The full linear depth point stretched to the output size (the upsample's) and to half of it, where the engine's
         // 2x2 min would read the output-sized depth Luma doesn't have (identical at 50%, the render size is half the output)
         DrawStateStack<DrawStateStackType::FullGraphics> stretch_state;
         stretch_state.Cache(native_device_context, device_data.uav_max_count);
         draw_scaled(device_data.sampler_state_point.get(), gd->srv_scene_depth.get(), gd->ao.depth_rtv.get(), size);
         // At a non integer ratio (67%) point taps land 1 or 2 render pixels apart: the normals HBAO rebuilds from neighbour
         // depths tilt (bent normal .zw +0.05 at 67%). Bilinear keeps the taps evenly spaced; 1:1 (50%) stays a plain copy.
         ID3D11SamplerState* const half_depth_sampler = (render_size == pass_size ? device_data.sampler_state_point.get() : device_data.sampler_state_linear.get());
         draw_scaled(half_depth_sampler, gd->srv_scene_depth.get(), gd->ao.half_depth_rtv.get(), pass_size);
         stretch_state.Restore(native_device_context);
         if (hash == kSSAOHash)
         {
            // c2.xy = the size its depth loads scale UVs by (the kernel offsets are UV based). t0 = the noise. c0.xy (not read by
            // the PS) = the texel size Luma's VS tiles the noise by, one noise texel per pixel as at 100%.
            rows[0][0] = 1.f / float(pass_size.x);
            rows[0][1] = 1.f / float(pass_size.y);
            rows[2][0] = float(pass_size.x);
            rows[2][1] = float(pass_size.y);
            pass_srvs[1] = gd->ao.half_depth_srv.get();
            pass_rtv = gd->ao.half_result_rtv.get();
            break;
         }
         // c0.xy = the texel size (normals are rebuilt from depth), c5.x = the depth scale as Full's
         rows[0][0] = 1.f / float(pass_size.x);
         rows[0][1] = 1.f / float(pass_size.y);
         rows[5][0] /= scale;
         pass_srvs[0] = gd->ao.half_depth_srv.get();
         pass_uavs[0] = gd->ao.layer_depth_uav.get();
         pass_uavs[1] = gd->ao.layer_normals_uav.get();
         // 16x16 threads per group, one 4x4 block of each layer (no extra group here)
         groups[0] = (pass_size.x + 15) / 16;
         groups[1] = (pass_size.y + 15) / 16;
         break;
      }
      case kAOHorizonHash:
      case kAOHalfHorizonHash:
      {
         // The layer texel size, the deinterleave's depth scale back, the radius in pixels and its cap
         rows[1][0] = 1.f / float(layer_size.x);
         rows[1][1] = 1.f / float(layer_size.y);
         rows[1][2] = float(layer_size.x);
         rows[1][3] = float(layer_size.y);
         rows[2][0] *= scale;
         rows[2][1] *= scale;
         rows[2][2] *= scale;
         pass_srvs[0] = gd->ao.layer_depth_srv.get();
         pass_srvs[1] = gd->ao.layer_normals_srv.get();
         pass_uavs[0] = (half ? gd->ao.half_result_uav.get() : gd->ao.result_uav.get());
         groups[0] = (layer_size.x + 7) / 8;
         groups[1] = (layer_size.y + 7) / 8;
         groups[2] = 4; // 16 layers, 4 per group
         break;
      }
      case kAOBlurXHash:
      case kAOBlurYHash:
      {
         // (W, H) as uints, then the texel size
         const UINT size_bits[2] = {pass_size.x, pass_size.y};
         std::memcpy(&rows[0][0], size_bits, sizeof(size_bits));
         rows[0][2] = 1.f / float(pass_size.x);
         rows[0][3] = 1.f / float(pass_size.y);
         const bool blur_x = (hash == kAOBlurXHash);
         ID3D11ShaderResourceView* const result_srv = (half ? gd->ao.half_result_srv.get() : gd->ao.result_srv.get());
         ID3D11UnorderedAccessView* const result_uav = (half ? gd->ao.half_result_uav.get() : gd->ao.result_uav.get());
         pass_srvs[0] = (half ? gd->ao.half_depth_srv.get() : gd->ao.depth_srv.get());
         pass_srvs[1] = (blur_x ? result_srv : gd->ao.blur_srv.get());
         pass_uavs[0] = (blur_x ? gd->ao.blur_uav.get() : result_uav);
         // 192 pixels along the blur, 2 lines across it, per group
         const uint2 blur_size = (blur_x ? pass_size : uint2{pass_size.y, pass_size.x});
         groups[0] = (blur_size.x + 191) / 192;
         groups[1] = (blur_size.y + 1) / 2;
         break;
      }
      case kSSAOBlurXHash:
      case kSSAOBlurYHash:
      {
         // c0.xy = the texel size (the horizontal taps); the vertical ones are bilinear pair offsets in UV (c1.xz, c2.xz),
         // render texels apart
         rows[0][0] = 1.f / float(pass_size.x);
         rows[0][1] = 1.f / float(pass_size.y);
         const bool blur_x = (hash == kSSAOBlurXHash);
         if (!blur_x)
         {
            rows[1][0] /= scale;
            rows[1][2] /= scale;
            rows[2][0] /= scale;
            rows[2][2] /= scale;
         }
         pass_srvs[0] = (blur_x ? gd->ao.half_result_srv.get() : gd->ao.blur_srv.get());
         pass_rtv = (blur_x ? gd->ao.blur_rtv.get() : gd->ao.half_result_rtv.get());
         break;
      }
      case kAOUpsampleHash:
      case kSSAOUpsampleHash:
      {
         // c0 = (output W, H as uints, then the half texel size). A thread per half pixel writes the 2x2 output pixels at
         // (2 xy - 1, 2 xy): the engine's ceil(half / 16) groups leave the last column and row unwritten, one more group fills them
         const UINT size_bits[2] = {size.x, size.y};
         std::memcpy(&rows[0][0], size_bits, sizeof(size_bits));
         rows[0][2] = 1.f / float(pass_size.x);
         rows[0][3] = 1.f / float(pass_size.y);
         pass_srvs[0] = gd->ao.half_depth_srv.get();
         pass_srvs[1] = gd->ao.depth_srv.get();
         pass_srvs[2] = gd->ao.half_result_srv.get();
         pass_uavs[0] = gd->ao.result_uav.get();
         groups[0] = pass_size.x / 16 + 1;
         groups[1] = pass_size.y / 16 + 1;
         break;
      }
      default:
         return fail();
      }

      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(native_device_context->Map(gd->ao.cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
         return fail();
      std::memcpy(mapped.pData, rows, sizeof(rows));
      native_device_context->Unmap(gd->ao.cb.get(), 0);

      com_ptr<ID3D11UnorderedAccessView> engine_target_uav;
      const bool last_pass = (pass.last_of & AOModeBit(mode)) != 0;
      if (last_pass)
      {
         native_device_context->CSGetUnorderedAccessViews(0, 1, &engine_target_uav);
         if (!engine_target_uav)
            return fail();
      }

      ID3D11Buffer* const cb = gd->ao.cb.get();
      if (pass.pixel)
      {
         // The engine's pixel shader and sampler, drawn with Luma's full screen VS (the engine's takes pixel positions from a
         // vertex buffer made for its size). The VS reads the texel size from the same constants.
         com_ptr<ID3D11PixelShader> engine_ps;
         native_device_context->PSGetShader(&engine_ps, nullptr, nullptr);
         DrawStateStack<DrawStateStackType::FullGraphics> pixel_state;
         pixel_state.Cache(native_device_context, device_data.uav_max_count);
         native_device_context->VSSetConstantBuffers(0, 1, &cb);
         native_device_context->PSSetConstantBuffers(0, 1, &cb);
         native_device_context->PSSetShaderResources(1, 1, &pass_srvs[1]);
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, ssao_vs, engine_ps.get(), pass_srvs[0],
            pass_rtv, pass_size.x, pass_size.y);
         pixel_state.Restore(native_device_context);
      }
      else
      {
         DrawStateStack<DrawStateStackType::Compute> compute_state;
         compute_state.Cache(native_device_context, device_data.uav_max_count);
         native_device_context->CSSetConstantBuffers(0, 1, &cb);
         native_device_context->CSSetShaderResources(0, 3, pass_srvs);
         native_device_context->CSSetUnorderedAccessViews(0, 2, pass_uavs, nullptr);
         native_device_context->Dispatch(groups[0], groups[1], groups[2]);
         compute_state.Restore(native_device_context);
      }
      MEA_COUNT(ao_output_size_passes);
      if (first_pass)
      {
         gd->ao.render_size = render_size;
         gd->ao.frame = frame;
      }

      if (last_pass)
      {
         // Into the engine's AO target (RGBA8 for HBAO, R8 for SSAO), bilinear (a 2x2 average at 50%)
         com_ptr<ID3D11Resource> engine_target;
         engine_target_uav->GetResource(&engine_target);
         if (gd->ao.engine_target.get() != engine_target.get())
         {
            gd->ao.engine_target_rtv.reset();
            gd->ao.engine_target = engine_target.get();
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            engine_target_uav->GetDesc(&uav_desc);
            const CD3D11_RENDER_TARGET_VIEW_DESC rtv_desc(D3D11_RTV_DIMENSION_TEXTURE2D, uav_desc.Format);
            if (FAILED(native_device->CreateRenderTargetView(engine_target.get(), &rtv_desc, gd->ao.engine_target_rtv.put())))
            {
               gd->ao.engine_target_rtv.reset();
               gd->ao.engine_target.reset(); // Retried next frame
               return fail();
            }
         }
         DrawStateStack<DrawStateStackType::FullGraphics> downscale_state;
         downscale_state.Cache(native_device_context, device_data.uav_max_count);
         draw_scaled(device_data.sampler_state_linear.get(), gd->ao.result_srv.get(), gd->ao.engine_target_rtv.get(), gd->ao.render_size);
         downscale_state.Restore(native_device_context);
      }
      return true;
   }
   // The hooked TAA resolve dispatch replaced with DLSS/FSR (or, for the DOF variant, run first and then overwritten by them)
   static DrawOrDispatchOverrideType ReplaceTAAResolve(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, [[maybe_unused]] CommandListData& cmd_list_data,
      DeviceData& device_data, MassEffectAndromedaGameDeviceData* gd, bool dof_variant, std::function<void()>* original_draw_dispatch_func)
   {
      SR::InstanceData* const sr_instance_data = (gd->sr_active ? device_data.GetSRInstanceData() : nullptr);
      if (!sr_instance_data)
         return DrawOrDispatchOverrideType::None;
      gd->taa_resolve_frame = cb_luma_global_settings.FrameIndex;
      // The frame the resolve comes back (after the galaxy map), still at the game's own scale: the native TAA resolves it,
      // so the upscaler isn't created for that size one frame before the render scale reallocates everything again
      if (g_game_render_settings && !g_game_render_settings_written)
         return DrawOrDispatchOverrideType::None;

      // The resolve's bindings first: its input size keys the camera probe and the SR settings
      ComPtr<ID3D11ShaderResourceView> srv_depth, srv_mvs, srv_color;
      native_device_context->CSGetShaderResources(0, 1, srv_depth.put()); // t0
      native_device_context->CSGetShaderResources(1, 1, srv_mvs.put());   // t1
      native_device_context->CSGetShaderResources(3, 1, srv_color.put()); // t3
      ComPtr<ID3D11UnorderedAccessView> uav_resolved, uav_history;
      native_device_context->CSGetUnorderedAccessViews(2, 1, uav_resolved.put()); // u2
      native_device_context->CSGetUnorderedAccessViews(3, 1, uav_history.put());  // u3
      if (!srv_depth || !srv_mvs || !srv_color || !uav_resolved)
         return DrawOrDispatchOverrideType::None;

      ComPtr<ID3D11Resource> res_depth, res_mvs, res_color, res_u2;
      srv_depth->GetResource(res_depth.put());
      srv_mvs->GetResource(res_mvs.put());
      srv_color->GetResource(res_color.put());
      uav_resolved->GetResource(res_u2.put());

      // The render res comes from the t3 scene-color texture: the engine renders the whole frame up to the tonemap at its
      // render scale (separate, smaller targets), which also keys the camera CB. The upscaler outputs at the swapchain size
      // when the engine renders below it, else at the render size (DLAA).
      const uint2 size = GetViewTextureSize(srv_color.get());
      if (size.x == 0 || size.y == 0)
         return DrawOrDispatchOverrideType::None;
      const uint32_t w = size.x, h = size.y;
      const float rw = (float)w, rh = (float)h;
      gd->scene_size = size;
      const uint2 swapchain_size = {uint32_t(device_data.output_resolution.x), uint32_t(device_data.output_resolution.y)};
      const uint2 output_size = ((w <= swapchain_size.x && h <= swapchain_size.y) ? swapchain_size : size);

      // The camera is normally captured at a scene VS this frame (see "CaptureCamera"); the bound-CB probe is the fallback
      if (ID3D11DeviceContext1* context1 = (gd->cam_valid_this_frame ? nullptr : GetImmediateContext1(native_device_context, gd)))
      {
         // Guarded: it reads the map cache, which Frostbite worker threads mutate via OnMapBufferRegion.
         const std::shared_lock lock(gd->map_cache.mutex);
         if (!gd->cam_probe_slot || !TryStoreCamera(context1, gd, gd->cam_probe_slot->compute, gd->cam_probe_slot->slot, &size))
         {
            // Slots 2/1/3 first (the observed camera slots), then the whole CB range. Frames without a main
            // gbuffer pass (menus) bind the per-view CB at less common slots, if at all. TryStoreCamera's
            // validation (layout signature + render-res match) rejects non-camera CBs, so the wide scan is safe.
            static constexpr UINT kProbeSlots[] = {2, 1, 3, 0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
            for (UINT slot : kProbeSlots)
            {
               if (TryStoreCamera(context1, gd, false, slot, &size))
               {
                  gd->cam_probe_slot = MassEffectAndromedaGameDeviceData::CameraSlot{.compute = false, .slot = slot};
                  break;
               }
               if (TryStoreCamera(context1, gd, true, slot, &size))
               {
                  gd->cam_probe_slot = MassEffectAndromedaGameDeviceData::CameraSlot{.compute = true, .slot = slot};
                  break;
               }
            }
         }
         if (gd->cam_valid_this_frame)
         {
            MEA_COUNT(camera_probe);
         }
      }
      // No camera (menu and dialogue frames without a main gbuffer pass), no jitter: SR with jitter 0 on the jittered scene
      // shakes every frame. The native resolve de-jitters itself.
      // Must stay before the DOF variant's dispatch below, so None still means the native dispatch runs exactly once.
      if (!gd->cam_valid_this_frame)
      {
         MEA_COUNT(camera_misses);
         return DrawOrDispatchOverrideType::None;
      }

      // The hand-off into u2/u3 is the copy CS (see Luma_MEA_CopyColor.hlsl): the game's "Buffer Format" setting swaps them
      // between rgba16f and r11g11b10_float, where a plain copy silently no-ops, and u3 needs the native history encoding.
      // Upscaling, it downsamples: u2 feeds the render-sized passes up to the tonemap, u3 the native fallback.
      // Without it (shaders still compiling, or u2 of another size) the native resolve runs. Checked every dispatch: the
      // transient pool can recycle a pointer for a texture of another format, so a pointer key would go stale.
      auto* copy_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("MEA SR Output Copy CS"));
      auto* history_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("MEA SR History Copy CS"));
      D3D11_TEXTURE2D_DESC u2_desc = {};
      if (ComPtr<ID3D11Texture2D> u2_tex; SUCCEEDED(res_u2->QueryInterface(u2_tex.put())))
      {
         u2_tex->GetDesc(&u2_desc);
      }
      if (u2_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT)
      {
         MEA_COUNT(u2_r11g11b10);
      }
      if (copy_cs == nullptr || history_cs == nullptr || u2_desc.Width != w || u2_desc.Height != h)
      {
         MEA_COUNT(handoff_incompatible);
         return DrawOrDispatchOverrideType::None;
      }

      // DOF variant (see "shader_hashes_taa_resolve_dof"): the native dispatch is issued here first; from then on every exit
      // must return Replaced, never None: the fall-through dispatch would run the resolve twice.
      if (dof_variant)
      {
         if (original_draw_dispatch_func == nullptr)
            return DrawOrDispatchOverrideType::None; // can't re-issue manually, let the native dispatch run itself
         // It writes u0 (mask) and u4/u5 (the temporally filtered CoC); its u2/u3 color is overwritten by the SR copy below,
         // and the SR inputs (t0/t1/t3) stay bound and unchanged across it.
         (*original_draw_dispatch_func)();
         MEA_COUNT(dof_resolves);
      }
      // Back to the native TAA until the upscaler is picked again
      const auto suppress_sr = [&]
      {
         MEA_COUNT(sr_failures);
         device_data.sr_suppressed = true;
         return (dof_variant ? DrawOrDispatchOverrideType::Replaced : DrawOrDispatchOverrideType::None);
      };

      D3D11_TEXTURE2D_DESC output_desc = {};
      if (device_data.sr_output_color)
      {
         device_data.sr_output_color->GetDesc(&output_desc);
      }
      const bool output_resized = output_desc.Width != output_size.x || output_desc.Height != output_size.y;
      if (output_resized || !gd->sr_output_srv)
      {
         gd->sr_output_srv.reset();
         device_data.sr_output_color.reset();
         CreateTexture(native_device, DXGI_FORMAT_R16G16B16A16_FLOAT, output_size, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, &device_data.sr_output_color,
            gd->sr_output_srv.put());
         gd->sr_output_recreated = true;
      }
      if (!gd->sr_output_srv)
         return suppress_sr();

      const bool fsr_4 = (device_data.sr_type == SR::Type::FSR_4);
      const SR::SettingsData settings_data = {
         .output_width = output_size.x,
         .output_height = output_size.y,
         .render_width = w,
         .render_height = h,
         .hdr = true,            // MEA scene color = linear HDR
         .inverted_depth = true, // reverse-Z
         .mvs_jittered = g_mv_jittered,
         .mvs_x_scale = (g_mv_flip_x ? -1.f : 1.f) * g_mv_scale_mult * 0.5f * rw,
         .mvs_y_scale = (g_mv_flip_y ? 1.f : -1.f) * g_mv_scale_mult * 0.5f * rh,
         .auto_exposure = (device_data.sr_type != SR::Type::FSR && !fsr_4),
         .render_preset = dlss_render_preset,
      };
      sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

      // Without auto exposure, FSR 4 refuses a dispatch that has no exposure texture (FFX_API_RETURN_ERROR_PARAMETER): a unit one
      if (fsr_4 && !device_data.sr_exposure)
      {
         const D3D11_TEXTURE2D_DESC exposure_desc = {
            .Width = 1,
            .Height = 1,
            .MipLevels = 1,
            .ArraySize = 1,
            .Format = DXGI_FORMAT_R32_FLOAT,
            .SampleDesc = {.Count = 1},
            .Usage = D3D11_USAGE_IMMUTABLE,
            .BindFlags = D3D11_BIND_SHADER_RESOURCE,
         };
         const D3D11_SUBRESOURCE_DATA exposure_data = {.pSysMem = &device_data.sr_exposure_texture_value, .SysMemPitch = sizeof(float)};
         native_device->CreateTexture2D(&exposure_desc, &exposure_data, &device_data.sr_exposure);
      }

      // No bias mask: MEA has no usable reactivity source. Jitter is this frame's: the capture-miss case bailed above.
      const SR::SuperResolutionImpl::DrawData draw_data = {
         // Smooth FOV changes are left to the upscaler's own history rejection: resetting every ramp frame starves it.
         .reset = device_data.force_reset_sr || output_resized,
         .output_color = device_data.sr_output_color.get(),
         .source_color = res_color.get(),
         .motion_vectors = res_mvs.get(),
         .depth_buffer = res_depth.get(),
         .exposure = (fsr_4 ? device_data.sr_exposure.get() : nullptr),
         .render_width = w,
         .render_height = h,
         .jitter_x = (g_jitter_flip_x ? 1.f : -1.f) * gd->cam_jitter_clip_x * 0.5f * rw,
         .jitter_y = (g_jitter_flip_y ? 1.f : -1.f) * gd->cam_jitter_clip_y * 0.5f * rh,
         // FSR reads the vertical FOV (DLSS ignores it) and asserts on <= 0: ~60 degrees if the captured m11 isn't positive
         .vert_fov = (gd->cam_proj_m11 > 0.f ? (2.f * atanf(1.f / gd->cam_proj_m11)) : 1.047f),
         .near_plane = gd->cam_near,
         .far_plane = kCamFar,
         .frame_index = cb_luma_global_settings.FrameIndex,
      };

      // NGX/FFX don't restore the state they touch
      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(native_device_context, device_data.uav_max_count);
      compute_state.Cache(native_device_context, device_data.uav_max_count);

      const bool drawn = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
      if (drawn)
      {
         MEA_COUNT(handoff_cs);
         native_device_context->CSSetShader(copy_cs, nullptr, 0);
         // UAV first, then SRV (an SRV whose resource is still UAV-bound gets silently NULLed). The FULL range: besides the
         // game's own u2/u3 bindings, the SR backend may have left sr_output_color on any UAV slot.
         ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT] = {uav_resolved.get()};
         native_device_context->CSSetUnorderedAccessViews(0, device_data.uav_max_count, uavs, nullptr);
         ID3D11ShaderResourceView* srv = gd->sr_output_srv.get();
         native_device_context->CSSetShaderResources(0, 1, &srv);
         ID3D11SamplerState* const linear_sampler = device_data.sampler_state_linear.get(); // a downsample when upscaling
         native_device_context->CSSetSamplers(0, 1, &linear_sampler);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
         if (uav_history)
         {
            native_device_context->CSSetShader(history_cs, nullptr, 0); // the native history encoding, see the shader
            ID3D11UnorderedAccessView* uav_h = uav_history.get();
            native_device_context->CSSetUnorderedAccessViews(0, 1, &uav_h, nullptr);
            native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
         }
      }
      compute_state.Restore(native_device_context);
      graphics_state.Restore(native_device_context);

      if (!drawn)
         return suppress_sr();
      // DLSS draws nothing into a new output texture (the session's first, or one made after "None", which Core frees) until its feature
      // is created again after a draw: other settings now make the next frame's "UpdateSettings" recreate it
      if (std::exchange(gd->sr_output_recreated, false) && device_data.sr_type == SR::Type::DLSS)
      {
         SR::SettingsData feature_recreate_settings = settings_data;
         feature_recreate_settings.mvs_jittered = !feature_recreate_settings.mvs_jittered;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, feature_recreate_settings);
      }
      MEA_COUNT(sr_draws);
      if (output_size != size)
      {
         gd->output_twins.push_back({.engine = res_u2.get(), .srv = gd->sr_output_srv.get()});
      }
      device_data.render_resolution = {rw, rh};
      device_data.has_drawn_sr = true;
      device_data.has_drawn_main_post_processing = true;
      return DrawOrDispatchOverrideType::Replaced; // cancel native TAA
   }
#endif // ENABLE_SR

public:
   // "restore_engine_settings": the process outlives the addon (an unload), not at process exit, where the engine's memory
   // may already be freed
   static void UnregisterEvents(bool restore_engine_settings)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
#if ENABLE_SR
      reshade::unregister_event<reshade::addon_event::copy_buffer_region>(OnCopyBufferRegion);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
#endif
      reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::unregister_event<reshade::addon_event::destroy_swapchain>(NvapiHdr::OnDestroySwapchain);
      if (!restore_engine_settings)
         return;
      NvapiHdr::Uninstall(); // the detour would dangle after an unload
      if (g_world_render_settings)
      {
         WriteWorldRenderSettings(g_world_render_settings_vanilla);
      }
      if (g_game_render_settings_written)
      {
         WriteGameRenderSettings(g_game_render_settings_vanilla);
      }
      if (g_game_hdr10_peak_written)
      {
         std::memcpy(g_game_render_settings + kGrsDisplayMappingHdr10PeakLuma, &g_game_hdr10_peak_vanilla, sizeof(g_game_hdr10_peak_vanilla));
      }
   }

   void OnInit(bool async) override
   {
#if DEVELOPMENT
      // For the MCP "luma_dev_values" tool (the counters are the last complete frame's)
      Mcp::RegisterToggles({
         {"smaa_enable", &g_smaa_enable},
         {"hide_ui", &g_hide_ui},
         {"smaa_predication", &g_smaa_predication},
         {"mv_flip_x", &g_mv_flip_x},
         {"mv_flip_y", &g_mv_flip_y},
         {"mv_jittered", &g_mv_jittered},
         {"jitter_flip_x", &g_jitter_flip_x},
         {"jitter_flip_y", &g_jitter_flip_y},
         {"disable_taa_sharpening", &g_disable_taa_sharpening},
         {"halton_jitter", &g_halton_jitter},
         {"improve_taa_jitter", &g_improve_taa_jitter},
         {"ao_output_size", &g_ao_output_size},
         {"hdr_fix", &g_hdr_fix},
         {"hdr_gamma_correction", &g_hdr_gamma_correction},
         {"video_auto_hdr", &g_video_auto_hdr},
         {"dithering", &g_dithering},
      });
      Mcp::RegisterInts({
         {"halton_jitter_native_phases", &g_halton_jitter_native_phases, 1, 64},
         {"engine_aa_mode", &g_dev_engine_aa_mode, -1, 8},
         {"engine_resample_mode", &g_dev_engine_resample_mode, -1, 6},
      });
      Mcp::RegisterCounters({
         {"engine.aa_mode", &g_dev_engine_aa_mode_value},
         {"engine.resample_mode", &g_dev_engine_resample_mode_value},
      });
      Mcp::RegisterValues({{"rcas_sharpness", &g_rcas_sharpness, 0.f, 1.f}, {"smaa_pred_tolerance", &g_smaa_pred_tolerance, 0.002f, 0.2f}, {"mv_scale_mult", &g_mv_scale_mult, 0.25f, 4.f}, {"render_scale", &g_render_scale, kMinRenderScale, 1.f}});
      // The grade and video settings, read into the game settings buffer every present
      Mcp::RegisterValues({
         {"exposure", &g_exposure, 0.f, 2.f},
         {"contrast", &g_contrast, 0.f, 2.f},
         {"saturation", &g_saturation, 0.f, 2.f},
         {"highlight_dechroma", &g_highlight_dechroma, 0.f, 1.f},
         {"bloom_intensity", &g_bloom_intensity, 0.f, 2.f},
         {"vignette_intensity", &g_vignette_intensity, 0.f, 2.f},
         {"film_grain_intensity", &g_film_grain_intensity, 0.f, 2.f},
         {"chromatic_aberration_intensity", &g_chromatic_aberration_intensity, 0.f, 2.f},
         {"video_auto_hdr_boost", &g_video_auto_hdr_boost, 0.f, 1.f},
      });
      const FrameCounters& c = g_counters_last_frame;
      Mcp::RegisterCounters({
         {"taa.resolves", &c.taa_resolves},
         {"taa.resolves_deferred", &c.taa_resolves_deferred},
         {"taa.dof_resolves", &c.dof_resolves},
         {"camera.gbuffer", &c.camera_gbuffer},
         {"camera.scene_vs", &c.camera_scene_vs},
         {"camera.probe", &c.camera_probe},
         {"camera.misses", &c.camera_misses},
         {"handoff.cs", &c.handoff_cs},
         {"handoff.incompatible", &c.handoff_incompatible},
         {"handoff.u2_r11g11b10", &c.u2_r11g11b10},
         {"sr.draws", &c.sr_draws},
         {"sr.failures", &c.sr_failures},
         {"fxaa.draws", &c.fxaa_draws},
         {"smaa.draws", &c.smaa_draws},
         {"rcas.draws", &c.rcas_draws},
         {"upscale.tonemaps", &c.tonemaps_upscaled},
         {"upscale.motion_blurs", &c.motion_blurs_upscaled},
         {"upscale.post_failures", &c.post_pass_failures},
         {"upscale.dof_composites", &c.dof_composites_upscaled},
         {"upscale.bloom_pyramids", &c.bloom_pyramids_upscaled},
         {"upscale.blooms", &c.blooms_upscaled},
         {"upscale.scanner_passes", &c.scanner_passes_upscaled},
         {"upscale.post_blurs", &c.post_blurs_upscaled},

         {"upscale.twin_draws", &c.twin_draws},
         {"upscale.structure_counts", &c.structure_counts_redirected},
         {"upscale.stretched", &c.tonemaps_stretched},
         {"upscale.redirects", &c.tonemap_redirects},
         {"ao.output_size_passes", &c.ao_output_size_passes},
         {"ao.output_size_aborts", &c.ao_output_size_aborts},
      });
#if ENABLE_SR
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("sr.output", sr_output_srv), MCP_GAME_TEXTURE("ao.result", ao.result_srv)});
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("dof.t0", dof_debug_inputs[0]), MCP_GAME_TEXTURE("dof.t3", dof_debug_inputs[3]), MCP_GAME_TEXTURE("dof.t6", dof_debug_inputs[6]),
         MCP_GAME_TEXTURE("dof.t9", dof_debug_inputs[9]), MCP_GAME_TEXTURE("dof.t10", dof_debug_inputs[10]), MCP_GAME_TEXTURE("dof.t12", dof_debug_inputs[12]),
         MCP_GAME_TEXTURE("dof.out", dof_debug_output), MCP_GAME_TEXTURE("bloom.pyramid", bloom_pyramid_srv),
         MCP_GAME_TEXTURE("upscale.scene", upscaled_scene_srv)});
#endif
      Mcp::RegisterTextures({MCP_GAME_TEXTURE("rcas.input", tex_rcas_input_srv), MCP_GAME_TEXTURE("smaa.predication", srv_pred), MCP_GAME_TEXTURE("smaa.scene_depth", srv_scene_depth)});
#endif
      // LumaSettings (the HDR fix's settings) for the replaced presents, which only use b0; Luma's own passes read none
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = -1;
      luma_ui_cbuffer_index = -1;
#if ENABLE_SMAA
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA Depth Extract CS"),
         ShaderDefinition{"Luma_MEA_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader});
#endif
#if ENABLE_SR
      // The SR output hand-off into the resolve's u2/u3 (see Luma_MEA_CopyColor.hlsl)
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA SR Output Copy CS"),
         ShaderDefinition{"Luma_MEA_CopyColor", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "copy_color_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA SR History Copy CS"),
         ShaderDefinition{"Luma_MEA_CopyColor", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "copy_color_history_cs"});
      // The engine's SSAO pixel shaders at the output size (see "RunOutputSizedAOPass")
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA SSAO VS"),
         ShaderDefinition{"Luma_MEA_SSAO_VS", reshade::api::pipeline_subobject_type::vertex_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA Scale CoC PS"),
         ShaderDefinition{"Luma_MEA_ScaleCoC", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA Scale Array PS"),
         ShaderDefinition{"Luma_MEA_ScaleArray", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "scale_ps"});
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA Scale Array CS"),
         ShaderDefinition{"Luma_MEA_ScaleArray", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "scale_cs"});
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA Resample Depth Stencil PS"),
         ShaderDefinition{"Luma_MEA_ResampleDepthStencil", reshade::api::pipeline_subobject_type::pixel_shader});
#endif
      // The dynamic CB rings' CPU map pointers (the camera, the AO and post pass constants), read by bound offset with no GPU
      // readback. Needed in every config.
      reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
#if ENABLE_SR
      reshade::register_event<reshade::addon_event::copy_buffer_region>(OnCopyBufferRegion);
      reshade::register_event<reshade::addon_event::clear_render_target_view>(OnClearRenderTargetView);
#endif
      reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::register_event<reshade::addon_event::destroy_swapchain>(NvapiHdr::OnDestroySwapchain);
      if (async)
      {
         NvapiHdr::Install();
      }
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      NvapiHdr::OnInitSwapchain(swapchain);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new MassEffectAndromedaGameDeviceData;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // Also fires on Frostbite's deferred contexts (worker threads)
      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      if (original_shader_hashes.Contains(kVideoDecodeHash, reshade::api::shader_stage::pixel))
      {
         g_video_decoded.store(true, std::memory_order_relaxed);
      }
      // LumaSettings is re-uploaded at the next replaced draw once dirty: the loading present sees it in the same frame
      if (is_immediate && cb_luma_global_settings.GameSettings.VideoActive == 0.f && g_video_decoded.load(std::memory_order_relaxed))
      {
         cb_luma_global_settings.GameSettings.VideoActive = 1.f;
         device_data.cb_luma_global_settings_dirty = true;
      }

      if (is_immediate && !gd.cam_valid_this_frame && (stages & reshade::api::shader_stage::vertex) != 0)
      {
         CaptureCamera(native_device_context, cmd_list_data, &gd, original_shader_hashes);
      }

      // Every path below that draws the tonemap draws it with the effect sliders
      const bool tonemap_effects = is_immediate && original_draw_dispatch_func && *original_draw_dispatch_func &&
                                   shader_hashes_tonemap.Contains(original_shader_hashes.pixel_shaders[0], reshade::api::shader_stage::pixel) &&
                                   WrapTonemapEffects(native_device, native_device_context, &gd, original_draw_dispatch_func);

#if DEVELOPMENT
      if (original_shader_hashes.Contains(kFXAAHash, reshade::api::shader_stage::pixel))
      {
         MEA_COUNT(fxaa_draws);
      }
#endif
#if ENABLE_SMAA || ENABLE_SR
      if (is_immediate && original_shader_hashes.Contains(kLinearDepthHash, reshade::api::shader_stage::pixel))
      {
         bool capture_linear_depth = false;
#if ENABLE_SMAA
         // While SMAA runs: it ran last frame (the first FXAA frame goes without predication)
         capture_linear_depth |= g_smaa_enable && g_smaa_predication && cb_luma_global_settings.FrameIndex - gd.smaa_frame <= 1;
#endif
#if ENABLE_SR
         capture_linear_depth |= g_ao_output_size && gd.sr_active;
#endif
         if (capture_linear_depth)
         {
            CaptureLinearDepth(native_device, native_device_context, &gd);
         }
      }
#endif
#if ENABLE_SMAA
      if (g_smaa_enable && is_immediate && original_shader_hashes.Contains(kFXAAHash, reshade::api::shader_stage::pixel) && HasSMAAShaders(device_data))
      {
         return ReplaceFXAAWithSMAA(native_device, native_device_context, cmd_list_data, device_data, &gd);
      }
#endif

#if ENABLE_SR
      if (is_immediate && (stages & (reshade::api::shader_stage::compute | reshade::api::shader_stage::pixel)) != 0)
      {
         const bool compute = (stages & reshade::api::shader_stage::compute) != 0;
         const uint32_t hash = (compute ? original_shader_hashes.compute_shaders[0] : original_shader_hashes.pixel_shaders[0]);
         const auto ao_pass = std::find_if(std::begin(kOutputSizedAOPasses), std::end(kOutputSizedAOPasses), [&](const OutputSizedAOPass& pass)
            { return pass.hash == hash && pass.pixel != compute; });
         if (ao_pass != std::end(kOutputSizedAOPasses) && RunOutputSizedAOPass(native_device, native_device_context, cmd_list_data, device_data, &gd, *ao_pass))
            return DrawOrDispatchOverrideType::Replaced;
      }

      // The bloom pyramid's shaders, kept for "BuildOutputSizedBloom", which runs at the first one while upscaling (then the engine's)
      if (is_immediate && (stages & reshade::api::shader_stage::compute) != 0)
      {
         const auto* const bloom = std::find(std::begin(kBloomPyramidHashes), std::end(kBloomPyramidHashes), original_shader_hashes.compute_shaders[0]);
         if (bloom != std::end(kBloomPyramidHashes))
         {
            auto& shader = gd.bloom_shaders[bloom - std::begin(kBloomPyramidHashes)];
            if (!shader)
            {
               native_device_context->CSGetShader(shader.put(), nullptr, nullptr);
            }
            if (bloom == std::begin(kBloomPyramidHashes) && !gd.output_twins.empty() && !gd.post_fallback && !BuildOutputSizedBloom(native_device, native_device_context, cmd_list_data, device_data, &gd))
            {
               FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, &gd, true); // No output-sized tonemap over a render-sized bloom
            }
         }
      }

      // Upscaling: the depth of field at the output size once its input has a twin (the motion blur's or u2)
      if (is_immediate && !gd.output_twins.empty() && !gd.post_fallback && original_draw_dispatch_func && *original_draw_dispatch_func)
      {
         const bool compute = (stages & reshade::api::shader_stage::compute) != 0;
         const uint32_t hash = (compute ? original_shader_hashes.compute_shaders[0] : original_shader_hashes.pixel_shaders[0]);
         bool dof_pass = false;
         if (compute)
         {
            dof_pass = hash == kDoFSpriteBinHash || std::any_of(std::begin(kDoFBlurPasses), std::end(kDoFBlurPasses), [hash](const DoFBlurPass& pass)
                                                       { return pass.hash == hash; }) ||
                       std::any_of(std::begin(kDoFSpriteBuildPasses), std::end(kDoFSpriteBuildPasses), [hash](const DoFSpriteBuildPass& pass)
                          { return pass.hash == hash; });
         }
         else if ((stages & reshade::api::shader_stage::pixel) != 0)
         {
            dof_pass = (hash == kDoFSpriteHash || shader_hashes_dof_composite.Contains(hash, reshade::api::shader_stage::pixel));
            dof_pass |= (hash == kDoFLayerResampleHash && gd.dof_layers_open);
         }
         if (dof_pass)
         {
            if (DrawOutputSizedDoFPass(native_device, native_device_context, cmd_list_data, device_data, &gd, hash, *original_draw_dispatch_func))
               return DrawOrDispatchOverrideType::Replaced;
            if (!gd.post_fallback)
            {
               FallBackToRenderSize(native_device, native_device_context, cmd_list_data, device_data, &gd, compute);
            }
         }
      }
      // After a fallback the engine's buckets still hold the sprites of the last frame it built natively (this frame's are in their
      // twins, whose counts its draws copy): the binning and the sprites are skipped, the frame goes without bokeh
      if (is_immediate && gd.post_fallback && !gd.output_twins.empty())
      {
         const bool compute = (stages & reshade::api::shader_stage::compute) != 0;
         com_ptr<ID3D11ShaderResourceView> bucket_srv;
         if (compute && original_shader_hashes.compute_shaders[0] == kDoFSpriteBinHash)
         {
            native_device_context->CSGetShaderResources(3, 1, &bucket_srv);
         }
         else if (!compute && original_shader_hashes.pixel_shaders[0] == kDoFSpriteHash)
         {
            native_device_context->VSGetShaderResources(3, 1, &bucket_srv);
         }
         if (bucket_srv)
         {
            com_ptr<ID3D11Resource> bucket;
            bucket_srv->GetResource(&bucket);
            if (gd.FindOutputTwinRecord(bucket.get()))
               return DrawOrDispatchOverrideType::Replaced;
         }
      }

      // A native dispatch between the upscaler and the tonemap reads current engine textures (as the draws below)
      if (is_immediate && !gd.output_twins.empty() && !gd.tonemap_target && (stages & reshade::api::shader_stage::compute) != 0)
      {
         RestoreEngineInputs(native_device, native_device_context, device_data, &gd, true);
      }

      // Upscaling: the tonemap at the output size, then every draw on its target (see "upscaled_scene")
      // Probed only by the draws that get this far below
      const auto is_tonemap = [&]
      { return shader_hashes_tonemap.Contains(original_shader_hashes.pixel_shaders[0], reshade::api::shader_stage::pixel); };
      if (is_immediate && !gd.output_twins.empty() && (stages & reshade::api::shader_stage::pixel) != 0 && original_draw_dispatch_func && *original_draw_dispatch_func)
      {
         // A replaced present drawn from here reads LumaSettings, which Core binds only after this hook
         if (is_custom_pass && !updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            updated_cbuffers = true;
         }
         if (gd.tonemap_target)
         {
            if (RedirectTonemapTarget(native_device, native_device_context, cmd_list_data, device_data, &gd, *original_draw_dispatch_func))
               return DrawOrDispatchOverrideType::Replaced;
         }
         else if (const uint32_t hash = original_shader_hashes.pixel_shaders[0]; shader_hashes_tonemap.Contains(hash, reshade::api::shader_stage::pixel))
         {
            if (DrawUpscaledTonemap(native_device, native_device_context, cmd_list_data, device_data, &gd, *original_draw_dispatch_func))
               return DrawOrDispatchOverrideType::Replaced;
         }
         else if (!gd.post_fallback && shader_hashes_motion_blur.Contains(hash, reshade::api::shader_stage::pixel) &&
                  DrawOutputSizedMotionBlurPass(native_device, native_device_context, cmd_list_data, device_data, &gd, hash, *original_draw_dispatch_func))
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
         else if (!gd.post_fallback && gd.bloom_scene && (hash == kBloomUpsampleHash || (hash == kDoFLayerResampleHash && !gd.dof_layers_open)) &&
                  DrawOutputSizedBloomPass(native_device, native_device_context, cmd_list_data, device_data, &gd, hash == kBloomUpsampleHash, *original_draw_dispatch_func))
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
         else if (!gd.post_fallback &&
                  (shader_hashes_scanner.Contains(hash, reshade::api::shader_stage::pixel) || (hash == kDoFLayerResampleHash && gd.scanner_open && !gd.dof_layers_open)) &&
                  DrawOutputSizedScannerPass(native_device, native_device_context, cmd_list_data, device_data, &gd, *original_draw_dispatch_func))
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
         else if (const auto* const blur = std::find_if(std::begin(kPostBlurPasses), std::end(kPostBlurPasses), [hash](const PostBlurPass& pass)
                     { return pass.hash == hash; });
            !gd.post_fallback && blur != std::end(kPostBlurPasses) &&
            DrawOutputSizedBlurPass(native_device, native_device_context, cmd_list_data, device_data, &gd, *blur, *original_draw_dispatch_func))
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
         else if (!gd.post_fallback && DrawIntoOutputTwin(native_device, native_device_context, cmd_list_data, device_data, &gd, *original_draw_dispatch_func))
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
         else
         {
            // It draws natively at the render size: what it reads must be current there
            RestoreEngineInputs(native_device, native_device_context, device_data, &gd, false);
         }
      }
#endif

#if ENABLE_SMAA && ENABLE_SR
      // "has_drawn_sr" is this frame's (reset at present)
      if (g_rcas_sharpness > 0.f && device_data.has_drawn_sr && is_immediate && original_draw_dispatch_func && *original_draw_dispatch_func && is_tonemap())
      {
         return DrawTonemapWithRCAS(native_device, native_device_context, cmd_list_data, device_data, &gd, *original_draw_dispatch_func);
      }
#endif

      if (tonemap_effects)
      {
         (*original_draw_dispatch_func)();
         return DrawOrDispatchOverrideType::Replaced;
      }

      if ((stages & reshade::api::shader_stage::compute) == 0)
         return DrawOrDispatchOverrideType::None;
      const uint32_t compute_hash = original_shader_hashes.compute_shaders[0];
      const bool dof_variant = shader_hashes_taa_resolve_dof.Contains(compute_hash, reshade::api::shader_stage::compute);
      if (!dof_variant && !shader_hashes_taa_resolve.Contains(compute_hash, reshade::api::shader_stage::compute))
         return DrawOrDispatchOverrideType::None;

      device_data.taa_detected = true; // game's TAA pass present (feeds core's SR-engaged ✓ indicator); latched, never reset
      MEA_COUNT(taa_resolves);
      if (!is_immediate)
      {
         MEA_COUNT(taa_resolves_deferred);
         return DrawOrDispatchOverrideType::None; // SR can't run on a deferred context: native TAA
      }
#if ENABLE_SR
      return ReplaceTAAResolve(native_device, native_device_context, cmd_list_data, device_data, &gd, dof_variant, original_draw_dispatch_func);
#else
      return DrawOrDispatchOverrideType::None;
#endif
   }

#if ENABLE_SR
   void CleanExtraSRResources(DeviceData& device_data) override
   {
      GetGameDeviceData(device_data).release_sr_resources = true;
   }
#endif

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // The game turned the display's HDR on itself through NVAPI (now swallowed): with Windows HDR off, turn it on as Luma's Display
      // Mode HDR does (Core's "ChangeDisplayMode"). Once per launch, so turning Windows HDR off later sticks.
      if (NvapiHdr::requested_hdr.load() == 1 && hdr_supported_display && !hdr_enabled_display && !std::exchange(NvapiHdr::display_hdr_enabled_once, true))
      {
         ChangeDisplayMode(device_data, DisplayModeType::HDR, true);
      }

#if ENABLE_SR
      // The upscaler's history restarts after any frame it didn't draw (menus, loading, FXAA mode, just picked)
      device_data.force_reset_sr = !device_data.has_drawn_sr;
      device_data.has_drawn_sr = false;
      gd.sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#endif
      UpdateWorldRenderSettings(device_data);
#if ENABLE_SR
      // The render scale only while there's a TAA resolve to upscale at: without one (the galaxy map) the engine would only
      // stretch the frame. A few frames of grace, so a frame without the resolve doesn't reallocate the targets twice.
      constexpr uint32_t kRenderScaleGraceFrames = 30;
      UpdateGameRenderSettings(gd.sr_active && gd.taa_resolve_frame != UINT32_MAX && cb_luma_global_settings.FrameIndex - gd.taa_resolve_frame <= kRenderScaleGraceFrames);
#if DEVELOPMENT
      // The permutation census knobs, written after Luma's own writes so they win (the resample mode included). One byte: every
      // value fits and the enums' upper bytes are 0.
      const auto apply_engine_mode = [](uint8_t* settings, size_t offset, int forced, uint32_t* value)
      {
         if (!settings)
            return;
         if (forced >= 0)
         {
            settings[offset] = uint8_t(forced);
         }
         *value = settings[offset];
      };
      apply_engine_mode(g_world_render_settings, kWrsPostProcessAntialiasingMode, g_dev_engine_aa_mode, &g_dev_engine_aa_mode_value);
      apply_engine_mode(g_game_render_settings, kGrsRenderScaleResampleMode, g_dev_engine_resample_mode, &g_dev_engine_resample_mode_value);
#endif
      // None picked: Core freed its upscaler resources and output, ours go too (recreated when an upscaler is picked again)
      if (device_data.sr_type == SR::Type::None && gd.release_sr_resources.exchange(false))
      {
         gd.sr_output_srv.reset();
         gd.ReleaseUpscaledScene();
         gd.ao = {};
      }
      gd.output_twins.clear();
      for (auto& target : gd.output_targets)
      {
         target.in_use = false;
      }
      for (auto& buffer : gd.output_buffers)
      {
         buffer.in_use = false;
      }

      gd.dof_layers_open = false;
      gd.bloom_scene.reset();
      gd.post_fallback = false;
      gd.output_depth_stencil_engine.reset();
      gd.scanner_open = false;
      gd.tonemap_target.reset();
      gd.tonemap_upscaled = false;
      gd.upscaled_scene_filled = false;
#endif

#if ENABLE_SMAA
      // SMAA's resources go once it stopped running (TAA mode, or it's off), RCAS's once it stopped as well
      if (cb_luma_global_settings.FrameIndex - gd.smaa_frame > smaa_idle_release_frames)
      {
         gd.cb_smaa_metrics.reset();
         gd.ReleasePredication();
         ReleaseSMAAIntermediates(device_data);
      }
      else if (!g_smaa_predication && gd.tex_pred)
      {
         gd.ReleasePredication();
      }
      if (gd.tex_rcas_input && cb_luma_global_settings.FrameIndex - gd.sharpen_frame > smaa_idle_release_frames)
      {
         gd.ReleaseRCAS();
      }
#endif

      // From the last resolve's render height to the output: -1 at native resolution, -2 at half, 1 less with XeSS (Core biases
      // the game's anisotropic samplers, all upgraded to AF16x); vanilla without SR
#if ENABLE_SR
      const float mip_lod_bias = (gd.sr_active ? SR::GetMipLODBias(device_data.sr_type, device_data.render_resolution.y, device_data.output_resolution.y) : 0.f);
#else
      constexpr float mip_lod_bias = 0.f;
#endif
      // Picked up by the samplers as the game binds them
      // Only this thread writes it: the lock (which sampler creation also takes) only on a change
      if (!custom_texture_mip_lod_bias_offset && device_data.texture_mip_lod_bias_offset != mip_lod_bias)
      {
         const std::unique_lock lock(s_mutex_samplers);
         device_data.texture_mip_lod_bias_offset = mip_lod_bias;
      }

      const CB::LumaGameSettings game_settings = {
         .HDRFix = (g_hdr_fix ? 1.f : 0.f),
         .GammaCorrection = (g_hdr_gamma_correction ? 1.f : 0.f),
         .VideoActive = 0.f, // Set again by the next frame's decode
         .VideoAutoHDREnable = (g_video_auto_hdr ? 1.f : 0.f),
         .VideoAutoHDRBoost = g_video_auto_hdr_boost,
         .Dithering = (g_dithering ? 1.f : 0.f),
         .Contrast = g_contrast,
         .Saturation = g_saturation,
         .HighlightDechroma = g_highlight_dechroma,
         .HideUI = (g_hide_ui ? 1.f : 0.f),
      };
      g_video_decoded.store(false, std::memory_order_relaxed);
      if (std::memcmp(&cb_luma_global_settings.GameSettings, &game_settings, sizeof(game_settings)) != 0)
      {
         cb_luma_global_settings.GameSettings = game_settings;
         device_data.cb_luma_global_settings_dirty = true;
      }

      gd.cam_valid_this_frame = false;
#if DEVELOPMENT
      g_counters_last_frame = std::exchange(g_counters_this_frame, {});
#endif
      {
         const std::unique_lock lock(gd.map_cache.mutex);
         gd.map_cache.count = 0; // re-capture ring ptr next frame
      }
   }

   // The user settings from the ReShade config (section NAME, "Luma"). Called on boot.
   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      // "SMAASharpness" is the key before the rename: still read so existing values carry over
      reshade::get_config_value(nullptr, NAME, "SMAASharpness", g_rcas_sharpness);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
      reshade::get_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      reshade::get_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
      reshade::get_config_value(nullptr, NAME, "DisableTAASharpening", g_disable_taa_sharpening);
      reshade::get_config_value(nullptr, NAME, "ImproveTAAJitter", g_improve_taa_jitter);
      reshade::get_config_value(nullptr, NAME, "RenderScale", g_render_scale);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", g_video_auto_hdr);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", g_video_auto_hdr_boost);
      reshade::get_config_value(nullptr, NAME, "Dithering", g_dithering);
      reshade::get_config_value(nullptr, NAME, "Exposure", g_exposure);
      reshade::get_config_value(nullptr, NAME, "Contrast", g_contrast);
      reshade::get_config_value(nullptr, NAME, "Saturation", g_saturation);
      reshade::get_config_value(nullptr, NAME, "HighlightDechroma", g_highlight_dechroma);
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      reshade::get_config_value(nullptr, NAME, "VignetteIntensity", g_vignette_intensity);
      reshade::get_config_value(nullptr, NAME, "FilmGrainIntensity", g_film_grain_intensity);
      reshade::get_config_value(nullptr, NAME, "ChromaticAberrationIntensity", g_chromatic_aberration_intensity);
#if DEVELOPMENT
      reshade::get_config_value(nullptr, NAME, "HDRFix", g_hdr_fix);
      reshade::get_config_value(nullptr, NAME, "HDRGammaCorrection", g_hdr_gamma_correction);
#endif
      g_render_scale = std::clamp(g_render_scale, kMinRenderScale, 1.f);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Display");
      DrawDisplaySettings(device_data);

#if ENABLE_SR
      const bool sr_selected = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#else
      constexpr bool sr_selected = false;
#endif
#if DEVELOPMENT
      // Always on outside of development builds
      DrawSettingCheckbox("HDR Fix", "HDRFix", &g_hdr_fix, kDefaultHDRFix, "Luma's display mapping of the game's HDR (a C-infinity rolloff to the peak, paper white, UI white). Off = the game's own HDR.");
      ImGui::BeginDisabled(!g_hdr_fix);
      DrawSettingCheckbox("Gamma Correction", "HDRGammaCorrection", &g_hdr_gamma_correction, kDefaultHDRGammaCorrection, "Shows the shadows and the UI as a gamma 2.2 SDR display would, as the game was mastered.");
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Anti-Aliasing");
#if ENABLE_SR
      ImGui::BeginDisabled(!sr_selected);
      // Saved as a 0-1 float, applied on release: every render size recreates the upscaler's feature, a hitch per 1% step while dragging
      static int held_render_scale = 0; // The slider's value while it's held, else 0
      int render_scale_percent = (held_render_scale != 0 ? held_render_scale : int(std::round(g_render_scale * 100.f)));
      ImGui::SliderInt("Render Scale (%)", &render_scale_percent, int(kMinRenderScale * 100.f), 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
      held_render_scale = (ImGui::IsItemActive() ? render_scale_percent : 0);
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
         g_render_scale = float(render_scale_percent) / 100.f;
         reshade::set_config_value(nullptr, NAME, "RenderScale", g_render_scale);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("The resolution the game renders at, upscaled by DLSS/FSR. Replaces the game's Resolution Scale while they run.");
      }
      DrawResetButton(g_render_scale, 1.f, "RenderScale");
      ImGui::EndDisabled();
#endif
      DrawSettingCheckbox("SMAA Enable", "SMAAEnable", &g_smaa_enable, kDefaultSmaaEnable, "Replaces the game's FXAA with SMAA (only active when in-game Anti-Aliasing is set to FXAA).");
      ImGui::BeginDisabled(!g_smaa_enable && !sr_selected);
      DrawSettingSlider("RCAS Sharpness", "RCASSharpness", &g_rcas_sharpness, kDefaultRcasSharpness, 0.f, 1.f, "Sharpening applied on top of SMAA or DLSS/FSR (0 = off).");
      ImGui::EndDisabled();
#if DEVELOPMENT || TEST
      ImGui::BeginDisabled(!g_smaa_enable);
      DrawSettingCheckbox("SMAA Predication", "SMAAPredication", &g_smaa_predication, kDefaultSmaaPredication, "Finds edges by geometry (scene depth) as well as by brightness.\nKeeps textures sharp while still antialiasing real silhouettes.");
      DrawSettingSlider("SMAA Predication Tolerance", "SMAAPredicationTolerance", &g_smaa_pred_tolerance, kDefaultSmaaPredTolerance, 0.002f, 0.2f, "How far a surface may deviate from its local plane before it counts as an edge,\nas a fraction of view depth. Lower = more edges.", "%.3f", ImGuiSliderFlags_Logarithmic);
      ImGui::EndDisabled();
#endif

      ImGui::SeparatorText("Grade");
      DrawSettingSlider("Exposure", "Exposure", &g_exposure, kDefaultExposure, 0.f, 2.f, "Overall image brightness (1 = vanilla).");
      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         DrawSettingSlider("Contrast", "Contrast", &g_contrast, kDefaultContrast, 0.f, 2.f, "Overall image contrast, HDR only (1 = vanilla).");
         DrawSettingSlider("Saturation", "Saturation", &g_saturation, kDefaultSaturation, 0.f, 2.f, "Color saturation, HDR only (1 = vanilla).");
         DrawSettingSlider("Highlights Desaturation", "HighlightDechroma", &g_highlight_dechroma, kDefaultHighlightDechroma, 0.f, 1.f,
            "How far the brightest sources fade to neutral white, HDR only (0 = keep color at any brightness).");
      }

      ImGui::SeparatorText("Bloom");
      DrawSettingSlider("Bloom Intensity", "BloomIntensity", &g_bloom_intensity, kDefaultBloomIntensity, 0.f, 2.f, "Bloom strength (1 = vanilla, 0 = none).");

      ImGui::SeparatorText("Effects");
      DrawSettingSlider("Vignette Intensity", "VignetteIntensity", &g_vignette_intensity, kDefaultVignetteIntensity, 0.f, 2.f, "Scales the game's vignette darkening (1 = vanilla, 0 = none).");
      DrawSettingSlider("Film Grain Intensity", "FilmGrainIntensity", &g_film_grain_intensity, kDefaultFilmGrainIntensity, 0.f, 2.f, "Scales the game's film grain (1 = vanilla, 0 = off).");
      DrawSettingSlider("Chromatic Aberration Intensity", "ChromaticAberrationIntensity", &g_chromatic_aberration_intensity, kDefaultChromaticAberrationIntensity, 0.f, 2.f,
         "Scales the game's color fringing toward the screen edges (1 = vanilla, 0 = none).");
      DrawSettingCheckbox("Video AutoHDR", "VideoAutoHDREnable", &g_video_auto_hdr, kDefaultVideoAutoHDR, "Adds HDR highlights to pre-rendered videos (HDR only).");
      ImGui::BeginDisabled(!g_video_auto_hdr);
      DrawSettingSlider("Video HDR Boost", "VideoAutoHDRBoost", &g_video_auto_hdr_boost, kDefaultVideoAutoHDRBoost, 0.f, 1.f, "Video highlight strength (0 = off).");
      ImGui::EndDisabled();
      DrawSettingCheckbox("Dithering", "Dithering", &g_dithering, kDefaultDithering, "Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui); // Never saved, see "g_hide_ui"
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Disables the in-game UI.");
      }

      ImGui::SeparatorText("Fixes");
      // SR replaces the TAA resolve (no built-in sharpening) and uses its own Halton jitter: both fixes are shown as always on
      DrawSettingCheckbox("Disable Native TAA Sharpening", "DisableTAASharpening", &g_disable_taa_sharpening, kDefaultDisableTaaSharpening, "The game's TAA sharpens its output in tiles, which boosts leftover noise and leaves a faint grid. This turns it off, for a cleaner image.\nDLSS / FSR never use it.", sr_selected, true);
      DrawSettingCheckbox("Fix Native TAA Jitter", "ImproveTAAJitter", &g_improve_taa_jitter, kDefaultImproveTaaJitter, "The game's TAA samples each pixel in a clumped pattern, so still images flicker. This spreads the samples evenly, for a steadier image.\nDLSS / FSR always use their own fix.", sr_selected, true);
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Motion vectors");
      ImGui::Checkbox("MV flip X", &g_mv_flip_x);
      ImGui::Checkbox("MV flip Y", &g_mv_flip_y);
      ImGui::SliderFloat("MV scale mult", &g_mv_scale_mult, 0.25f, 4.0f);
      ImGui::Checkbox("MVs already jittered", &g_mv_jittered);

      ImGui::SeparatorText("Jitter");
      ImGui::Checkbox("Jitter flip X", &g_jitter_flip_x);
      ImGui::Checkbox("Jitter flip Y", &g_jitter_flip_y);
      ImGui::Checkbox("Halton jitter with SR", &g_halton_jitter);
      if (ImGui::BeginCombo("Halton phases with the game's TAA", std::to_string(g_halton_jitter_native_phases).c_str()))
      {
         for (const int phases : {4, 8, 16, 32, 64})
         {
            const bool selected = g_halton_jitter_native_phases == phases;
            if (ImGui::Selectable(std::to_string(phases).c_str(), selected))
            {
               g_halton_jitter_native_phases = phases;
            }
            if (selected)
            {
               ImGui::SetItemDefaultFocus();
            }
         }
         ImGui::EndCombo();
      }
   }
#endif // DEVELOPMENT

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Mass Effect: Andromeda\" is developed by DristoforColumb and is open source and free.\n"
         "It replaces the game's TAA with DLSS, FSR or XeSS upscaling and its FXAA with SMAA, plus 16x anisotropic filtering and a fix for the game's TAA jitter.\n"
         "With the game's HDR enabled it also replaces the native HDR tonemap with a higher quality one.\n"
         "Set Anti-Aliasing in the game's video settings to TAA for DLSS, FSR and XeSS, or to FXAA for SMAA.\n"
         "Do NOT run another HDR mod (e.g. RenoDX) alongside it.\n"
         "Thanks to the Luma team and contributors.\n"
         "If you enjoy it, consider donating.");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link = std::string("Buy DristoforColumb a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link.c_str()))
      {
         ShellExecuteA(nullptr, "open", "https://ko-fi.com/dristoforcolumb", nullptr, nullptr, SW_SHOWNORMAL);
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
         static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
         ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);
      }

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nDristoforColumb"
                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nRenoDX (HDR tonemap method)"
                  "\nSMAA (Iryoku)"
                  "\nAMD FidelityFX (RCAS + FSR 3)"
                  "\nNVIDIA NGX (DLSS)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Mass Effect: Andromeda Luma mod", "", 2);
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Finished;

      // The game owns its HDR10 output (the HDR fix works inside its presents): no swapchain or texture format upgrades
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;

      // Core's display composition writes scRGB only: on the game's HDR10 (and SDR) backbuffer it would only darken it
      force_disable_display_composition = true;

      // FSE is blocked by default: center the window and drop its borders when the game asks for it
      force_borderless = true;

      // Sharper textures: AF16x + negative mip LOD bias (mode 4 = AF16x + additive bias), the bias set every present from the
      // render size (see OnPresent)
      enable_samplers_upgrade = true; // boot-time only
      samplers_upgrade_mode = 4;

      // The present permutations share one file each, "Present_0x########" and "Loading_0x########": every hash selects its axes in it
      redirected_shader_hashes["Present"] = {
         "0BF0C1CA",
         "0BF80A15",
         "0E11A4EE",
         "19404650",
         "20EE9B0F",
         "22179331",
         "3196D1D2",
         "4091E710",
         "4840CCE4",
         "4BCF53E3",
         "5C1B1CD3",
         "66868010",
         "6A9767E9",
         "6BDFE71C",
         "6DA050CF",
         "6F7FC946",
         "7D79A746",
         "8498DBD5",
         "8ECC1073",
         "8FD42D7C",
         "9201B072",
         "950D3AE4",
         "9BFA3175",
         "9E9044E7",
         "9FCE7944",
         "A04BFC23",
         "A1B754F2",
         "A550FC99",
         "A888DCF8",
         "ACA466A8",
         "AFFFA4AB",
         "BE69B105",
         "BF88A9F8",
         "D0200425",
         "D30EB54B",
         "E0AA30A6",
         "E1CF4FA8",
         "EE044AAF",
         "F5B0DBFA",
         "FED81CFB",
      };
      redirected_shader_hashes["Loading"] = {
         "182C8867",
         "4739F9F6",
         "48E0CE55",
         "4ACE2324",
         "667C56AF",
         "70888904",
         "7C642BA7",
         "A4E53513",
         "B78EE6A9",
         "BF998050",
         "EF390C6B",
         "F5B7A93D",
      };

      game = new MassEffectAndromeda();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      // We registered these in OnInit; unregister so a reload doesn't double-register / dangle. A null "lpReserved" is an unload.
      MassEffectAndromeda::UnregisterEvents(lpReserved == nullptr);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
