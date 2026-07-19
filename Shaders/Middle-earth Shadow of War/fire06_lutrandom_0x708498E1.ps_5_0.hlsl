// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:29 2026
Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2DArray<float4> t0 : register(t0);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[53];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[9];
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

  r0.xy = v3.xy / v3.ww;
  r0.xy = min(cb2[22].zw, r0.xy);
  r0.x = t2.Sample(s2_s, r0.xy).x;
  r0.x = v3.w + -r0.x;
  r0.x = saturate(abs(r0.x) / cb3[7].w);
  r0.x = 1 + -r0.x;
  r0.x = log2(r0.x);
  r0.x = cb3[8].x * r0.x;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  r0.yz = t0.Sample(s1_s, v1.xyz).xw;
  r0.x = saturate(r0.z * r0.x);
  r0.y = v4.x * r0.y;
  r1.x = r0.y * cb3[7].z + cb3[0].y;
  o0.w = v4.w * r0.x;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK * 1.32);
  r0.xyz = t1.SampleLevel(s0_s, r1.xy, 0).xyz;
    r0.xyz *= FIRE_BOOST;
  r0.w = !cb3[0].x ? cb2[24].w : 1;
  r0.xyz = r0.www * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[0].xxx ? r1.xyz : r0.xyz;
  o0.xyz = r0.xyz * v2.www + v2.xyz;


  return;
}