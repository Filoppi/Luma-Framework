// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 12 12:44:40 2025

Texture2D<float4> t4 : register(t4);

Texture3D<float4> t2 : register(t2);

SamplerState s4_s : register(s4);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[23];
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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // CA coords offset
  r0.xyz = cb2[22].zzz * cb2[0].xyz;
  r0.w = 0;
  r1.xy = v1.xy + r0.zw;
  r0.xyzw = v1.xyxy + r0.xwyw;

  // CA sample
  r1.z = t4.Sample(s4_s, r1.xy).z;
  r1.x = t4.Sample(s4_s, r0.xy).x;
  r1.y = t4.Sample(s4_s, r0.zw).y;

  // Color
  r0.xyzw = t4.Sample(s4_s, v1.xy).xyzw;

  // CA composite
  r1.xyz = r1.xyz + -r0.xyz;
  r0.xyz = cb2[0].www * r1.xyz + r0.xyz;
  r0.xyz = saturate(r0.xyz);

  LUT_Color_Internal(r0.xyz, r0.w);

  // sRGB Encode
  LUT_Gamma();

  // LUT
  LUT_LUT(t2, s0_s);

  // Saturation and Tint
  LUT_SaturationAndTint(1);

  // Upgrade and Tonemap
  LUT_UpgradeAndTonemap();

  o0.xyz = li.r0;
  o0.w = 1;
  return;
}