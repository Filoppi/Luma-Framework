// Borderlands 2 / The Pre-Sequel — Luma HDR pyramidal bloom: the Bloom * entry points DrawBloom calls, core
// auto-registers them at ENABLE_BLOOM=1. Replaces the game's quarter-res UNORM-clamped glow with a multi-mip fp16
// one off the linear HDR scene. Only the WEIGHT comes from the game; magnitude belongs to the tonemap's composite.
//
// The knee mirrors the native bright pass (0x997ACB8E under dgVoodoo 2.87.3, 0x5605F6C2 under 2.81.3):
// saturate((max3(source * BloomScale) - BloomThreshold) * 0.5), BloomScale a constant 4 that only undoes the pass's
// pre-divided-by-4 source - applying it here as well opened the knee 4x too low and put ~10x the native energy in.
// C++ keeps GameSettings.BloomThreshold live off that pass (measured 0.50 to 1.32) and binds LumaSettings here.
// Karis firefly weighting runs before this (DrawKarisAverage); no TAA to hide sparkle.

// clang-format off
#include "Includes/Common.hlsl"   // game-local: LumaGameSettings - keep FIRST (see the tonemap's include note)
#include "../Includes/Color.hlsl" // GetLuminance, gamma helpers used by Bloom.hlsl
// clang-format on

float3 bl2_bloom_threshold(float3 color)
{
   // Non-finite or negative taps would poison a whole Gaussian kernel and read as a hue shift downstream.
   color = (IsAnyNaN_Strict(color) || any(isinf(color))) ? 0.0 : max(color, 0.0);
   const float mch = max3(color);
   // Native shape: the WHOLE sample is kept once it passes, over a ramp of 2.0 above the threshold. Raw scene, no rescale.
   const float weight = saturate((mch - LumaSettings.GameSettings.BloomThreshold) * 0.5);
   return color * weight;
}

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) bl2_bloom_threshold(color)

#include "../Includes/Bloom.hlsl"
