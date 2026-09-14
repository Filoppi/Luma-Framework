cbuffer GFD_PSCONST_CORRECT : register(b12)
{
  float3 colorBalance : packoffset(c0);
  float _reserve00 : packoffset(c0.w);
  float2 colorBlend : packoffset(c1);
}

cbuffer GFD_PSCONST_HDR : register(b11)
{
  float middleGray : packoffset(c0);
  float adaptedLum : packoffset(c0.y);
  float bloomScale : packoffset(c0.z);
  float starScale : packoffset(c0.w);
}

SamplerState opaueSampler_s : register(s0);
SamplerState bloomSampler_s : register(s1);
SamplerState brightSampler_s : register(s2);
Texture2D<float4> opaueTexture : register(t0);
Texture2D<float4> bloomTexture : register(t1);
Texture2D<float4> brightTexture : register(t2);

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

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = sample_bicubic(bloomTexture, bloomSampler_s, v1.xy).xyz;
  r0.xyz = bloomScale * r0.xyz;
  r1.xyz = brightTexture.Sample(brightSampler_s, v1.xy).xyz;
  r1.xyz = bloomScale * r1.xyz;
  r2.xyz = opaueTexture.Sample(opaueSampler_s, v1.xy).xyz;
  r2.xyz = r2.xyz;
  r3.xyz = r1.xyz + r0.xyz;
  r0.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r0.xyz;
  r0.xyz = r3.xyz + r0.xyz;
  r1.xyz = -r1.xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r3.xyz = int3(0,0,0);
  r1.xyz = max(r3.xyz, r1.xyz);
  r3.xyz = r1.xyz + r0.xyz;
  r0.xyz = r1.xyz * r0.xyz;
  r0.xyz = -r0.xyz;
  r0.xyz = r3.xyz + r0.xyz;
  r0.xyz = max(r2.xyz, r0.xyz);
  r1.w = 1;
  r0.w = max(r0.x, r0.y);
  r0.w = max(r0.w, r0.z);
  r2.x = -r0.w;
  r2.x = 1 + r2.x;
  r0.xyz = -r0.xyz;
  r0.xyz = float3(1,1,1) + r0.xyz;
  r2.yzw = -r2.xxx;
  r0.xyz = r2.yzw + r0.xyz;
  r0.xyz = r0.xyz / r0.www;
  r0.xyz = colorBalance.xyz + r0.xyz;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = r0.xyz + r2.xxx;
  r0.xyz = -r0.xyz;
  r0.xyz = float3(1,1,1) + r0.xyz;
  r0.xyz = r0.xyz / colorBlend.xxx;
  r0.xyz = -r0.xyz;
  r0.xyz = float3(1,1,1) + r0.xyz;
  r0.xyz = r0.xyz / colorBlend.yyy;
  r0.xyz = -r0.xyz;
  r1.xyz = float3(1,1,1) + r0.xyz;
  r1.xyz = r1.xyz;
  r1.w = r1.w;
  o0.xyzw = r1.xyzw;
  return;
}