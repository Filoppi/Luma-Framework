// FFXV Luma bloom implementation.
// The game's _Globals cbuffer (b0) is rebound at bloom time so we can read the same
// highpass_gamma and highpass_threshold values the vanilla highpass shader used,
// producing identical highlight extraction to the original.

// Declare only the fields we need; packoffset lets us skip unrelated registers.
cbuffer _Globals : register(b0)
{
    float3 highpass_gamma     : packoffset(c13);   // per-channel power curve
    float3 highpass_threshold : packoffset(c15);   // .x = minimum luma to bloom
}

float3 ffxv_threshold(float3 color)
{
    // Replicate Bloom_Highpass_0xFF665135 exactly:
    //   1. 4-tap box filter is handled by bloom_prefilter_ps before calling this.
    //   2. Luma-ratio mask removes darks while preserving hue.
    //   3. Per-channel gamma sharpens the highlight cutoff.
    float luma = dot(color, float3(0.299, 0.587, 0.114));
    luma = max(1e-6, luma);
    float mask = max(1e-6, luma - highpass_threshold.x) / luma;
    return exp2(highpass_gamma * log2(max(1e-6, color * mask)));
}

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) ffxv_threshold(color)

#include "../Includes/Bloom.hlsl"

