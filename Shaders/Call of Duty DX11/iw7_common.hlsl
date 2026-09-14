#define LUT_SIZE 32u
#define LUT_3D 1

#include "common.hlsl"
// #include "../Includes/ColorGradingLUT.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
// #ifdef COMMON_LUT
struct TMInfo
{
  float2 uvRaw; //raw texture coordinates after +v1 & before any modifications
  float3 r0; //main color
  float3 r0SDR; //main color from SDR rolloff
  float r0y; //main color luminance from HDR rolloff
  float colorNy; //neutral color, input into SDR colorgrade
  float aay; //anti-aliasing luminance
  float ldrPeak; //temporary tonemap peak for SDR rolloff
};
static TMInfo tmi = { float2(0,0), float3(0,0,0), float3(0,0,0), 0, 0, 0, 0 };

void TM_UV(float2 uv) {
  tmi.uvRaw = uv;
}

void TM_Color(float3 col) {
  col = max(0, col);
  tmi.r0 = col;
  tmi.ldrPeak = 0.845;
}

#ifndef COMMON_NOCB13
void TM_Rolloff() {
  // colorU / Rolloff
  RolloffResult r = Rolloff_Complete(tmi.r0, cb13[0].x, cb13[1], cb13[2]);
  tmi.r0 = r.color;
  tmi.r0y = r.y;
  tmi.r0SDR = r.colorSDR;

  // colorN
  float y0 = tmi.r0y;
  float y1 = y0;
  y1 = HermiteSpline::HermiteSplineLuminanceRolloff(y1, tmi.ldrPeak, 100 * 10 * GS.ExpectedMax);
  // y1 = Neutwo(y1, tmi.ldrPeak);
  tmi.colorNy = y1;
  tmi.r0 *= safeDivision(y1, y0, 1);
  tmi.r0 = saturate(tmi.r0);
}
#endif

// When tonemap doesnt have rolloff, we need a temporary one to compress in color for SDR color grading
void TM_Rolloff_Insert() {
  // colorU / Rolloff
  tmi.r0y = GetLuminance(tmi.r0, CS_BT709);

  // colorN
  float y0 = tmi.r0y;
  float y1 = y0;
  y1 = HermiteSpline::HermiteSplineLuminanceRolloff(y1, tmi.ldrPeak, 100 * GS.ExpectedMax);
  tmi.colorNy = y1;
  tmi.r0 *= safeDivision(y1, y0, 1);
  tmi.r0 = saturate(tmi.r0);
}

void TM_LumaThingy(int cbStart) {
  if (!GS.AllowVanillaColorGrade) return;

  float3 r1;
  r1.x = /* saturate */(dot(tmi.r0, cb2[cbStart + 0].xyz));
  r1.y = /* saturate */(dot(tmi.r0, cb2[cbStart + 1].xyz));
  r1.z = /* saturate */(dot(tmi.r0, cb2[cbStart + 2].xyz));
  r1 = ClampByMaxChannel(r1, 1);
  // r1 = max(r1, 0); //clean
  tmi.r0 = r1;

  // midgray change
  {
    float mg_in = 0.18;
    float3 r2;
    r2.x = dot(mg_in, cb2[cbStart + 0].xyz);
    r2.y = dot(mg_in, cb2[cbStart + 1].xyz);
    r2.z = dot(mg_in, cb2[cbStart + 2].xyz);
    r2 = max(r2, 0); //clean
    float mg_out = GetLuminance(r2, CS_BT709);
    float ratio = safeDivision(mg_out, mg_in, 1);
    tmi.r0y *= ratio;
  }
}

void TM_Gamma() {
  // tmi.r0 = max(0, tmi.r0);
  // tmi.r0 = linear_to_sRGB_gamma(tmi.r0, GCT_NONE);

  tmi.r0 = pow(tmi.r0, 0.416666657);
  tmi.r0 = tmi.r0 * 1.05499995 + -0.0549999997;
  tmi.r0 = max(0, tmi.r0);

  tmi.r0SDR = pow(tmi.r0SDR, 0.416666657);
  tmi.r0SDR = tmi.r0SDR * 1.05499995 + -0.0549999997;
  tmi.r0SDR = max(0, tmi.r0SDR);
}

