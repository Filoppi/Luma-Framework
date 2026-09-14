// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 22:37:59 2026
Texture2DArray<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

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
  float4 cb3[7];
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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = v5.z;
  r0.y = v6.z;
  r0.z = v7.z;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  r0.w = dot(v8.xyz, v8.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = v8.xyz * r0.www;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.x = cb3[4].y * abs(r0.x);
  r0.x = log2(r0.x);
  r0.x = cb3[4].z * r0.x;
  r0.x = exp2(r0.x);
  r1.xyz = cb3[3].xyw * cb1[1].xxx;
  r0.yz = v1.xy * cb3[2].zw + r1.xy;
  r0.yz = t0.Sample(s0_s, r0.yz).yw;
  r2.xy = r0.yz * float2(2,2) + float2(-1,-1);
  r2.z = -r2.y;
  r0.yz = r2.xz * cb3[3].zz + v1.xy;
  r1.w = cb3[4].x * cb1[1].x;
  r0.yz = r1.zw + r0.yz;
  r0.yz = t1.Sample(s1_s, r0.yz).xw;
  r0.w = v3.w * 1.20000005 + r0.z;
  r0.zw = float2(-0.00499999989,-1) + r0.zw;
  r0.w = saturate(4.99999905 * r0.w);
  r1.x = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r1.x * r0.w;
  r0.y = v3.w * r0.y;
  r0.z = saturate(ceil(r0.z));
  r0.z = r0.z * r0.w;
  r0.x = r0.z * r0.x;
  r0.zw = v2.xy / v2.ww;
  r0.zw = min(cb2[22].zw, r0.zw);
  r0.z = t2.Sample(s2_s, r0.zw).x;
  r0.z = v2.w + -r0.z;
  r0.z = saturate(abs(r0.z) / cb3[4].w);
  r0.z = 1 + -r0.z;
  r0.z = log2(r0.z);
  r0.z = cb3[5].x * r0.z;
  r0.z = exp2(r0.z);
  r0.z = 1 + -r0.z;
  r0.x = saturate(r0.x * r0.z);
  r0.x = r0.y * r0.x;
  r1.x = r0.x * cb3[5].y + cb3[0].w;
  r0.x = log2(r0.x);
  r0.x = cb3[6].x * r0.x;
  r0.x = exp2(r0.x);
  r0.y = cb3[5].z + v3.x;
  r0.y = cmp(r0.y < 0.5);
  r1.z = r0.y ? cb3[5].w : 2;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK);;
  r0.yzw = t3.SampleLevel(s3_s, r1.xyz, 0).xyz;
    r0.yzw *= FIRE_BOOST;
  r0.yzw = cb3[0].xyz * r0.yzw;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = !cb3[1].z ? cb2[24].w : 1;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = cb3[6].yyy * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[1].zzz ? r1.xyz : r0.xyz;
  o0.xyz = v4.www * r0.xyz;
  o0.w = 0;
  return;
}