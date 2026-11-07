// Upstream: Downsample.usf (13-tap CoD-style downsample, RenderDownsample).

#include "Includes/HMAFlare.hlsl"

Texture2D<float4> t0 : register(t0); // Previous flare mip (border sampler s1)

float4 main(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float3 c = Downsample13(t0, s1, uv, 0.5 * OutSize.zw);
  return float4(c, 1.0f);
}
