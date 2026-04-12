#include "Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

Texture2D<float4> g_Texture0 : register(t0); // Last pass output (tonemap, cutscene overlay, or PostSREncode)

void main(float4 v0: SV_Position0, out float4 o0: SV_Target0)
{
   float4 color = g_Texture0.Load(int3(v0.xy, 0));

   // SDR mode: pass through unchanged (UIEncode runs unconditionally to simplify resource chain)
   if (LumaSettings.DisplayMode == 0)
   {
      o0 = color;
      return;
   }

   // Input is always gamma after PostSREncode or direct from tonemap.
   // Linearize for UI paper-white scaling, then re-gamma encode.
   float3 color_linear = gamma_sRGB_to_linear(color.rgb, GCT_MIRROR);

#if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and
                      // then multiply it back to its intended range
   ColorGradingLUTTransferFunctionInOutCorrected(color_linear, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
   color_linear *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
   ColorGradingLUTTransferFunctionInOutCorrected(color_linear, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
#endif

   color.rgb = linear_to_sRGB_gamma(color_linear, GCT_MIRROR);
   // Alpha already set by PostSREncode

   o0 = color;
}
