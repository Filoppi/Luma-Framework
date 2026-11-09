// The game's HDR10 present (gameplay, menus), with the HDR fix (see Common.hlsl). Every row is the same body; its wrappers set
// the axes:
//   MEA_PRESENT_LUT3D   1 = a 32^3 calibration LUT at t5 (PQ to PQ) before the 1D output LUT
//   MEA_PRESENT_SCALED  0 = the scene is at the output size (sampler s0), 1 = smaller (sampled through s1, no s0)
//   MEA_PRESENT_FILTER  the scene fetch: 0 = one bilinear tap, 1 = Keys cubic, 2 = Keys cubic in y, 3 = B-spline
//   MEA_OUTPUT_SDR      1 = the row's SDR twin (in-game HDR off, sRGB out): vanilla, with the optional dithering
// The three output gamuts of each row (BT.2020, DCI-P3, no matrix) share a wrapper: the output is always BT.2020 HDR10.

#include "Common.hlsl"

#ifndef MEA_OUTPUT_SDR
#define MEA_OUTPUT_SDR 0
#endif

#if !defined(MEA_PRESENT_LUT3D) || !defined(MEA_PRESENT_SCALED) || !defined(MEA_PRESENT_FILTER)
#error "Define MEA_PRESENT_LUT3D, MEA_PRESENT_SCALED and MEA_PRESENT_FILTER before including Present.hlsl"
#endif

Texture2D<float4> sceneTexture : register(t0);
Texture2D<float4> uiTexture : register(t1); // An sRGB view: linear on read
Texture1D<float4> outputLut : register(t2);
#if MEA_PRESENT_LUT3D
Texture3D<float4> calibrationLut : register(t5);
#endif

#if !MEA_PRESENT_SCALED
SamplerState sceneSampler : register(s0);
#define MEA_SCENE_SAMPLER sceneSampler
#else
#define MEA_SCENE_SAMPLER lutSampler
#endif
SamplerState lutSampler : register(s1); // Bilinear: the 1D LUT is read at the raw value, not at texel centres
SamplerState uiSampler : register(s2);

cbuffer cbData : register(b0)
{
   float4 cbData[3] : packoffset(c0); // [0] = source size, 1 / source size; [2] = scene scale, UI gate, UI alpha scale
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
   // The cubic rows read rgb only and output an alpha of 1, as the vanilla ones
#if MEA_PRESENT_FILTER == 0
   float4 scene = sceneTexture.SampleLevel(MEA_SCENE_SAMPLER, texcoord, 0);
#elif MEA_PRESENT_FILTER == 1
   float4 scene = float4(SampleKeysCubic(sceneTexture, MEA_SCENE_SAMPLER, texcoord, cbData[0]), 1.0);
#elif MEA_PRESENT_FILTER == 2
   float4 scene = float4(SampleKeysCubicY(sceneTexture, MEA_SCENE_SAMPLER, texcoord, cbData[0]), 1.0);
#else
   float4 scene = SampleBSpline(sceneTexture, MEA_SCENE_SAMPLER, texcoord, cbData[0]);
#endif
   scene *= cbData[2].x;

   float3 color = max(scene.rgb, 0.0);
#if MEA_PRESENT_LUT3D
   color = calibrationLut.SampleLevel(lutSampler, color * (31.0 / 32.0) + (0.5 / 32.0), 0).rgb;
#endif
   color = float3(outputLut.SampleLevel(lutSampler, color.r, 0).r, outputLut.SampleLevel(lutSampler, color.g, 0).r, outputLut.SampleLevel(lutSampler, color.b, 0).r);

   float4 ui = uiTexture.SampleLevel(uiSampler, texcoord, 0);
   // cbData[2].y gates the UI layer: "Hide Gameplay UI" closes it, unless a movie was decoded into it this frame
   float4 ui_params = cbData[2];
   if (LumaSettings.GameSettings.HideUI != 0.0 && LumaSettings.GameSettings.VideoActive == 0.0)
   {
      ui_params.y = 0.0;
   }
#if MEA_OUTPUT_SDR
   // The UI premultiplied over the scene, weighted by the square of its inverse coverage, in linear
   float alpha = scene.a;
   if (ui_params.y > 0.0)
   {
      const float t = 1.0 - ui.a * ui_params.z;
      color = color * (t * t) + ui.rgb * ui_params.y;
      alpha = alpha * (t * t) + 1.0 - (t * t);
   }
   return OutputSDR(linear_to_sRGB_gamma(saturate(color), GCT_NONE), alpha, texcoord);
#endif
   if (GammaCorrectionEnabled() && ui_params.y > 0.0) // Only the UI's own branch of "OutputHDR10" reads it
   {
      ui.rgb = EmulateGamma22(ui.rgb);
   }
   return OutputHDR10(max(color, 0.0), scene.a, ui, ui_params, texcoord);
}
