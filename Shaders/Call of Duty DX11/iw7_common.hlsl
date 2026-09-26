// IW7 owns b13 in its tonemap shaders. Use IW7's actual Luma slots even
// when the shared shader folder is compiled from another Call of Duty title.
#ifdef LUMA_SETTINGS_CB_INDEX
#undef LUMA_SETTINGS_CB_INDEX
#endif
#define LUMA_SETTINGS_CB_INDEX b10
#ifdef LUMA_DATA_CB_INDEX
#undef LUMA_DATA_CB_INDEX
#endif
#define LUMA_DATA_CB_INDEX b9

#define LUT_SIZE 32u
#define LUT_3D 1

#include "common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

struct TMInfo
{
  float2 uvRaw; //raw texture coordinates after +v1 & before any modifications
  float3 r0;             // Working color; encoded after TM_Gamma().
  float3 colorHDR;       // Per-channel display rolloff, before grading.
  float3 colorNeutral;   // Matching linear SDR proxy, before grading.
  float aay; //anti-aliasing luminance
  bool restoreGrade;
};
static TMInfo tmi = { float2(0, 0), float3(0, 0, 0), float3(0, 0, 0), float3(0, 0, 0), 0, false };

void TM_UV(float2 uv) {
  tmi.uvRaw = uv;
}

void TM_Color(float3 col) {
  tmi.r0 = max(0, col);
  tmi.restoreGrade = false;
}

#ifndef COMMON_NOCB13
void TM_Rolloff() {
  float3 scene = tmi.r0 * GS.ExposurePre;
  float3 vanilla = saturate(scene < cb13[0].x
      ? MobiusRolloff(scene, cb13[2])
      : MobiusRolloff(scene, cb13[1]));

  // SDR uses the actual game curve and per-channel clipping.
  if (!(HDR_ENABLED) || HDR_PEAK <= 1.f) {
    tmi.r0 = vanilla;
    return;
  }

  // Extend the game's lower segment with its tangent at the shoulder.
  float4 coefficients = cb13[0].x > 0 ? cb13[2] : cb13[1];
  float shoulder = MobiusRolloff(cb13[0].x, coefficients);
  float slope = MobiusRolloffDerivative(cb13[0].x, coefficients);
  float3 extended = max(0, scene < cb13[0].x
      ? vanilla : slope * (scene - cb13[0].x) + shoulder);
  float peak = max(HDR_PEAK, 1.f);
  float shoulderSafe = clamp(shoulder, 0.0001f, peak * 0.99f);

  // Keep the rolled-off RGB, not the pre-rolloff luminance. The old
  // luminance restoration undid this peak limit and needed another tonemap.
#if PCC_TONEMAP == 1
  tmi.colorHDR = ExponentialRollOff(extended, shoulderSafe, peak);
#else
  tmi.colorHDR = Reinhard::ReinhardPiecewise(extended, peak, shoulderSafe);
#endif
  tmi.colorHDR = clamp(tmi.colorHDR, 0, peak);

  // Exact white clip is the HDR peak, not an unrelated scene maximum.
  // Preserve identity below midgray and map neutral HDR white to SDR 1.
  float y = GetLuminance(tmi.colorHDR, CS_BT709);
  float proxyY = peak > 1.f
      ? Reinhard::ReinhardPiecewiseExtended(y, peak, 1.f, 0.18f) : y;
  tmi.colorNeutral = ClampByMaxChannel(tmi.colorHDR * safeDivision(proxyY, y, 0), 1.f);
  tmi.r0 = tmi.colorNeutral;

  {
    // Correct only hue shift introduced by the per-channel shoulder.
    // Keep its highlight desaturation. Apply the same hue correction to
    // both bridge references so it does not contaminate the vanilla grade.
    float3 hueReference = extended * safeDivision(
        GetLuminance(tmi.colorNeutral, CS_BT709), GetLuminance(extended, CS_BT709), 0);
    tmi.r0 = RestoreHueAndChrominance(tmi.r0, hueReference, 1.f, 0.f, 0.f, FLT_MAX, 0.f, CS_BT709);
    tmi.r0 = ClampByMaxChannel(SimpleGamutClip(tmi.r0, false), 1.f);
    tmi.colorNeutral = tmi.r0;
    tmi.colorHDR = tmi.colorNeutral * safeDivision(
        y, GetLuminance(tmi.colorNeutral, CS_BT709), 0);
    tmi.colorHDR = ClampByMaxChannel(tmi.colorHDR, peak);
  }
  // Grade this exact SDR proxy. RestorePostProcess must compare the graded
  // result against the same pre-matrix, pre-LUT input to isolate the grade's
  // luminance and chrominance changes from the SDR compression itself.
  tmi.r0 = tmi.colorNeutral;
  tmi.restoreGrade = true;
}
#endif

