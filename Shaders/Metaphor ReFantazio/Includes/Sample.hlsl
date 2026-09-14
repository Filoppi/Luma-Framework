// From Shaders\Includes\Bloom.hlsl
// Bicubic upsampling in 4 texture fetches.
//
// f(x) = (4 + 3 * |x|^3 – 6 * |x|^2) / 6 for 0 <= |x| <= 1
// f(x) = (2 – |x|)^3 / 6 for 1 < |x| <= 2
// f(x) = 0 otherwise
//
// Source: https://www.researchgate.net/publication/220494113_Efficient_GPU-Based_Texture_Interpolation_using_Uniform_B-Splines
float4 sample_bicubic(Texture2D tex, SamplerState smp, float2 texcoord) {
	uint2 src_size;
	tex.GetDimensions(src_size.x, src_size.y);
	float2 inv_src_size = rcp(src_size);
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
		float4 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * inv_src_size, 0.0);
		float4 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * inv_src_size, 0.0);
		float4 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * inv_src_size, 0.0);
		float4 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * inv_src_size, 0.0);

		// weigh along the y-direction
		tex00 = lerp(tex01, tex00, g0.y);
		tex10 = lerp(tex11, tex10, g0.y);

		// weigh along the x-direction
		return lerp(tex10, tex00, g0.x);
}