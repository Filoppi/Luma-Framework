// Trilogy-wide fp16 pyramidal bloom: native bright-pass selection over the shared fp16 Gaussian pyramid. Stage 1
// samples the result through the native bloom slot and keeps the game's tint, luma-gated screen blend, and user
// intensity. Bright pass 0xF8942FF1 supplies live BloomScale and Threshold in cb0.xy; C++ folds scale into the
// effective intensity and the prefilter reads threshold from GameSettings.BloomThreshold. Tiny highlights end up
// slightly dimmer than native because Luma thresholds before the blur rather than after it.

// DrawBloom binds only b11; main.cpp explicitly preserves live LumaSettings in b13 for the prefilter.
#include "Includes/Common.hlsl"

// Native max-channel soft knee; the tonemap applies BloomTint downstream.
float3 me1_bloom_threshold(float3 color)
{
   float w = saturate((max(color.r, max(color.g, color.b)) - LumaSettings.GameSettings.BloomThreshold) * 0.5);
   return color * w;
}

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) me1_bloom_threshold(color)
#define LUMA_BLOOM_SCALE                     0.25                  // 4 bilinear taps * 0.0625 = 0.25 of their mean.
#define LUMA_BLOOM_TINT                      float3(1.0, 1.0, 1.0) // Tonemap applies BloomTint downstream.

#include "../Includes/Bloom.hlsl"
