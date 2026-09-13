// ---- Created with 3Dmigoto v1.3.16 on Sat Jun 13 00:23:27 2026
Texture2D<float4> t4 : register(t4);

SamplerState s4_s : register(s4);

cbuffer cb2 : register(b2)
{
  float4 cb2[4];
}

// 3Dmigoto declarations
#define cmp -
#define COMMON_LUT
#include "h1_common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // Color
  LUT_Color(v1);

  // sRGB Encode
  LUT_Gamma();

  // No LUT

  // Saturation and Tint
  LUT_SaturationAndTint();

  // Upgrade and Tonemap
  LUT_UpgradeAndTonemap();

  o0.xyz = li.r0;
  o0.w = 1;
  return;
}

/*




*/