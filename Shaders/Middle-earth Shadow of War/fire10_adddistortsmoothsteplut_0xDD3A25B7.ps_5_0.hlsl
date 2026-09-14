// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:46 2026
Texture2DArray<float4> t4 : register(t4);

Texture2D<float4> t3 : register(t3);

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
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD1,
  float4 v4 : COLOR0,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = cb3[1].w * v1.x;
  r0.y = cb3[2].x * v1.y;
  r0.xy = cb3[2].yz + r0.xy;
  r1.x = cb3[2].w * cb1[1].x;
  r1.y = cb3[3].x * cb1[1].x;
  r0.xy = r1.xy + r0.xy;
  r0.xy = t0.Sample(s0_s, r0.xy).yw;
  r0.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r0.z = -r0.y;
  r0.xy = r0.xz * cb3[3].yy + v1.xy;
  r0.zw = t2.Sample(s1_s, r0.xy).xw;
  r1.xyz = t1.Sample(s1_s, r0.xy).xyz;
  r0.x = -1 + r0.w;
  r2.x = max(0.00999999978, r0.z);
  r2.y = max(0.00999999978, cb3[3].z);
  r0.yz = float2(1,1) / r2.xy;
  r0.x = r0.y * r0.x;
  r0.y = -1 + r0.z;
  r0.z = 1 + -v4.w;
  r0.w = cb3[4].x + -cb3[3].w;
  r0.z = r0.z * r0.w + cb3[3].w;
  r0.z = r0.z / r2.y;
  r0.x = saturate(r0.x * r0.y + r0.z);
  r0.yz = v3.xy / v3.ww;
  r0.yz = min(cb2[22].zw, r0.yz);
  r0.y = t3.Sample(s2_s, r0.yz).x;
  r0.y = v3.w + -r0.y;
  r0.y = saturate(abs(r0.y) / cb3[4].y);
  r0.y = 1 + -r0.y;
  r0.y = log2(r0.y);
  r0.y = cb3[4].z * r0.y;
  r0.y = exp2(r0.y);
  r0.y = 1 + -r0.y;
  r0.x = r0.x * r0.y;
  r0.x = max(0, r0.x);
  r0.yz = cb3[1].xy * r1.xy;
  r0.y = r0.y + r0.z;
  r0.y = cb3[1].z * r1.z + r0.y;
  r0.y = v4.w * r0.y;
  r0.x = r0.y * r0.x;
  r1.x = r0.x * cb3[4].w + cb3[0].w;
  r0.x = log2(r0.x);
  r0.x = cb3[5].z * r0.x;
  r0.x = exp2(r0.x);
  r0.y = cmp(cb3[5].x < 0.5);
  r1.z = r0.y ? cb3[5].y : 2;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK);;
  r0.yzw = t4.SampleLevel(s3_s, r1.xyz, 0).xyz;
    r0.yzw *= FIRE_BOOST;
  r0.yzw = cb3[0].xyz * r0.yzw;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = !cb3[5].w ? cb2[24].w : 1;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = cb3[6].xxx * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[5].www ? r1.xyz : r0.xyz;
  o0.xyz = v2.www * r0.xyz;
  o0.w = 0;
  return;
}