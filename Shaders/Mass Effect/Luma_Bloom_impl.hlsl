// Mass Effect (2007) - Luma HDR bloom pyramid (core DrawBloom). Input = the grade's scene texture (PS t0):
// fp16, LINEAR, pre-glow, no gamma decode. Threshold 1.0 = the game's bright-pass; knee = half.

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaSettings) before the shared includes
// clang-format on

// Read as macros so a runtime cbuffer value works (BioShock). main.cpp re-binds b13 before DrawBloom, which owns b11.
#define LUMA_BLOOM_THRESHOLD max(0.0, LumaSettings.GameSettings.BloomThreshold)
#define LUMA_BLOOM_SOFT_KNEE (LUMA_BLOOM_THRESHOLD * 0.5)

// Vanilla's SHAPE, not the shared quadratic: the gather keeps the WHOLE sample past the threshold, quadratic_threshold
// only the EXCESS - measured 6x too dim at 1.2 and 2x at 2.0. Knee on the WEIGHT, ONE-SIDED, starting AT the threshold.
float3 me1_bloom_threshold(float3 color)
{
   const float br = max(color.r, max(color.g, color.b));
   const float k = max(1e-6, LUMA_BLOOM_SOFT_KNEE);
   const float t = LUMA_BLOOM_THRESHOLD;
   return color * saturate((br - t) * rcp(k));
}
#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) me1_bloom_threshold(color)

#include "../Includes/Bloom.hlsl"
