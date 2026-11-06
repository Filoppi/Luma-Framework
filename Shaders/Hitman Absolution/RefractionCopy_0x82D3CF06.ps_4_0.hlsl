// Luma: HDR-preserving replacement for the refraction copy pass 0x82D3CF06.
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 negative values are removed
// and the color is not clipped to 1, so the HDR value is kept (alpha 1).

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
  c.rgb = max(c.rgb, 0.0);
  o0 = float4(c.rgb, 1.0);
#else
  float s = saturate(1.0 / max(c.z, max(c.y, c.x)));
  o0 = float4(c.rgb * s, s);
#endif
}
