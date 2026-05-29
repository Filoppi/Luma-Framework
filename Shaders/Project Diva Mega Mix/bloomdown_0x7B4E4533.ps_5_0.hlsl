// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:08 2025

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

cbuffer GaussianCoef : register(b1)
{
  float4 g_coef[8] : packoffset(c0);
}

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = g_texel_size.xy * g_color.xy + v1.xy;
  r1.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r1.xyz = max(r1.xyz, 0);
  r0.xy = g_texel_size.xy * g_color.zw + r0.xy;
  r1.xyz = g_coef[1].xyz * r1.xyz;
  r2.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw; r2 = max(r2, 0);
  r1.xyz = r2.xyz * g_coef[0].xyz + r1.xyz;
  o0.w = r2.w;
  r0.zw = g_texel_size.xy * -g_color.xy + v1.xy;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.zw).xyz; r2.xyz = max(r2.xyz, 0);
  r0.zw = -g_texel_size.xy * g_color.zw + r0.zw;
  r1.xyz = r2.xyz * g_coef[1].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r2.xyz = max(r2.xyz, 0);
  r0.xy = g_texel_size.xy * g_color.zw + r0.xy;
  r1.xyz = r2.xyz * g_coef[2].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.zw).xyz; r2.xyz = max(r2.xyz, 0);
  r0.zw = -g_texel_size.xy * g_color.zw + r0.zw;
  r1.xyz = r2.xyz * g_coef[2].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r2.xyz = max(r2.xyz, 0);
  r0.xy = g_texel_size.xy * g_color.zw + r0.xy;
  r1.xyz = r2.xyz * g_coef[3].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.zw).xyz; r2.xyz = max(r2.xyz, 0);
  r0.zw = -g_texel_size.xy * g_color.zw + r0.zw;
  r1.xyz = r2.xyz * g_coef[3].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r2.xyz = max(r2.xyz, 0);
  r0.xy = g_texel_size.xy * g_color.zw + r0.xy;
  r1.xyz = r2.xyz * g_coef[4].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.zw).xyz; r2.xyz = max(r2.xyz, 0);
  r0.zw = -g_texel_size.xy * g_color.zw + r0.zw;
  r1.xyz = r2.xyz * g_coef[4].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r2.xyz = max(r2.xyz, 0);
  r0.xy = g_texel_size.xy * g_color.zw + r0.xy;
  r3.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r3.xyz = max(r3.xyz, 0);
  r1.xyz = r2.xyz * g_coef[5].xyz + r1.xyz;
  r2.xyz = g_texture.Sample(g_sampler_s, r0.zw).xyz; r2.xyz = max(r2.xyz, 0);
  r0.xy = -g_texel_size.xy * g_color.zw + r0.zw;
  r0.xyz = g_texture.Sample(g_sampler_s, r0.xy).xyz; r0.xyz = max(r0.xyz, 0);
  r1.xyz = r2.xyz * g_coef[5].xyz + r1.xyz;
  r1.xyz = r3.xyz * g_coef[6].xyz + r1.xyz;
  o0.xyz = r0.xyz * g_coef[6].xyz + r1.xyz;
  return;
}