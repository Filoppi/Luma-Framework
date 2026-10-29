#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

// // \left\{x<0.081:\frac{x}{4.5},\left(\frac{x+0.099}{1.099}\right)^{\frac{1}{0.45}}\right\}
// #define DECODEREC709(T)\
// T DecodeRec709(T x) {\
//   return x < 0.081 ? x / 4.5 : pow((x + 0.099) / 1.099, 1.0 / 0.45);\
// }
// DECODEREC709(float)
// DECODEREC709(float3)
// DECODEREC709(float4)
// #undef DECODEREC709
// 
// // \left\{x<0.018:4.5x,1.099x^{0.45}-0.099\right\}
// #define ENCODEREC709(T)\
// T EncodeRec709(T x) {\
//   return x < 0.018 ? 4.5 * x : pow(x * 1.099, 0.45) - 0.099;\
// }
// ENCODEREC709(float)
// ENCODEREC709(float3)
// ENCODEREC709(float4)
// #undef ENCODEREC709

#define DECODEREC709(T)\
T DecodeRec709(T x) {\
  T r0, r2, r3, r4;\
  r0 = x;\
  r2 = 0.0989999995 + r0; \
  r2 = 0.909918129 * r2;\
  r2 = pow(r2, 2.22222233);\
  r3 = -(0.0810000002 >= r0);\
  r4 = 0.222222224 * r0;\
  r2 = r3 ? r4 : r2;\
  return r2;\
}
DECODEREC709(float3)
DECODEREC709(float4)
#undef DECODEREC709

#define ENCODEREC709(T)\
T EncodeRec709(T x) {\
  T r0, r1, r2;\
  r1 = x;\
  r0 = pow(r1, 0.449999988);\
  r0 = r0 * 1.09899998 + -0.0989999995;\
  r2 = -(0.0179999992 >= r1);\
  r1 = 4.5 * r1;\
  r0 = r2 ? r1 : r0;\
  return r0;\
}
ENCODEREC709(float3)
ENCODEREC709(float4)
#undef ENCODEREC709

#if CUSTOM_LUMA == 1
  #if CUSTOM_SDR == 1
    #undef CUSTOM_LUTBUILDER_COLORSPACE
    #define CUSTOM_LUTBUILDER_COLORSPACE 0
  #endif

  #define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
  #define HDR_INTSCALING  GamePaperWhiteNits / UIPaperWhiteNits
  #define HDR_SHOULDERSTART GS_TonemapperRolloffStart / GamePaperWhiteNits
  #define HDR_MAXEXPECTED GS_TonemapperMaxExpected / GamePaperWhiteNits
#else
  // #define CUSTOM_SDR_GAMMA 2.2
  #undef GAMMA_CORRECTION_TYPE
  #define GAMMA_CORRECTION_TYPE 0
  #define CUSTOM_GAMMA_CORRECTION_MODE 0
  #define CUSTOM_HDTVREC709 0 /*TODO: Add?*/
  #define CUSTOM_TONEMAP 2
  #define CUSTOM_TONEMAP_SCALING 0
  #define CUSTOM_TONEMAP_CLAMP 1
  #define CUSTOM_RCAS 1
  #define CUSTOM_LUTBUILDER_COLORSPACE 0
  #define CUSTOM_LUTBUILDER_VANILLA 0
  #define CUSTOM_LUTBUILDER_SATBOOST 0
  #define CUSTOM_LUTBUILDER_NEUTRAL 0
  #define CUSTOM_LUTBUILDER_NEUTRAL_LUMA 0
  #define CUSTOM_PCC 1
  #define CUSTOM_UPGRADE_DEBUG 0
  #define CUSTOM_UCS_TYPE 2
  #define CUSTOM_COLORGRADE 0
  #define CUSTOM_UPSCALE_MOV 0
  #define CUSTOM_CHROMABER 1
  #define CUSTOM_MB_QUALITY 0
  #define CUSTOM_BLACKFLOOR_LUT 0
  #define CUSTOM_PERCHANNELLUMAEMULATE 0
  #define CUSTOM_SDRTONEMAP 0
  #define CUSTOM_SR 0
  #define CUSTOM_SDR 0

  #define HDR_PEAK GS_PeakWhiteNits / GS_GamePaperWhiteNits
  #define HDR_INTSCALING GS_GamePaperWhiteNits / GS_UIPaperWhiteNits
  #define HDR_MAXEXPECTED GS_TonemapperMaxExpected / GS_GamePaperWhiteNits
#endif

//force BT709 in SDR, just in case.
#if CUSTOM_SDR > 0
  #undef CUSTOM_LUTBUILDER_COLORSPACE
  #define CUSTOM_LUTBUILDER_COLORSPACE 0
#endif

#endif // __COMMON_HLSLI__