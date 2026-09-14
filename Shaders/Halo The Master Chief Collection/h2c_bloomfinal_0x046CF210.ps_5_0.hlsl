// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 12:24:05 2026

cbuffer _Globals : register(b0)
{
  float4 brightness : packoffset(c0);
  float bloom_alpha_flag : packoffset(c1);
}

SamplerState blurtexture_s : register(s0);
Texture2D<float4> blurtexture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  float4 v6 : TEXCOORD3,
  float4 v7 : TEXCOORD4,
  float4 v8 : TEXCOORD5,
  float4 v9 : TEXCOORD6,
  float4 v10 : TEXCOORD7,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = blurtexture.Sample(blurtexture_s, v3.xy).xyzw;
  r0.xyz *= min(1, GS.Bloom);
  r0.xyzw = brightness.xyzw * r0.xyzw;
  r0.xyz = r0.www * bloom_alpha_flag + r0.xyz;
  o0.xyz = r0.xyz + r0.xyz;
  o0.w = 0;
  return;
}