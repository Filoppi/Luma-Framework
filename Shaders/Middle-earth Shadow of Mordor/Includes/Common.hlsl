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

float3 sRGB_Encode(float3 x) {return x < 0.0031308f ? x * 12.92f : 1.055f * pow(x, 1.f / 2.4f) - 0.055f;}
float  sRGB_Encode(float  x) {return x < 0.0031308f ? x * 12.92f : 1.055f * pow(x, 1.f / 2.4f) - 0.055f;}
float3 sRGB_Decode(float3 x) {return x <= 0.04045f ? x / 12.92f : pow((x + 0.055f) / 1.055f, 2.4f);}
float  sRGB_Decode(float  x) {return x <= 0.04045f ? x / 12.92f : pow((x + 0.055f) / 1.055f, 2.4f);}

#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits
// #if GAMMA_CORRECTION_RANGE_TYPE == 0 && GAMMA_CORRECTION_TYPE > 0
// static float HDR_PEAK_GAMMA = sRGB_Decode(pow(HDR_PEAK, 1/DefaultGamma));
// #else
// static float HDR_PEAK_GAMMA = HDR_PEAK;
// #endif

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
