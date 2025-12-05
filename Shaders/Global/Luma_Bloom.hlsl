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

float karis_average(float3 color)
{
    const float luma = dot(color, Rec709_Luminance);
    return 1.0 / (1.0 + luma);
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
    return br >= epsilon ? color * max(rq, br - BLOOM_THRESHOLD) / br : 0.0;
}

// Implementation
/////////////////////////////////////////////////////////////////////

// Fullscreen triangle.
void bloom_main_vs(uint vid : SV_VertexID, out float4 pos : SV_Position, out float2 texcoord : TEXCOORD)
{
    texcoord = float2((vid << 1) & 2, vid & 2);
    pos = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 bloom_prefilter_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // Take 13 samples around current texel (e):
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    //

    const float3 a = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 2)).rgb;
    const float3 b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 2)).rgb;
    const float3 c = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 2)).rgb;

    const float3 d = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 0)).rgb;
    const float3 e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
    const float3 f = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 0)).rgb;

    const float3 g = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, -2)).rgb;
    const float3 h = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -2)).rgb;
    const float3 i = tex.SampleLevel(smp, texcoord, 0.0, int2(2, -2)).rgb;

    const float3 j = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
    const float3 k = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;
    const float3 l = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
    const float3 m = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;

    //

    // Partial Karis average.
    // Apply the Karis average in blocks of 4 samples.
    //
    // Is this correct?
    float3 groups[5];
    groups[0] = (a + b + d + e) / 4.0f;
    groups[1] = (b + c + e + f) / 4.0f;
    groups[2] = (d + e + g + h) / 4.0f;
    groups[3] = (e + f + h + i) / 4.0f;
    groups[4] = (j + k + l + m) / 4.0f;
    float weights[5];
    weights[0] = karis_average(groups[0]);
    weights[1] = karis_average(groups[1]);
    weights[2] = karis_average(groups[2]);
    weights[3] = karis_average(groups[3]);
    weights[4] = karis_average(groups[4]);
    float3 color = (groups[0] * weights[0] + groups[1] * weights[1] + groups[2] * weights[2] + groups[3] * weights[3] + groups[4] * weights[4]) * rcp(weights[0] + weights[1] + weights[2] + weights[3] + weights[4]);

    color = quadratic_threshold(color);

    return float4(color, 1.0);
}

float4 bloom_downsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // Take 13 samples around current texel (e):
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    //

    const float3 a = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 2)).rgb;
    const float3 b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 2)).rgb;
    const float3 c = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 2)).rgb;

    const float3 d = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, 0)).rgb;
    const float3 e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
    const float3 f = tex.SampleLevel(smp, texcoord, 0.0, int2(2, 0)).rgb;

    const float3 g = tex.SampleLevel(smp, texcoord, 0.0, int2(-2, -2)).rgb;
    const float3 h = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -2)).rgb;
    const float3 i = tex.SampleLevel(smp, texcoord, 0.0, int2(2, -2)).rgb;

    const float3 j = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
    const float3 k = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;
    const float3 l = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
    const float3 m = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;

    //

    // Apply weighted distribution:
    // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
    // a, b, d, e * 0.125
    // b, c, e, f * 0.125
    // d, e, g, h * 0.125
    // e, f, h, i * 0.125
    // j, k, l, m * 0.5
    // This shows 5 square areas that are being sampled. But some of them overlap,
    // so to have an energy preserving downsample we need to make some adjustments.
    // The weights are the distributed, so that the sum of j, k, l, m (e.g.)
    // contribute 0.5 to the final color output. The code below is written
    // to effectively yield this sum. We get:
    // 0.125 * 5 + 0.03125 * 4 + 0.0625 * 4 = 1
    float3 color = e * 0.125;
    color += (a + c + g + i) * 0.03125;
    color += (b + d + f + h) * 0.0625;
    color += (j + k + l + m) * 0.125;

    return float4(color, 1.0);
}

float4 bloom_upsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // Take 9 samples around current texel (e):
    // a - b - c
    // d - e - f
    // g - h - i
    //
    // Additionaly apply the bloom radius.
    //

    float3 a;
    float3 b;
    float3 c;
    float3 d;
    float3 e;
    float3 f;
    float3 g;
    float3 h;
    float3 i;

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

    // Apply weighted distribution, by using a 3x3 tent filter:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    float3 color = e * 0.25;
    color += (b + d + f + h) * 0.125;
    color += (a + c + g + i) * 0.0625;

    return float4(color, 1.0);
}