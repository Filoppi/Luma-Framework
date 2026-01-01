// Bloom
//
// Based on:
// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
// https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

#include "../Includes/Color.hlsl"

SamplerState smp : register(s0);
Texture2D tex : register(t0);

// User configurable
//

#ifndef LUMA_BLOOM_THRESHOLD
#define LUMA_BLOOM_THRESHOLD 1.0
#endif

#ifndef LUMA_BLOOM_SOFT_KNEE
#define LUMA_BLOOM_SOFT_KNEE 0.5
#endif

#ifndef LUMA_BLOOM_RADIUS
#define LUMA_BLOOM_RADIUS 1.0
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

// Prefilter + downsample PS.
//

float get_karis_weight(float3 color)
{
    const float luma = GetLuminance(color);
    return rcp(1.0 + luma);
}

float3 karis_average(float3 a, float3 b, float3 c, float3 d)
{
    float4 sum = float4(a.rgb, 1.0) * get_karis_weight(a);
    sum += float4(b.rgb, 1.0) * get_karis_weight(b);
    sum += float4(c.rgb, 1.0) * get_karis_weight(c);
    sum += float4(d.rgb, 1.0) * get_karis_weight(d);

    return sum.rgb / sum.a;
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

    // Apply partial Karis average in blocks of 4 samples.
    float3 groups[5];
    groups[0] = karis_average(d, e, i, j);
    groups[1] = karis_average(a, b, g, f);
    groups[2] = karis_average(b, c, h, g);
    groups[3] = karis_average(f, g, l, k);
    groups[4] = karis_average(g, h, m, l);

    // Apply weighted distribution.
    float3 color = groups[0] * 0.125 + groups[1] * 0.03125 + groups[2] * 0.03125 + groups[3] * 0.03125 + groups[4] * 0.03125;

    // Apply threshold.
    color = quadratic_threshold(color);

    // Apply tint.
    color = RestoreLuminance(color * LUMA_BLOOM_TINT, color);

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

    float3 a, b, c, d, e, f, g, h, i;

    // Can't use float in preprocessor.
    // This should get optimized out.
    if (LUMA_BLOOM_RADIUS != 1.0)
    {
        float x, y;
        tex.GetDimensions(x, y);
        const float2 texel_size = 1.0 / float2(x, y);

        a = tex.SampleLevel(smp, texcoord + float2(-1.0, 1.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;
        b = tex.SampleLevel(smp, texcoord + float2(0.0, 1.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;
        c = tex.SampleLevel(smp, texcoord + float2(1.0, 1.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;

        d = tex.SampleLevel(smp, texcoord + float2(-1.0, 0.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;
        e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
        f = tex.SampleLevel(smp, texcoord + float2(1.0, 0.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;

        g = tex.SampleLevel(smp, texcoord + float2(-1.0, -1.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;
        h = tex.SampleLevel(smp, texcoord + float2(0.0, -1.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;
        i = tex.SampleLevel(smp, texcoord + float2(1.0, -1.0) * LUMA_BLOOM_RADIUS * texel_size, 0.0).rgb;

    }
    else // Optimized for radius 1.
    {
        a = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
        b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 1)).rgb;
        c = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;

        d = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 0)).rgb;
        e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
        f = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 0)).rgb;

        g = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
        h = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -1)).rgb;
        i = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;
    }

    //

    // Apply weighted distribution.
    float3 color = e * 0.25;
    color += (b + d + f + h) * 0.125;
    color += (a + c + g + i) * 0.0625;

    return float4(color, 1.0);
}