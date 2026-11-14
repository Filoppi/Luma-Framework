// Luma: HDR-preserving replacement for the tonemap/color grading pass 0x46C50FC2.
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 the scene is not clamped to SDR:
// the LUT curves are extended above 1 and sampled per channel (with optional hue preservation), then user grading and highlight saturation are applied.

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

// Vanilla grading on an SDR-range scene color. depthCoord is the linearized depth remapped to [0,1].
// hazeScale multiplies LUT row 6 (distance haze blend): 0 = near curve only, 1 = full haze.
float3 LUTGradeScaled(float3 scene, float4 fade, float depthCoord, float hazeScale)
{
  float3 faded = lerp(scene, fade.rgb, fade.a);
  float3 q = sqrt(faded);

  float4 s = t2.Sample(s2, float2(depthCoord, 0.928570)) * hazeScale;
  float3 c = float3(s.x * 0.142857 + 0.071429,
                    s.z * 0.142857 + 0.642857,
                    s.y * 0.142857 + 0.357143);

  float4 p1 = t2.Sample(s2, float2(q.r, c.x));
  float4 p2 = t2.Sample(s2, float2(q.g, c.z));
  float4 p3 = t2.Sample(s2, float2(q.b, c.y));

  float3 sum = (p1.rgb + p2.rgb) + p3.rgb;
  return sum * sum;
}

float3 LUTGrade(float3 scene, float4 fade, float depthCoord)
{
  return LUTGradeScaled(scene, fade, depthCoord, 1.0);
}

#if TONEMAP_TYPE >= 1
#define LUT_KNEE 0.75
#define LUT_MID 0.42

// Catmull-Rom sample of one LUT row at x (sqrt space, clamped to [0,1]).


// Curve value at xc (<= 1), with the highlight rolloff blend and the linear extension above the knee.
float3 CurveAt(float v, float xc, float3 fk, float3 slope)
{
  float3 f = SampleCurve(v, xc);
  if (xc > LUT_KNEE)
  {
    float3 knee_ext = fk + slope * (xc - LUT_KNEE);
    f = lerp(max(f, knee_ext), f, LumaSettings.GameSettings.HighlightRolloff);
  }
  return f;
}

// One channel's grade curve in sqrt space. x may be > 1 (HDR).
float3 GradeCurve(float v, float x)
{
  float3 f0 = SampleCurve(v, 0.0);
  float3 fk = SampleCurve(v, LUT_KNEE);
  float3 fa = SampleCurve(v, LUT_KNEE - 0.25);
  float3 slope = (fk - fa) / 0.25;
  float xc = min(x, 1.0);
  float3 f = CurveAt(v, xc, fk, slope);
  if (x > 1.0)
  {
    float3 f1 = CurveAt(v, 1.0, fk, slope);
    float3 top = (SampleCurve(v, 1.0) - SampleCurve(v, 1.0 - 1.0 / 255.0)) * 255.0;
    // One shared growth rate for all channels: per-channel slopes differ a lot (red is much steeper), which tinted
    // bright highlights pink. Keeping the curve's color at white matches vanilla's clipped highlights.
    float3 s3 = max(slope, top);
    // Slope stays continuous at white (a hard cap made a kink that read as clipping), but any rate above 1 decays within
    // ~0.25 (sqrt space) into scene-linear growth: the LUT top slope reached ~2.2, which blew lit walls up superlinearly
    // (2x white -> 3.6x, 4x -> 10x) in high-exposure interiors. Light sources stay HDR.
    float rate = (s3.r + s3.g + s3.b) / max(f1.r + f1.g + f1.b, 1e-6);
    float t = x - 1.0;
    float grow = rate <= 1.0 ? rate * t : t + (rate - 1.0) * 0.25 * (1.0 - exp(-t / 0.25));
    f = f1 * (1.0 + grow);
  }
  // White normalization: scale so the curve's white reaches 1.
  float3 w1 = SampleCurve(v, 1.0);
  float wN = 1.0 / max(max(w1.r, max(w1.g, w1.b)), 0.5);
  f *= lerp(1.0, wN, smoothstep(LUT_KNEE, 1.0, x)); // only toward white: applied everywhere it lifted midtones ~10-20% over vanilla
  // Black floor removal, fading out toward mid gray.
  f -= f0 * saturate(1.0 - x / LUT_MID) * LumaSettings.GameSettings.BlackFloorRemoval;
  return f;
}

