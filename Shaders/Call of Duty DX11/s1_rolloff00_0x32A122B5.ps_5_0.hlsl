// ---- Created with 3Dmigoto v1.3.16 on Mon Jun 22 17:26:41 2026
Texture2D<float4> t4 : register(t4);

Texture2D<float4> t2 : register(t2);

SamplerState s4_s : register(s4);

SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[4];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[30];
}

// 3Dmigoto declarations
#define cmp -
#include "common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Color + Bloom
  r0.xy = max(cb2[29].xy, v1.xy);
  r0.xy = min(cb2[29].zw, r0.xy);
  r0.xyz = t2.Sample(s0_s, r0.xy).xyz;

  r0.xyz = cb2[27].x * GS.Bloom * r0.xyz;
  r1.xy = max(cb2[28].xy, v1.xy);
  r1.xy = min(cb2[28].zw, r1.xy);
  r1.xyz = t4.Sample(s4_s, r1.xy).xyz;
  r0.xyz = cb2[27].yyy * r1.xyz + r0.xyz;

  // Rolloff
  RolloffResult r = Rolloff_Complete(r0.xyz, cb4[0].x, cb4[1], cb4[2]);
  r0.xyz = r.color;
  o0.w = r.y;

  // Gamma thing
  r0.xyz = pow(r0.xyz, cb4[3].zzz);

  o0.xyz = r0.xyz;
  return;
}