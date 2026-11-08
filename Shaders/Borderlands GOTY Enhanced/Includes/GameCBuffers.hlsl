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
   float Saturation;         // 1 = vanilla. Scales the RGB distance from relative luminance. HDR display path only.
   float HighlightDechroma;  // 0 = off (default). Highlight whitening of the hue reference (MacLeod-Boynton purity). HDR display path only.
   float BloomIntensity;     // 1 = vanilla. Scales the game's bloom contribution in the scene mix.
   float Contrast;           // 1 = vanilla. Power of relative luminance around mid gray. HDR display path only.
   float Dithering;          // 0/1 toggle. Animated triangular dither at output to break gradient banding.
   float FlareOut;           // 1 = vanilla. Scales the additive lens-flare/glare overlay (pass 0x010371F2).
   float VideoAutoHDREnable; // 0/1. Light AutoHDR on Bink movies (HDR only; pass 0x0E97A4A0). 0 = flat SDR at paper white.
   float VideoAutoHDRBoost;  // 0..1 highlight-expansion strength. 0 = off (peak at sRGB white); 1 = a 250 nit peak.
};

// Game specific cbuffer (instance/pass) data.
struct LumaGameData
{
   float Dummy; // hlsl doesn't support empty structs
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
