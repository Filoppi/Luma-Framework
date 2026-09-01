#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
// User grade controls: drawn in DrawImGuiSettings (main.cpp), read in Luma_BL2TPS_Tonemap.hlsl, all defaulting to a
// vanilla no-op. Exposure/BloomIntensity/VignetteIntensity act on SDR too (shared scene mix and vignette block);
// Saturation/HighlightDechroma/Contrast are HDR-display-path only. SMAA metrics use their own CB at b1.
struct LumaGameSettings
{
   float Exposure;           // 1 = vanilla. Scene exposure multiplier, scene-referred / pre-grade.
   float Saturation;         // 1 = vanilla. Oklab saturation multiplier on the final HDR color.
   float HighlightDechroma;  // 0 = off (only the mandatory DICE/gamut desat applies); higher = bright sources fade to white sooner.
   float BloomIntensity;     // 1 = vanilla. Scales the active bloom (Luma pyramid or the game's); C++ pre-folds the pyramid energy gain.
   float Contrast;           // 1 = vanilla. Slope contrast around 18% mid-gray on the final HDR color.
   float VignetteIntensity;  // 1 = vanilla. Scales the game's vignette darkening (0 = no vignette).
   float LumaBloomEnable;    // 0/1. 1 = composite Luma HDR pyramidal bloom (t5 BL2 / t8 TPS, additive); 0 = vanilla game bloom (t1).
   float Dithering;          // 0/1 toggle. Animated triangular dither at output (HDR only) to break gradient banding.
   float VideoAutoHDREnable; // 0/1. 1 = light PumboAutoHDR on Bink videos (HDR only); 0 = flat SDR at paper white.
   float VideoAutoHDRBoost;  // 0..1. Highlight-expansion strength; peak = lerp(sRGB white, 250 nits, boost). 0 = off.
   float BloomThreshold;     // Native bright pass's threshold, kept live off cb4[17].y -> the Luma prefilter's knee.
};

// Game specific cbuffer (instance/pass) data.
struct LumaGameData
{
   float Dummy; // hlsl doesn't support empty structs
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
