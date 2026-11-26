// Luma: HDR-preserving replacement for the tonemap/color grading passes 0x46C50FC2 and 0xEED9AC77 (Chinatown variant).
// 0x46C50FC2: always applies the LUT grade.
// 0xEED9AC77: when the raw depth coord is > 1 the LUT grade is skipped and the faded scene is output ungraded.
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 the scene is not clamped to SDR:
// the LUT curves are extended above 1 and sampled per channel (with optional hue preservation), then the grading settings and highlight saturation are applied.

#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "Includes/AutoExposure.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[102];
}

Texture2D<float4> t0 : register(t0); // Depth
Texture2D<float4> t1 : register(t1); // Scene
Texture2D<float4> t2 : register(t2); // Color grading LUT
Texture2D<float4> t3 : register(t3); // Fade
#if TONEMAP_TYPE >= 1
Texture2D<float4> t4 : register(t4); // Luma bloom
#endif

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
#if TONEMAP_TYPE >= 1
#include "../Includes/Tonemap.hlsl"
#include "Includes/HMAHaze.hlsl"
#endif

#include "Includes/HMATonemap.hlsl"

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float depth = t0.Sample(s0, v2).x;
  float d = depth * cb4[8].z - cb4[8].y;
  d = -cb4[8].x / d;
#if _EED9AC77
  float dRaw = d * cb4[101].y + cb4[101].x;
  bool noGrade = dRaw > 1.0;
  d = saturate(dRaw);
#else
  d = saturate(d * cb4[101].y + cb4[101].x);
#endif

  float4 fade = t3.Sample(s3, v2);
  float3 lumaWeights = float3(0.299, 0.587, 0.114);

#if TONEMAP_TYPE >= 1
  float scale = GetHMAExposure(cb4[41].z);
  float3 hdr = max(t1.Sample(s1, v2).rgb, 0.0) * scale;
  hdr *= LumaSettings.GameSettings.Exposure;
  // Sun is baked into the sky dome and cloud textures, so sky pixels that are bright relative to exposure get a boost.
  // The ramp must end below the dome's depth, or float depth steps draw stepped contour lines inside the dome.
  float skyMask = smoothstep(0.99999, 0.9999915, depth);
  // The boost is applied after the LUT grade, since boosting before it pushes the sun core past the LUT top and knee kinks (contour lines).
  // The sun scales like the rest of the sky; there is no separate per-sun brightness control.
  float skyBoost = 1.0;
  // Sky HDR expansion, applied here rather than in Sky_0xACE3BFA3 so the auto-exposure meter sees the vanilla sky.
  // Keyed to the sky's pre-exposure brightness (divisor 1.06) so dim-exposure levels still get the lift.
  float skyY = SoftCubicRamp(GetLuminance(hdr) / max(scale * LumaSettings.GameSettings.Exposure, 1e-6) * skyBoost / 1.06);
  skyBoost *= 1.0 + LumaSettings.GameSettings.SkyHDRBoost * 3.0 * skyMask * skyY;
  hdr = lerp(hdr, fade.rgb, fade.a);

  float restore = LumaSettings.GameSettings.VanillaHueRestore;
  bool useLinearHaze = LumaSettings.GameSettings.TonemapVersion != 0 && LumaSettings.GameSettings.HazeModel >= 0.5;
  float curveHaze = useLinearHaze ? 0.0 : saturate(LumaSettings.GameSettings.DistanceHaze);
  float3 neutral = NeutralCompress(hdr);
  float3 hdrGraded;
  if (LumaSettings.GameSettings.TonemapVersion == 0)
  {
#if _EED9AC77
    hdrGraded = (noGrade ? hdr : LUTGradeHDR(hdr, d)) * lerp(skyBoost, 1.0, fade.a);
#else
    hdrGraded = LUTGradeHDR(hdr, d) * lerp(skyBoost, 1.0, fade.a);
#endif
    // Pull hue and chrominance toward the vanilla SDR grade of the same pixel (VanillaHueRestore, same as Tonemap_0xEED9AC77).
    float3 sdrScene = saturate(t1.Sample(s1, v2).rgb * GetHMAExposure(cb4[41].z) * LumaSettings.GameSettings.Exposure);
#if _EED9AC77
    float3 sdrRef = noGrade ? lerp(sdrScene, fade.rgb, fade.a) : LUTGradeScaled(sdrScene, fade, d, curveHaze);
#else
    float3 sdrRef = LUTGradeScaled(sdrScene, fade, d, curveHaze);
#endif
    if (restore > 0.0)
      hdrGraded = RestoreHueAndChrominance(hdrGraded, sdrRef, 0.75 * restore, restore);
  }
  else
  {
#if _EED9AC77
    float3 neutralGraded = neutral;
    if (!noGrade)
    {
      neutralGraded = LUTGradeNeutral(neutral, d, curveHaze);
      // The floor uses the near curve only (haze 0), so the depth haze veil is not subtracted back out.
      float3 floorC = LUTGradeNeutral(0.0, d, 0.0);
      neutralGraded = max(neutralGraded - floorC * saturate(1.0 - GetLuminance(neutral) / 0.18) * LumaSettings.GameSettings.BlackFloorRemoval, 0.0);
    }
#else
    float3 neutralGraded = LUTGradeNeutral(neutral, d, curveHaze);
    // Floor uses the near curve only (no haze), so the depth haze veil isn't subtracted from shadows.
    float3 floorC = LUTGradeNeutral(0.0, d, 0.0);
    neutralGraded = max(neutralGraded - floorC * saturate(1.0 - GetLuminance(neutral) / 0.18) * LumaSettings.GameSettings.BlackFloorRemoval, 0.0);
#endif
    hdrGraded = UpgradeToneMap(hdr, neutral, neutralGraded, 1.0) * lerp(skyBoost, 1.0, fade.a);
    if (restore > 0.0)
      hdrGraded = RestoreHueAndChrominance(hdrGraded, neutralGraded * lerp(skyBoost, 1.0, fade.a), 0.75 * restore, restore);
  }
