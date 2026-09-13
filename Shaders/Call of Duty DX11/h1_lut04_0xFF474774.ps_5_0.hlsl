// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 12 20:03:53 2025

Texture3D<float4> t5 : register(t5);

Texture2D<float4> t4 : register(t4);

Texture2D<uint4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

SamplerState s4_s : register(s4);

SamplerState s2_s : register(s2);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[28];
}

// 3Dmigoto declarations
#define cmp -
#define COMMON_LUT
#include "h1_common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Color
  r0.xyzw = t4.Sample(s4_s, v1.xy).xyzw;
  float yRecover = r0.w;

  // Distortion / Mask ???
  r1.xy = cb2[23].xy * v1.xy;
  r1.xy = (int2)r1.xy;
  r0.w = (uint)cb2[0].w;
  r2.xz = (int2)r1.yx + (int2)-r0.ww;
  r1.z = r2.x;
  r1.w = 0;

  r1.z = t3.Load(r1.xzw).y;
  r3.xy = (int2)r1.yx + (int2)r0.ww;
  r3.zw = r1.xw;
  r0.w = t3.Load(r3.zxw).y;
  r2.yw = r1.yw;
  r1.x = t3.Load(r2.zyw).y;
  r2.xzw = r3.yww;
  r1.y = t3.Load(r2.xyz).y;
  r0.w = max((uint)r1.z, (uint)r0.w);
  r1.x = max((uint)r1.x, (uint)r1.y);
  r0.w = max((uint)r1.x, (uint)r0.w);
  r0.w = (uint)r0.w >> 5;
  r0.w = cmp((int)r0.w == 1);
  r1.x = t2.SampleLevel(s0_s, v1.xy, 0).x;
  r1.y = cmp(0.984375 < r1.x);
  r1.z = r1.y ? 0.100000001 : cb2[27].x;
  r1.yw = r1.yy ? float2(64,-63) : float2(1,0);
  r1.x = r1.y * r1.x + r1.w;
  r1.x = max(9.99999994e-009, r1.x);
  r1.x = r1.z / r1.x;
  r1.x = saturate(0.00033333333 * r1.x);
  r1.x = 1 + -r1.x;
  r1.x = cb2[23].z * r1.x;
  r1.xyz = cb2[1].xyz * r1.xxx;
  r1.w = 0;
  r2.xyzw = v1.xyxy + r1.xwyw;

  // CA
  r3.x = t4.Sample(s4_s, r2.xy).x;
  r3.y = t4.Sample(s4_s, r2.zw).y;
  r1.xy = v1.xy + r1.zw;
  r3.z = t4.Sample(s4_s, r1.xy).z;
  if (r0.w != 0) {
    r1.xyz = r3.xyz + -r0.xyz;
    r0.xyz = cb2[1].www * r1.xyz + r0.xyz;
  }
  r0.xyz = saturate(r0.xyz);

  LUT_Color_Internal(r0.xyz, yRecover);

  // sRGB Encode
  LUT_Gamma();
  
  // LUT
  LUT_LUT(t5, s2_s);

  // Saturation and Tint
  LUT_SaturationAndTint(2);

  // Upgrade and Tonemap
  LUT_UpgradeAndTonemap();

  o0.xyz = li.r0;
  o0.w = 1;
  return;
}