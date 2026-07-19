// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:36 2026
Texture2D<float4> t1 : register(t1);

Texture2DArray<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[53];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[3];
}




// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"
#if FIRE_RETUNED == 0
LET_THIS_BREAK
#endif

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD1,
  float4 v4 : COLOR0,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.z = cb3[0].x * v4.x;
  r0.xy = v1.xy;
  r0.xyz = t0.Sample(s0_s, r0.xyz).xyz;
  r0.x = dot(r0.xyz, cb3[0].yzw);
  r0.z = -cb3[1].y + r0.x;
  r0.w = cb3[1].z + -cb3[1].y;
  r0.z = saturate(r0.z / r0.w);
  r0.z = v4.w * r0.z;
  r0.y = cb3[1].x;
  r0.xyw = t1.Sample(s1_s, r0.xy).xyz;
    // r0.xyw = FireTonemap(r0.xyw, FIRE_PEAK);
    // r0.xyz *= FIRE_BOOST;

  r0.xyz = r0.xyw * r0.zzz;
  r0.w = !cb3[1].w ? cb2[24].w : 1;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = cb3[2].xxx * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[1].www ? r1.xyz : r0.xyz;
  o0.xyz = v2.www * r0.xyz;
  o0.w = 0;
  return;
}