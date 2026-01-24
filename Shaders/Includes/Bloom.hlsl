// Bloom
//

#include "Color.hlsl"

cbuffer LumaBloom : register(b11)
{
    float2 src_size;
    float2 inv_src_size;
    float2 axis;
    float sigma;
    float tex_noise_index;
}

SamplerState smp : register(s0);
Texture2D tex : register(t0);

// User configurable
//

#ifndef LUMA_BLOOM_THRESHOLD
#define LUMA_BLOOM_THRESHOLD 1.0
#endif

#ifndef LUMA_BLOOM_SOFT_KNEE
#define LUMA_BLOOM_SOFT_KNEE 1.0
#endif

#ifndef LUMA_BLOOM_TINT
#define LUMA_BLOOM_TINT float3(1.0, 1.0, 1.0)
#endif

//

// Fullscreen triangle VS.
void bloom_main_vs(uint vid : SV_VertexID, out float4 pos : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((vid << 1) & 2, vid & 2);
    pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float3 quadratic_threshold(float3 color)
{
    const float epsilon = 1e-6;

    // Pixel brightness.
    float br = max(max(color.r, color.g), color.b);
    br = max(epsilon, br);

    // Under the threshold part, a quadratic curve.
    // Above the threshold part will be a linear curve.
    const float k = max(epsilon, LUMA_BLOOM_SOFT_KNEE);
    const float3 curve = float3(LUMA_BLOOM_THRESHOLD - k, k * 2.0, 0.25 / k);
    float rq = clamp(br - curve.x, 0.0, curve.y);
    rq = curve.z * rq * rq;

    // Combine and apply the brightness response curve.
    return color * max(rq, br - LUMA_BLOOM_THRESHOLD) * rcp(br);
}

float get_gaussian_weight(float x)
{
    return exp(-x * x * rcp(2.0 * sigma * sigma));
}

float4 bloom_prefilter_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // Calculate fractional part and texel center.
    const float f = dot(frac(texcoord * src_size - 0.5), axis);
    const float2 tc = texcoord - f * inv_src_size * axis;

    float3 csum = 0.0;
    float wsum = 0.0;

    // Calculate kernel radius.
    const float radius = ceil(sigma * 3.0);

    for (float i = 1.0 - radius; i <= radius; ++i) {
        const float weight = get_gaussian_weight(i - f);
        csum += tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0).rgb * weight;
        wsum += weight;
    }

    // Normalize.
    csum *= rcp(wsum);

    // Apply threshold.
    float3 color = quadratic_threshold(csum);

    // Apply tint.
    const float luma = GetLuminance(color);
    color *= LUMA_BLOOM_TINT;
    color *= luma * rcp(max(1e-6, GetLuminance(color)));

    return float4(color, 1.0);
}

float4 bloom_downsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // Calculate fractional part and texel center.
    const float f = dot(frac(texcoord * src_size - 0.5), axis);
    const float2 tc = texcoord - f * inv_src_size * axis;

    float3 csum = 0.0;
    float wsum = 0.0;

    // Calculate kernel radius.
    const float radius = ceil(sigma * 3.0);

    for (float i = 1.0 - radius; i <= radius; ++i) {
        const float weight = get_gaussian_weight(i - f);
        csum += tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0).rgb * weight;
        wsum += weight;
    }

    // Normalize.
    csum *= rcp(wsum);

    return float4(csum, 1.0);
}

// Bicubic upsampling in 4 texture fetches.
//
// f(x) = (4 + 3 * |x|^3 – 6 * |x|^2) / 6 for 0 <= |x| <= 1
// f(x) = (2 – |x|)^3 / 6 for 1 < |x| <= 2
// f(x) = 0 otherwise
//
// Source: https://www.researchgate.net/publication/220494113_Efficient_GPU-Based_Texture_Interpolation_using_Uniform_B-Splines
float4 bloom_upsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // transform the coordinate from [0,extent] to [-0.5, extent-0.5]
    float2 coord_grid = texcoord * src_size - 0.5;
    float2 index = floor(coord_grid);
    float2 fraction = coord_grid - index;
    float2 one_frac = 1.0 - fraction;
    float2 one_frac2 = one_frac * one_frac;
    float2 fraction2 = fraction * fraction;
    float2 w0 = 1.0 / 6.0 * one_frac2 * one_frac;
    float2 w1 = 2.0 / 3.0 - 0.5 * fraction2 * (2.0 - fraction);
    float2 w2 = 2.0 / 3.0 - 0.5 * one_frac2 * (2.0 - one_frac);
    float2 w3 = 1.0 / 6.0 * fraction2 * fraction;
    float2 g0 = w0 + w1;
    float2 g1 = w2 + w3;

    // h0 = w1/g0 - 1, move from [-0.5, extent-0.5] to [0, extent]
    float2 h0 = (w1 / g0) - 0.5 + index;
    float2 h1 = (w3 / g1) + 1.5 + index;

    // fetch the four linear interpolations
    float3 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * inv_src_size, 0.0).rgb;
    float3 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * inv_src_size, 0.0).rgb;
    float3 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * inv_src_size, 0.0).rgb;
    float3 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * inv_src_size, 0.0).rgb;

    // weigh along the y-direction
    tex00 = lerp(tex01, tex00, g0.y);
    tex10 = lerp(tex11, tex10, g0.y);

    // weigh along the x-direction
    return float4(lerp(tex10, tex00, g0.x), 1.0);
}