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

float4 bloom_upsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // a - b - c
    // d - e - f
    // g - h - i
    const float3 a = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 1)).rgb;
    const float3 b = tex.SampleLevel(smp, texcoord, 0.0, int2(0, 1)).rgb;
    const float3 c = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 1)).rgb;

    const float3 d = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, 0)).rgb;
    const float3 e = tex.SampleLevel(smp, texcoord, 0.0).rgb;
    const float3 f = tex.SampleLevel(smp, texcoord, 0.0, int2(1, 0)).rgb;

    const float3 g = tex.SampleLevel(smp, texcoord, 0.0, int2(-1, -1)).rgb;
    const float3 h = tex.SampleLevel(smp, texcoord, 0.0, int2(0, -1)).rgb;
    const float3 i = tex.SampleLevel(smp, texcoord, 0.0, int2(1, -1)).rgb;

    // Apply weighted distribution.
    float3 color = e * 0.25;
    color += (b + d + f + h) * 0.125;
    color += (a + c + g + i) * 0.0625;

    return float4(color, 1.0);
}