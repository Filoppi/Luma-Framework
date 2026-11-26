// Luma: HDR-preserving replacement for the bloom source passes 0x8C21ED83 and 0xA439D28A.
// 0x8C21ED83: two outputs; also writes the exposed scene color and the game's t1 alpha to o1 (includes AutoExposure).
// 0xA439D28A: single output variant of 0x8C21ED83.
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 negative values are removed before sqrt
// (avoids NaN) and the color is not clipped to 1, so the HDR value is kept.

#include "Includes/Common.hlsl"
#if _8C21ED83
#include "Includes/AutoExposure.hlsl"
#endif
#include "Includes/HMABloom.hlsl"

#if _8C21ED83
cbuffer cb4 : register(b4)
{
  float4 cb4[42];
}
#endif

Texture2D<float4> t0 : register(t0);
#if _8C21ED83
Texture2D<float4> t1 : register(t1);
#endif

SamplerState s0 : register(s0);
#if _8C21ED83
SamplerState s1 : register(s1);
#endif

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
#if _8C21ED83
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
#else
  out float4 o0 : SV_TARGET0)
#endif
{
  float4 c = t0.Sample(s0, v2);

  o0 = HMABloomSourceEncode(c);
#if _8C21ED83
  o1 = float4(c.rgb * GetHMAExposure(cb4[41].z), t1.Sample(s1, v2).a);
#endif
}
