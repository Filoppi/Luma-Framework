#include "common.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
// #ifdef COMMON_LUT
struct TMInfo
{
  float3 r0; //main color
  float r0y; //main color luminance from HDR rolloff
  float colorNy; //neutral color, input into SDR colorgrade
  float ldrPeak; //UpgradeToneMap peak, useful because tint will shift it
};
static TMInfo tmi = { float3(0,0,0), 0, 0, 1 };

void TM_Color(float3 col) {
  col = max(0, col);
  tmi.r0 = col;
}

void TM_Rolloff() {
  // colorU / Rolloff
  RolloffResult r = Rolloff_Complete(tmi.r0, cb4[0].x, cb4[1], cb4[2]);
  tmi.r0 = r.color;
  tmi.r0y = r.y;

  // Gamma thing idk, not/barely used
  tmi.r0 = pow(tmi.r0, cb4[3].z);

  // colorN
  float y0 = tmi.r0y;
  float y1 = y0;
  y1 = HermiteSpline::HermiteSplineLuminanceRolloff(y1, 0.96, 100 * GS.ExpectedMax);
  tmi.colorNy = y1;
  tmi.r0 *= safeDivision(y1, y0, 1);
  tmi.r0 = saturate(tmi.r0);
}

void TM_LumaThingy() {
  if (!GS.AllowVanillaColorGrade) return;

  float3 r1;
  r1.x = /* saturate */(dot(tmi.r0, cb4[65].xyz));
  r1.y = /* saturate */(dot(tmi.r0, cb4[66].xyz));
  r1.z = /* saturate */(dot(tmi.r0, cb4[67].xyz));
  r1 = ClampByMaxChannel(r1, 1);
  r1 = max(r1, 0); //clean
  tmi.r0 = r1;

  // midgray change
  {
    float mg_in = 0.18;
    float3 r2;
    r2.x = dot(mg_in, cb4[65].xyz);
    r2.y = dot(mg_in, cb4[66].xyz);
    r2.z = dot(mg_in, cb4[67].xyz);
    r2 = max(r2, 0); //clean
    float mg_out = GetLuminance(r2, CS_BT709);
    float ratio = safeDivision(mg_out, mg_in, 1);
    tmi.r0y *= ratio;
  }
}

void TM_Gamma() {
  tmi.r0 = linear_to_sRGB_gamma(tmi.r0, GCT_NONE);
}

float3 TM_Tint_Internal(float3 x, float l, int cboffset) {
  float3 r1 = cb2[2 + cboffset].xyz * l + cb2[1 + cboffset].xyz;
  r1 = r1 * l + cb2[0 + cboffset].xyz;
  x = x * r1 + cb2[3 + cboffset].xyz;
  return x;
}

void TM_SaturationAndTint(int cboffset = 0) {
  if (!GS.AllowVanillaColorGrade) return;

  // Setup
  float4 r0, r1;
  r0.xyz = tmi.r0;
  r0.w = saturate(dot(r0.xyz, float3(0.298999995,0.587000012,0.114)));
  
  // Saturation
  r1.xyz = r0.www + -r0.xyz;
  r1.w = cb2[1 + cboffset].w * r0.w + cb2[0 + cboffset].w;
  r0.xyz = r1.www * r1.xyz + r0.xyz;

  // Tint
  r0.xyz = TM_Tint_Internal(r0.xyz, r0.w, cboffset);

  // tint midgray change //TODO: remove, this game is very light on color grade
  {
    float mg_in = 0.46;
    float3 mg = TM_Tint_Internal(float3(mg_in, mg_in, mg_in), mg_in, cboffset);
    mg = gamma_sRGB_to_linear(mg, GCT_NONE);
    float mg_luma = GetLuminance(mg, CS_BT709);
    float ratio = safeDivision(mg_luma, 0.18, 1);
    tmi.r0y *= ratio;
  }

  // tint peak change //TODO: remove, this game is very light on color grade
  {
    float3 peak = TM_Tint_Internal(1, 1, cboffset);
    float peak_max = max(peak.x, max(peak.y, peak.z));
    if (peak_max > 1) { //must be a change upwards to matter
      peak_max = gamma_sRGB_to_linear1(peak_max, GCT_NONE);
      tmi.ldrPeak = peak_max;
    }
  }

  // Out 
  tmi.r0 = r0.xyz;
}

void TM_Overlay(float2 w1, Texture2D overlayTex, SamplerState overlayS) {
  if (!GS.AllowVanillaColorGrade) return;

  float3 r1 = float3(1,1,1) + -tmi.r0; //bruh what?! Tint overshoots, so this won't cause errors?
  float3 r2 = overlayTex.Sample(overlayS, w1.xy).xyz;
  tmi.r0 = r2 * r1 + tmi.r0;
  tmi.r0 = max(tmi.r0, 0); //clean
}

void TM_Upgrade() {
  tmi.r0 = gamma_sRGB_to_linear(tmi.r0, GCT_NONE); //linear

  // Upgrade()
  float ratio = 1.f;
  float y_untonemapped = tmi.r0y;
  float y_tonemapped = /* tmi.colorNy */ HermiteSpline::HermiteSplineLuminanceRolloff(tmi.r0y, tmi.ldrPeak, 100 * GS.ExpectedMax);
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
  tmi.r0 *= safeDivision(y1, y, 1);

  tmi.r0 = max(tmi.r0, 0); //clean
  tmi.r0 = linear_to_sRGB_gamma(tmi.r0, GCT_NONE); //gamma
}
// #endif
///////////////////////////////////////////////////////////////////////////////////////////////////