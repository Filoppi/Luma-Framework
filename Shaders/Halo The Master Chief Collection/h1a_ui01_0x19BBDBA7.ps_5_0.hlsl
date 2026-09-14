// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:24 2026

cbuffer _cb0 : register(b0)
{
  float4 c[6] : packoffset(c0);
}

SamplerState TexS0_s : register(s0);
Texture2D<float4> Texture0 : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = Texture0.Sample(TexS0_s, v3.xy).yw;
  r0.y = -c[0].w + r0.y;
  o0.w = c[1].y * r0.x;
  r0.x = cmp(r0.y < 0);
  if (r0.x != 0) discard;
  o0.xyz = c[1].xxx * c[0].xyz;

  o0.xyz = UIScaling(o0.xyz);
  return;
}