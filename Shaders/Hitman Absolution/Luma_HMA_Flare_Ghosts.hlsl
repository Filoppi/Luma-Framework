// Adapted from Pretty Post Process Ghosts.usf (starburst term omitted, as in the shipped upstream output). See Includes/HMAFlare.hlsl for license.
#include "Includes/HMAFlare.hlsl"

Texture2D<float4> t0 : register(t0); // mip0 (W/2 x H/2), border sampler s1
Texture2D<float4> t1 : register(t1); // halo (W/4 x H/4), clamp sampler s0

float4 main(float4 pos : SV_Position) : SV_Target0
{
  float2 uv = pos.xy * OutSize.zw;
  float2 sp = uv * 2.0 - 1.0;
  float aspect = InSize.x / InSize.y;

  float intensity = GhostParams.x;
  float chromaShift = GhostParams.y;
  float compression = GhostParams.z;

  float2 SquareUVv = SquareUV(uv, aspect);
  float ScaleAlpha = length(SquareUVv - 0.5);
  float2 ScaleUV = (uv - 0.5) * (1.0 - compression * ScaleAlpha) + 0.5;

  float3 Color = float3(0.0, 0.0, 0.0);
  [unroll]
  for (int i = 0; i < 8; i++)
  {
    if (abs(GhostColors[i].a * GhostScales[i].x) > 0.0001)
    {
      float2 NewUV = (ScaleUV - 0.5) * GhostScales[i].x;

      // Local mask
      float DistanceMask = 1.0 - length(NewUV);
      float Mask = smoothstep(0.5, 0.9, DistanceMask);
      float Mask2 = smoothstep(0.75, 1.0, DistanceMask) * 0.95 + 0.05;

      Color.r += t0.SampleLevel(s1, NewUV * (1.0 + chromaShift) + 0.5, 0).r * GhostColors[i].r * Mask * Mask2;
      Color.g += t0.SampleLevel(s1, NewUV + 0.5, 0).g * GhostColors[i].g * Mask * Mask2;
      Color.b += t0.SampleLevel(s1, NewUV * (1.0 - chromaShift) + 0.5, 0).b * GhostColors[i].b * Mask * Mask2;
    }
  }

  float ScreenborderMask = DiscMask(sp * 0.9);
  Color = Color * ScreenborderMask * (intensity / 100.0) / 10.0;

  // Halo is added on top of the ghosts (upstream halo sampled at the output uv).
  Color += t1.SampleLevel(s0, uv, 0).rgb;

  return float4(Color, 0.0);
}
