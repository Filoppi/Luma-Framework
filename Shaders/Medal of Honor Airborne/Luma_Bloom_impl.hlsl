// Medal of Honor: Airborne - Luma HDR bloom pyramid (core DrawBloom). Input = the grade's scene texture (PS t0):
// fp16, LINEAR, pre-glow, no gamma decode. Threshold 1.0 = the game's bright-pass (scene peaks ~3.9); knee = half.

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaSettings) before the shared includes
// clang-format on

// The threshold function reads these two as macros, so a runtime cbuffer value works (BioShock precedent).
// b13 (LumaSettings) is re-bound by main.cpp right before DrawBloom, which owns b11 for its own constants.
#define LUMA_BLOOM_THRESHOLD max(0.0, LumaSettings.GameSettings.BloomThreshold)
#define LUMA_BLOOM_SOFT_KNEE (LUMA_BLOOM_THRESHOLD * 0.5)

// Vanilla's SHAPE, not the shared quadratic one: the gather keeps the WHOLE sample once a channel passes the
// threshold, while quadratic_threshold keeps only the EXCESS, which measured 6x too dim at brightness 1.2 and 2x at
// 2.0 — the band where nearly every glowing pixel lives when the scene tops out at 3.9. That is why the
// replacement bloom first read as barely visible next to the vanilla halo.
// The knee is applied to the WEIGHT, not the colour: vanilla's hard step was affordable for a dithered quarter-res
// gather and is not affordable here, with no TAA to hide the flicker on a surface drifting across the threshold.
// The ramp is ONE-SIDED, starting AT the threshold: vanilla's bright-pass is a `ge` against a literal, so nothing
// below it contributes and a centred ramp would invent glow across the whole band underneath (MELE/Burnout shape).
float3 moha_bloom_threshold(float3 color)
{
   const float br = max(color.r, max(color.g, color.b));
   const float k = max(1e-6, LUMA_BLOOM_SOFT_KNEE);
   const float t = LUMA_BLOOM_THRESHOLD;
   return color * saturate((br - t) * rcp(k));
}
#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) moha_bloom_threshold(color)

#include "../Includes/Bloom.hlsl"
