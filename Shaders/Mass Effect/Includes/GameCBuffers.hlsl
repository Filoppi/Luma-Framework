#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
// Grade controls, drawn in DrawImGuiSettings (main.cpp), read in Luma_ME1_Tonemap.hlsl unless noted. HDR tonemap path
// only, except Exposure and the bloom fields, which apply on the vanilla SDR path too (bloom only when the uber pass
// runs).
struct LumaGameSettings
{
   float Exposure;          // exposure multiplier (1 = vanilla). Applied scene-referred, pre-grade.
   float Saturation;        // 1 = vanilla. Saturation multiplier on the final HDR color (lerp against luminance).
   float HighlightDechroma; // 0 = off (default; keep color, only mandatory gamut desat applies); higher = bright sources fade further toward white.
   float BloomIntensity;    // 1 = default. Scales the Luma pyramid ONLY; the game's own glow shares a buffer with the DoF blur and is never scaled.
   float Contrast;          // 1 = vanilla. Power contrast around 18% mid-gray, applied before the display map.
   float Dithering;         // 0/1 toggle. Animated triangular dither at output to break gradient banding.
   // Appended, never reordered: this struct is a C++/HLSL ABI mirror.
   float LumaBloomEnable;    // 0/1. 1 = the Luma multi-scale HDR pyramid REPLACES the game's bloom (which the gather replacement then stops writing).
   float BloomThreshold;     // Luma_Bloom_impl.hlsl: linear scene brightness where bloom starts. 1.0 matches the game's own bright-pass; near 0 makes the whole scene glow.
   float VideoAutoHDREnable; // Video_0x1A82565B: 0/1. 1 = light PumboAutoHDR on the Bink movie pass (HDR only); 0 = flat SDR at paper white.
   float VideoAutoHDRBoost;  // 0..1. Highlight-expansion strength; peak = lerp(sRGB white, 250 nits, boost). 0 = off.
   // Not a user setting: the inverse display gamma the FGammaCorrection pass applies (cb4[11].x, measured 0.625), read
   // back by main.cpp and folded into the uber grade's display-linear decode (VanillaToLinear), because the two live in
   // different passes.
   float DisplayGammaInverse;
   // Not a user setting: the engine's BloomScale (gather cb4[11].x, measured 0.1), read back by main.cpp. The Luma
   // pyramid is energy-preserving, so this is the gain that makes BloomIntensity 1 mean vanilla strength.
   float BloomScaleLive;
};

// Game specific cbuffer (instance/pass) data, uploaded at the gamma-correction draw.
struct LumaGameData
{
   // 1 when UberPostProcessBlend ran this frame (its replacement left LINEAR HDR in the intermediate), 0 when the
   // engine skipped it and the gamma pass reads the RAW fp16 scene, where it must run the whole HDR block itself.
   float UberRanThisFrame;
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
