// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:19 2026
Texture2DArray<float4> t2 : register(t2);

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
  float4 cb3[6];
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

  r0.xy = v2.xy / v2.ww;
  r0.xy = min(cb2[22].zw, r0.xy);
  r0.x = t1.Sample(s1_s, r0.xy).x;
  r0.x = v2.w + -r0.x;
  r0.x = saturate(abs(r0.x) / cb3[3].w);
  r0.x = 1 + -r0.x;
  r0.x = log2(r0.x);
  r0.x = cb3[4].x * r0.x;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  r0.y = cb3[3].y / cb3[3].z;
  r0.z = cmp(r0.y >= -r0.y);
  r0.y = frac(abs(r0.y));
  r0.y = r0.z ? r0.y : -r0.y;
  r0.y = cb3[3].z * r0.y;
  r1.z = floor(r0.y);
  r1.xy = v1.xy;
  r1.xyzw = t0.Sample(s0_s, r1.xyz).xyzw;
  r0.x = saturate(r1.w * r0.x);
  r0.yz = cb3[1].xy * r1.xy;
  r0.y = r0.y + r0.z;
  r0.y = cb3[1].z * r1.z + r0.y;
  r0.y = v3.w * r0.y;
  r0.x = r0.y * r0.x;
  r1.x = r0.x * cb3[4].y + cb3[0].w;
  r0.x = log2(r0.x);
  r0.x = cb3[5].x * r0.x;
  r0.x = exp2(r0.x);
  r0.y = cmp(cb3[4].z < 0.5);
  r1.z = r0.y ? cb3[4].w : 2;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK);
  r0.yzw = t2.SampleLevel(s2_s, r1.xyz, 0).xyz;
    r0.yzw *= FIRE_BOOST;
  r0.yzw = cb3[0].xyz * r0.yzw;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = !cb3[2].y ? cb2[24].w : 1;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = cb3[5].yyy * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[2].yyy ? r1.xyz : r0.xyz;
  o0.xyz = v4.www * r0.xyz;
  o0.w = 0;
  return;
}