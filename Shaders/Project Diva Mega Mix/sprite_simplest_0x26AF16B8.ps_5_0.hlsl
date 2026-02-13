// ---- Created with 3Dmigoto v1.3.16 on Mon Sep 01 02:43:29 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw;
  o0 = max(0, o0); //clamp
  return;
}