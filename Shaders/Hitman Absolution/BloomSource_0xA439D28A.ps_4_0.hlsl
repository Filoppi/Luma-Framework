// Luma: HDR-preserving replacement for the bloom source pass 0xA439D28A (single output variant of 0x8C21ED83).
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 negative values are removed before sqrt
// (avoids NaN) and the color is not clipped to 1, so the HDR value is kept.

#include "Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0 : register(s0);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 c = t0.Sample(s0, v2);

#if TONEMAP_TYPE >= 1
  // Keep vanilla's scaled encode (rgb = sqrt(c / maxc), a = 1 / maxc): the bright pass decodes rgb^2 / a back to the full
  // HDR value, and the auto-exposure meter depends on this exact encoding. Only guard against NaN.
  c.rgb = max(c.rgb, 0.0);
  float s = saturate(1.0 / max(max(c.z, max(c.y, c.x)), 1e-6));
  o0 = float4(sqrt(c.rgb * s), s);
#else
  float s = saturate(1.0 / max(c.z, max(c.y, c.x)));
  o0 = float4(sqrt(c.rgb * s), s);
#endif
}
