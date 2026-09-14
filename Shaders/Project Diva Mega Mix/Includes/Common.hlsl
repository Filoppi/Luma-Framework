#ifndef __COMMON_HLSL__
#define __COMMON_HLSL__

#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings
#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits

#endif