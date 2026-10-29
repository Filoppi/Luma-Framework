// Game-local Common: defines LumaGameSettings before the shared Settings.hlsl declares the LumaSettings cbuffer.
// clang-format off
// ORDER MATTERS: sorted, the shared Common comes first and its dummy LumaGameSettings wins (X3018 on every field).
#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
// clang-format on

#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// Whether the scene takes the Luma HDR path. TONEMAP_TYPE 0 keeps the vanilla SDR output (dither and clips included) on HDR displays too.
#define SRTTR_HDR_SCENE (TONEMAP_TYPE >= 1 && LumaSettings.DisplayMode == 1)
// Whether the Luma output dither runs (SRTTR_DitherOutput). TONEMAP_TYPE 0 is the vanilla reference: its Bayer dither stays and nothing is added.
#define SRTTR_LUMA_DITHER (TONEMAP_TYPE >= 1 && LumaSettings.GameSettings.Dithering > 0.5)
// Whether the vanilla SDR 4x4 Bayer dither runs at all: the Luma output dither replaces it.
#define SRTTR_VANILLA_BAYER (!SRTTR_HDR_SCENE && !SRTTR_LUMA_DITHER)
// Whether the tonemap writes that dither into the scene. Not when compose sharpens the scene (RCAS would amplify the
// pattern): compose applies it after sharpening instead, on the graded colour rather than before the LUT.
#define SRTTR_BAYER_IN_SCENE (SRTTR_VANILLA_BAYER && LumaSettings.GameSettings.RCASSharpness <= 0.0)

// The vanilla 4x4 Bayer dither: the tonemap multiplies its SDR output by "Bayer * scale + 1", compose divides it back out in the darks.
static const float kBayer4x4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static const float3 kBayerScale = float3(1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0);

float SRTTR_Bayer(uint2 pixel)
{
   return kBayer4x4[((pixel.x & 3u) << 2) | (pixel.y & 3u)] * 0.125 - 1.0;
}

// Anti-banding dither at the last scene pass, one step of the output quantizer: the 8-bit code in SDR, 10-bit BT.2020
// PQ in HDR and SDR on HDR (as the other Luma mods). The input is gamma code with 1 = UI paper white (UI_DRAW_TYPE 2).
void SRTTR_DitherOutput(inout float3 color, float2 uv)
{
   if (!SRTTR_LUMA_DITHER)
   {
      return;
   }
   [branch] if (LumaSettings.DisplayMode != 0)
   {
      // The PQ code steps sit where the display composition maps 1 to UI paper white
      const float pqScale = max(LumaSettings.UIPaperWhiteNits, 1.0) / HDR10_MaxWhiteNits;
      float3 pq = Linear_to_PQ(BT709_To_BT2020(gamma_to_linear(color, GCT_MIRROR) * pqScale), GCT_MIRROR);
      ApplyDithering(pq, uv, true, 1.0, 10u, LumaSettings.FrameIndex, true);
      color = linear_to_gamma(BT2020_To_BT709(PQ_to_Linear(pq, GCT_MIRROR)) / pqScale, GCT_MIRROR);
   }
   else
   {
      ApplyDithering(color, uv, true, 1.0, 8u, LumaSettings.FrameIndex, true);
   }
}

// UI_DRAW_TYPE 2: the display composition scales the whole canvas by UI paper white, so the scene, which the GUI layer is
// blended over in gamma space, is pre-scaled to land back at game paper white
float SRTTR_UIPaperWhitePreScale()
{
#if UI_DRAW_TYPE >= 2
   return LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#else
   return 1.0;
#endif
}