// Per-channel HDR grade of a linear HDR color. d is the depth coordinate.
float3 GradePerChannel(float3 hdr, float d)
{
  float3 q = sqrt(max(hdr, 0.0));
  float4 s = t2.SampleLevel(s2, float2(d, 0.928570), 0) * saturate(LumaSettings.GameSettings.DistanceHaze);
  float3 c = float3(s.x * 0.142857 + 0.071429,
                    s.z * 0.142857 + 0.642857,
                    s.y * 0.142857 + 0.357143);
  float3 sum = GradeCurve(c.x, q.r) + GradeCurve(c.z, q.g) + GradeCurve(c.y, q.b);
  return sum * sum;
}

#define NEUTRAL_SHOULDER 0.5

// Identity below NEUTRAL_SHOULDER, slope-continuous rolloff above it; max channel stays below 1.
float3 NeutralCompress(float3 c)
{
  c = max(c, 0.0);
  float S = NEUTRAL_SHOULDER;
  float m = max(c.r, max(c.g, c.b));
  if (m <= S) return c;
  float mc = S + (1.0 - S) * (m - S) / ((m - S) + (1.0 - S));
  return c * (mc / m);
}

// LUTGrade without the fade lerp, with the depth row scaled by DistanceHaze (as GradePerChannel).
// hazeScale replaces the DistanceHaze slider here, so callers can drop the depth haze (pass 0.0 for the near curve's floor only).
float3 LUTGradeNeutral(float3 x, float d, float hazeScale)
{
  float3 q = sqrt(max(x, 0.0));

  float4 s = t2.Sample(s2, float2(d, 0.928570)) * hazeScale;
  float3 c = float3(s.x * 0.142857 + 0.071429,
                    s.z * 0.142857 + 0.642857,
                    s.y * 0.142857 + 0.357143);

  float4 p1 = t2.Sample(s2, float2(q.r, c.x));
  float4 p2 = t2.Sample(s2, float2(q.g, c.z));
  float4 p3 = t2.Sample(s2, float2(q.b, c.y));

  float3 sum = (p1.rgb + p2.rgb) + p3.rgb;
  return sum * sum;
}

// Keeps targetBT709's luminance and hue, and sets its CIE u'v' distance from white to the source's (scaled by amount).
float3 TransferPurity(float3 targetBT709, float3 sourceBT709, float amount)
{
  float3 xyzT = mul(BT709_2_XYZ, targetBT709);
  float3 xyzS = mul(BT709_2_XYZ, sourceBT709);
  float denT = max(xyzT.x + 15.0 * xyzT.y + 3.0 * xyzT.z, 1e-7);
  float denS = max(xyzS.x + 15.0 * xyzS.y + 3.0 * xyzS.z, 1e-7);
  float2 uvT = float2(4.0 * xyzT.x, 9.0 * xyzT.y) / denT;
  float2 uvS = float2(4.0 * xyzS.x, 9.0 * xyzS.y) / denS;
  float2 w = float2(0.1978, 0.4683);
  float rt = length(uvT - w);
  float rs = length(uvS - w);
  if (rt < 1e-6 || xyzT.y <= 0.0)
    return targetBT709;
  float2 uv = w + (uvT - w) * lerp(1.0, rs / rt, amount);
  float Y = xyzT.y;
  float v = max(uv.y, 1e-7);
  float X = Y * 9.0 * uv.x / (4.0 * v);
  float Z = Y * (12.0 - 3.0 * uv.x - 20.0 * uv.y) / (4.0 * v);
  float3 c = mul(XYZ_2_BT709, float3(X, Y, Z));
  // Gamut compress: desaturate toward the same luminance until no channel is negative (keeps hue and luminance).
  float minC = min(c.r, min(c.g, c.b));
  return minC < 0.0 ? Y + (c - Y) * (Y / max(Y - minC, 1e-7)) : c;
}

