// Luma: faithful HLSL translation of Hitman Absolution emissive pixel shader 0xED6B7B39.
// Under TONEMAP_TYPE >= 1 the mapEmissive_01 sample is boosted by EmissiveHDRBoost (cubic in luminance) to add HDR headroom.
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[9];
};

Texture2D<float4> mapEmissive_01 : register(t0);

SamplerState samplerEmissive_01 : register(s0);

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

  r0 = mapEmissive_01.Sample(samplerEmissive_01, v2.xy);
#if TONEMAP_TYPE >= 1
  float eY = GetLuminance(saturate(r0.rgb));
  r0.rgb *= 1.0 + LumaSettings.GameSettings.EmissiveHDRBoost * 3.0 * eY * eY * eY;
#endif
  r0.xyz = r0.xyz * cb4[8].xyz;
  r0.xyz = r0.xyz * v6.xyz;
  r0.xyz = r0.xyz * cb4[7].xyz;
  o0.xyz = r0.xyz * v6.www;
  o0.w = 1.0;
}
