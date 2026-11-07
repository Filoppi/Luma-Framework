#include "Includes/Common.hlsl"

// PS_CopyBuffer is a plain copy, which the engine also uses outside the frame end. Its draw of the scene onto the swapchain (scaled to
// the window, before the UI) is flagged through LumaData.CustomData1, and is where the scene gets its UI paper white pre-scale and its
// one dither.

SamplerState g_CopyBufferMapSampler_s : register(s6);
Texture2D<float4> g_CopyBufferMap : register(t6);

void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float4 o0 : SV_TARGET0)
{
   o0 = g_CopyBufferMap.Sample(g_CopyBufferMapSampler_s, v1);

   if (LumaData.CustomData1 == 0)
   {
      return;
   }
   o0.rgb = PreScaleForUIPaperWhite(o0.rgb);
   if (LumaSettings.GameSettings.Dithering <= 0.5)
   {
      return;
   }
   // One step of the output quantizer: an 8 bit code in SDR, 10 bit BT.2020 PQ in HDR and SDR on HDR.
   // The value is the game's sRGB-encoded color, which the display composition decodes with gamma 2.2 and scales by the UI paper white
   // (after the pre-scale above).
   [branch] if (LumaSettings.DisplayMode != 0)
   {
      // The PQ code steps sit where the composition maps 1 to that paper white
      const float pqScale = max(UI_DRAW_TYPE >= 2 ? UIPaperWhiteNits : GamePaperWhiteNits, 1.0) / HDR10_MaxWhiteNits;
      float3 pq = Linear_to_PQ(BT709_To_BT2020(gamma_to_linear(o0.rgb, GCT_MIRROR) * pqScale), GCT_MIRROR);
      ApplyDithering(pq, v1, true, 1.0, 10u, LumaSettings.FrameIndex, true);
      o0.rgb = linear_to_gamma(BT2020_To_BT709(PQ_to_Linear(pq, GCT_MIRROR)) / pqScale, GCT_MIRROR);
   }
   else
   {
      ApplyDithering(o0.rgb, v1, true, 1.0, 8u, LumaSettings.FrameIndex, true);
   }
}
