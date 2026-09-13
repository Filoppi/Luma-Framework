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


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

#if 1
  r0.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw;
  r1.xyzw = g_texture.Sample(g_sampler_s, v1.zw).xyzw;
  r0.xyzw = r1.xyzw + r0.xyzw;
  r1.xyzw = g_texture.Sample(g_sampler_s, v2.xy).xyzw;
  r0.xyzw = r1.xyzw + r0.xyzw;
  r1.xyzw = g_texture.Sample(g_sampler_s, v2.zw).xyzw;
  r0.xyzw = r1.xyzw + r0.xyzw;
  o0.xyzw = g_color.xyzw * 0.25 * r0.xyzw;
#elif 0
  o0 = BloomUpsample1(v0.xy, g_texture, g_sampler_s, pow(2,1)) * g_color.xyzw;
#endif

  return;
}