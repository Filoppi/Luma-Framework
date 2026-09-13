// ---- Created with 3Dmigoto v1.3.16 on Wed Jun 24 23:02:03 2026
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[8];
}




// 3Dmigoto declarations
#define cmp -
#include "common.hlsl"


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : TEXCOORD0,
  nointerpolation float v3 : TEXCOORD14,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = t0.SampleBias(s0_s, v2.xy, cb2[7].y).xyzw;
  r0.xyzw = v1.xyzw * r0.xyzw;
  r0.xyz = r0.xyz * r0.xyz;
  // float3 r0Before = r0.xyz;
  r0.xyz = min(float3(0.99000001,0.99000001,0.99000001), r0.xyz);
  // r0.xyz = Neutwo(r0.xyz, 0.99000001, 1.225);
  r0.xyz = float3(1,1,1) + -r0.xyz;
  r0.xyz = log2(r0.xyz);
  r0.xyz = v3.xxx * -r0.xyz;
  r0.xyz = cb2[0].xxx * r0.xyz;
  r1.x = max(r0.y, r0.z);
  r1.x = max(r1.x, r0.x);
  r1.x = cmp(6.09999988e-005 >= r1.x);
  if (r1.x != 0) discard;

  // r0.xyz = NeutwoI(r0.xyz, 0.99000001, 1.225);
  r0.xyz = r0.xyz * r0.www * 2;
  o0.xyz = r0.xyz;
  // o0.xyz = min(float3(32255,32255,32255), r0.xyz);
  o0.w = 0;
  o1.xyzw = float4(0,0,0,0);
  return;
}