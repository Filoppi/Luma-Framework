// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:11 2025

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_textures_0_ : register(t0); // 255x144 (1/1)
Texture2D<float4> g_textures_1_ : register(t1); // 128x72  (1/2)
Texture2D<float4> g_textures_2_ : register(t2); // 64x36   (1/4)
Texture2D<float4> g_textures_3_ : register(t3); // 32x18   (1/8)
// out: 255x144


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

// float3 BloomUpsample2(Texture2D tex, SamplerState smp, float2 texcoord, float2 texSize, float2 pixSize) {
//   float2 coord_grid = texcoord * texSize - 0.5;
//   float2 index = floor(coord_grid);
//   float2 fraction = coord_grid - index;
//   float2 one_frac = 1.0 - fraction;
//   float2 one_frac2 = one_frac * one_frac;
//   float2 fraction2 = fraction * fraction;
//   float2 w0 = 1.0 / 6.0 * one_frac2 * one_frac;
//   float2 w1 = 2.0 / 3.0 - 0.5 * fraction2 * (2.0 - fraction);
//   float2 w2 = 2.0 / 3.0 - 0.5 * one_frac2 * (2.0 - one_frac);
//   float2 w3 = 1.0 / 6.0 * fraction2 * fraction;
//   float2 g0 = w0 + w1;
//   float2 g1 = w2 + w3;
// 
//   // h0 = w1/g0 - 1, move from [-0.5, extent-0.5] to [0, extent]
//   float2 h0 = (w1 / g0) - 0.5 + index;
//   float2 h1 = (w3 / g1) + 1.5 + index;
// 
//   // fetch the four linear interpolations
//   float3 tex00 = tex.SampleLevel(smp, float2(h0.x, h0.y) * pixSize, 0.0).xyz;
//   float3 tex10 = tex.SampleLevel(smp, float2(h1.x, h0.y) * pixSize, 0.0).xyz;
//   float3 tex01 = tex.SampleLevel(smp, float2(h0.x, h1.y) * pixSize, 0.0).xyz;
//   float3 tex11 = tex.SampleLevel(smp, float2(h1.x, h1.y) * pixSize, 0.0).xyz;
// 
//   // weigh along the y-direction
//   tex00 = lerp(tex01, tex00, g0.y);
//   tex10 = lerp(tex11, tex10, g0.y);
// 
//   // weigh along the x-direction
//   return lerp(tex10, tex00, g0.x);
// }

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

// #if BLOOM_IMPROVE == 0
    r0.xyz = g_textures_3_.Sample(g_sampler_s, v1.xy).xyz;
    r1.xyz = g_textures_3_.Sample(g_sampler_s, v1.zw).xyz;
    r0.xyz = r1.xyz + r0.xyz;
    r1.xyz = g_textures_3_.Sample(g_sampler_s, v2.xy).xyz;
    r0.xyz = r1.xyz + r0.xyz;
    r1.xyz = g_textures_3_.Sample(g_sampler_s, v2.zw).xyz;
    r0.xyz = r1.xyz + r0.xyz;
    r0.xyz = (g_color.w * GS.BloomStrengths.w) * r0.xyz;
    
    r1.xyzw = g_textures_0_.Sample(g_sampler_s, v3.xy).xyzw;
    r1.xyz = (g_color.x * GS.BloomStrengths.x) * r1.xyz;
    o0.w = r1.w;

    r0.xyz = r0.xyz * float3(0.25,0.25,0.25) + r1.xyz;
    
    r1.xyz = g_textures_1_.Sample(g_sampler_s, v3.xy).xyz;
    r0.xyz = r1.xyz * (g_color.y * GS.BloomStrengths.y) + r0.xyz;
    r1.xyz = g_textures_2_.Sample(g_sampler_s, v3.xy).xyz;
    o0.xyz = r1.xyz * (g_color.z * GS.BloomStrengths.z) + r0.xyz; 
// #else
//     uint2 texSize1; 
//     g_textures_1_.GetDimensions(texSize1.x, texSize1.y);
//     float2 texSize2 = texSize1 * 0.5f;
//     float2 texSize3 = texSize2 * 0.5f;
// 
//     float2 pixSize1 = rcp(texSize1);
//     float2 pixSize2 = rcp(texSize2);
//     float2 pixSize3 = rcp(texSize3);
// 
//     float4 b0 = g_textures_0_.SampleLevel(g_sampler_s, v3.xy, 0).xyzw; 
//     float3 b1 = BloomUpsample2(g_textures_1_, g_sampler_s, v3.xy, texSize1, pixSize1);
//     float3 b2 = BloomUpsample2(g_textures_2_, g_sampler_s, v3.xy, texSize2, pixSize2);
//     float3 b3 = BloomUpsample2(g_textures_3_, g_sampler_s, v3.xy, texSize3, pixSize3);
// 
//     o0.w = b0.w;
//     o0.xyz =  b0.xyz * g_color.x;
//     o0.xyz += b1.xyz * g_color.y;
//     o0.xyz += b2.xyz * g_color.z;
//     o0.xyz += b3.xyz * g_color.w;
// #endif

  o0.xyz *= GS.BloomStrength;

  return;
}