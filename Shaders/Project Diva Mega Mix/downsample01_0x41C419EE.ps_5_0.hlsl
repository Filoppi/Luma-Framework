// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:07 2025

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

float get_gaussian_weight(float x, float sigma)
{
    return exp(-x * x * rcp(2.0 * sigma * sigma));
}
float4 bloom_downsample_ps(
  float2 pos, float2 texcoord, float2 src_size, float2 inv_src_size, float2 axis, float sigma,
  Texture2D tex, SamplerState smp
)
{
    // Calculate fractional part and texel center.
    const float f = dot(frac(texcoord * src_size - 0.5), axis);
    const float2 tc = texcoord - f * inv_src_size * axis;

    float3 csum = 0.0;
    float wsum = 0.0;

    // Calculate kernel radius.
    const float radius = ceil(sigma * 3.0);

    for (float i = 1.0 - radius; i <= radius; ++i) {
        const float weight = get_gaussian_weight(i - f, sigma);
        csum += tex.SampleLevel(smp, tc + i * inv_src_size * axis, 0.0).rgb * weight;
        wsum += weight;
    }

    // Normalize.
    csum *= rcp(wsum);

    return float4(csum, 1.0);
}

// used by bloom
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // if (true) {
    r0.xyz = g_texture.Sample(g_sampler_s, v1.xy).xyz;
    r1.xyz = g_texture.Sample(g_sampler_s, v1.zw).xyz;
    r2.xyz = r1.xyz + r0.xyz; // avg 
    r0.xyz = max(r1.xyz, r0.xyz); // max
    r1.xyz = g_texture.Sample(g_sampler_s, v2.xy).xyz;
    r2.xyz = r2.xyz + r1.xyz;
    r0.xyz = max(r1.xyz, r0.xyz);
    r1.xyz = g_texture.Sample(g_sampler_s, v2.zw).xyz;
    r2.xyz = r2.xyz + r1.xyz;
    r0.xyz = max(r1.xyz, r0.xyz);

    // threshold
    r0.xyz = -g_color.xyz + r0.xyz; 
    o0.xyz = max(float3(0,0,0), r0.xyz);

    // luminance
    r0.xyz = float3(0.25,0.25,0.25) * r2.xyz;
    o0.w = dot(r0.xyz, float3(0.349999994,0.449999988,0.200000003));
//   } else {
//     uint2 texSize;
//     g_texture.GetDimensions(texSize.x, texSize.y);
//     float2 pixSize = 1.f / texSize;
//     r0.xyz = bloom_downsample_ps(v0.xy, v1.xy, texSize, pixSize, float2(1, 0), 2, g_texture, g_sampler_s).xyz;
//     r0.xyz += bloom_downsample_ps(v0.xy, v1.xy, texSize, pixSize, float2(0, 1), 2, g_texture, g_sampler_s).xyz;
//     r0.xyz /= 2;
// 
//     r0.xyz = -g_color.xyz + r0.xyz; 
//     o0.xyz = max(float3(0,0,0), r0.xyz);
//     o0.w = dot(r0.xyz, float3(0.349999994,0.449999988,0.200000003));
//   }


  return;
}