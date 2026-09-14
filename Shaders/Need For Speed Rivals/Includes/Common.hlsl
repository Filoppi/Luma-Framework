#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"

#define GS LumaSettings.GameSettings

#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
// #define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
// #define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits
#define HDR_STOPS log2(HDR_PEAK)

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
float RenderIntermediatePass_Decode(float x) {
  #if GAMMA_CORRECTION_TYPE == 0
    x = gamma_sRGB_to_linear1(x, GCT_NONE);
  #else
    x = gamma_to_linear1(x, GCT_NONE, DefaultGamma);
  #endif
  return x;
}
float RenderIntermediatePass_Encode(float x) {
  #if GAMMA_CORRECTION_TYPE == 0
    x = linear_to_sRGB_gamma1(x, GCT_NONE);
  #else
    x = linear_to_gamma1(x, GCT_NONE, DefaultGamma);
  #endif
  return x;
}

float3 RenderIntermediatePass(float3 x) {
  x = max(x, 0);
  x = RenderIntermediatePass_Decode(x);
  x *= HDR_INTSCALING;
  x = RenderIntermediatePass_Encode(x);
  return x;
}

float3 RenderIntermediatePassFromLinear(float3 x) {
  x = max(x, 0);
  x = GammaCorrectionLinearDown(x);
  x *= HDR_INTSCALING;
  x = RenderIntermediatePass_Encode(x);
  return x;
}