#ifndef HMA_OUTPUT_HLSL
#define HMA_OUTPUT_HLSL

// Shared HDR output path for Hitman Absolution final/composite passes.
// Includers must include "Common.hlsl" (mod) first; Tonemap/Oklab are guarded and safe to repeat.
#include "../../Includes/Tonemap.hlsl"
#include "../../Includes/Oklab.hlsl"

// Largest OKLCH chroma at (L, h) whose linear BT.709 (or BT.2020) conversion has no negative channel.
float MaxChromaOklch(float L, float h, bool bt2020)
{
  float lo = 0.0;
  float hi = 0.5 * max(L, 1.0);
  for (int i = 0; i < 14; i++)
  {
    float mid = 0.5 * (lo + hi);
    float3 rgb = bt2020 ? Oklab::oklch_to_linear_bt2020(float3(L, mid, h)) : Oklab::oklch_to_linear_srgb(float3(L, mid, h));
    if (min(rgb.r, min(rgb.g, rgb.b)) >= 0.0) lo = mid;
    else hi = mid;
  }
  return lo;
}

// Expands already saturated colors up to the BT.2020 boundary (input and output linear BT.709).
float3 ExpandGamut(float3 c, float amount)
{
  amount = saturate(amount);
  float Y = GetLuminance(c);
  if (amount <= 0.0 || Y <= 0.0) return c;

  float3 lch = Oklab::linear_srgb_to_oklch(c); // L, C, h (radians)
  float L = lch.x;
  float C = lch.y;
  float h = lch.z;
  float c709 = MaxChromaOklch(L, h, false);
  float c2020 = max(MaxChromaOklch(L, h, true), c709);

  const float PI_ = 3.14159265358979;
  float dh = h - 50.0 * PI_ / 180.0;
  dh = atan2(sin(dh), cos(dh)); // wrap to [-pi, pi]
  float skin = 1.0 - 0.75 * saturate(1.0 - abs(dh) / (25.0 * PI_ / 180.0));

  float t = saturate(C / max(c709, 1e-6));
  float w = amount * smoothstep(0.5, 0.95, t) * skin;
  float Cn = min(C * (1.0 + 0.25 * w), max(0.98 * c2020, C)); // stay just inside BT.2020 (never reduce chroma here)

  float3 out2020 = Oklab::oklch_to_linear_bt2020(float3(L, Cn, h));
  return BT2020_To_BT709(max(out2020, 0.0));
}

// Maps a linear BT.709 color into BT.2020 at constant OkLab lightness and hue, reducing chroma only where needed.
float3 GamutMapToBT2020(float3 lin709)
{
  float Y = GetLuminance(lin709);
  if (Y <= 0.0) return 0.0;
  float3 c2 = BT709_To_BT2020(lin709);
  // Margin: colors at the exact boundary crossed it after fp16 storage and the gamma encode (AP0 in gamut views).
  if (min(c2.r, min(c2.g, c2.b)) >= 0.005 * Y) return lin709;
  float3 lch = Oklab::linear_srgb_to_oklch(lin709);
  lch.y = min(lch.y, 0.98 * MaxChromaOklch(lch.x, lch.z, true));
  return BT2020_To_BT709(max(Oklab::oklch_to_linear_bt2020(lch), 0.0));
}

// Identity below the knee, smoothly approaching peak while retaining highlight separation.
float ShoulderToPeak(float x, float peak, float s, float strength)
{
  if (x <= s) return x;
  float excess = x - s; float room = max(peak - s, 1e-4);
  return s + room * (1.0 - pow(1.0 + excess / (room * strength), -strength));
}

// Margin over the histogram p99.99 used as the source peak (grade, sky, bloom), in paper-white units.
#define HMA_EETF_HISTOGRAM_MARGIN 1.5
// Source peak as a multiple of the display peak when there is no histogram (exposedBrightPoint unset).
#define HMA_EETF_FALLBACK_SRC_PEAK_SCALE 8.0

