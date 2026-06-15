// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers

#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

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