// HDR grade with optional hue preservation in bright highlights.
float3 LUTGradeHDR(float3 hdr, float d)
{
  float3 A = GradePerChannel(hdr, d);
  float m = max(hdr.r, max(hdr.g, hdr.b));
  float kl = LUT_KNEE * LUT_KNEE;
  // RenoDX-style hue preserving grade: luminance and tint from the luminance path, purity from the per-channel grade A.
  float Yin = GetLuminance(hdr);
  if (Yin > 0.0 && LumaSettings.GameSettings.ShadowLuminanceGrading > 0.0)
  {
    float3 G = GradePerChannel(Yin.xxx, d);
    float3 L = hdr * (G / Yin);
    float3 P = TransferPurity(L, A, 1.0);
    A = lerp(A, P, LumaSettings.GameSettings.ShadowLuminanceGrading);
  }
  float3 Ck = GradePerChannel(hdr * min(1.0, kl / max(m, 1e-6)), d);
  float3 B = Ck * (GetLuminance(A) / max(GetLuminance(Ck), 1e-6));
  float w = smoothstep(kl, 1.0, m) * LumaSettings.GameSettings.HighlightHuePreservation;
  return lerp(A, B, w);
}
#endif

float SoftCubicRamp(float x) { return (1.0 - exp(-x * x * x)) / (1.0 - exp(-1.0)); } // ~x^3 near 0, 1 at x=1, smooth to 1.58

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float depth = t0.Sample(s0, v2).x;
  float d = depth * cb4[8].z - cb4[8].y;
  d = -cb4[8].x / d;
  d = saturate(d * cb4[101].y + cb4[101].x);

  float4 fade = t3.Sample(s3, v2);
  float3 lumaWeights = float3(0.299, 0.587, 0.114);

