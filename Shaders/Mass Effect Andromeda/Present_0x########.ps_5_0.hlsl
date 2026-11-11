// The game's HDR10 present (gameplay, menus), with the HDR fix (see "Includes/Common.hlsl"). All its permutations share this
// body, their hashes are listed in main.cpp ("redirected_shader_hashes") and each one selects its axes below:
//   MEA_PRESENT_LUT3D   1 = a 32^3 calibration LUT at t5 (PQ to PQ) before the 1D output LUT
//   MEA_PRESENT_SCALED  0 = the scene is at the output size (sampler s0), 1 = smaller (sampled through s1, no s0)
//   MEA_PRESENT_FILTER  the scene fetch: 0 = one bilinear tap, 1 = Keys cubic, 2 = Keys cubic in y, 3 = B-spline
//   MEA_OUTPUT_SDR      1 = the SDR twin (in-game HDR off, sRGB out): vanilla, with the optional dithering
// The three output gamuts of each permutation (BT.2020, DCI-P3, no matrix) share a hash: the output is always BT.2020 HDR10.

#include "Includes/Common.hlsl"

#if _0BF0C1CA || _0E11A4EE || _19404650 || _20EE9B0F || _22179331 || _3196D1D2 || _66868010 || _6BDFE71C || _6DA050CF || _6F7FC946 || _7D79A746 || _9201B072 || _9E9044E7 || _A1B754F2 || _A888DCF8 || _ACA466A8 || _BE69B105 || _BF88A9F8 || _E0AA30A6 || _FED81CFB
#define MEA_PRESENT_LUT3D 1
#else
#define MEA_PRESENT_LUT3D 0
#endif

#if _19404650 || _3196D1D2 || _4840CCE4 || _9FCE7944 || _BE69B105 || _D0200425 || _F5B0DBFA || _FED81CFB
#define MEA_PRESENT_SCALED 0
#else
#define MEA_PRESENT_SCALED 1
#endif

#if _0BF0C1CA || _22179331 || _5C1B1CD3 || _6A9767E9 || _8FD42D7C || _A1B754F2 || _AFFFA4AB || _BF88A9F8
#define MEA_PRESENT_FILTER 1
#elif _4091E710 || _66868010 || _6DA050CF || _6F7FC946 || _9E9044E7 || _A04BFC23 || _E1CF4FA8 || _EE044AAF
#define MEA_PRESENT_FILTER 2
#elif _0BF80A15 || _0E11A4EE || _4BCF53E3 || _7D79A746 || _8ECC1073 || _9201B072 || _ACA466A8 || _D30EB54B
#define MEA_PRESENT_FILTER 3
#else
#define MEA_PRESENT_FILTER 0
#endif

#if _0E11A4EE || _19404650 || _4091E710 || _4840CCE4 || _5C1B1CD3 || _6DA050CF || _8ECC1073 || _9BFA3175 || _BF88A9F8 || _E0AA30A6
#define MEA_OUTPUT_SDR 1
#else
#define MEA_OUTPUT_SDR 0
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
   // The cubic filters read rgb only and output an alpha of 1, as the vanilla ones
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