// BT.2390 EETF (adaptive, PQ domain). x, srcPeak, dstPeak are in paper-white units; returns paper-white units.
float EETF_BT2390(float x, float srcPeak, float dstPeak)
{
  if (x <= 0.0) return x;
  float pw = LumaSettings.GamePaperWhiteNits;
  float srcPQ = Linear_to_PQ(max(srcPeak * pw / 10000.0, 1e-7), GCT_POSITIVE);
  float dstPQ = Linear_to_PQ(max(dstPeak * pw / 10000.0, 1e-7), GCT_POSITIVE);
  if (srcPQ <= dstPQ) return x;
  float e = Linear_to_PQ(x * pw / 10000.0, GCT_POSITIVE) / srcPQ;
  float maxLum = dstPQ / srcPQ;
  float KS = 1.5 * maxLum - 0.5;
  if (e > KS)
  {
    float t = (min(e, 1.0) - KS) / (1.0 - KS);
    float t2 = t * t; float t3 = t2 * t;
    e = (2.0 * t3 - 3.0 * t2 + 1.0) * KS + (t3 - 2.0 * t2 + t) * (1.0 - KS) + (-2.0 * t3 + 3.0 * t2) * maxLum;
  }
  float nits = PQ_to_Linear(e * srcPQ, GCT_POSITIVE) * 10000.0;
  return nits / pw;
}

// Keeps targetBT709's luminance and hue, and sets its CIE u'v' distance from white to the source's (scaled by amount).
float3 TransferPurityFP(float3 targetBT709, float3 sourceBT709, float amount)
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
  float2 uv = w + (uvT - w) * lerp(1.0, min(rs / rt, 1.0), saturate(amount));
  float Y = xyzT.y;
  float v = max(uv.y, 1e-7);
  float X = Y * 9.0 * uv.x / (4.0 * v);
  float Z = Y * (12.0 - 3.0 * uv.x - 20.0 * uv.y) / (4.0 * v);
  float3 c = mul(XYZ_2_BT709, float3(X, Y, Z));
  // Gamut compress: desaturate toward the same luminance until no channel is negative (keeps hue and luminance).
  // Bound to BT.2020 (not BT.709) so Gamut Expansion survives; desaturation toward gray is the same line in any RGB space.
  float3 c2 = BT709_To_BT2020(c);
  float minC = min(c2.r, min(c2.g, c2.b));
  return minC < 0.0 ? Y + (c - Y) * (Y / max(Y - minC, 1e-7)) : c;
}

float3 GrainLevels(float3 c, float3 grain, float3 add)
{
  float3 d = c * 2.0 - 1.0;
  return grain * (1.0 - abs(d)) + saturate(d) + add;
}

// Vanilla gamma-space signal to linear (per channel vs. luminance-weighted, blended by game setting).
float3 HMA_GammaToLinear(float3 x)
{
  float3 perChannel = pow(abs(x), 2.2) * sign(x);
  float3 lin2 = x * abs(x);
  float Y2 = GetLuminance(lin2);
  float3 byLum = Y2 > 0.0 ? lin2 * (pow(Y2, 1.1) / Y2) : lin2;
  return lerp(perChannel, byLum, LumaSettings.GameSettings.GammaCorrectionByLuminance);
}

// Blend a linear BT.709 flare into scene headroom relative to the display peak.
float3 HMA_AddFlareHeadroom(float3 sceneLin, float3 flare)
{
  float3 scene2020 = BT709_To_BT2020(sceneLin);
  float headroom = max(LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits - max(scene2020.r, max(scene2020.g, scene2020.b)), 0.0);
  float3 flare2020 = BT709_To_BT2020(flare);
  float flareMax = max(flare2020.r, max(flare2020.g, flare2020.b));
  float flareScale = flareMax > 0.0 ? headroom * (1.0 - exp(-flareMax / max(headroom, 1e-4))) / flareMax : 0.0;
  return sceneLin + flare * flareScale;
}