#if TONEMAP_TYPE >= 1
  float scale = GetHMAExposure(cb4[41].z);
  float3 hdr = max(t1.Sample(s1, v2).rgb, 0.0) * scale;
  hdr *= LumaSettings.GameSettings.Exposure;
  // Sun: it's baked into the sky dome/cloud textures, so boost sky pixels (raw depth: sky dome ~0.9999945, far terrain <= ~0.99999)
  // that are bright relative to exposure (measured: sun haze ~0.5, open sky ~0.18 after exposure).
  // Ramp must end below the dome's depth (seen 0.999992-0.9999945): inside it, float depth steps drew stepped contour lines.
  float skyMask = smoothstep(0.99999, 0.9999915, depth);
  // The boost is applied after the LUT grade: boosting before it pushed the sun core past the LUT top/knee kinks (contour lines).
  // The sun scales like the rest of the sky: the old SunBrightness per-sun knob was multiplied by a hard zero and has been removed with the setting.
  float skyBoost = 1.0;
  // Sky HDR expansion (moved here from Sky_0xACE3BFA3 so the auto-exposure meter sees the vanilla sky). Same curve; the sky
  // texture luminance is estimated from the sky's pre-exposure brightness (sun center ~0.5 at ~0.47 exposure = 1.06 raw),
  // so dim-exposure scenes still get the lift (same as Tonemap_0xEED9AC77).
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
    hdrGraded = LUTGradeHDR(hdr, d) * lerp(skyBoost, 1.0, fade.a);
    // Pull hue and chrominance toward the vanilla SDR grade of the same pixel (VanillaHueRestore, same as Tonemap_0xEED9AC77).
    float3 sdrScene = saturate(t1.Sample(s1, v2).rgb * GetHMAExposure(cb4[41].z) * LumaSettings.GameSettings.Exposure);
    float3 sdrRef = LUTGradeScaled(sdrScene, fade, d, curveHaze);
    if (restore > 0.0)
      hdrGraded = RestoreHueAndChrominance(hdrGraded, sdrRef, 0.75 * restore, restore);
  }
  else
  {
    float3 neutralGraded = LUTGradeNeutral(neutral, d, curveHaze);
    // Floor uses the near curve only (no haze), so the depth haze veil isn't subtracted from shadows.
    float3 floorC = LUTGradeNeutral(0.0, d, 0.0);
    neutralGraded = max(neutralGraded - floorC * saturate(1.0 - GetLuminance(neutral) / 0.18) * LumaSettings.GameSettings.BlackFloorRemoval, 0.0);
    hdrGraded = UpgradeToneMap(hdr, neutral, neutralGraded, 1.0) * lerp(skyBoost, 1.0, fade.a);
    if (restore > 0.0)
      hdrGraded = RestoreHueAndChrominance(hdrGraded, neutralGraded * lerp(skyBoost, 1.0, fade.a), 0.75 * restore, restore);
  }
  if (restore > 0.0)
  {
    // Linear haze uses haze-free shadow references; the legacy curve model follows the saturated slider.
    float3 vanillaScene = saturate(t1.Sample(s1, v2).rgb * GetHMAExposure(cb4[41].z) * LumaSettings.GameSettings.Exposure);
    float3 vanillaRef = LUTGradeScaled(vanillaScene, fade, d, curveHaze);
    float vanillaY = GetLuminance(vanillaRef);
    float gradedY = GetLuminance(hdrGraded);
    if (vanillaY > 1e-6 && gradedY > 0.0)
    {
      float sceneY = GetLuminance(vanillaScene);
      // Near true black the raw scene hue is noise (e.g. pure green 1e-5 pixels drew green speckles): fade to vanilla's graded hue.
      // The LUT adds a coloured black floor (blue in some maps): strip it (faded out by mid grey, like BlackFloorRemoval) so its hue
      // doesn't tint shadows once they are scaled to the brighter HDR level.
      float3 vanillaHue = max(vanillaRef - LUTGradeScaled(0.0, fade, d, 0.0) * saturate(1.0 - vanillaY / 0.18), 0.0);
      float vanillaHueY = GetLuminance(vanillaHue);
      float3 vanillaTarget = vanillaHueY > 1e-6 ? vanillaHue * (gradedY / vanillaHueY) : gradedY.xxx;
      float3 sceneTarget = sceneY > 1e-6 ? lerp(vanillaTarget, vanillaScene * (gradedY / sceneY), smoothstep(0.0005, 0.003, sceneY)) : vanillaTarget;
      float3 shadowTarget = lerp(sceneTarget, vanillaTarget, smoothstep(0.009, 0.030, sceneY));
      hdrGraded = lerp(hdrGraded, shadowTarget, (1.0 - smoothstep(0.035, 0.14, vanillaY)) * restore);
    }
  }
  if (useLinearHaze && fade.a < 1.0)
  {
    float3 hazed = ApplyDistanceHaze(hdrGraded, neutral, d, clamp(LumaSettings.GameSettings.DistanceHaze, 0.0, 2.0));
    hdrGraded = lerp(hazed, hdrGraded, fade.a);
  }
  // Luma bloom is added after the grade, linearly: added before it, anything pushed past white went through the grade's steep
  // above-white extension (sqrt space, then squared) and blew highlights out (~5x at p99).
  if (LumaSettings.GameSettings.LumaBloom != 0)
  {
    hdrGraded += max(t4.Sample(s2, v2).rgb, 0.0) /* s2 = linear clamp (s1 is the game's point sampler: blocky half-res bloom) */ * LumaSettings.GameSettings.LumaBloomStrength * scale * (1.0 - fade.a);
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

  float3 graded = LUTGrade(scene, fade, d);

  o0 = float4(graded, dot(graded, lumaWeights));
#endif
}
