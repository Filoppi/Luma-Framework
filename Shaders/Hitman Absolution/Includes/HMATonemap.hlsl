// Shared HDR-preserving tonemap helpers for the Hitman Absolution tonemap passes 0x46C50FC2 and 0xEED9AC77.
// The including file must declare t2 (LUT), s2, cb4 and LumaSettings first. When TONEMAP_TYPE >= 1 it must also include
// ../Includes/Tonemap.hlsl and Includes/HMAHaze.hlsl before this file.

#ifndef HMA_TONEMAP_HLSL
#define HMA_TONEMAP_HLSL

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

// Vanilla grading at full haze strength.
float3 LUTGrade(float3 scene, float4 fade, float depthCoord)
{
  return LUTGradeScaled(scene, fade, depthCoord, 1.0);
}

#if TONEMAP_TYPE >= 1
#define LUT_KNEE 0.75
#define LUT_MID 0.42

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
    // One shared growth rate for all channels: per-channel slopes differ (red is much steeper), which tints
    // bright highlights. Keeping the curve's color at white matches vanilla's clipped highlights.
    float3 s3 = max(slope, top);
    // Slope stays continuous at white (a hard cap reads as a kink). Any rate above 1 decays within ~0.25 (sqrt space)
    // into scene-linear growth, so lit surfaces in high-exposure interiors do not grow superlinearly. Light sources stay HDR.
    float rate = (s3.r + s3.g + s3.b) / max(f1.r + f1.g + f1.b, 1e-6);
    float t = x - 1.0;
    float grow = rate <= 1.0 ? rate * t : t + (rate - 1.0) * 0.25 * (1.0 - exp(-t / 0.25));
    f = f1 * (1.0 + grow);
  }
  // White normalization: scale so the curve's white reaches 1.
  float3 w1 = SampleCurve(v, 1.0);
  float wN = 1.0 / max(max(w1.r, max(w1.g, w1.b)), 0.5);
  f *= lerp(1.0, wN, smoothstep(LUT_KNEE, 1.0, x)); // only toward white: below LUT_KNEE the curve is unchanged
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

float SoftCubicRamp(float x) { return (1.0 - exp(-x * x * x)) / (1.0 - exp(-1.0)); } // ~x^3 near 0, 1 at x=1, approaching 1 / (1 - exp(-1)) (about 1.58) for large x

#endif  // HMA_TONEMAP_HLSL
