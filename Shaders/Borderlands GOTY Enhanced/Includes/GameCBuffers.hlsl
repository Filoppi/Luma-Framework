#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
// User-facing controls, drawn in DrawImGuiSettings (main.cpp) and read by the replaced shaders.
// SMAA metrics are passed via a dedicated CB at b1, not here.
struct LumaGameSettings
{
   float Exposure;           // exposure multiplier (1 = vanilla). Applied scene-referred, pre-grade.
   float Saturation;         // 1 = vanilla. Luminance-relative saturation multiplier on the final HDR color.
   float HighlightDechroma;  // 0 = off (default). DICE highlight desaturation: sources above a third of peak fade toward white, mid-tones untouched.
   float BloomIntensity;     // 1 = vanilla. Scales the game's bloom contribution in the scene mix.
   float Contrast;           // 1 = vanilla. Multiplicative contrast around 18% mid-gray, applied before the display map.
   float Dithering;          // 0/1 toggle. Animated triangular dither at output to break gradient banding.
   float FlareOut;           // 1 = vanilla. Scales the additive lens-flare/glare overlay (pass 0x010371F2).
   float VideoAutoHDREnable; // 0/1. Light AutoHDR on Bink movies (HDR only; pass 0x0E97A4A0). 0 = flat SDR at paper white.
   float VideoAutoHDRBoost;  // 0..1 highlight-expansion strength. 0 = off (peak == paper white); 1 = full VIDEO_AUTO_HDR_PEAK_NITS.
};

// Game specific cbuffer (instance/pass) data.
struct LumaGameData
{
   float Dummy; // hlsl doesn't support empty structs
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
