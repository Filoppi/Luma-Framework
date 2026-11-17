// Pre-rendered video YUV (BT.601) -> RGB decode, writes the scene target.
// Faithful translation of 0x809ECC07.ps_4_0.cso.
// Luma: with TONEMAP_TYPE >= 1 the decoded video gets HDR highlight expansion (AutoHDR), scaled by VideoAutoHDR.

#include "Includes/Common.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[99];
}

SamplerState samplerYplane : register(s0);
SamplerState samplercRplane : register(s1);
SamplerState samplercBplane : register(s2);
Texture2D<float4> mapYplane : register(t0);
Texture2D<float4> mapcRplane : register(t1);
Texture2D<float4> mapcBplane : register(t2);

#if TONEMAP_TYPE >= 1
float DeblockAxis(Texture2D<float4> plane, SamplerState planeSampler, float2 uv, float2 size, float2 axis, float strength)
{
  float coordinate = dot(uv * size - 0.5, axis);
  float boundary = floor((coordinate + 4.5) / 8.0) * 8.0;
  float leftIndex = boundary - 1.0;
  float extent = dot(size, axis);
  // Equal and opposite pair corrections preserve the mean; tent weights follow bilinear footprints.
  float weight = max(1.0 - abs(coordinate - leftIndex), 0.0) - max(1.0 - abs(coordinate - boundary), 0.0);
  if (weight == 0.0 || leftIndex < 1.0 || leftIndex + 2.0 >= extent)
    return 0.0;

  float2 stepUV = axis / size;
  float2 leftUV = uv + (leftIndex - coordinate) * stepUV;
  float leftShoulder = plane.SampleLevel(planeSampler, leftUV - stepUV, 0).w;
  float left = plane.SampleLevel(planeSampler, leftUV, 0).w;
  float right = plane.SampleLevel(planeSampler, leftUV + stepUV, 0).w;
  float rightShoulder = plane.SampleLevel(planeSampler, leftUV + 2.0 * stepUV, 0).w;
  float jump = right - left;
  float gate = saturate(1.0 - abs(jump) * (255.0 / 8.0));
  gate *= saturate(1.0 - max(abs(left - leftShoulder), abs(rightShoulder - right)) * (255.0 / 4.0));
  return strength * jump * gate * weight;
}

float DeblockPlane(Texture2D<float4> plane, SamplerState planeSampler, float2 uv, float strength)
{
  uint width, height;
  plane.GetDimensions(width, height);
  float2 size = float2(width, height);
  float value = plane.Sample(planeSampler, uv).w;
  value += DeblockAxis(plane, planeSampler, uv, size, float2(1.0, 0.0), strength);
  value += DeblockAxis(plane, planeSampler, uv, size, float2(0.0, 1.0), strength);
  return value;
}
#endif

float4 main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0) : SV_TARGET0
{
  float3 rgb;
#if TONEMAP_TYPE >= 1
  rgb = DeblockPlane(mapcRplane, samplercRplane, v2.xy, 0.12) * float3(1.595795, -0.813477, 0.0);
  rgb += DeblockPlane(mapYplane, samplerYplane, v2.xy, 0.17) * 1.164124;
  rgb += DeblockPlane(mapcBplane, samplercBplane, v2.xy, 0.17) * float3(0.0, -0.391449, 2.017822);
#else
  rgb = mapcRplane.Sample(samplercRplane, v2.xy).w * float3(1.595795, -0.813477, 0.0);
  rgb += mapYplane.Sample(samplerYplane, v2.xy).w * 1.164124;
  rgb += mapcBplane.Sample(samplercBplane, v2.xy).w * float3(0.0, -0.391449, 2.017822);
#endif
  rgb += float3(-0.870655, 0.529705, -1.081669);

#if TONEMAP_TYPE >= 1
  rgb = max(rgb, 0.0);
  float3 lin = pow(rgb, 2.2);
  if (LumaSettings.GameSettings.VideoAutoHDREnable > 0 && LumaSettings.GameSettings.VideoAutoHDR > 0.0)
  {
    // PumboAutoHDR caps at MaxPeakWhiteNits / sRGB_WhiteLevelNits (80): pass the target as a paper white multiple so 1 = display peak.
    float targetNits = lerp(LumaSettings.GamePaperWhiteNits, LumaSettings.PeakWhiteNits, LumaSettings.GameSettings.VideoAutoHDR);
    lin = PumboAutoHDR(lin, targetNits / LumaSettings.GamePaperWhiteNits * sRGB_WhiteLevelNits, LumaSettings.GamePaperWhiteNits);
  }
#if UI_DRAW_TYPE == 2 // The UI draws on top in gamma space; display composition scales everything by UI/game paper white, so pre-divide the video
  lin *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif
  rgb = pow(lin, 1.0 / 2.2);
#endif

  return float4(rgb, 1.0) * cb4[1];
}