#if _EED9AC77
  if (!noGrade && restore > 0.0)
#else
  if (restore > 0.0)
#endif
  {
    // Linear haze uses haze-free shadow references; the curve model follows the saturated slider.
    float3 vanillaScene = saturate(t1.Sample(s1, v2).rgb * GetHMAExposure(cb4[41].z) * LumaSettings.GameSettings.Exposure);
    float3 vanillaRef = LUTGradeScaled(vanillaScene, fade, d, curveHaze);
    float vanillaY = GetLuminance(vanillaRef);
    float gradedY = GetLuminance(hdrGraded);
    if (vanillaY > 1e-6 && gradedY > 0.0)
    {
      float sceneY = GetLuminance(vanillaScene);
      // Near true black the raw scene hue is noise: fade to vanilla's graded hue.
      // The LUT adds a coloured black floor: strip it (faded out by mid grey, like BlackFloorRemoval) so its hue
      // does not tint shadows once they are scaled to the brighter HDR level.
      float3 vanillaHue = max(vanillaRef - LUTGradeScaled(0.0, fade, d, 0.0) * saturate(1.0 - vanillaY / 0.18), 0.0);
      float vanillaHueY = GetLuminance(vanillaHue);
      float3 vanillaTarget = vanillaHueY > 1e-6 ? vanillaHue * (gradedY / vanillaHueY) : gradedY.xxx;
      float3 sceneTarget = sceneY > 1e-6 ? lerp(vanillaTarget, vanillaScene * (gradedY / sceneY), smoothstep(0.0005, 0.003, sceneY)) : vanillaTarget;
      float3 shadowTarget = lerp(sceneTarget, vanillaTarget, smoothstep(0.009, 0.030, sceneY));
      hdrGraded = lerp(hdrGraded, shadowTarget, (1.0 - smoothstep(0.035, 0.14, vanillaY)) * restore);
    }
  }
#if _EED9AC77
  if (useLinearHaze && !noGrade && fade.a < 1.0)
#else
  if (useLinearHaze && fade.a < 1.0)
#endif
  {
    float3 hazed = ApplyDistanceHaze(hdrGraded, neutral, d, clamp(LumaSettings.GameSettings.DistanceHaze, 0.0, 2.0));
    hdrGraded = lerp(hazed, hdrGraded, fade.a);
  }
  // Luma bloom is added after the grade, linearly: added before it, anything pushed past white would go through the grade's steep
  // above-white extension (sqrt space, then squared) and blow highlights out.
  if (LumaSettings.GameSettings.LumaBloom != 0)
  {
    // s2 = linear clamp (s1 is the game's point sampler: blocky half-res bloom).
    hdrGraded += max(t4.Sample(s2, v2).rgb, 0.0) * LumaSettings.GameSettings.LumaBloomStrength * scale * (1.0 - fade.a);
  }
  {
    // Luminance grade around middle grey (0.18): contrast, then highlight/shadow pull.
    const float mid = 0.18;
    float Y = GetLuminance(hdrGraded);
    if (Y > 0.0)
    {
      float Yn = mid * pow(Y / mid, LumaSettings.GameSettings.Contrast);
      if (Yn > mid)
         Yn = mid * pow(Yn / mid, LumaSettings.GameSettings.Highlights);
      else
         Yn = mid * pow(Yn / mid, 1.0 / LumaSettings.GameSettings.Shadows);
      hdrGraded *= Yn / Y;
    }
    float Y2 = GetLuminance(hdrGraded);
    hdrGraded = Y2 + (hdrGraded - Y2) * LumaSettings.GameSettings.Saturation;
  }

  float Y = GetLuminance(hdrGraded);
  float w = saturate((Y - 1.0) / 3.0);
  hdrGraded = Y + (hdrGraded - Y) * lerp(1.0, LumaSettings.GameSettings.HighlightSaturation, w);

  // Highlight compression: soft-rolls graded luminance above SDR white (0 = off, 1 = ceiling 1.25x white).
  if (LumaSettings.GameSettings.TonemapVersion == 0 && LumaSettings.GameSettings.HighlightCompression > 0.0)
  {
    float Yhc = GetLuminance(hdrGraded);
    if (Yhc > 1.0)
    {
      float ehc = Yhc - 1.0;
      float Ycomp = 1.0 + ehc / (1.0 + ehc * LumaSettings.GameSettings.HighlightCompression * 4.0);
      hdrGraded *= Ycomp / Yhc;
    }
  }

  float3 outRgb = hdrGraded;
  o0 = float4(outRgb, dot(saturate(outRgb), lumaWeights));
#else
  float3 scene = saturate(t1.Sample(s1, v2).rgb * GetHMAExposure(cb4[41].z));
#if _EED9AC77
  float3 graded = noGrade ? lerp(scene, fade.rgb, fade.a) : LUTGrade(scene, fade, d);
#else
  float3 graded = LUTGrade(scene, fade, d);
#endif

  o0 = float4(graded, dot(graded, lumaWeights));
#endif
}
