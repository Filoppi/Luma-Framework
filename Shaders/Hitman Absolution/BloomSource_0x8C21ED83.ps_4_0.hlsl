// Luma: HDR-preserving replacement for the bloom source pass 0x8C21ED83.
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 negative values are removed before sqrt
// (avoids NaN) and the color is not clipped to 1, so the HDR value is kept.

#include "Includes/Common.hlsl"
#include "Includes/AutoExposure.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[42];
}

Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
{
  float4 c = t0.Sample(s0, v2);

#if TONEMAP_TYPE >= 1
  // Keep vanilla's scaled encode (rgb = sqrt(c / maxc), a = 1 / maxc): the bright pass decodes rgb^2 / a back to the full
  // HDR value, and the auto-exposure meter (0x6AD9E762 -> 0x2D239654) depends on this exact encoding. Only guard against NaN.
  c.rgb = max(c.rgb, 0.0);
  float s = saturate(1.0 / max(max(c.z, max(c.y, c.x)), 1e-6));
  o0 = float4(sqrt(c.rgb * s), s);
#else
  float s = saturate(1.0 / max(c.z, max(c.y, c.x)));
  o0 = float4(sqrt(c.rgb * s), s);
#endif

  o1 = float4(c.rgb * GetHMAExposure(cb4[41].z), t1.Sample(s1, v2).a);
}
