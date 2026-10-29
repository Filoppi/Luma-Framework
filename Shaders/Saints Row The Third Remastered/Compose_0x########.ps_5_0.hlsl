#include "../Includes/RCAS.hlsl"
#include "Includes/Common.hlsl"

// hdr_filter compose of the tonemapped scene, the separate GUI layer and film noise onto the swapchain. Hashes in
// main.cpp: the SDR perm and every HDR_DISPLAY scene perm, so the game's own HDR setting (its NVAPI/AGS scRGB
// output, brightness, saturation, compare and calibration views) no longer changes the image: all of them run the
// SDR perm's math and Luma owns the display encoding.
// Vanilla divides the tonemap's 4x4 Bayer dither back out in the darks; that step only runs while the vanilla dither does
// (SDR, Luma Dithering off). The GUI blend stays in gamma space, as vanilla (UI_DRAW_TYPE 2 scales the scene instead).
// "Hide Gameplay UI" skips the GUI layer (Bink videos are drawn in it too).
// RCAS Sharpness sharpens the scene here, after anti-aliasing and before grain and GUI (the game's own sharpen is disabled).

cbuffer CB_CUSTOM : register(b10)
{
   float4 cb10[5]; // COMPOSE_PARAMS
}

#define NoiseLevel  cb10[0].x
#define NoiseOffset asint(cb10[1].zw)

Texture2D<float4> SceneImage : register(t0);
Texture2D<float4> GuiImage : register(t1);
Texture2D<float4> NoiseTexture : register(t4);
Texture2D<float2> UnusedMotionVectors : register(t9); // RCAS only reads it with dynamic sharpening, which is off
SamplerState Sampler_Linear_CC : register(s2);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
   float3 scene;
   [branch] if (LumaSettings.GameSettings.RCASSharpness > 0.0)
   {
      // The scene is gamma code with 1 = UI paper white (UI_DRAW_TYPE 2). RCAS's overshoot limiter assumes a peak of 1,
      // so in HDR it is normalized to the display peak in the same encoding.
      const float peak = LumaSettings.DisplayMode == 1 ? pow(LumaSettings.PeakWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0), 1.0 / DefaultGamma) : 1.0;
      uint2 size;
      SceneImage.GetDimensions(size.x, size.y);
      scene = RCAS(int2(pos.xy), 0, int2(size) - 1, LumaSettings.GameSettings.RCASSharpness, SceneImage, UnusedMotionVectors, peak).rgb;
   }
   else
   {
      scene = SceneImage.SampleLevel(Sampler_Linear_CC, uv, 0).rgb;
   }

   if (SRTTR_VANILLA_BAYER)
   {
      const float bayer = SRTTR_Bayer(uint2(pos.xy));
      if (!SRTTR_BAYER_IN_SCENE)
      {
         scene *= bayer * kBayerScale + 1.0;
      }
      const float weight = saturate(dot(scene, float3(0.3, 0.5, 0.2)) * -2.0 + 2.0);
      scene /= (bayer * weight) * kBayerScale + 1.0;
   }

   const uint2 noisePos = uint2(float2(NoiseOffset << 3) + pos.xy) & 63u;
   scene += (abs(NoiseTexture.Load(int3(noisePos, 0)).x) * 2.0 - 1.0) * NoiseLevel * LumaSettings.GameSettings.FilmGrainIntensity;

   if (LumaSettings.GameSettings.HideGameplayUI <= 0.5)
   {
      const float4 gui = GuiImage.SampleLevel(Sampler_Linear_CC, uv, 0);
      scene = gui.a * (gui.rgb - scene) + scene;
   }
   SRTTR_DitherOutput(scene, uv);
   return float4(scene, 1.0);
}
