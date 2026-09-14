// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:51 2026
Texture2D<float4> t3 : register(t3);

Texture2DArray<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2DArray<float4> t0 : register(t0);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s1_s : register(s1);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[53];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[15];
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
  nointerpolation int v5 : TEXCOORD3,
  uint v6 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = !cb3[0].x ? cb2[24].w : 1;
  r0.y = cmp((int)v5.x >= 6);
  r0.zw = cb3[13].xy * cb1[1].xx;
  r0.zw = v1.xy * cb3[13].zw + r0.zw;
  r0.zw = t1.Sample(s2_s, r0.zw).yw;
  r0.zw = r0.zw * float2(2,2) + float2(-1,-1);
  r1.x = r0.y ? cb3[14].y : cb3[14].x;
  r1.xy = saturate(r0.zw * r1.xx + v1.xy);
  r1.z = v1.z;
  r1.xyzw = t0.Sample(s1_s, r1.xyz).xyzw;
  if (r0.y == 0) {
    r2.xyzw = cmp((int4)v5.xxxx == int4(1,3,5,7));
    r0.y = (int)r2.y | (int)r2.x;
    r0.y = (int)r2.z | (int)r0.y;
    r0.y = (int)r2.w | (int)r0.y;
    r0.z = cmp((int)v5.x == 9);
    r0.y = (int)r0.z | (int)r0.y;
    r0.zw = cmp((int2)v5.xx >= int2(0,3));
    r2.xy = cmp((int2)v5.xx < int2(3,5));
    r0.zw = r0.zw ? r2.xy : 0;
    r0.w = r0.w ? r1.y : r1.x;
    r0.z = r0.z ? r1.z : r0.w;
    r2.w = v3.w * r0.z;
    r0.zw = r2.ww * float2(0.5,0.699999988) + float2(0.550000012,0.449999988);
    r1.x = r0.y ? r0.w : r0.z;
    r1.z = r0.y ? 2.000000 : 0;
    r1.y = 0;
      r1.x = FireTonemap(r1.x, FIRE_PEAK);
    r2.xyz = t2.SampleLevel(s0_s, r1.xyz, 0).xyz;
      r2.xyz *= FIRE_BOOST;
  } else {
    r0.y = saturate(r1.w);
    r3.w = r0.y * r0.y;
    r3.xyz = r1.www;
    r2.xyzw = v3.xyzw * r3.xyzw;
  }
  r0.xyz = r2.xyz * r0.xxx;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[0].xxx ? r1.xyz : r0.xyz;
  o0.xyz = r0.xyz * v4.www + v4.xyz;
  r0.xy = v2.xy / v2.ww;
  r0.xy = min(cb2[22].zw, r0.xy);
  r0.x = t3.Sample(s3_s, r0.xy).x;
  r0.x = v2.w + -r0.x;
  r0.x = saturate(abs(r0.x) / cb3[14].z);
  r0.x = 1 + -r0.x;
  r0.x = log2(r0.x);
  r0.x = cb3[14].w * r0.x;
  r0.x = exp2(r0.x);
  r0.x = 1 + -r0.x;
  o0.w = saturate(r2.w * r0.x);
  return;
}