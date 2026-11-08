#include "Includes/Common.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Oklab.hlsl"
#include "Includes/HMAOutput.hlsl"

// Final post pass (grain + levels + bloom blend), writes the swapchain.
// Faithful translation of 0x53909A5F.ps_4_0.cso.
// Luma: with TONEMAP_TYPE >= 1 the SDR-range levels math is kept on the clamped part, the
// excess above 1 is added back afterwards and the result is tonemapped to the display peak.

cbuffer cb4 : register(b4)
{
  float4 cb4[99];
}

SamplerState samplerTLC0 : register(s0);
SamplerState samplerTLC2 : register(s1);
SamplerState samplerTLW1 : register(s2);
Texture2D<float4> mapTLC0 : register(t0);
Texture2D<float4> mapTLC2 : register(t1);
Texture2D<float4> mapTLW1 : register(t2);
#if TONEMAP_TYPE >= 1
// Pretty Post Process style flare (W/2 x H/2, linear BT.709, exposed scene units, fade applied). Null/0-size when not produced this frame.
Texture2D<float4> mapFlare : register(t3);
Texture2D<float> mapExposure : register(t7);
SamplerState samplerFlare : register(s3);
#endif

float4 main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float3 v2 : COLOR0,
  float3 v3 : COLOR1) : SV_TARGET0
{
  float3 bloom = mapTLC2.Sample(samplerTLC2, v1.xy).rgb;

#if TONEMAP_TYPE >= 1
  // Vanilla grain darkens on average (1 - noise * intensity), so scaling the intensity shifted brightness.
  // Scale only the deviation from the average-noise (0.5) result: strength 1 = vanilla, 0 = no grain at the same mean brightness.
  float grainIntensity = cb4[98].w;
  float grainMaskVanilla = saturate(1.0 - mapTLW1.Sample(samplerTLW1, v1.zw).x * grainIntensity);
  float grainMaskMean = saturate(1.0 - 0.5 * grainIntensity);
  float grainMask = grainMaskMean + (grainMaskVanilla - grainMaskMean) * LumaSettings.GameSettings.FilmGrainStrength;
  float3 vig = lerp(0.5, v2, LumaSettings.GameSettings.VignetteStrength);
  float3 grain = grainMask * vig;

  float3 c = sqrt(max(mapTLC0.Sample(samplerTLC0, v1.xy).rgb, 0.0));
  float3 cS = min(c, 1.0);
  float3 excess = c - cS;

  // The top gradient is a gamma-space add that lifted pure black to ~v3^2: fade it out toward black (full above ~0.25 gamma).
  float gradientFade = smoothstep(0.0, 0.25, max(cS.r, max(cS.g, cS.b)));
  float3 x = max(GrainLevels(cS, grain, v3 * LumaSettings.GameSettings.TopGradientStrength * gradientFade), 0.0);
  x += excess;

  uint flareWidth, flareHeight;
  mapFlare.GetDimensions(flareWidth, flareHeight);
  bool newFlareValid = LumaSettings.GameSettings.LumaBloom != 0 && LumaSettings.GameSettings.ModernLensFlares != 0 && flareWidth > 0;

  float3 b = max(bloom, 0.0) * LumaSettings.GameSettings.BloomStrength * (LumaSettings.GameSettings.LumaBloom != 0 ? LumaSettings.GameSettings.VanillaGlareStrength : 1.0);
  if (newFlareValid)
    b = 0.0;
  float3 lin = HMA_GammaToLinear(x);
  lin += pow(b, 2.2);
  // Gamut is mapped into BT.2020 at constant OkLab L and h before expansion and once at the end.
  // Hue is recovered from the pre-expansion color.
  if (newFlareValid)
  {
    // Linear scene and flare share units (1 = paper white). Soft peak-relative shoulder with one RGB scale.
    float3 flare = max(mapFlare.SampleLevel(samplerFlare, v1.xy, 0).rgb, 0.0);
    float flarePeak = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
    float3 scene2020 = BT709_To_BT2020(lin);
    float headroom = max(flarePeak - max(scene2020.r, max(scene2020.g, scene2020.b)), 0.0);
    float3 flare2020 = BT709_To_BT2020(flare);
    float flareMax = max(flare2020.r, max(flare2020.g, flare2020.b));
    float flareScale = flareMax > 0.0 ? headroom * (1.0 - exp(-flareMax / max(headroom, 1e-4))) / flareMax : 0.0;
    lin += flare * flareScale;
  }
  uint exposureWidth, exposureHeight;
  mapExposure.GetDimensions(exposureWidth, exposureHeight);
  float exposedBrightPoint = exposureWidth == 2 && exposureHeight == 1 ? mapExposure.Load(int3(1, 0, 0)) * LumaSettings.GameSettings.Exposure : 0.0;
  x = HMA_DisplayOutput(lin, exposedBrightPoint);
#else
  float grainMask = saturate(1.0 - mapTLW1.Sample(samplerTLW1, v1.zw).x * cb4[98].w);
  float3 grain = grainMask * v2;

  float3 c = sqrt(mapTLC0.Sample(samplerTLC0, v1.xy).rgb);
  float3 x = GrainLevels(c, grain, v3);
  x = x * (1.0 - bloom) + bloom;
#endif

  return float4(x, 0.0);
}
