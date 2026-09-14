#include "common.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
// #ifdef COMMON_LUT
struct LUTInfo
{
  float colorUy; //luma saved from rolloff pass prior
  float colorNy; //neutral color, input into SDR colorgrade
  float3 r0; //main color
  float ySatTint; //luminance for saturation and tint
  float ldrPeak; //UpgradeToneMap peak, useful because tint will shift it
};
static LUTInfo li = { 0, 0, float3(0,0,0), 0, 1 };

void LUT_Color_Internal(float3 col, float y) {
  li.colorUy = y; //luma saved from rolloff pass
  li.r0 = col; //main color

  // colorN
  float y0 = li.colorUy;
  float y1 = y0;
  y1 = HermiteSpline::HermiteSplineLuminanceRolloff(y1, 0.96, 100 * GS.ExpectedMax);
  li.colorNy = y1;
  li.r0 *= safeDivision(y1, y0, 1);
  li.r0 = saturate(li.r0);
}

void LUT_Color(float2 v1) {
  // colorU
  float4 col = t4.Sample(s4_s, v1.xy);
  LUT_Color_Internal(col.xyz, col.w);
}

void LUT_Gamma() {
  li.r0 = linear_to_sRGB_gamma(li.r0, GCT_NONE);
}

void LUT_LUT(Texture3D lut, SamplerState lutS) {
  if (!GS.AllowVanillaColorGrade) return;

  li.r0 = li.r0 * 0.96875 + 0.015625; //pad (32x)
  li.r0 = lut.Sample(lutS, li.r0).xyz;

  // midgray change
  {
    float mg_in = 0.46 * 0.96875 + 0.015625;
    float3 mg = lut.Sample(lutS, mg_in).xyz;
    mg = gamma_sRGB_to_linear(mg, GCT_NONE);
    float mg_luma = GetLuminance(mg, CS_BT709);
    float ratio = safeDivision(mg_luma, 0.18, 1);
    li.colorUy *= ratio;
  }
}

float3 LUT_Tint_Internal(float3 x, float l, int cboffset) {
  float3 r1 = cb2[2 + cboffset].xyz * l + cb2[1 + cboffset].xyz;
  r1 = r1 * l + cb2[0 + cboffset].xyz;
  x = x * r1 + cb2[3 + cboffset].xyz;
  return x;
}

void LUT_SaturationAndTint(int cboffset = 0) {
  if (!GS.AllowVanillaColorGrade) return;

  float4 r0, r1; 
  li.ySatTint = dot(li.r0, float3(0.298999995,0.587000012,0.114));

  // sat
  r1.x = cb2[1 + cboffset].w * li.ySatTint + cb2[0 + cboffset].w;
  li.ySatTint = saturate(li.ySatTint);
  r1.yzw = li.ySatTint + -li.r0;
  li.r0 = r1.x * r1.yzw + li.r0;
  // li.r0 = max(li.r0, 0); //clean

  // tint (user brighntess, overshoots SDR)
  li.r0 = LUT_Tint_Internal(li.r0, li.ySatTint, cboffset);
  li.r0 = max(li.r0, 0); //clean

  // tint midgray change
  {
    float mg_in = 0.46;
    float3 mg = LUT_Tint_Internal(float3(mg_in, mg_in, mg_in), mg_in, cboffset);
    mg = gamma_sRGB_to_linear(mg, GCT_NONE);
    float mg_luma = GetLuminance(mg, CS_BT709);
    float ratio = safeDivision(mg_luma, 0.18, 1);
    li.colorUy *= ratio;
  }

  // tint peak change //TODO:
  {
    float3 peak = LUT_Tint_Internal(1, 1, cboffset);
    float peak_max = max(peak.x, max(peak.y, peak.z));
    if (peak_max > 1) { //must be a change upwards to matter (invalidates AC-130 scene invert)
      peak_max = gamma_sRGB_to_linear1(peak_max, GCT_NONE);
      li.ldrPeak = peak_max;
    }
  }
}

void LUT_Overlay(float2 w1, Texture2D overlayTex, SamplerState overlayS) {
  if (!GS.AllowVanillaColorGrade) return;

  float3 r1 = float3(1,1,1) + -li.r0; //bruh what?! Tint overshoots, so this won't cause errors?
  float3 r2 = overlayTex.Sample(overlayS, w1.xy).xyz;
  li.r0 = r2 * r1 + li.r0;
  li.r0 = max(li.r0, 0); //clean
}

void LUT_UpgradeAndTonemap() {
    li.r0 = gamma_sRGB_to_linear(li.r0, GCT_NONE); //linear

    float ratio = 1.f;
    float y_untonemapped = li.colorUy;
    float y_tonemapped = /* li.colorNy */ HermiteSpline::HermiteSplineLuminanceRolloff(li.colorUy, li.ldrPeak, 100 * GS.ExpectedMax);
    float y_tonemapped_graded = GetLuminance(li.r0, CS_BT709);
    if (y_untonemapped < y_tonemapped) {
      ratio = y_untonemapped / y_tonemapped;
    } else {
      float y_delta = y_untonemapped - y_tonemapped;
      y_delta = max(0, y_delta);
      const float y_new = y_tonemapped_graded + y_delta;

      const bool y_valid = (y_tonemapped_graded > 0);
      ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
    }
    float y = y_tonemapped_graded;
    float y1 = y;
    y1 *= ratio;
    li.r0 *= safeDivision(y1, y, 1); //apply

    li.r0 = max(li.r0, 0); //clean
    li.r0 = linear_to_sRGB_gamma(li.r0, GCT_NONE); //encode
}
// #endif
///////////////////////////////////////////////////////////////////////////////////////////////////