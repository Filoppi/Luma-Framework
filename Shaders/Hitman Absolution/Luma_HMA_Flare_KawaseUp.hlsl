// Upstream: DualKawaseBlur.usf KawaseBlurUpsamplePS (dual Kawase up pass).

#include "Includes/HMAFlare.hlsl"

Texture2D<float4> t0 : register(t0); // Previous blur level (clamp sampler s0)

float4 main(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 hp = 0.5 * OutSize.zw;

  float3 c = t0.Sample(s0, uv + float2(-hp.x, hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(hp.x, hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(hp.x, -hp.y)).rgb;
  c += t0.Sample(s0, uv + float2(-hp.x, -hp.y)).rgb;

  c += t0.Sample(s0, uv + float2(-hp.x, 0.0f)).rgb * 2.0f;
  c += t0.Sample(s0, uv + float2(hp.x, 0.0f)).rgb * 2.0f;
  c += t0.Sample(s0, uv + float2(0.0f, hp.y)).rgb * 2.0f;
  c += t0.Sample(s0, uv + float2(0.0f, -hp.y)).rgb * 2.0f;
  c /= 12.0f;

  return float4(c, 1.0f);
}
