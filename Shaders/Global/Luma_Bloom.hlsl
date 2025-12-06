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

#ifndef BLOOM_THRESHOLD
#define BLOOM_THRESHOLD 1.0
#endif

#ifndef BLOOM_SOFT_KNEE
#define BLOOM_SOFT_KNEE 0.5
#endif

#ifndef BLOOM_RADIUS
#define BLOOM_RADIUS 1.0
#endif

//



// Fullscreen triangle VS.
void bloom_main_vs(uint vid : SV_VertexID, out float4 pos : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((vid << 1) & 2, vid & 2);
    pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

// Prefilter (downsample) PS.
//

float karis_average(float3 color)
{
    const float luma = dot(color, Rec709_Luminance);
    return 1.0 * rcp(1.0 + luma);
}

float3 quadratic_threshold(float3 color)
{
    // Pixel brightness.
    const float br = max(max(color.r, color.g), color.b);

    // Under-threshold part: quadratic curve.
    const float3 curve = float3(BLOOM_THRESHOLD - BLOOM_SOFT_KNEE, BLOOM_SOFT_KNEE * 2.0, 0.25 / BLOOM_SOFT_KNEE);
    float rq = clamp(br - curve.x, 0.0, curve.y);
    rq = curve.z * rq * rq;

    // Combine and apply the brightness response curve.
    const float epsilon = 1e-6;
    return br >= epsilon ? color * max(rq, br - BLOOM_THRESHOLD) * rcp(br) : 0.0;
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

    // Partial Karis average.
    // Apply the Karis average in blocks of 4 samples.
    // And additionaly apply weighted distribution.
    float3 groups[5];
    groups[0] = (d + e + i + j);
    groups[1] = (a + b + g + f);
    groups[2] = (b + c + h + g);
    groups[3] = (f + g + l + k);
    groups[4] = (g + h + m + l);
    float weights[5];
    weights[0] = karis_average(groups[0]);
    weights[1] = karis_average(groups[1]);
    weights[2] = karis_average(groups[2]);
    weights[3] = karis_average(groups[3]);
    weights[4] = karis_average(groups[4]);
    float3 color = (groups[0] * weights[0] * 0.125 + groups[1] * weights[1] * 0.03125 + groups[2] * weights[2] * 0.03125 + groups[3] * weights[3] * 0.03125 + groups[4] * weights[4] * 0.03125) * rcp(weights[0] + weights[1] + weights[2] + weights[3] + weights[4]);

    color = quadratic_threshold(color);

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

    // Cant use float in preprocessor.
    // This should get optimized out.
    if (BLOOM_RADIUS != 1.0)
    {
        float x, y;
        tex.GetDimensions(x, y);
        const float2 texel_size = 1.0 / float2(x, y);

        a = tex.SampleLevel(smp, texcoord + float2(-1.0, 1.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;
        b = tex.SampleLevel(smp, texcoord + float2(0.0, 1.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;
        c = tex.SampleLevel(smp, texcoord + float2(1.0, 1.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;

        d = tex.SampleLevel(smp, texcoord + float2(-1.0, 0.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;
        e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
        f = tex.SampleLevel(smp, texcoord + float2(1.0, 0.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;

        g = tex.SampleLevel(smp, texcoord + float2(-1.0, -1.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;
        h = tex.SampleLevel(smp, texcoord + float2(0.0, -1.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;
        i = tex.SampleLevel(smp, texcoord + float2(1.0, -1.0) * BLOOM_RADIUS * texel_size, 0.0).rgb;

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