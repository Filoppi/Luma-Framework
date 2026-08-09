cbuffer cb13 : register(b13)
{
    float2 bloom_src_size;
    float2 bloom_inv_src_size;
    float2 bloom_axis;
    float bloom_sigma;
    uint frame_index;
    float2 inv_renderer_resolution;
}

SamplerState smp : register(s0); // Should be linear clamp.
Texture2D tex : register(t0);

float get_gaussian_weight(float x)
{
	return exp(-x * x * rcp(2.0 * bloom_sigma * bloom_sigma));
}

// Samples one axis at a time.
float4 downsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
	// Calculate fractional part and texel center.
	const float f = dot(frac(texcoord * bloom_src_size - 0.5), bloom_axis);
	const float2 tc = texcoord - f * bloom_inv_src_size * bloom_axis;

	float3 csum = 0.0;
	float wsum = 0.0;

	// Calculate kernel radius.
	const float radius = ceil(bloom_sigma * 3.0);

	for (float i = 1.0 - radius; i <= radius; ++i) {
		const float weight = get_gaussian_weight(i - f);
		csum += tex.SampleLevel(smp, tc + i * bloom_inv_src_size * bloom_axis, 0.0).rgb * weight;
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
float4 upsample_ps(float4 pos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
	// transform the coordinate from [0,extent] to [-0.5, extent-0.5]
	float2 coord_grid = texcoord * bloom_src_size - 0.5;
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
	float2 h0 = ((w1 / g0) - 0.5 + index) * bloom_inv_src_size;
	float2 h1 = ((w3 / g1) + 1.5 + index) * bloom_inv_src_size;

	// fetch the four linear interpolations
	float3 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y), 0.0).rgb;
	float3 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y), 0.0).rgb;
	float3 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y), 0.0).rgb;
	float3 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y), 0.0).rgb;

	// weigh along the y-direction
	tex00 = lerp(tex01, tex00, g0.y);
	tex10 = lerp(tex11, tex10, g0.y);

	// weigh along the x-direction
	return float4(lerp(tex10, tex00, g0.x), 1.0);
}