void TM_LUT(Texture3D<float4> tLUT, SamplerState sLUT) {
  if (!GS.AllowVanillaColorGrade) return;

  // midgray change
  {
    const float mg_in_linear = 105/200.f;
    const float mg_in = linear_to_sRGB_gamma1(mg_in_linear, GCT_NONE);
    float3 l = tLUT.SampleLevel(sLUT, mg_in, 0).xyz;
    l = gamma_sRGB_to_linear(l, GCT_NONE);
    float mg_out = GetLuminance(l, CS_BT709);
    float ratio = safeDivision(mg_out, mg_in_linear, 1);
    tmi.r0y *= ratio;
  }

  float3 r0Before = tmi.r0;

  tmi.r0 = mad(tmi.r0, 0.96875, 0.015625);
  tmi.r0 = tLUT.SampleLevel(sLUT, tmi.r0, 0).xyz; //half half

  //AHHHHHHHHHHHH
  // r0Before = gamma_sRGB_to_linear(r0Before, GCT_NONE); //neutral
  // r0Before = UCS_ToUCS(r0Before);

  tmi.r0SDR = mad(tmi.r0SDR, 0.96875, 0.015625); //exact SDR
  tmi.r0SDR = tLUT.SampleLevel(sLUT, tmi.r0SDR, 0).xyz;
  tmi.r0SDR = gamma_sRGB_to_linear(tmi.r0SDR, GCT_NONE);
  tmi.r0SDR = UCS_ToUCS(tmi.r0SDR);

  tmi.r0 = gamma_sRGB_to_linear(tmi.r0, GCT_NONE);
  tmi.r0 = UCS_ToUCS(tmi.r0);
  // float s = tmi.r0y;
  // s *= 0.4915;
  // s = saturate(s);
  // s *= /* 0.97125 */ /* 0.825 */ DVS5;
  tmi.r0 = RestoreHueAndChrominanceUcs(tmi.r0, tmi.r0SDR, /* s * */ DVS1, /* s *  */DVS2, 0);
  tmi.r0.x = lerp(tmi.r0.x, tmi.r0SDR.x, 0.176); //take minor contrast adjust
  // tmi.r0 = RestoreHueAndChrominanceUcs(tmi.r0, r0Before, s * DVS3, s * DVS4, 1);
  tmi.r0 = UCS_FromUCS(tmi.r0);
  tmi.r0 = max(0, tmi.r0); //clean
  tmi.r0 = linear_to_sRGB_gamma(tmi.r0, GCT_NONE);

//     LUTExtrapolationData ld;
//     ld.inputColor = tmi.r0;
//     ld.inputColor = gamma_sRGB_to_linear(ld.inputColor, GCT_NONE);
//     ld.inputColor *= safeDivision(tmi.r0y, GetLuminance(ld.inputColor, CS_BT709), 1);
//     ld.inputColor = linear_to_sRGB_gamma(ld.inputColor, GCT_NONE);
//     ld.vanillaInputColor = tmi.r0;
// 
//     LUTExtrapolationSettings ls;
//     ls.lutSize = LUT_SIZE;
//     ls.inputLinear = false;
//     ls.lutInputLinear = false;
//     ls.lutOutputLinear = false;
//     ls.outputLinear = false;
//     ls.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
//     ls.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
//     ls.samplingQuality = 1;
//     ls.neutralLUTRestorationAmount = DVS6;
//     ls.vanillaLUTRestorationAmount = DVS7;
//     ls.vanillaLUTRestorationType = 0;
//     ls.enableExtrapolation = true;
//     ls.extrapolationQuality = 2;
//     ls.backwardsAmount = 0.5;
//     ls.clipExtrapolationToWhite = false;
//     ls.whiteLevelNits = Rec709_WhiteLevelNits;
//     ls.inputTonemapToPeakWhiteNits = 0;
//     ls.clampedLUTRestorationAmount = 1;
//     ls.fixExtrapolationInvalidColors = true;
// 
//     tmi.r0 = SampleLUTWithExtrapolation(tLUT, sLUT, ld, ls);
}

float TM_LumaForAA_Internal(float x) {
  return Neutwo(x);
}
float TM_LumaForAA(float3 x, bool decodeGamma, bool encodeGamma) {
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
  tmi.r0 = gamma_sRGB_to_linear(tmi.r0, GCT_NONE); //linear

  // Upgrade()
  float ratio = 1.f;
  float y_untonemapped = tmi.r0y;
  float y_tonemapped = /* tmi.colorNy */ HermiteSpline::HermiteSplineLuminanceRolloff(tmi.r0y, 1/* tmi.ldrPeak */, 100 * 10 * GS.ExpectedMax);
  // float y_tonemapped = Neutwo(tmi.r0y, tmi.ldrPeak);
  float y_tonemapped_graded = GetLuminance(tmi.r0, CS_BT709);
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
  tmi.aay = TM_LumaForAA(y1, false, true);
  tmi.r0 *= safeDivision(y1, y, 1);

  tmi.r0 = max(tmi.r0, 0); //clean
  tmi.r0 = linear_to_sRGB_gamma(tmi.r0, GCT_NONE); //gamma
}