// Linear scene (1 = paper white, flare already added) to the encoded swapchain value.
float3 HMA_DisplayOutput(float3 lin, float exposedBrightPoint = 0.0)
{
  // Hue reference is taken after the flare is added so the hue lock keeps the flare tint and ghost chroma.
  float3 hueRef = lin;
  lin = GamutMapToBT2020(lin);
  lin = ExpandGamut(lin, LumaSettings.GameSettings.GamutExpansion);
  float peak = max(LumaSettings.PeakWhiteNits / max(LumaSettings.GamePaperWhiteNits, 1e-4), 1e-4);
  if (LumaSettings.GameSettings.DisplayMapping == 0u)
  {
    lin = Tonemap_DICE(lin, peak);
  }
  else if (LumaSettings.GameSettings.DisplayMapping == 1u)
  {
    float shoulder = LumaSettings.GameSettings.ShoulderStart * peak;
    float strength = 0.45;
    if (exposedBrightPoint > 0.0 && exposedBrightPoint < FLT_MAX)
      strength = lerp(0.45, 0.18, smoothstep(2.0, 8.0, log2(max(exposedBrightPoint / peak, 1.0))));
    float Y = GetLuminance(lin);
    if (Y > 0.0)
    {
      float3 lumMapped = lin * (ShoulderToPeak(Y, peak, shoulder, strength) / Y);
      float3 ch2020 = max(BT709_To_BT2020(lin), 0.0);
      float3 ch = BT2020_To_BT709(float3(ShoulderToPeak(ch2020.r, peak, shoulder, strength), ShoulderToPeak(ch2020.g, peak, shoulder, strength), ShoulderToPeak(ch2020.b, peak, shoulder, strength)));
      lin = TransferPurityFP(lumMapped, ch, LumaSettings.GameSettings.HighlightPurity);
    }
  }
  else
  {
    float srcPeak = (exposedBrightPoint > 0.0 && exposedBrightPoint < FLT_MAX) ? exposedBrightPoint * HMA_EETF_HISTOGRAM_MARGIN : peak * HMA_EETF_FALLBACK_SRC_PEAK_SCALE;
    srcPeak = max(srcPeak, peak * 1.01);
    float Y = GetLuminance(lin);
    if (Y > 0.0)
    {
      float3 lumMapped = lin * (EETF_BT2390(Y, srcPeak, peak) / Y);
      float3 ch2020 = max(BT709_To_BT2020(lin), 0.0);
      float3 ch = BT2020_To_BT709(float3(EETF_BT2390(ch2020.r, srcPeak, peak), EETF_BT2390(ch2020.g, srcPeak, peak), EETF_BT2390(ch2020.b, srcPeak, peak)));
      lin = TransferPurityFP(lumMapped, ch, LumaSettings.GameSettings.HighlightPurity);
    }
  }
  if (LumaSettings.GameSettings.GamutExpansionHueLock > 0.0 && GetLuminance(hueRef) > 0.0)
    lin = RestoreHueAndChrominance(lin, hueRef, LumaSettings.GameSettings.GamutExpansionHueLock, 0.0, 0.0, FLT_MAX, 0.0, CS_DEFAULT);
  lin = GamutMapToBT2020(lin);
  // Channel cap in BT.2020: soft knee toward luminance so the cap is gentle and runs last.
  float3 c2 = BT709_To_BT2020(lin);
  float m = max(c2.r, max(c2.g, c2.b));
  float Yl = GetLuminance(lin);
  float knee = max(0.75 * peak, Yl);
  if (m > knee && m > Yl)
  {
    float e = m - knee; float room = max(peak - knee, 1e-4);
    float mT = knee + e / (1.0 + e / room);
    lin = Yl + (lin - Yl) * saturate((mT - Yl) / (m - Yl));
  }
#if UI_DRAW_TYPE == 2 // The UI draws on top in gamma space; display composition scales everything by UI/game paper white, so pre-divide the scene
  lin *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif
  return pow(abs(lin), 1.0 / 2.2) * sign(lin);
}

#endif // HMA_OUTPUT_HLSL
