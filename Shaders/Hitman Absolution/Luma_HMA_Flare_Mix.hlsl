// Upstream: Mix.usf (blurred feature + glare composite, FlareTint, FlareIntensity). Gradient texture omitted (white).

#include "Includes/HMAFlare.hlsl"

Texture2D<float4> t0 : register(t0); // Blurred feature (W/2, clamp s0)
Texture2D<float4> t1 : register(t1); // Glare (W/2, clamp s0), only if MixParams.y
Texture2D<float4> t3 : register(t3); // Game fade, only if MixParams.z

float4 main(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;

  float3 flare = t0.Sample(s0, uv).rgb;

  if (MixParams.y > 0.5f)
  {
    // Glare is W/2 x H/2 (half of the output size), so 2 * OutSize.zw is the same UV offset as before.
    float2 gt = 2.0f * OutSize.zw;
    float3 g = t1.Sample(s0, uv + float2(-gt.x, gt.y)).rgb;
    g += t1.Sample(s0, uv + float2(gt.x, gt.y)).rgb;
    g += t1.Sample(s0, uv + float2(-gt.x, -gt.y)).rgb;
    g += t1.Sample(s0, uv + float2(gt.x, -gt.y)).rgb;
    flare += g * 0.25f;
  }

  float3 o = flare * FlareTint.rgb * MixParams.x;

  if (MixParams.z > 0.5f)
  {
    float4 fade = t3.Sample(s0, uv);
    o *= saturate(1.0f - fade.a);
  }

  if (!IsFinite3(o))
    o = float3(0.0f, 0.0f, 0.0f);

  return float4(o, 1.0f);
}
