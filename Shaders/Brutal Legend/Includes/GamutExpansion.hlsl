#ifndef LUMA_BRUTAL_LEGEND_GAMUT_EXPANSION_HLSL
#define LUMA_BRUTAL_LEGEND_GAMUT_EXPANSION_HLSL

#include "../../Includes/Oklab.hlsl"

#ifndef GAMUT_EXPANSION_POWER
#define GAMUT_EXPANSION_POWER 2.0
#endif

#ifndef GAMUT_EXPANSION_KNEE
#define GAMUT_EXPANSION_KNEE 0.9
#endif

float Min3GE(float3 v)
{
  return min(v.x, min(v.y, v.z));
}

// Largest chroma ratio s = C / L that stays inside the RGB gamut (BT.709 or BT.2020) at hue h.
// Oklab is a cube root space, so the gamut boundary in s depends only on h.
float MaxChromaRatio(float h, bool bt2020)
{
  float lo = 0.0;
  float hi = 1.5;
  [unroll]
  for (int i = 0; i < 14; i++)
  {
    float mid = 0.5 * (lo + hi);
    float3 rgb = bt2020 ? Oklab::oklch_to_linear_bt2020(float3(1.0, mid, h))
                        : Oklab::oklch_to_linear_srgb(float3(1.0, mid, h));
    if (Min3GE(rgb) >= 0.0)
      lo = mid;
    else
      hi = mid;
  }
  return lo;
}

// Identity below knee*sMax, then smoothly asymptotes to sMax (C1 continuous at the knee).
float SoftKneeChroma(float s, float sMax, float knee)
{
  if (s <= knee * sMax)
    return s;
  float k = knee * sMax;
  float r = sMax - k;
  float x = s - k;
  return k + r * x / (x + r);
}

// Uniformly scales the color down (keeps hue and ratios) so its largest channel does not exceed peak.
float3 ClampByMaxChannel(float3 x, float peak)
{
  float m = max(x.r, max(x.g, x.b));
  return (m > peak) ? x * (peak / m) : x;
}

// Expands a linear BT.709 color to linear BT.2020 by scaling its chroma at constant hue and lightness.
// amount 0 only soft-caps to the BT.2020 gamut.
float3 ExpandGamutBT709ToBT2020(float3 lin709, float amount)
{
  float3 lch = Oklab::linear_srgb_to_oklch(lin709);
  if (lch.x <= 1e-6 || lch.y <= 1e-6)
    return BT709_To_BT2020(lin709);

  float L = lch.x;
  float h = lch.z;
  float s = lch.y / L;
  float s709 = MaxChromaRatio(h, false);
  float s2020 = MaxChromaRatio(h, true);
  float t = saturate(s / max(s709, 1e-6));
  float sExp = s + (s2020 - s709) * amount * pow(t, GAMUT_EXPANSION_POWER);
  float sOut = SoftKneeChroma(sExp, s2020, GAMUT_EXPANSION_KNEE);
  return Oklab::oklch_to_linear_bt2020(float3(L, sOut * L, h));
}

#endif
