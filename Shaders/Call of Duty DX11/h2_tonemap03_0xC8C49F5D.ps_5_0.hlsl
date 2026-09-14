// ---- Created with 3Dmigoto v1.3.16 on Sat Jun 20 22:45:40 2026
Texture2D<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

SamplerState s4_s : register(s4);

SamplerState s2_s : register(s2);

SamplerState s0_s : register(s0);

cbuffer cb4 : register(b4)
{
  float4 cb4[68];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[38];
}




// 3Dmigoto declarations
#define cmp -
#include "h2_common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  // Bloom & Color
  r0.xy = max(cb2[37].xy, v1.xy);
  r0.xy = min(cb2[37].zw, r0.xy);
  r0.xyz = t2.Sample(s0_s, r0.xy).xyz;
  r0.xyz = cb2[33].xxx * GS.Bloom * r0.xyz;
  
  r1.xyz = t4.Sample(s4_s, v1.xy).xyz;
  r0.xyz = cb2[33].yyy * r1.xyz + r0.xyz;
  
  TM_Color(r0.xyz);

  TM_Rolloff();

  TM_LumaThingy();

  TM_Gamma();

  TM_SaturationAndTint();

  TM_Overlay(w1, t3, s2_s);

  TM_Upgrade();

  // r1.xy = cb2[21].yz + v1.xy;
  // r0.w = dot(r1.xy, float2(353632,4234));
  // r0.w = (uint)r0.w;
  // r1.x = (int)r0.w ^ 61;
  // r0.w = (uint)r0.w >> 16;
  // r0.w = (int)r0.w ^ (int)r1.x;
  // r0.w = (int)r0.w * 7;
  // r1.x = (uint)r0.w >> 4;
  // r0.w = (int)r0.w ^ (int)r1.x;
  // r1.xyz = (int3)r0.www * int3(356,2543,0x424df4);
  // r1.xyz = (int3)r1.xyz & int3(0x3fffc,0x3ffff,0x7fffc);
  // o0.xyz = (int3)r0.xyz + (int3)r1.xyz;

  o0.xyz = tmi.r0;
  return;
}