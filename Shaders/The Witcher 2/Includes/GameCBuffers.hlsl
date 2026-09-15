#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
// User grade settings, mirrored to c++ ("OnInit" defaults / ImGui in main.cpp) and read through
// LumaSettings.GameSettings: Exposure in the tonemap replacement (Luma_TW2_Tonemap.hlsl), the video knobs in
// Video_0x30BE6D87, everything else in the final grade. Defaults are vanilla no-ops, apart from Dithering and Video AutoHDR.
struct LumaGameSettings
{
   float Exposure;              // scene multiplier before the game grade (1 = vanilla). SDR + HDR
   float Saturation;            // saturation around luminance, after the display map (1 = vanilla). HDR display path only
   float HighlightDechroma;     // 0 = off. DICE highlight desaturation: sources above a third of peak fade toward white, mid-tones untouched. HDR display path only
   float Dithering;             // 1 = animated triangular output dither (anti-banding). SDR + HDR
   float VideoAutoHDREnable;    // 0/1. 1 = light PumboAutoHDR on pre-rendered videos (HDR only); 0 = flat SDR at paper white
   float VideoAutoHDRBoost;     // 0..1. Highlight-expansion strength; peak = lerp(sRGB white, 250 nits, boost). 0 = off
   float VignetteIntensity;     // 1 = vanilla vignette darkening, 0 = none. Applies in SDR and HDR (the vignette lives in the vanilla grade tail)
   float Contrast;              // 1 = vanilla. Multiplicative contrast around 18% mid-gray, applied before the display map. HDR display path only
   float BloomIntensity;        // 1 = vanilla, 0 = none. Scales the engine's glow where it enters the screen blend (SDR + HDR)
   float ColorGradingIntensity; // 1 = vanilla, 0 = no split toning. Fades the vanilla shadow/highlight split toning out of the grade (SDR + HDR)
};

// Define the game specific cbuffer (instance/pass) data here
struct LumaGameData
{
   float Dummy; // hlsl doesn't support empty structs
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
