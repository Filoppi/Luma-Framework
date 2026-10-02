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

  // taps
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

  // threshold (using max)
  o0.xyz = BloomThreshold(r0.xyz, g_color.xyz); //TODO: this is only used by bloom, right!?!?
    // r0.xyz -= g_color.xyz;
    // o0.xyz = max(0, r0.xyz);

  // luminance (using avg)
  r2.xyz *= 0.25;
  o0.w = dot(r2.xyz, float3(0.349999994,0.449999988,0.200000003)); // ~40% BT709 y

  return;
}