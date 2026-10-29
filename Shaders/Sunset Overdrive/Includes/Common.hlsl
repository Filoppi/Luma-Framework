// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers

// Define the game custom cbuffer structs
#include "GameCBuffers.hlsl"
// Global common
#include "../../Includes/Common.hlsl"
// Game specific settings
#include "Settings.hlsl"

// The display composition's transfer functions
#include "../../Includes/ColorGradingLUT.hlsl"

// The scene and the full-screen videos reach the swapchain scaled by GamePaperWhite / UIPaperWhite (UI_DRAW_TYPE 2), so the Scaleform
// UI blended on top of them lands at UIPaperWhite once the display composition rescales the whole buffer by it. The scale goes through
// the composition's own decode (gamma 2.2 within 0-1, sRGB beyond), so the scene keeps its level exactly.
float3 PreScaleForUIPaperWhite(float3 encoded)
{
#if UI_DRAW_TYPE >= 2
   [branch] if (GamePaperWhiteNits != UIPaperWhiteNits)
   {
      const float3 composed = ColorGradingLUTTransferFunctionOutCorrected(encoded, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE);
      encoded = ColorGradingLUTTransferFunctionInCorrected(composed * (GamePaperWhiteNits / max(UIPaperWhiteNits, 1.0)), GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE);
   }
#endif
   return encoded;
}
