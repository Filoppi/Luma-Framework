// Medal of Honor: Airborne - game-local Common. Include this instead of "../Includes/Common.hlsl": it defines
// LUMA_GAME_CB_STRUCTS (via GameCBuffers.hlsl) BEFORE Settings.hlsl, so GameSettings is the real grade struct.

// Define the game custom cbuffer structs.
#include "GameCBuffers.hlsl"
// Shared global common (pulls in the shared Settings.hlsl -> LumaSettings cbuffer with our GameSettings).
#include "../../Includes/Common.hlsl"

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
