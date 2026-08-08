#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// Expose HLSL-compatible types to C++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors the C++ namespace.
namespace CB
{
// C++/HLSL ABI for user controls and per-draw video classification. Preserve field order and alignment.
struct LumaGameSettings
{
   float Exposure;           // 1 = vanilla. Scene exposure multiplier, scene-referred / pre-grade.
   float Saturation;         // 1 = vanilla. Luminance-based saturation multiplier on the final HDR color.
   float HighlightDechroma;  // 0 = off (only the mandatory DICE/gamut desat applies); higher = bright sources fade to white sooner.
   float Contrast;           // 1 = vanilla. Overall image contrast on the final HDR color.
   float VignetteIntensity;  // 1 = vanilla. Scales the game's vignette darkening (0 = no vignette).
   float FilmGrainIntensity; // 1 = vanilla. Scales the game's film grain (0 = off).
   float BloomIntensity;     // 1 = vanilla-matched. Scales the Luma fp16 pyramidal bloom (0 = no bloom).
   float BloomThreshold;     // = native bright-pass cb0.y (per-scene artist dial), captured live; 1.2 = ME1 vanilla default until first readback.
   float Dithering;          // 0/1 toggle. Animated triangular output dither in HDR and SDR.
   float VideoAutoHDREnable; // 0/1 toggle. 1 = expand Bink movie highlights into HDR, 0 = vanilla SDR videos (no expansion).
   float VideoAutoHDRBoost;  // 0..1. Bink highlight range relative to UI white: 0 = 1x/no-op, 1 = up to 3.125x. Default 0.5.
   float VideoOnSwapchain;   // Set by C++ per draw: 1 = Bink writes Game-relative linear scRGB, 0 = intermediate gamma buffer.
};

// Game-specific per-pass cbuffer data.
struct LumaGameData
{
   // 1 when stage 2 has not decoded the final swapchain into Game-relative linear scRGB this frame.
   float SwapchainGammaEncoded;
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
