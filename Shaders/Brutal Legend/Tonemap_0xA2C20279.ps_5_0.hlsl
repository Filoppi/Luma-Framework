#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "Includes/GamutExpansion.hlsl"
#include "Includes/FilmGrain.hlsl"
#if TONEMAP_TYPE >= 1
#include "Includes/PsychoV35.hlsl"
#include "../Includes/MacLeodBoynton.hlsl"
#endif

Texture2D<float4> t2 : register(t2); // Noise / dither
Texture2D<float4> t3 : register(t3); // Scene
Texture2D<float4> t4 : register(t4); // Bloom
#if TONEMAP_TYPE >= 1
Texture2D<float> t8 : register(t8); // Scene depth (1.0 = far plane / sky), bound only when SkyDepthActive > 0.5
Texture2D<float4> t10 : register(t10); // Luma bloom (half res, linear), bound only with LumaBloomEnable

#ifndef SKY_DEPTH_THRESHOLD
#define SKY_DEPTH_THRESHOLD 0.99999
#endif
#ifndef FOG_KEEP_FADE_END
#define FOG_KEEP_FADE_END 0.7 // Fog Keeps Surface Colour fades out linearly to 0 at this fog amount (thick fog keeps the vanilla fog colour; the un-fogged reconstruction is unreliable there)
#endif
#ifndef SKY_HUE_RESTORE_SCALE
#define SKY_HUE_RESTORE_SCALE 0.5 // Hue restore strength on sky pixels, relative to VanillaHueRestore
#endif
#endif

SamplerState s2_s : register(s2);
SamplerState s3_s : register(s3);
SamplerState s4_s : register(s4);

cbuffer cb4 : register(b4)
{
  float4 cb4[236];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[77];
}

float4 ApplyMask(float4 v, float4 andMask, float4 orMask)
{
  return asfloat((asuint(v) & asuint(andMask)) | asuint(orMask));
}

// Vanilla per channel pow: exp2(log2(|x|) * e), with the DXBC -inf guard
float VanillaPowChannel(float x, float e)
{
  float l = log2(abs(x));
  l = (asuint(l) == 0xff800000u) ? -9999999933815812510711506376257961984.0 : l;
  return exp2(l * e);
}

float3 SignPow(float3 x, float e)
{
  return pow(abs(x), e) * sign(x);
}

// Vanilla alpha-driven bloom blend, colour matrix and grade offset. x = scene*cb4[116].x + bloom
float3 GradePre(float3 x, float4 bloomS)
{
  float2 bt = bloomS.w + float2(-0.65, -0.92);
  float2 bw = saturate(float2(6.0, 10.0) * bt + float2(0.65, 0.92));
  x *= 1.0 - bw.x * 0.4;
  x *= 1.0 - bw.y;

  float3 e = x * cb4[117].x;
  float blendW = saturate(dot(cb4[120].xyz, e));
  float3 m = e.x * cb4[16].xyz + e.y * cb4[17].xyz + e.z * cb4[18].xyz;
  float3 offs = m + cb4[19].xyz;
  return blendW * (x * cb4[117].x - offs) + offs; // Pre-clamp colour
}

#if TONEMAP_TYPE >= 1
#ifndef BLACK_FLOOR_TOE_RANGE
#define BLACK_FLOOR_TOE_RANGE 0.15 // gamma-space range the toe acts on
#endif
// Cubic toe: 0->0, t->t, slope 1 at t, slope a at 0. Monotonic.
float BlackFloorToe(float x, float strength)
{
  float t = BLACK_FLOOR_TOE_RANGE;
  if (x >= t || strength <= 0.0)
    return x;
  float u = max(x, 0.0) / t;
  float a = 1.0 - saturate(strength);
  return t * (a * u + 2.0 * (1.0 - a) * u * u + (a - 1.0) * u * u * u);
}

// Black Floor max-channel toe
float3 ApplyBlackFloor(float3 c)
{
  float cMax = max(max(c.r, c.g), c.b);
  if (cMax > 0)
    c *= BlackFloorToe(cMax, LumaSettings.GameSettings.BlackFloor) / cMax;
  return c;
}
#endif

#if TONEMAP_TYPE >= 1
cbuffer FogVSCB : register(b6)
{
  float4 fogVS[32];
}

