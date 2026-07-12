#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
// User-facing grade controls, drawn in DrawImGuiSettings (main.cpp) and read in Luma_BL_Tonemap.hlsl.
// All apply only on the HDR tonemap path. SMAA metrics are passed via a dedicated CB at b1, not here.
struct LumaGameSettings
{
   float Exposure;           // exposure multiplier (1 = vanilla). Applied scene-referred, pre-grade.
   float Saturation;         // 1 = vanilla. Oklab saturation multiplier on the final HDR color.
   float HighlightDechroma;  // 0 = off (default; keep color, only mandatory gamut desat applies); higher = bright sources fade to white sooner. Optional perceptual taste.
   float BloomIntensity;     // 1 = vanilla. Scales the game's bloom contribution in the scene mix.
   float Contrast;           // 1 = vanilla. Slope contrast around 18% mid-gray on the final HDR color.
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
