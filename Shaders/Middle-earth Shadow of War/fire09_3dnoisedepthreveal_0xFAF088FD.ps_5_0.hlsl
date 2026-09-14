// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:51 2026
Texture2DArray<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture3D<float4> t3 : register(t3);

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
  float4 cb3[12];
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

  r0.xyz = cb3[7].xyz * v4.xyz;
  r0.xyzw = t3.Sample(s2_s, r0.xyz).xyzw;
  r0.x = saturate(dot(cb3[6].xyzw, r0.xyzw));
  r0.y = 1 + -cb3[7].w;
  r0.zw = float2(1,1) + cb3[8].xz;
  r0.y = r0.y * r0.z + r0.x;
  r0.x = -0.00499999989 + r0.x;
  r0.x = ceil(r0.x);
  r0.y = -1 + r0.y;
  r1.xy = float2(1,1) / cb3[8].xz;
  r0.y = saturate(r1.x * r0.y);
  r0.z = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r0.z * r0.y;
  r0.x = r0.x * r0.y;
  r0.x = saturate(cb3[8].y * r0.x);
  r0.yz = cb3[4].xy * cb1[1].xx;
  r0.yz = v1.xy * cb3[3].zw + r0.yz;
  r0.yz = t0.Sample(s0_s, r0.yz).yw;
  r2.xy = r0.yz * float2(2,2) + float2(-1,-1);
  r2.z = -r2.y;
  r0.yz = r2.xz * cb3[4].zz + v1.xy;
  r2.xyzw = t1.Sample(s0_s, r0.yz).xyzw;
  r1.x = cb3[5].w * r2.w;
  r0.x = r1.x * r0.x;
  r1.x = t4.SampleLevel(s3_s, v4.xy, 0).x;
  r1.x = saturate(cb3[9].x * r1.x);
  r0.w = r0.w * cb3[8].w + r1.x;
  r0.w = -1 + r0.w;
  r0.w = saturate(r0.w * r1.y);
  r1.x = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r1.x * r0.w;
  r0.w = min(1, r0.w);
  r0.w = saturate(cb3[10].x * r0.w);
  r0.x = r0.x * r0.w;
  r1.x = cb3[4].w * r0.y;
  r1.y = cb3[5].x * r0.z;
  r0.yz = cb1[1].xx * cb3[5].yz + r1.xy;
  r1.xyzw = t2.Sample(s1_s, r0.yz).xyzw;
  r0.x = r1.w * r0.x;
  r0.yzw = r2.xyz * r1.xyz;
  r0.yz = cb3[0].yz * r0.yz;
  r0.y = r0.y + r0.z;
  r0.y = cb3[0].w * r0.w + r0.y;
  r0.x = r0.y * r0.x;
  r1.x = r0.x * cb3[10].y + cb3[0].x;
  r0.x = log2(r0.x);
  r0.x = cb3[11].x * r0.x;
  r0.x = exp2(r0.x);
  r0.y = cmp(cb3[10].z < 0.5);
  r1.z = r0.y ? cb3[10].w : 2;
  r1.y = 0;
    r1.x = FireTonemap(r1.x, FIRE_PEAK * 1.3);
  r0.yzw = t5.SampleLevel(s3_s, r1.xyz, 0).xyz;
    r0.yzw *= FIRE_BOOST;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = !cb3[3].y ? cb2[24].w : 1;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = cb3[11].yyy * r0.xyz;
  r1.xyz = cb2[26].www * r0.xyz;
  r0.xyz = !cb3[3].yyy ? r1.xyz : r0.xyz;
  o0.xyz = v2.www * r0.xyz;
  o0.w = 0;
  return;
}