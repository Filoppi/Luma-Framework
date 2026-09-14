// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:37 2026

cbuffer _cb0 : register(b0)
{
  float4 c[6] : packoffset(c0);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
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
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = Texture0.Sample(TexS1_s, v3.xy).xyzw;
  r1.x = c[0].w + -r0.w;
  r0.x = dot(r0.xyzw, float4(1,1,1,1));
  r0.x = -0.00100000005 + r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = -0.00100000005 + r1.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = Texture0.Sample(TexS0_s, v3.xy).x;
  o0.w = r0.x;
  o0.xyz = c[1].xxx * c[0].xyz;

  o0.xyz = UIScaling(o0.xyz);
  return;
}