// Luma: faithful HLSL translation of Hitman Absolution emissive material pixel shader 0x66F241DF (dither-masked).
// Under TONEMAP_TYPE >= 1 the mapEmissive_01 sample is boosted by LumaSettings.GameSettings.EmissiveHDRBoost (cubic in luminance).
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

cbuffer PerObject_cb0 : register(b0)
{
  float4 cb0[8];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[20];
};

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[9];
};

Texture3D<float4> mapDitherMask : register(t0);
Texture2D<float4> mapEmissive_01 : register(t1);

SamplerState samplerDitherMask : register(s0);
SamplerState samplerEmissive_01 : register(s1);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  float4 v6 : TEXCOORD6,
  float4 v7 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0;

  r0.xyz = v1.y * cb1[5].xyw;
  r0.xyz = v1.x * cb1[4].xyw + r0.xyz;
  r0.xyz = v1.z * cb1[6].xyw + r0.xyz;
  r0.xyz = v1.w * cb1[7].xyw + r0.xyz;
  r0.xy = r0.xy / r0.z;
  r0.xy = r0.xy * cb1[19].zw;
  r0.xy = r0.xy * 0.25;
  r0.z = cb0[7].y;
  r0 = mapDitherMask.Sample(samplerDitherMask, r0.xyz);
  r0.x = r0.x - 0.999;
  if (r0.x < 0.0)
    discard;
  r0 = mapEmissive_01.Sample(samplerEmissive_01, v2.xy);
#if TONEMAP_TYPE >= 1
  float eY = GetLuminance(saturate(r0.rgb));
  r0.rgb *= 1.0 + LumaSettings.GameSettings.EmissiveHDRBoost * 3.0 * eY * eY * eY;
#endif
  r0.xyz = r0.xyz * cb4[8].xyz;
  r0.xyz = r0.xyz * v6.xyz;
  r0.xyz = r0.xyz * cb4[7].xyz;
  o0.xyz = r0.xyz * v6.w;
  o0.w = 1.0;
}
