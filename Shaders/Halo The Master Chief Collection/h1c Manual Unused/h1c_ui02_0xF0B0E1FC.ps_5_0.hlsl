// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:40 2026

SamplerState S0_s : register(s0);
Texture2D<float4> S0Tex : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float2 v3 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = S0Tex.Sample(S0_s, v3.xy).xyzw;
  o0.xyzw = v1.xyzw * r0.xyzw;
  o0.xyz = UIScaling(o0.xyz);
  return;
}