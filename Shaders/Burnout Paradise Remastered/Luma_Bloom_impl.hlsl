#include "../Includes/Color.hlsl"

cbuffer _Globals : register(b0)
{
    float3 kDotWithWhiteLevel : packoffset(c0);
    float3 kThresholdAndScale : packoffset(c1);
}

float3 threshold(float3 color)
{
    color = linear_to_sRGB_gamma(color);
    float w = dot(color, kDotWithWhiteLevel);
    w -= kThresholdAndScale.x;
    return gamma_sRGB_to_linear(max(0.0, color * w));
}

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) threshold(color)
#include "../Includes/Bloom.hlsl"