#ifndef SRC_SETTINGS_HLSL
#define SRC_SETTINGS_HLSL

/////////////////////////////////////////
// LUMA advanced settings
// 
// Note that the defaults might be mirrored in c++, the shader values will be overridden anyway.
// Include the game cbuffer structs define before this ("LUMA_GAME_CB_STRUCTS") if you have one.
/////////////////////////////////////////

// Whether we store the post process buffers in linear space scRGB or gamma space (sRGB under normal circumstances) (like in the vanilla game, though now we use FP16 textures as opposed to UNORM8 ones).
// Note that converting between linear and gamma space back and forth results in quality loss, especially over very high and very low values, so this is best left on.
// 0 Gamma space:
//   vanilla like (including UI, depending on "UI_DRAW_TYPE"), but on float/linear buffers
//   this has as tiny loss of quality due to storing sRGB gamma space on linear buffers
//   gamma correction happens at the end, in the linearization pass
//   colors are stored with the same white level (paper white) of SDR, with 1 being the SDR peak white
// 1 Linear space:
//   UI might look slightly different from vanilla (in alpha blends) (depending on "UI_DRAW_TYPE")
//   gamma correction happens early, in tonemapping, or at the end (depending on "EARLY_DISPLAY_ENCODING")
//   colors are stored with the same white level (paper white) of SDR, with 1 being the SDR peak white, or in dynamic paper white range (also depending on "EARLY_DISPLAY_ENCODING")
// 2 Linear space until UI, then gamma space:
//   more specifically, linear until the last pass that always runs before UI (e.g. "PostAAComposites" for "Prey"),
//   this has the some of the advantage of both linear and gamma methods, and UI looks like vanilla out of the box
//   gamma correction happens at the end, in the linearization pass
//   (to avoid a billion different formulas around the code and to make gamma blends look like vanilla,
//    if we corrected in the tonemap/grading shader, then we'd need to use sRGB gamma in the end,
//    also we wouldn't know whether to correct the 0-1 range of the whole range)
//   ideally we would have gamma corrected before HDR tonemapping, but the complexity cost is too big for the small visual gains
#ifndef POST_PROCESS_SPACE_TYPE
#define POST_PROCESS_SPACE_TYPE 0
#endif
// How did the Vanilla SDR encode after tonemapping?
// If the game used sRGB "linear" buffers, select sRGB.
// If the game drew in gamma space already without ever passing through linear HDR before tonemapping, select Gamma 2.2.
// If the game used a random encoding formula (e.g. tonemap and encoding in a single function), also select Gamma 2.2.
// If "POST_PROCESS_SPACE_TYPE" is 0, we assume the HDR implementation uses the same encoding.
// Note that all gamma 2.2 setting with inherit whatever gamma is set by "DefaultGamma", be it 2.2, 2.4 or anything else.
//
// 0 sRGB
// 1 Gamma 2.2
#ifndef VANILLA_ENCODING_TYPE
#define VANILLA_ENCODING_TYPE 0
#endif
// As many games, Prey rendered and tonemapped in linear space, though applied the sRGB gamma transfer function to apply the color grading LUT.
// Almost all TVs follow gamma 2.2 and most monitors also do, so to maintain the SDR look (and near black level), we need to linearize with gamma 2.2 and not sRGB (1).
// Disabling this will linearize with gamma sRGB, ignoring that the game would have been developed on (and for) gamma 2.2 displays (<=0).
// If you want something in between, thus keeping the sRGB color hue (channels ratio) but with the gamma 2.2 corrected luminance, set this to a higher value (>=2).
// Note that if "POST_PROCESS_SPACE_TYPE" is 0, this simply determines how gamma is linearized for intermediary operations,
// while everything stays in sRGB gamma (as theoretically it would have been originally, even if it was displayed on 2.2) when stored in textures,
// so this determines how the final shader should linearize (if >=1, from 2.2, if <=0, from sRGB, thus causing raised blacks compared to how the gamma would have appeared on gamma 2.2 displays).
// Note that by gamma correction we mean fixing up the game's bad gamma implementation, though sometimes this term is used to imply "display encoding".
// Note that all gamma 2.2 setting with inherit whatever gamma is set by "DefaultGamma", be it 2.2, 2.4 or anything else.
// 
// 0 sRGB
// 1 Gamma 2.2
// 2 sRGB (color hues) with gamma 2.2 luminance (kinda assuming "VANILLA_ENCODING_TYPE" is 0)
// 3 sRGB (color hues) with gamma 2.2 luminance (corrected by luminance)
// 4 Gamma 2.2 (corrected by luminance) with per channel correction chrominance
#ifndef GAMMA_CORRECTION_TYPE
#define GAMMA_CORRECTION_TYPE 1
#endif
// Whether we correct gamma even on colors beyond the 0-1 range. Usually that's not suggested as they can overshoot due to pow differences (sRGB is completely different below and around zero),
// and given these colors were never "seen" and we generated them ourselves with tonemapper modifications, it makes no sense to apply gamma correction on them too.
// If se set this to 0, we'd also need to pre-acknowledge the later peak shift when tonemapping to the display. Plus, pow changes direction around 1, so doing that would contribute to creating a less contiguous curve.
// 
// 0 Whole range
// 1 0-1 range only
#ifndef GAMMA_CORRECTION_RANGE_TYPE
#define GAMMA_CORRECTION_RANGE_TYPE 1
#endif
// Whether the main gamma correction and paper white scaling happens early in post processing or in the final display composition pass (only applies if "POST_PROCESS_SPACE_TYPE" is set to linear).
// For simplicity and consistency of post processing passes, it's better to keep this disabled.
#ifndef EARLY_DISPLAY_ENCODING
#define EARLY_DISPLAY_ENCODING 0
#endif
// Ensures the final colors are valid and contained within the display/output gamut range.
// It's possibly good to turn this on if "EARLY_DISPLAY_ENCODING" is false, in case late gamma correction generated any invalid luminance or out of gamut colors. Film grain and sharpening etc can also often generated invalid colors.
// 
// 0 None
// 1 Auto (SDR/HDR)
// 2 SDR - BT.709
// 3 HDR - BT.2020
#ifndef GAMUT_MAPPING_TYPE
#define GAMUT_MAPPING_TYPE 0
#endif
// See c++
#ifndef UI_DRAW_TYPE
#define UI_DRAW_TYPE 0
#endif
// Higher quality gamma<->linear conversions, it avoids the error generated from the conversion by restoring the change on the original color in an additive way.
// This has a relatively high performance cost for the visual gains it returns.
#define HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS 1
// Necessary for HDR to work correctly
#ifndef ENABLE_LUT_EXTRAPOLATION
#define ENABLE_LUT_EXTRAPOLATION 1
#endif
// See "LUTExtrapolationSettings::extrapolationQuality"
#ifndef LUT_EXTRAPOLATION_QUALITY
#define LUT_EXTRAPOLATION_QUALITY 1
#endif
// It's better to leave the classic LUT interpolation (bilinear/trilinear),
// LUTs in Prey are very close to being neutral so tetrahedral interpolation just shifts their colors without gaining much, possibly actually losing quality.
// This is even less necessary when LUTs input and output is linear.
#ifndef ENABLE_LUT_TETRAHEDRAL_INTERPOLATION
#define ENABLE_LUT_TETRAHEDRAL_INTERPOLATION 0
#endif
// Do it higher than 8 bit for HDR
#ifndef DITHERING_BIT_DEPTH
#define DITHERING_BIT_DEPTH 9u
#endif
// Disables development features if off
#ifndef DEVELOPMENT
#define DEVELOPMENT 0
#endif
#ifndef TEST
#define TEST 0
#endif
#ifndef LUMA_SETTINGS_CB_INDEX
#define LUMA_SETTINGS_CB_INDEX b13
#endif
#ifndef LUMA_DATA_CB_INDEX
#define LUMA_DATA_CB_INDEX b12
#endif
#ifndef LUMA_UI_DATA_CB_INDEX
#define LUMA_UI_DATA_CB_INDEX b11
#endif

