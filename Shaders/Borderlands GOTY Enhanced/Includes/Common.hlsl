// Include this instead of the shared "../Includes/Common.hlsl" from any shader that reads the per-game LumaGameSettings: it defines
// LUMA_GAME_CB_STRUCTS through GameCBuffers.hlsl before the shared Settings.hlsl declares the LumaSettings cbuffer, so GameSettings
// is the real grade struct rather than the empty default.

#include "GameCBuffers.hlsl"
// Keep after GameCBuffers.hlsl (see above); this comment also stops clang-format from sorting the two
#include "../../Includes/Common.hlsl"

// UI_DRAW_TYPE 2, for scene content written under a gamma SDR UI on the same canvas: the display composition scales the whole canvas
// by UI Paper White, so scene content pre-scaled by this lands back at Game Paper White. 1 in the other UI draw types.
float GetUIPaperWhitePreScale(float gamePaperWhiteNits, float uiPaperWhiteNits)
{
#if UI_DRAW_TYPE >= 2
   return gamePaperWhiteNits / max(uiPaperWhiteNits, 1.0);
#else
   return 1.0;
#endif
}
