// The game's HDR10 present for loading screens and movies, with the HDR fix (see Common.hlsl). No 1D output LUT here: the art is
// sRGB encoded. The wrappers set the axes:
//   MEA_LOADING_SCENE_SAMPLER  the scene sampler: 0 = s0, 1 = s1
//   MEA_LOADING_FILTER         the scene fetch: 0 = one bilinear tap, 1 = Keys cubic (Resolution Scale below 100%)
//   MEA_OUTPUT_SDR             1 = the row's SDR twin (in-game HDR off, sRGB out): vanilla, with the optional dithering
// The three output gamuts of each row share a wrapper: the output is always BT.2020 HDR10.

#include "Common.hlsl"

#ifndef MEA_OUTPUT_SDR
#define MEA_OUTPUT_SDR 0
#endif

#if !defined(MEA_LOADING_SCENE_SAMPLER) || !defined(MEA_LOADING_FILTER)
#error "Define MEA_LOADING_SCENE_SAMPLER and MEA_LOADING_FILTER before including Loading.hlsl"
#endif

Texture2D<float4> sceneTexture : register(t0);
Texture2D<float4> uiTexture : register(t1); // Unorm, sRGB encoded (movies are decoded into it)

#if MEA_LOADING_SCENE_SAMPLER
SamplerState sceneSampler : register(s1);
#else
SamplerState sceneSampler : register(s0);
#endif
SamplerState uiSampler : register(s2);

cbuffer cbData : register(b0)
{
   float4 cbData[3] : packoffset(c0); // [0] = source size, 1 / source size; [2] = scene scale, UI gate, UI alpha scale
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
#if MEA_LOADING_FILTER == 1
   float4 scene = float4(SampleKeysCubic(sceneTexture, sceneSampler, texcoord, cbData[0]), 1.0);
#else
   float4 scene = sceneTexture.SampleLevel(sceneSampler, texcoord, 0);
#endif
   scene *= cbData[2].x;

   float4 ui = uiTexture.SampleLevel(uiSampler, texcoord, 0);
#if MEA_OUTPUT_SDR
   // Composed in sRGB encoded space, with the scene weighted by the inverse UI coverage (not squared). The vanilla shader decodes,
   // saturates and re-encodes, which is the same as saturating the encoded value.
   if (cbData[2].y > 0.0)
   {
      scene = scene * (1.0 - ui.a * cbData[2].z) + ui * cbData[2].yyyz;
   }
   return OutputSDR(scene.rgb, scene.a, texcoord);
#endif
   ui.rgb = GammaCorrectionEnabled() ? gamma_to_linear(max(ui.rgb, 0.0), GCT_NONE, 2.2) : gamma_sRGB_to_linear(max(ui.rgb, 0.0), GCT_NONE);
   // A movie fills the UI layer: it goes to the scene's paper white (OutputHDR10 scales the UI by the UI white), with the optional
   // AutoHDR. "Video HDR Boost" 0 puts the AutoHDR peak at sRGB white, where it's a no-op.
   if (HDRFixEnabled() && LumaSettings.GameSettings.VideoActive != 0.0)
   {
      ui.rgb = VideoAutoHDR(ui.rgb, LumaSettings.GameSettings.VideoAutoHDREnable != 0.0, LumaSettings.GameSettings.VideoAutoHDRBoost, SceneWhiteNits());
      ui.rgb *= SceneWhiteNits() / UIWhiteNits();
   }
   return OutputHDR10(gamma_sRGB_to_linear(max(scene.rgb, 0.0), GCT_NONE), scene.a, ui, cbData[2], texcoord);
}
