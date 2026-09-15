#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
// User-facing grade controls, drawn in DrawImGuiSettings (main.cpp) and read in Luma_MOHA_Tonemap.hlsl unless a
// field says otherwise. Saturation, HighlightsDesaturation, Contrast and Dithering act only on the HDR tonemap path
// (TONEMAP_TYPE >= 1); Exposure and the three bloom fields act on the vanilla SDR path too.
struct LumaGameSettings
{
   float Exposure;               // exposure multiplier (1 = vanilla). Applied scene-referred, pre-grade.
   float Saturation;             // 1 = vanilla. Saturation multiplier on the final HDR color (lerp against luminance).
   float HighlightsDesaturation; // 0 = off (default). DICE highlight desaturation: sources above a third of peak fade toward white, mid-tones untouched.
   float BloomIntensity;         // 1 = default. Scales the Luma pyramid ONLY; the game's own glow shares a buffer with the DoF blur and is never scaled.
   float Contrast;               // 1 = vanilla. Multiplicative contrast around 18% mid-gray, applied before the display map.
   float Dithering;              // 0/1 toggle. Animated triangular dither at output to break gradient banding.
   // Appended, never reordered: this struct is a C++/HLSL ABI mirror.
   float LumaBloomEnable;    // 0/1. 1 = the Luma multi-scale HDR pyramid REPLACES the game's bloom (which the gather replacement then stops writing).
   float BloomThreshold;     // linear scene brightness where bloom starts. 1.0 matches the game's own bright-pass; near 0 makes the whole scene glow.
   float VideoAutoHDREnable; // 0/1. 1 = light PumboAutoHDR on the Bink movie pass (HDR only); 0 = flat SDR at paper white.
   float VideoAutoHDRBoost;  // 0..1. Highlight-expansion strength; peak = lerp(sRGB white, 250 nits, boost). 0 = off.
};

// Game specific cbuffer (instance/pass) data.
struct LumaGameData
{
   float Dummy; // hlsl doesn't support empty structs
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