cbuffer FogPSCB : register(b7)
{
  float4 fogPS[119];
}

float Fog_Det3(float3x3 m)
{
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
       - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
       + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// General 4x4 inverse (cofactor / adjugate method)
float4x4 Fog_Invert4x4(float4x4 m)
{
  float4x4 r = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
  float det = 0.0;
  for (int i = 0; i < 4; i++)
  {
    for (int j = 0; j < 4; j++)
    {
      float3x3 mnr = float3x3(0, 0, 0, 0, 0, 0, 0, 0, 0);
      int mi = 0;
      for (int ri = 0; ri < 4; ri++)
      {
        if (ri == i) continue;
        int mj = 0;
        for (int cj = 0; cj < 4; cj++)
        {
          if (cj == j) continue;
          mnr[mi][mj] = m[ri][cj];
          mj++;
        }
        mi++;
      }
      float cof = (((i + j) & 1) != 0 ? -1.0 : 1.0) * Fog_Det3(mnr);
      r[j][i] = cof; // adjugate = transposed cofactors
      if (i == 0) det += m[0][j] * cof;
    }
  }
  float id = (abs(det) > 1e-20) ? (1.0 / det) : 0.0;
  return r * id;
}

// Reconstructs the game's fog amount f (0 = clear, 1 = fully fogged) at uv from the scene depth. Outputs the clamped view distance and height term.
float FogReconstruct(float2 uv, float depth, out float dmin, out float h)
{
  float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
  float4x4 M = float4x4(fogVS[24], fogVS[25], fogVS[26], fogVS[27]);
  float4x4 invM = Fog_Invert4x4(M);
  float4 rh = mul(float4(ndc, depth, 1.0), invM);
  float3 rel = rh.xyz / rh.w;

  dmin = min(length(rel), fogVS[30].y);
  float f0 = 1.0 - min(exp((fogVS[30].x - dmin) * fogVS[30].w), 1.0);
  float xh = saturate((fogVS[31].y - (rel.y + fogVS[28].y)) * fogVS[31].z);
  h = lerp(fogVS[31].w, 1.0, xh);
  return f0 * h;
}

// Fog amount at uv for non-sky pixels. valid is false when the fog constants or the scene depth are not bound, or on sky.
float ComputeFogAmount(float2 uv, out bool valid)
{
  valid = false;
  if (LumaSettings.GameSettings.FogDataActive <= 0.5 || LumaSettings.GameSettings.SkyDepthActive <= 0.5)
    return 0.0;
  float depth = t8.SampleLevel(s3_s, uv, 0);
  if (depth >= SKY_DEPTH_THRESHOLD)
    return 0.0;
  valid = true;
  float dminU, hU;
  return FogReconstruct(uv, depth, dminU, hU);
}

// Shrinks the part of a pixel above SDR white in proportion to its fog amount, so fogged highlights don't glow through the fog.
float3 ApplyFogHighlightLimit(float3 sceneG, float2 uv)
{
  if (LumaSettings.GameSettings.FogHighlightLimit <= 0.5)
    return sceneG;
  bool valid;
  float fLim = ComputeFogAmount(uv, valid);
  if (!valid)
    return sceneG;
  float m = max(sceneG.r, max(sceneG.g, sceneG.b));
  if (m > 1.0)
  {
    float m2 = 1.0 + (m - 1.0) * (1.0 - fLim);
    sceneG *= m2 / m;
  }
  return sceneG;
}

// Re-applies the game's baked fog with the LumaSettings fog overrides. Returns sceneG when no override is set.
float3 AdjustFog(float3 sceneG, float2 uv, float skyMask)
{
  if (LumaSettings.GameSettings.FogDataActive <= 0.5 || LumaSettings.GameSettings.SkyDepthActive <= 0.5 || skyMask >= 0.5)
    return sceneG;
  float fogStartS = LumaSettings.GameSettings.FogStart;
  float fogDensityS = LumaSettings.GameSettings.FogDensity;
  float fogSatS = LumaSettings.GameSettings.FogColorSaturation;
  float fogBrightS = LumaSettings.GameSettings.FogColorBrightness;
  float fogKeepS = LumaSettings.GameSettings.FogKeepColor;
  if (fogStartS == 0.0 && fogDensityS == 1.0 && fogSatS == 1.0 && fogBrightS == 1.0 && fogKeepS == 0.0)
    return sceneG;

  float depth = t8.SampleLevel(s3_s, uv, 0);
  float dmin;
  float h;
  float f = FogReconstruct(uv, depth, dmin, h);
  if (f >= 0.95)
    return sceneG;

  float k = fogPS[116].y;
  if (k <= 0.0)
    return sceneG;
  float3 F = fogPS[118].rgb;
  float3 lit = (sceneG / k - F * f) / (1.0 - f);

  float start2 = fogVS[30].x + fogStartS;
  float dens2 = fogVS[30].w * fogDensityS;
  float f2 = (1.0 - min(exp((start2 - dmin) * dens2), 1.0)) * h;
  f2 = min(f2, f);

  float lum = dot(F, float3(0.2126, 0.7152, 0.0722));
  float3 F2 = lerp(float3(lum, lum, lum), F, fogSatS) * fogBrightS;
  float3 outG = lerp(lit, F2, f2);

  if (fogKeepS > 0.0)
  {
    float3 outLin = gamma_to_linear(outG, GCT_MIRROR);
    float3 litLin = gamma_to_linear(max(lit, 0.0), GCT_MIRROR);
    // Trust the un-fogged reconstruction only where all channels stay positive, and fade it out in thick fog, where 1/(1-f) amplifies errors.
    float trust = saturate(min(lit.r, min(lit.g, lit.b)) * 20.0 + 1.0) * saturate((FOG_KEEP_FADE_END - f) / FOG_KEEP_FADE_END);
    // Restores both hue and chroma from the surface colour. Chroma alone would be taken at the fogged hue, which shifts the colour.
    float keepW = fogKeepS * f2 * trust;
    outLin = RestoreHueAndChrominance(outLin, litLin, keepW, keepW);
    outG = linear_to_gamma(outLin, GCT_MIRROR);
  }
  return outG * k;
}
#endif

// Applies a screen-flash tint (cb4[119]): the vanilla clipped result, or the HDR result where bright pixels keep their brightness and take the tint hue, whichever is higher.
// The HDR tint is divided by its largest channel when that is above 1.
float3 FlashTint(float3 x, float3 tint)
{
  float3 vanillaFlash = min(min(x, 1.0) * tint, 1.0);
  float3 hdrFlash = x * (tint / max(max(tint.r, max(tint.g, tint.b)), 1.0));
  return max(vanillaFlash, hdrFlash);
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD8,
  float4 v2 : COLOR0,
  float4 v3 : COLOR1,
  float4 v4 : TEXCOORD9,
  float4 v5 : TEXCOORD0,
  float4 v6 : TEXCOORD1,
  float4 v7 : TEXCOORD2,
  float4 v8 : TEXCOORD3,
  float4 v9 : TEXCOORD4,
  float4 v10 : TEXCOORD5,
  float4 v11 : TEXCOORD6,
  float4 v12 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
#if TONEMAP_TYPE >= 1
  // True SDR uses the game's original grade, clipping, dither and alpha.
  if (LumaSettings.DisplayMode == 0)
  {
    float4 vanillaScene = ApplyMask(t3.Sample(s3_s, v5.xy), cb3[50], cb3[51]);
    float4 vanillaBloom = ApplyMask(t4.Sample(s4_s, v6.xy), cb3[52], cb3[53]);
    float3 vanillaPre = GradePre(vanillaScene.xyz * cb4[116].x + vanillaBloom.xyz, vanillaBloom);
    float2 vanillaNuv = (v5.xy + cb4[121].zw) * cb4[121].xy;
    float3 vanillaNoise = (ApplyMask(t2.Sample(s2_s, vanillaNuv), cb3[48], cb3[49]).xyz * 2.0 - 1.0) * cb4[118].x;
    float3 g = min(max(vanillaPre, cb4[122].x), cb4[122].y) * cb4[119].xyz + vanillaNoise;
    float3 o = float3(VanillaPowChannel(g.x, cb4[123].x), VanillaPowChannel(g.y, cb4[123].x), VanillaPowChannel(g.z, cb4[123].x));
    float rs = rsqrt(abs(dot(o, float3(0.3, 0.59, 0.11))));
    rs = (asuint(rs) == 0x7f800000u) ? 9999999933815812510711506376257961984.0 : rs;
    o0 = float4(o, (0.0 < abs(rs)) ? (1.0 / rs) : 9999999933815812510711506376257961984.0);
    return;
  }
  float skyMask = 0;
  if (LumaSettings.GameSettings.SkyDepthActive > 0.5)
    skyMask = t8.SampleLevel(s3_s, v5.xy, 0) >= SKY_DEPTH_THRESHOLD ? 1.0 : 0.0;
#endif

  // Scene and bloom
  float4 sceneS = ApplyMask(t3.Sample(s3_s, v5.xy), cb3[50], cb3[51]);
#if TONEMAP_TYPE >= 1
  // Additive emissives can overflow FP16 (max 65504) in the gamma-space scene. Hue-preserving cap at 16, above the brightest real content.
  sceneS.xyz = ClampByMaxChannel(sceneS.xyz, 16.0);
#endif
  float4 bloomS = ApplyMask(t4.Sample(s4_s, v6.xy), cb3[52], cb3[53]);
  float3 bloom = bloomS.xyz;
#if TONEMAP_TYPE >= 1
  bloom *= LumaSettings.GameSettings.BloomIntensity;
  // Luma bloom replaces the game's: zeroing it makes bloomLin below vanish, the Luma term is added in linear instead
  if (LumaSettings.GameSettings.LumaBloomEnable > 0.5)
    bloom = 0.0;
#endif
#if TONEMAP_TYPE >= 1
  float3 sceneFog = AdjustFog(sceneS.xyz, v5.xy, skyMask);
  sceneFog = ApplyFogHighlightLimit(sceneFog, v5.xy);
  float3 x = sceneFog * cb4[116].x + bloom;
#else
  float3 x = sceneS.xyz * cb4[116].x + bloom;
#endif

  // Vanilla alpha blend, colour matrix and grade offset (scene + bloom)
  float3 pre = GradePre(x, bloomS);

#if TONEMAP_TYPE >= 1
  // Bloom Intensity only scales brightness: hue and chroma are taken from the full-intensity bloom result for this colour
  if (LumaSettings.GameSettings.BloomIntensity != 1.0 && LumaSettings.GameSettings.LumaBloomEnable <= 0.5)
  {
    float3 bloomFull = bloomS.xyz;
    float3 preFull = GradePre(sceneFog * cb4[116].x + bloomFull, bloomS);
    float3 preLin = gamma_to_linear(pre, GCT_MIRROR);
    float3 preFullLin = gamma_to_linear(preFull, GCT_MIRROR);
    // Full-bloom chromaticity scaled to the reduced-bloom luminance
    const float3 lumW = float3(0.2126, 0.7152, 0.0722);
    preLin = preFullLin * (max(dot(preLin, lumW), 0.0) / max(dot(preFullLin, lumW), 1e-6));
    pre = linear_to_gamma(preLin, GCT_MIRROR);
  }
  pre = ApplyBlackFloor(pre);
  // Same grade with the scene only (no bloom): bloom is added in linear after the expansion
  float3 preScene = ApplyBlackFloor(GradePre(sceneFog * cb4[116].x, bloomS));
  preScene = lerp(preScene, pre, skyMask); // Sky keeps bloom inside its soft shoulder (Sky Brightness cap)
#endif

  // Noise (dither texture)
  float2 nuv = (v5.xy + cb4[121].zw) * cb4[121].xy;
  float4 nS = ApplyMask(t2.Sample(s2_s, nuv), cb3[48], cb3[49]);
  float3 noise = (nS.xyz * 2.0 - 1.0) * cb4[118].x;

#if TONEMAP_TYPE >= 1
  // SDR chain: vanilla clamp. HDR chain: only the lower clamp.
  float3 sdrG = SignPow(min(max(pre, cb4[122].x), cb4[122].y) * cb4[119].xyz + noise, cb4[123].x);
  float3 hdrG = SignPow(FlashTint(max(preScene, cb4[122].x), cb4[119].xyz) + noise, cb4[123].x);

  float3 sdrLin = gamma_to_linear(sdrG, GCT_MIRROR);
  // Hue reference from a hue-preserving compressed SDR: per-channel clipping invents hues on over-bright colours
  float preMax = max(pre.r, max(pre.g, max(pre.b, 1e-6)));
  float preMaxC = preMax <= 0.8 ? preMax : 0.8 + 0.2 * (preMax - 0.8) / (preMax - 0.8 + 0.2); // identity below 0.8 keeps the vanilla reference for midtones
  float3 preRef = pre * (preMaxC / preMax);
  float3 sdrRefG = SignPow(FlashTint(min(max(preRef, cb4[122].x), cb4[122].y), cb4[119].xyz) + noise, cb4[123].x);
  float3 sdrRefLin = gamma_to_linear(sdrRefG, GCT_MIRROR);
  float hs = LumaSettings.GameSettings.HighlightsStrength;
  float3 hiPart = 1.0 + (hdrG - 1.0) * 2.2 * hs;
  float sb = max(LumaSettings.GameSettings.SkyBrightness, 1e-4);
  float3 skyHi = 1.0 + sb * (1.0 - exp(-(max(hdrG - 1.0, 0.0) * 2.2) / sb));
  hiPart = lerp(hiPart, skyHi, skyMask);
  float3 loPart = gamma_to_linear(min(hdrG, 1.0), GCT_MIRROR);
  float3 hdrLin = lerp(hiPart, loPart, step(hdrG, 1.0));

  // Bloom added in linear: the brightening vanilla shows below white, plus bloom above white (linear, not the highlight slope)
  float3 gFull = FlashTint(max(pre, 0), cb4[119].xyz);
  float3 gScene = FlashTint(max(preScene, 0), cb4[119].xyz);
  float3 bloomLin = gamma_to_linear(min(gFull, 1.0), GCT_MIRROR) - gamma_to_linear(min(gScene, 1.0), GCT_MIRROR);
  bloomLin *= 1.0 - skyMask;
  bloomLin += max(max(gFull, 1.0) - max(gScene, 1.0), 0.0);
  hdrLin += max(bloomLin, 0.0);

  // Luma bloom: linear, added where the game's bloom is added (above), so 1.0 = SDR white as for hdrLin
  float3 lumaBloomLin = 0.0;
  if (LumaSettings.GameSettings.LumaBloomEnable > 0.5)
    lumaBloomLin = max(t10.SampleLevel(s3_s, v5.xy, 0).rgb, 0.0) * LumaSettings.GameSettings.LumaBloomIntensity * (1.0 - skyMask);
  hdrLin += lumaBloomLin;

  float s = LumaSettings.GameSettings.VanillaHueRestore;
  float sHue = s * lerp(1.0, SKY_HUE_RESTORE_SCALE, skyMask);
  // PsychoV35 desaturates highlights itself: restore vanilla hue only, not its clipped chroma
  float sChroma = (LumaSettings.DisplayMode == 1 && LumaSettings.GameSettings.Tonemapper >= 0.5) ? 0.0 : sHue;
  hdrLin = RestoreHueAndChrominance(hdrLin, sdrRefLin, 0.75 * sHue, sChroma);

  float3 result = max(sdrLin + lumaBloomLin, 0.0); // SDR output gets the Luma bloom too (zero when off)
  if (LumaSettings.DisplayMode == 1)
  {
    // Hue-exact Oklch chroma expansion, bounded by the BT.2020 gamut
    float ge = LumaSettings.GameSettings.GamutExpansion;
    float3 c2020 = ExpandGamutBT709ToBT2020(max(hdrLin, 0.0), ge);
    c2020 = SimpleGamutClip(c2020, true);

    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
#if TONEMAP_TYPE >= 1
    if (LumaSettings.GameSettings.Tonemapper >= 0.5)
    {
      // PsychoV35 (RenoDX test35)
      float3 psyIn = BT2020_To_BT709(c2020);
      float peakRel = peakWhite / paperWhite;
      bool smoothFit = LumaSettings.GameSettings.PsychoVGamutFit >= 0.5;
      float3 psy = renodx::tonemap::psychov::psychotm_test35(psyIn, peakRel, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 100.f, 1.f, 1.f, 0, 1.f, 0.18f.xxx, 0.18f.xxx, 1.f, 1, 1.f, LumaSettings.GameSettings.PsychoVShoulder, renodx::tonemap::psychov::PSYCHO35_SOURCE_BOUNDARY_BT709, false, false, smoothFit ? 3 : 2, smoothFit ? 2.f : 0.f);
      if (any(isnan(psy)) || any(isinf(psy))) psy = clamp(psyIn, 0.0, peakRel);
      float3 psy2020 = BT709_To_BT2020(psy);
      float hueLock = LumaSettings.GameSettings.PsychoVHueLock;
      if (hueLock > 0.0) psy2020 = MacLeodBoynton::HueAndPurityEmulationBT2020(psy2020, c2020, hueLock, 0.0);
      // Hue clip emulation (RenoDX hue shift, CLIP method): take the hue of the per-channel clipped SDR input (BT.709)
      float hueClip = LumaSettings.GameSettings.PsychoVHueClip;
      if (hueClip > 0.0)
      {
        float3 clipRef709 = lerp(psyIn, saturate(psyIn), hueClip);
        psy2020 = MacLeodBoynton::HueAndPurityEmulationBT2020(psy2020, BT709_To_BT2020(clipRef709), 1.0, 0.0);
      }
      float psyS = LumaSettings.GameSettings.PsychoVHighlightSaturation;
      if (psyS > 0.0) psy2020 = lerp(psy2020, MacLeodBoynton::HueAndPurityEmulationBT2020(psy2020, c2020, 0.0, 1.0), psyS);
      result = BT2020_To_BT709(ClampByMaxChannel(SimpleGamutClip(psy2020, true), peakRel));
    }
    else
#endif
    {
      DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      settings.ShoulderStart = paperWhite / peakWhite;
      result = DICETonemap(BT2020_To_BT709(c2020) * paperWhite, peakWhite, settings) / paperWhite;
      // Final BT.2020 cap
      result = BT2020_To_BT709(ClampByMaxChannel(SimpleGamutClip(BT709_To_BT2020(result), true), peakWhite / paperWhite));
    }
  }

#if TONEMAP_TYPE >= 1
  // Screen flashes (cb4[119] tint above 1, e.g. 2.74,0.90,0.83) are authored for vanilla's clipping: bright pixels become the tint colour clipped at white.
  // The result blends to that exact vanilla flash, by flash strength (tint 1 -> 0%, 2+ -> 100%).
  float flashW = saturate(max(cb4[119].x, max(cb4[119].y, cb4[119].z)) - 1.0);
  if (flashW > 0.0)
  {
    // Uses full-intensity bloom, as authored
    float3 preFlash = ApplyBlackFloor(GradePre(sceneFog * cb4[116].x + bloomS.xyz, bloomS));
    float3 sdrFlashG = SignPow(min(max(preFlash, cb4[122].x), cb4[122].y) * cb4[119].xyz + noise, cb4[123].x);
    result = lerp(result, gamma_to_linear(saturate(sdrFlashG), GCT_MIRROR), flashW);
  }
#endif
  if (LumaSettings.GameSettings.FilmGrainStrength > 0)
    result = BL_ApplyFilmGrain(result, v5.xy, frac(LumaSettings.FrameIndex / 1000.0), LumaSettings.GameSettings.FilmGrainStrength * 0.03, 1.0);
  // Inverse of Display Composition's decode (gamma 2.2 in 0-1, sRGB above 1 with GAMMA_CORRECTION_RANGE_TYPE 1), so the peak lands exactly
  o0.rgb = ColorGradingLUTTransferFunctionInCorrected(result, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE);
  o0.a = sqrt(dot(saturate(sdrG), float3(0.3, 0.59, 0.11)));
#else
  // Exact vanilla
  float3 g = min(max(pre, cb4[122].x), cb4[122].y) * cb4[119].xyz + noise;
  float3 o = float3(
    VanillaPowChannel(g.x, cb4[123].x),
    VanillaPowChannel(g.y, cb4[123].x),
    VanillaPowChannel(g.z, cb4[123].x));
  o0.xyz = o;

  float d = dot(o, float3(0.3, 0.59, 0.11));
  float rs = rsqrt(abs(d));
  rs = (asuint(rs) == 0x7f800000u) ? 9999999933815812510711506376257961984.0 : rs;
  o0.w = (0.0 < abs(rs)) ? (1.0 / rs) : 9999999933815812510711506376257961984.0;
#endif
}
