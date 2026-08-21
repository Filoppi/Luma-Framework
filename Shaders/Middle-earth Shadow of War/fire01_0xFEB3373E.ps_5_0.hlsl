// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:52 2026
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
  float4 cb3[6];
}

cbuffer cb1 : register(b1)
{
  float4 cb1[2];
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
  float4 v2 : TEXCOORD1,
  float4 v3 : COLOR0,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float4 v6 : TEXCOORD4,
  float4 v7 : TEXCOORD5,
  float3 v8 : TEXCOORD6,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 0.00333333341 * cb1[1].x;
  r0.y = cmp(r0.x >= -r0.x);
  r0.x = frac(abs(r0.x));
  r0.x = r0.y ? r0.x : -r0.x;
  r0.x = cb3[4].y * r0.x;
  r0.x = 300 * r0.x;
  r0.x = r0.x / cb3[4].z;
  r0.y = cmp(r0.x >= -r0.x);
  r0.x = frac(abs(r0.x));
  r0.x = r0.y ? r0.x : -r0.x;
  r0.x = cb3[4].z * r0.x;
  r0.z = floor(r0.x);
  r0.xy = v1.zw;
  r0.xyzw = t0.Sample(s0_s, r0.xyz).xyzw;
  r0.xy = cb3[2].xy * r0.xy;
  r0.x = r0.x + r0.y;
  r0.x = cb3[2].z * r0.z + r0.x;
  r0.x = cb3[2].w * r0.w + r0.x;
  r0.x = v3.w * r0.x;
  r1.x = r0.x * cb3[4].w + cb3[1].x;
  r0.x = log2(r0.x);
  r0.x = cb3[5].z * r0.x;
  o0.w = exp2(r0.x);
  r0.x = cb3[5].x + v3.x;
  r0.x = cmp(r0.x < 0.5);
  r1.z = r0.x ? cb3[5].y : 2;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK);
  r0.xyz = t1.SampleLevel(s1_s, r1.xyz, 0).xyz;
    r0.xyz *= FIRE_BOOST;
  r0.xyz = cb3[0].yzw * r0.xyz;
  r0.w = !cb3[0].x ? cb2[24].w : 1;
  r0.xyz = r0.www * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  o0.xyz = !cb3[0].xxx ? r1.xyz : r0.xyz;
  return;
}