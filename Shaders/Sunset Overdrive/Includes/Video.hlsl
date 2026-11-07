// Shared tail of the Bink video replacements (PS_BinkYCbCr, PS_BinkNV12 and PS_BinkYCbCrA). They convert BT.601 limited range Y'CbCr
// to R'G'B' with the vanilla matrix, which is "YUVtoRGB"'s type 3.
// Full-screen movies draw straight into the swapchain, pre-scaled like the scene so they land at the game's paper white under the UI.
// main.cpp sets LumaData.CustomData1 to 1 only there, the only target that keeps AutoHDR highlights above 1.
// The AutoHDR stays conservative because the videos are low bitrate and compression artifacts blow up when pushed hard.

#include "Common.hlsl"

float3 VideoOutput(float3 color)
{
   // The vanilla 8-bit targets clamped the conversion (negatives and whites up to 1.25)
   color = saturate(color);
   [branch] if (LumaData.CustomData1 != 0)
   {
      [branch] if (LumaSettings.DisplayMode == 1 && LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
      {
         // "Boost" 0 puts the AutoHDR peak at sRGB white (a no-op), 1 at 250 nits: higher exposes the movies' compression blocks
         const float peakNits = lerp(sRGB_WhiteLevelNits, 250.0, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
         color = linear_to_gamma(PumboAutoHDR(gamma_to_linear(color), peakNits, LumaSettings.GamePaperWhiteNits), GCT_NONE);
      }
      color = PreScaleForUIPaperWhite(color);
   }
   return color;
}