/////////////////////////////////////////
// Rendering features toggles (development)
/////////////////////////////////////////

// This might also disable decals interfaces (like computer screens) in the 3D scene
#define ENABLE_UI (!DEVELOPMENT || 1)

/////////////////////////////////////////
// Debug toggles
/////////////////////////////////////////

// 0 None
// 1 Neutral LUT
// 2 Neutral LUT + bypass extrapolation
#if !defined(FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE) || !DEVELOPMENT
#undef FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE
#define FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE (DEVELOPMENT && 0)
#endif
#if !defined(DRAW_LUT) || !DEVELOPMENT
#undef DRAW_LUT
#define DRAW_LUT (DEVELOPMENT && 0)
#endif
// Debug LUT Pixel scale (this is rounded to the closest integer value for the size of the LUT)
// 10u is a good value for 2560 horizontal res. 20 for 5120 horizontal res or more.
#define DRAW_LUT_TEXTURE_SCALE 10u

/////////////////////////////////////////
// LUMA user settings
/////////////////////////////////////////

// In case the per game code had not defined a custom struct, define a generic empty one. This behaviour is matched in c++.
#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS
#if REQUIRES_LUMA_GAME_CB_STRUCTS
#error "Settings.hlsl has not been included after the per game CB structs definitions"
#endif
namespace CB
{
	// hlsl doesn't support empty structs, so add a dummy variable (ideally it'd be empty or optional but it won't realistically affect performance)
	struct LumaGameSettings
	{
		float Dummy;
	};
	struct LumaGameData
	{
		float Dummy;
	};
}
#endif

