// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:48 2026
Texture2DArray<float4> t1 : register(t1);

Texture2DArray<float4> t0 : register(t0);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[53];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[4];
}

#if FIRE_RETUNED == 0
LET_THIS_BREAK
#endif


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"

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

  r0.z = cb3[1].w * v4.x;
  r0.xy = v1.xy;
  r0.xyz = t0.Sample(s0_s, r0.xyz).xyz;
  r0.xy = cb3[1].xy * r0.xy;
  r0.x = r0.x + r0.y;
  r0.x = cb3[1].z * r0.z + r0.x;
  r0.y = -cb3[2].x + r0.x;
  r0.x = v4.w * r0.x;
  r0.z = cb3[2].y + -cb3[2].x;
  r0.y = saturate(r0.y / r0.z);
  r0.x = r0.x * r0.y;
  r1.x = r0.x * cb3[2].z + cb3[0].w;
  r0.x = log2(r0.x);
  r0.x = cb3[3].y * r0.x;
  r0.x = exp2(r0.x);
  r0.y = cmp(cb3[2].w < 0.5);
  r1.z = r0.y ? cb3[3].x : 2;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK);
  r0.yzw = t1.SampleLevel(s1_s, r1.xyz, 0).xyz;
    r0.yzw *= FIRE_BOOST;
  r0.yzw = cb3[0].xyz * r0.yzw;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = !cb3[3].z ? cb2[24].w : 1;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = cb3[3].www * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[3].zzz ? r1.xyz : r0.xyz;
  o0.xyz = v2.www * r0.xyz;
  o0.w = 0;
  return;
}