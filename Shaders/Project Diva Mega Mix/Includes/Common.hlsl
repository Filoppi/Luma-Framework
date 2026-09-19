#ifndef __COMMON_HLSL__
#define __COMMON_HLSL__

//CUSTOM_SDR_1 defs
#if CUSTOM_SDR_1 == 1
  #ifdef CUSTOM_GAMMACORRECT22
    #undef CUSTOM_GAMMACORRECT22
  #endif
  #define CUSTOM_GAMMACORRECT22 0

  #ifdef CUSTOM_FAKEBT2020
    #undef CUSTOM_FAKEBT2020
  #endif
  #define CUSTOM_FAKEBT2020 0

  #ifdef CUSTOM_COLORGRADE
    #undef CUSTOM_COLORGRADE
  #endif
  #define CUSTOM_COLORGRADE 0

  #ifdef CUSTOM_COLORGRADE_SATORDER
    #undef CUSTOM_COLORGRADE_SATORDER
  #endif
  #define CUSTOM_COLORGRADE_SATORDER 0
#endif

// Enocding defs
#if POST_PROCESS_SPACE_TYPE != 1
  THIS_SHOULDNT_HAPPEN
#endif

#if VANILLA_ENCODING_TYPE != 0
  THIS_SHOULDNT_HAPPEN
#endif

#if EARLY_DISPLAY_ENCODING != 0
  THIS_SHOULDNT_HAPPEN
#endif

#if GAMMA_CORRECTION_TYPE_HASH != 0
	THIS_SHOULDNT_HAPPEN
#endif

#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings
#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits
#define HDR_STOPS log2(HDR_PEAK)

#define LUT_CACHE_OUTPUT_SIZE 2048 // remember CPU-side too!

#endif