// Luma global settings (usually, but not necessarily, changed a maximum of once per frame)
// Regarding "LUMA_SETTINGS_CB_INDEX", most engines (e.g. CryEngine, Unreal, Unity) push the registers that are used by each shader again for every draw, so it's generally safe to overridden them anyway (they are all reset between frames).
// Game and Dev settings should only be accessed outside of global shaders and includes, given the struct isn't guaranteed to be fully defined there.
cbuffer LumaSettings : register(LUMA_SETTINGS_CB_INDEX)
{
  struct
  {
    float2 SwapchainSize;
    float2 SwapchainInvSize;
    float2 RenderSize;
    float2 RenderInvSize;
    // 0 for SDR (80 nits) (gamma sRGB output)
    // 1 for HDR
    // 2 for SDR on HDR (203 nits) (gamma 2.2 output)
    uint DisplayMode;
    float PeakWhiteNits; // Access this through the global variables below
    float GamePaperWhiteNits; // Access this through the global variables below (this either applies to the game scene colors, or to the whole final image)
    float UIPaperWhiteNits; // Access this through the global variables below (only usable in certain "UI_DRAW_TYPE" modes)
    uint SRType; // Which SR is enabled if any (implies it'd be engaged and compatible) (this is on even in fullscreen UI menus that don't use upscaling)
    uint FrameIndex; // Frame counter, no need for this to be by device or swapchain
    
// These ideally should be after anything else or if we toggled the "DEVELOPMENT" flag, the rest would get misaligned,
// however for some reason the alignment of nested structs (the ones below) doesn't match between c++ and hlsl,
// so we have to do tricks on this one. We already tried "alignas(16)" and other types of padding to no success.
#if DEVELOPMENT || (defined(CPU_DEVELOPMENT) && CPU_DEVELOPMENT)
    // These are reflected in ImGui (the number of them is hardcoded in c++).
    // You can add up to 3 numbers as comment to their right to define the UI settings sliders default, min and max values, and their name.
    // Like: float DevSetting01; // DefaultValue, MinValue, MaxValue, Name
    float DevSetting01; // 0, 0, 1
    float DevSetting02; // 0, 0, 1
    float DevSetting03; // 0, 0, 1
    float DevSetting04; // 0, 0, 1
    float DevSetting05; // 0, 0, 1
    float DevSetting06; // 0, 0, 1
    float DevSetting07; // 0, 0, 1
    float DevSetting08; // 0, 0, 1
    float DevSetting09; // 0, 0, 1
    float DevSetting10; // 0, 0, 1
#else
    float2 Padding1;
#endif

    // NOTE: keep anything before the game settings aligned to 16 bytes (4 floats/ints). This is the only way to match both c++ and hlsl quirks at the same time.

    // This is here to avoid taking too many cbuffer slots for separate buffers. It's at the end to avoid alignment issues with non game specific data in case the game specific includes were missing.
    CB::LumaGameSettings GameSettings; // Custom games setting, with a per game struct.
  } LumaSettings : packoffset(c0);
}

