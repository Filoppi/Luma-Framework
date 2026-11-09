// Upstream: Downsample.usf (13-tap, first mip from full-res scene) + soft threshold from PrettyPostProcess bloom input.

#include "Includes/Common.hlsl"
#include "Includes/AutoExposure.hlsl"
#include "Includes/HMAFlare.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[102];
}

Texture2D<float4> t0 : register(t0); // Full-res linear HDR scene (W x H)

float4 main(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float exposure = GetHMAExposure(cb4[41].z) * LumaSettings.GameSettings.Exposure;
  float2 px = 0.5 * OutSize.zw;

  // 13-tap downsample (same taps and weights as Downsample13) with per-sample max(scene, 0) * exposure.
  static const float2 Coords[13] =
  {
    float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, -1.0f),

    float2(-2.0f, 2.0f), float2(0.0f, 2.0f), float2(2.0f, 2.0f),
    float2(-2.0f, 0.0f), float2(0.0f, 0.0f), float2(2.0f, 0.0f),
    float2(-2.0f, -2.0f), float2(0.0f, -2.0f), float2(2.0f, -2.0f)
  };

  static const float Weights[13] =
  {
    0.125f, 0.125f,
    0.125f, 0.125f,

    0.0555555f, 0.0555555f, 0.0555555f,
    0.0555555f, 0.0555555f, 0.0555555f,
    0.0555555f, 0.0555555f, 0.0555555f
  };

  float3 c = float3(0.0f, 0.0f, 0.0f);
  [unroll]
  for (int i = 0; i < 13; i++)
  {
    float2 tapUV = uv + Coords[i] * px;
    c += Weights[i] * (max(t0.Sample(s0, tapUV).rgb, 0.0f) * exposure);
  }

  // Soft threshold on max channel (fading knee).
  float T = ThresholdParams.x;
  float K = T * ThresholdParams.y;
  float m = max(c.r, max(c.g, c.b));
  float soft = clamp(m - T + K, 0.0f, 2.0f * K);
  soft = soft * soft / (4.0f * K + 1e-5f);
  float w = max(soft, m - T) / max(m, 1e-5f);
  float3 o = c * w;

  if (!IsFinite3(o))
    o = float3(0.0f, 0.0f, 0.0f);

  return float4(o, 1.0f);
}
