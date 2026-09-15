// Mass Effect (2007) - game-local Common. Include this instead of "../Includes/Common.hlsl": it defines
// LUMA_GAME_CB_STRUCTS (via GameCBuffers.hlsl) BEFORE Settings.hlsl, so GameSettings is the real grade struct.

// clang-format off
// ORDER IS LOAD-BEARING - GameCBuffers must define LUMA_GAME_CB_STRUCTS before Settings.hlsl is pulled in below.
#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
// clang-format on

// UI_DRAW_TYPE 2, for any linear content the passes write onto the HUD's canvas (the grade, the movies): pre-scale so
// it lands at GamePaperWhite after composition rescales the canvas by UIPaperWhite. Guarded: an unset
// GamePaperWhiteNits would black the scene and leave the HUD, i.e. "the 3D disappeared".
float3 PreScaleForUIPaperWhite(float3 linearColor)
{
#if UI_DRAW_TYPE >= 2
   if (LumaSettings.GamePaperWhiteNits > 0.0)
      linearColor *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif
   return linearColor;
}
