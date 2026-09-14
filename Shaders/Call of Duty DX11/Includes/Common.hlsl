// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers

// Define the game custom cbuffer structs
#include "GameCBuffers.hlsl"
// Global common
#include "../../Includes/Common.hlsl"
// Game specific settings
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings

/*

#include "./Includes/Common.hlsl"
if (!GS.IsHud) discard;

#include "./common1.hlsl"

*/

#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits

///////////////////////////////////////////////////////////////////////////////////////////////////
//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/hermite_spline.hlsl
namespace HermiteSpline {
  float Rescale(float x, float x_min, float x_max, float y_min = 0, float y_max = 1, bool clamp = false) {
    float value = lerp(y_min, y_max, (x - x_min) / (x_max - x_min));
    if (clamp) {
      value = saturate(value);
    }
    return value;
  }
  float HermiteSplineRolloff(float input, float target_white = 1.f, float max_white = 20.f) {
    float l_w = max_white;
    // float l_b = min_black;
    // float l_min = target_black;
    float l_max = target_white;
    float e_1 = Rescale(input, 0, l_w);
    // float min_lum = Rescale(l_min, l_b, l_w);
    float max_lum = Rescale(l_max, 0, l_w);
    float knee_start = 1.5f * max_lum - 0.5f;
    // float b = min_lum;
    float t_b = Rescale(e_1, knee_start, 1.f);

    // float p_e1 = (((2 * t_b * t_b * t_b) - (3 * t_b * t_b) + 1) * knee_start)
    //              + (((t_b * t_b * t_b) - (2 * t_b * t_b) + t_b) * (1.f - knee_start))
    //              + ((-(2 * t_b * t_b * t_b) + (3 * t_b * t_b)) * max_lum);
    float t_b_squared = t_b * t_b;
    float t_b_cubed = t_b_squared * t_b;
    float two_t_b_cubed = 2.f * t_b_cubed;
    float three_t_b_squared = 3.f * t_b_squared;
    float p_e1_h00 = (two_t_b_cubed - three_t_b_squared + 1.f);
    float p_e1_h10 = (t_b_cubed - 2.f * t_b_squared + t_b);
    float p_e1_h01 = (-two_t_b_cubed + three_t_b_squared);
    // float p_e1_h11 = (t_b_cubed - t_b_squared); // Not used since derivative is 0 at max_lum

    float p_e1 = p_e1_h00 * knee_start
                 + p_e1_h10 * (1.f - knee_start)
                 + p_e1_h01 * max_lum;

    float e_2 = (e_1 < knee_start) ? e_1 : p_e1;

    // float e_3 = e_2 + b * pow(1-e_2, 4);
    // float e_3a1 = (1 - e_2) * (1 - e_2);
    // float e_3a2 = e_3a1 * (1 - e_2);
    float e_3 = e_2;

    // Custom: clamp before lerp
    // e_3 = saturate(e_3);

    // float e_4 = lerp(l_b, l_w, e_3);
    float e_4 = l_w * e_3;

    return min(e_4, target_white);
  }
  float3 HermiteSplinePerChannelRolloff(float3 input, float target_white = 1.f, float max_white = 20.f) {
    float target_white_log2 = log2(target_white);
    float max_white_log2 = log2(max_white);
    float3 scaled = float3(
        input.r == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.r), target_white_log2, max_white_log2)),
        input.g == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.g), target_white_log2, max_white_log2)),
        input.b == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.b), target_white_log2, max_white_log2)));
    return scaled;
  }

  float HermiteSplineLuminanceRolloff(float luminance, float target_white = 1.f, float max_white = 20.f) {
    if (luminance <= 0) return 0;
    return exp2(HermiteSplineRolloff(log2(luminance), log2(target_white), log2(max_white)));
  }
}
///////////////////////////////////////////////////////////////////////////////////////////////////

float3 TonemapHDRLuminance(float3 x) {
  float p = HDR_PEAK;
  float em = GS.ExpectedMax;
  em *= 100;
  // #ifdef IW7
  //   em *= 1.2;
  // #endif

  float y = GetLuminance(x, CS_BT709);
  float y1 = y;
  y1 *= GS.ExposurePost;
  y1 = HermiteSpline::HermiteSplineLuminanceRolloff(y1, p, em);
  x *= safeDivision(y1, y, 1); //apply
  x = clamp(x, 0, p); //clean
  return x;
}

float GammaCorrectionPeak(float x) {
  #if GAMMA_CORRECTION_TYPE > 0
    x = gamma_sRGB_to_linear1(x, GCT_NONE);
    x = linear_to_gamma1(x, GCT_NONE, DefaultGamma);
  #endif
  return x;
}

float3 GammaCorrectionLinearDown(float3 x) {
  #if GAMMA_CORRECTION_TYPE > 0
    x = linear_to_sRGB_gamma(x, GCT_NONE);
    x = gamma_to_linear(x, GCT_NONE, DefaultGamma);
  #endif
  return x;
}
float3 GammaCorrectionLinearUp(float3 x) {
  #if GAMMA_CORRECTION_TYPE > 0
    x = gamma_to_linear(x, GCT_NONE, DefaultGamma);
    x = linear_to_sRGB_gamma(x, GCT_NONE);
  #endif
  return x;
}

float3 RenderIntermediatePass_Decode(float3 x) {
  #if GAMMA_CORRECTION_TYPE == 0
    x = gamma_sRGB_to_linear(x, GCT_NONE);
  #else
    x = gamma_to_linear(x, GCT_NONE, DefaultGamma);
  #endif
  return x;
}
float3 RenderIntermediatePass_Encode(float3 x) {
  #if GAMMA_CORRECTION_TYPE == 0
    x = linear_to_sRGB_gamma(x, GCT_NONE);
  #else
    x = linear_to_gamma(x, GCT_NONE, DefaultGamma);
  #endif
  return x;
}

float3 RenderIntermediatePass(float3 x) {
  x = max(x, 0);
  x = RenderIntermediatePass_Decode(x);
  x = TonemapHDRLuminance(x);
  x *= HDR_INTSCALING;
  x = RenderIntermediatePass_Encode(x);
  return x;
}

float3 RenderIntermediatePassFromLinear(float3 x) {
  x = max(x, 0);
  x = GammaCorrectionLinearDown(x);
  x = TonemapHDRLuminance(x);
  x *= HDR_INTSCALING;
  x = RenderIntermediatePass_Encode(x);
  return x;
}