// These parameters are already pushed directly from c++ so we don't need to check them manually anymore
#if 1
bool ShouldForceWhiteLevel() { return false; }
#else
bool ShouldForceWhiteLevel() { return LumaSettings.DisplayMode == 0; }
#endif
float GetForcedWhileLevel() { return (LumaSettings.DisplayMode == 0) ? sRGB_WhiteLevelNits : ITU_WhiteLevelNits; }

#if DEVELOPMENT
#define DVS1 LumaSettings.DevSetting01
#define DVS2 LumaSettings.DevSetting02
#define DVS3 LumaSettings.DevSetting03
#define DVS4 LumaSettings.DevSetting04
#define DVS5 LumaSettings.DevSetting05
#define DVS6 LumaSettings.DevSetting06
#define DVS7 LumaSettings.DevSetting07
#define DVS8 LumaSettings.DevSetting08
#define DVS9 LumaSettings.DevSetting09
#define DVS10 LumaSettings.DevSetting10
#else // Default to 1 even if when enabled they default to 0, it's more likely to be the neutral value
#define DVS1 1.0
#define DVS2 1.0
#define DVS3 1.0
#define DVS4 1.0
#define DVS5 1.0
#define DVS6 1.0
#define DVS7 1.0
#define DVS8 1.0
#define DVS9 1.0
#define DVS10 1.0
#endif

#ifdef HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS // You can define this to force an hardcoded paper white (for whatever reason)
static const float GamePaperWhiteNits = HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS;
static const float UIPaperWhiteNits = HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS;
#elif DEVELOPMENT
static const float GamePaperWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.GamePaperWhiteNits != 0 ? LumaSettings.GamePaperWhiteNits : ITU_WhiteLevelNits);
static const float UIPaperWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.UIPaperWhiteNits != 0 ? LumaSettings.UIPaperWhiteNits : ITU_WhiteLevelNits);
#else // HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
static const float GamePaperWhiteNits = LumaSettings.GamePaperWhiteNits;
static const float UIPaperWhiteNits = LumaSettings.UIPaperWhiteNits;
#endif // HDR_TONEMAP_PAPER_WHITE_BRIGHTNESS
#ifdef HDR_TONEMAP_PEAK_BRIGHTNESS
static const float PeakWhiteNits = HDR_TONEMAP_PEAK_BRIGHTNESS;
#elif DEVELOPMENT
static const float PeakWhiteNits = ShouldForceWhiteLevel() ? GetForcedWhileLevel() : (LumaSettings.PeakWhiteNits != 0 ? LumaSettings.PeakWhiteNits : 1000.0); // Same peak white default as in c++
#else // HDR_TONEMAP_PEAK_BRIGHTNESS
static const float PeakWhiteNits = LumaSettings.PeakWhiteNits;
#endif // HDR_TONEMAP_PEAK_BRIGHTNESS

#endif // SRC_SETTINGS_HLSL