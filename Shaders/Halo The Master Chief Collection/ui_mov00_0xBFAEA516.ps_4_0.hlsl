// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 12:24:25 2026
Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}




// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t1.Sample(s0_s, v1.zw).xyzw;
  r0.xyzw = cb0[1].xyzw * r0.xxxx;
  r1.xyzw = t0.Sample(s0_s, v1.xy).xyzw;
  r0.xyzw = r1.xxxx * cb0[4].xyzw + r0.xyzw;
  r1.xyzw = t2.Sample(s0_s, v1.zw).xyzw;
  r0.xyzw = r1.xxxx * cb0[2].xyzw + r0.xyzw;
  r0.xyzw = cb0[3].xyzw + r0.xyzw;
  o0.xyzw = cb0[0].xyzw * r0.xyzw;
  o0.w = saturate(o0.w);
  o0.xyz = RenderIntermediatePass(o0.xyz);
  return;
}