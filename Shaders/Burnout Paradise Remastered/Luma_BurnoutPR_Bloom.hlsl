// Luma Bloom
//
// Adopted for Burnout Paradise.
// The game renders everything in sRGB color space, clipped.
//
// Based on:
// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
// https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

#include "../Includes/Color.hlsl"

SamplerState smp : register(s0);
Texture2D tex : register(t0);

cbuffer _Globals : register(b0)
{
  float3 kDotWithWhiteLevel : packoffset(c0);
  float3 kThresholdAndScale : packoffset(c1);
}

// Fullscreen triangle VS.
void bloom_main_vs(uint vid : SV_VertexID, out float4 pos : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((vid << 1) & 2, vid & 2);
    pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// Prefilter + downsample PS.
//

float get_karis_weight(float3 color)
{
    const float luma = GetLuminance(gamma_sRGB_to_linear(color));
    return rcp(1.0 + luma);
}

float3 karis_average(float3 a, float3 b, float3 c, float3 d)
{
    float4 sum = float4(a.rgb, 1.0) * get_karis_weight(a);
    sum += float4(b.rgb, 1.0) * get_karis_weight(b);
    sum += float4(c.rgb, 1.0) * get_karis_weight(c);
    sum += float4(d.rgb, 1.0) * get_karis_weight(d);

    return linear_to_sRGB_gamma(sum.rgb / sum.a);
}

float3 threshold(float3 color)
{
    float w = dot(color, kDotWithWhiteLevel);
    w -= kThresholdAndScale.x;
    return max(0.0, color * w);
}

float4 bloom_prefilter_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // a - b - c
    // - d - e -
    // f - g - h
    // - i - j -
    // k - l - m
    //

    const float3 a = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, -2)).rgb;
    const float3 b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -2)).rgb;
    const float3 c = tex.SampleLevel(smp, texcoord, 0.0, int2(2, -2)).rgb;

    const float3 d = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
    const float3 e = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;
    const float3 f = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 0)).rgb;

    const float3 g = tex.SampleLevel(smp, texcoord, 0.0).rgb;

    const float3 h = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 0)).rgb;
    const float3 i = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
    const float3 j = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;

    const float3 k = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 2)).rgb;
    const float3 l = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 2)).rgb;
    const float3 m = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 2)).rgb;

    //

    // Apply Partial Karis average in blocks of 4 samples.
    // Also we apply threshold per group.
    float3 groups[5];
    groups[0] = threshold(karis_average(d, e, i, j));
    groups[1] = threshold(karis_average(a, b, g, f));
    groups[2] = threshold(karis_average(b, c, h, g));
    groups[3] = threshold(karis_average(f, g, l, k));
    groups[4] = threshold(karis_average(g, h, m, l));

    // Apply weighted distribution.
    float3 color = groups[0] * 0.125 + groups[1] * 0.03125 + groups[2] * 0.03125 + groups[3] * 0.03125 + groups[4] * 0.03125;

    return float4(color, 1.0);
}

//

// Downsample PS.
float4 bloom_downsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // a - b - c
    // - d - e -
    // f - g - h
    // - i - j -
    // k - l - m
    //

    const float3 a = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, -2)).rgb;
    const float3 b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -2)).rgb;
    const float3 c = tex.SampleLevel(smp, texcoord, 0.0, int2(2, -2)).rgb;

    const float3 d = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
    const float3 e = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;
    const float3 f = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 0)).rgb;

    const float3 g = tex.SampleLevel(smp, texcoord, 0.0).rgb;

    const float3 h = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 0)).rgb;
    const float3 i = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
    const float3 j = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;

    const float3 k = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 2)).rgb;
    const float3 l = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 2)).rgb;
    const float3 m = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 2)).rgb;

    //

    // Apply weighted distribution.
    float3 color = g * 0.125;
    color += (a + c + k + m) * 0.03125;
    color += (b + f + h + l) * 0.0625;
    color += (d + e + i + j) * 0.125;

    return float4(color, 1.0);
}

// Upsample PS.
float4 bloom_upsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // a - b - c
    // d - e - f
    // g - h - i
    //

    const float3 a = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
    const float3 b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 1)).rgb;
    const float3 c = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;

    const float3 d = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 0)).rgb;
    const float3 e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
    const float3 f = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 0)).rgb;

    const float3 g = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
    const float3 h = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -1)).rgb;
    const float3 i = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;

    //

    // Apply weighted distribution.
    float3 color = e * 0.25;
    color += (b + d + f + h) * 0.125;
    color += (a + c + g + i) * 0.0625;

    return float4(color, 1.0);
}