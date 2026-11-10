// The game's HDR10 present for loading screens and movies, with the HDR fix (see "Includes/Common.hlsl"). No 1D output LUT here:
// the art is sRGB encoded. All its permutations share this body, their hashes are listed in main.cpp ("redirected_shader_hashes")
// and each one selects its axes below:
//   MEA_LOADING_SCENE_SAMPLER  the scene sampler: 0 = s0, 1 = s1
//   MEA_LOADING_FILTER         the scene fetch: 0 = one bilinear tap, 1 = Keys cubic (Resolution Scale below 100%)
//   MEA_OUTPUT_SDR             1 = the SDR twin (in-game HDR off, sRGB out): vanilla, with the optional dithering
// The three output gamuts of each permutation share a hash: the output is always BT.2020 HDR10.

#include "Includes/Common.hlsl"

#if _A4E53513 || _B78EE6A9 || _EF390C6B || _F5B7A93D
#define MEA_LOADING_SCENE_SAMPLER 0
#else
#define MEA_LOADING_SCENE_SAMPLER 1
#endif

#if _4739F9F6 || _48E0CE55 || _4ACE2324 || _667C56AF
#define MEA_LOADING_FILTER 1
#else
#define MEA_LOADING_FILTER 0
#endif

#if _4739F9F6 || _7C642BA7 || _EF390C6B
#define MEA_OUTPUT_SDR 1
#else
#define MEA_OUTPUT_SDR 0
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
#if MEA_LOADING_FILTER
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