void TM_Rolloff_Insert() {
  // Overlay-only reads an already graded, encoded scene. Do not expand or
  // tonemap its existing overlay operations a second time.
  tmi.restoreGrade = false;
}

void TM_LumaThingy(int cbStart) {
  if (!GS.AllowVanillaColorGrade) return;
  // Preserve the vanilla matrix and clipping, including in the SDR branch.
  tmi.r0 = saturate(float3(
      dot(tmi.r0, cb2[cbStart + 0].xyz),
      dot(tmi.r0, cb2[cbStart + 1].xyz),
      dot(tmi.r0, cb2[cbStart + 2].xyz)));
}

void TM_Gamma() {
  // IW7's original shaper deliberately has no piecewise sRGB linear toe.
  tmi.r0 = max(0, pow(max(tmi.r0, 0), 0.416666657f) * 1.05499995f - 0.0549999997f);
}

void TM_LUT(Texture3D<float4> tLUT, SamplerState sLUT) {
  if (!GS.AllowVanillaColorGrade) return;
  // Original 0xA6AFB6CC: 32^3 UNORM LUT, mip 0, RGB axes unchanged.
  // Map [0,1] to texel centers [0.5/32,31.5/32], using the game sampler.
  // HDR grades the matching SDR proxy; no midgray or second LUT probe.
  tmi.r0 = tLUT.SampleLevel(sLUT, mad(tmi.r0, 0.96875f, 0.015625f), 0).xyz;
}

float TM_LumaForAA_Internal(float x) {
  return Neutwo(x);
}
float TM_LumaForAA(float3 x, bool decodeGamma, bool encodeGamma) {
  if ((!(HDR_ENABLED) || HDR_PEAK <= 1.f) && decodeGamma && encodeGamma)
    return GetLuminance(x, CS_BT709);
  if (decodeGamma) x = gamma_sRGB_to_linear(x, GCT_NONE);
  float y = TM_LumaForAA_Internal(GetLuminance(x, CS_BT709));
  if (encodeGamma) y = linear_to_sRGB_gamma1(y, GCT_NONE);
  return y;
}
float TM_LumaForAA(float x, bool decodeGamma, bool encodeGamma) {
  if (decodeGamma) x = gamma_sRGB_to_linear1(x, GCT_NONE);
  x = TM_LumaForAA_Internal(x);
  if (encodeGamma) x = linear_to_sRGB_gamma1(x, GCT_NONE);
  return x;
}

void TM_Upgrade() {
  if (!(HDR_ENABLED) || !tmi.restoreGrade) {
    // Preserve encoded SDR/overlay output without a lossy round trip.
    tmi.aay = dot(tmi.r0, float3(0.212599993f, 0.715200007f, 0.0722000003f));
    return;
  }

  float3 color = tmi.colorHDR;
  if (GS.AllowVanillaColorGrade) {
    color = RestorePostProcess(tmi.colorHDR, tmi.colorNeutral,
        gamma_sRGB_to_linear(tmi.r0, GCT_NONE), 0.f, true);
  }
  // Grading can lift the peak again. Bound it with uniform RGB scaling;
  // another per-channel clip would change the final hue.
  color = ClampByMaxChannel(SimpleGamutClip(color, false), max(HDR_PEAK, 1.f));
  tmi.aay = TM_LumaForAA(color, false, true);
  tmi.r0 = linear_to_sRGB_gamma(color, GCT_NONE);
}
