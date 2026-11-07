#include "Includes/Common.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Oklab.hlsl"

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

#include "../Includes/Reinhard.hlsl"

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
  float2 uv = w + (uvT - w) * lerp(1.0, rs / rt, amount);
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

float3 GrainLevels(float3 c, float3 grain, float3 add)
{
  float3 d = c * 2.0 - 1.0;
  return grain * (1.0 - abs(d)) + saturate(d) + add;
}

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

  float3 b = max(bloom, 0.0) * LumaSettings.GameSettings.BloomStrength * (LumaSettings.GameSettings.LumaBloom != 0 ? LumaSettings.GameSettings.VanillaGlareStrength : 1.0);
  float3 perChannel = pow(abs(x), 2.2) * sign(x);
  float3 lin2 = x * abs(x);
  float Y2 = GetLuminance(lin2);
  float3 byLum = Y2 > 0.0 ? lin2 * (pow(Y2, 1.1) / Y2) : lin2;
  float3 lin = lerp(perChannel, byLum, LumaSettings.GameSettings.GammaCorrectionByLuminance);
  lin += pow(b, 2.2);
  // Gamut is mapped into BT.2020 at constant OkLab L and h before expansion and once at the end.
  // Hue is recovered from the pre-expansion color.
  float3 hueRef = lin;
  lin = GamutMapToBT2020(lin);
  lin = ExpandGamut(lin, LumaSettings.GameSettings.GamutExpansion);
  float peak = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
  if (LumaSettings.GameSettings.DisplayMapping == 0u)
  {
    lin = Tonemap_DICE(lin, peak);
  }
  else
  {
    float shoulder = LumaSettings.GameSettings.ShoulderStart * peak;
    float Y = GetLuminance(lin);
    if (Y > 0.0)
    {
      float3 lumMapped = lin * (Reinhard::ReinhardPiecewise(Y, peak, shoulder) / Y);
      float3 ch = BT2020_To_BT709(Reinhard::ReinhardPiecewise(max(BT709_To_BT2020(lin), 0.0), peak, shoulder));
      lin = TransferPurityFP(lumMapped, ch, LumaSettings.GameSettings.HighlightPurity);
    }
  }
  // Channel cap in BT.2020: desaturate toward luminance until no channel exceeds the peak.
  float3 c2 = BT709_To_BT2020(lin);
  float m = max(c2.r, max(c2.g, c2.b));
  float Yl = GetLuminance(lin);
  if (m > peak && m > Yl)
    lin = Yl + (lin - Yl) * saturate((peak - Yl) / (m - Yl));
  if (LumaSettings.GameSettings.GamutExpansionHueLock > 0.0 && GetLuminance(hueRef) > 0.0)
    lin = RestoreHueAndChrominance(lin, hueRef, LumaSettings.GameSettings.GamutExpansionHueLock, 0.0, 0.0, FLT_MAX, 0.0, CS_DEFAULT);
  lin = GamutMapToBT2020(lin);
#if UI_DRAW_TYPE == 2 // The UI draws on top in gamma space; display composition scales everything by UI/game paper white, so pre-divide the scene
  lin *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif
  x = pow(abs(lin), 1.0 / 2.2) * sign(lin);
#else
  float grainMask = saturate(1.0 - mapTLW1.Sample(samplerTLW1, v1.zw).x * cb4[98].w);
  float3 grain = grainMask * v2;

  float3 c = sqrt(mapTLC0.Sample(samplerTLC0, v1.xy).rgb);
  float3 x = GrainLevels(c, grain, v3);
  x = x * (1.0 - bloom) + bloom;
#endif

  return float4(x, 0.0);
}
