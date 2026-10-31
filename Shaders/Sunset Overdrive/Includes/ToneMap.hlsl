// Shared body of the 16 PS_ToneMap* permutations (names from the exe's shader tables).
// The including file defines TONEMAP_VIGNETTE, while the bloom, color correction (3D LUT) and film grain follow the hash.
//
// Vanilla, per channel: scene = scene-linear RGB (g_RadianceMap) * exposure -> 1D tone curve (R16_UNORM, 256 texels over
// log2(scene) in [-8, 5]) -> + bloom (display linear) -> saturate -> sRGB encode -> * g_TM_Brightness -> saturate -> 3D LUT
// (32^3 RGBA8, sRGB in and out) -> film grain lerp -> vignette. The output target is R10G10B10A2 (sRGB encoded), later SMAA
// and PS_CopyBuffer to the swapchain.
//
// Luma HDR runs that chain a second time with its two clips lifted: the tone curve is continued past its inflection with a
// secant read from the bound curve (it reaches 1 at scene ~16.7 and stays flat), and the LUT is extrapolated. Only the relative
// luminance of that working value is kept: the chromaticity (the RGB ratios, with the whitening of the per-channel curve) stays
// the vanilla grade's.
// DICE (by channel, in PQ) then maps the result to the display peak.

#define LUT_3D 1

// 0 is the vanilla SDR tonemap (also in HDR, as the reference), 1 is Luma's HDR one (see main.cpp)
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// The game's Common.hlsl must come first, or the shared headers declare a placeholder GameSettings struct
// clang-format off
#include "Common.hlsl"
#include "../../Includes/DICE.hlsl"
// clang-format on

#define TONEMAP_BLOOM            (_7880CCF1 || _C291251A || _6619803F || _EE7FB6CC || _633A5894 || _E3E7DD84 || _2F20FD47 || _B2BBC089)
#define TONEMAP_COLOR_CORRECTION (_86EE37B6 || _C291251A || _2A6A872A || _EE7FB6CC || _D6B7231C || _E3E7DD84 || _902D30F9 || _B2BBC089)
#define TONEMAP_FILM_GRAIN       (_E4EAD75E || _6619803F || _2A6A872A || _EE7FB6CC || _980ACE18 || _2F20FD47 || _902D30F9 || _B2BBC089)

cbuffer MultiPerViewportCB : register(b0)
{
   float4 g_VP_Unused0[44] : packoffset(c0);
   float g_Frame_NormalMapScale : packoffset(c44);
   float g_Frame_ShaderTimer : packoffset(c44.y);
   float g_Frame_DeltaTime : packoffset(c44.z);
   float g_Frame_Random : packoffset(c44.w);
}

cbuffer PSToneMapCB : register(b2)
{
   float4 g_TM_VignetteParams : packoffset(c0);
   float g_TM_BloomDirtiness : packoffset(c1);
   float g_TM_ColorCorrectionLUTDimension3D : packoffset(c1.y);
   float g_TM_Saturation : packoffset(c1.z);
   float g_TM_PadA : packoffset(c1.w);
   float g_TM_GrainSizeRcp : packoffset(c2);
   float g_TM_GrainStrength : packoffset(c2.y);
   float g_TM_GrainSecondOctaveStrength : packoffset(c2.z);
   float g_TM_ViewportAspectRatio : packoffset(c2.w);
   float g_TM_Brightness : packoffset(c3);
}

SamplerState g_LinearClampSampler_s : register(s2);
SamplerState g_LinearWrapSampler_s : register(s3);
SamplerState g_ToneCurveSampler_s : register(s5);
SamplerState g_RadianceMapSampler_s : register(s6);
Texture1D<float4> g_ToneCurve : register(t5);
Texture2D<float4> g_RadianceMap : register(t6);
StructuredBuffer<float> g_AdaptedLumBuffer : register(t7);
Texture2D<float4> g_BloomMap : register(t8);
Texture2D<float4> g_BloomDirtinessMap : register(t9);
Texture2D<float4> g_BloomLensFlareMap : register(t10);
Texture3D<float4> g_ColorLut3d : register(t12);
Texture2D<float4> g_FilmGrainNoise : register(t13);

// The curve is addressed with log2(scene) / 13 + 8 / 13, kept in [0, 1] as in vanilla by the 1/256 floor of the scene and a min.
float SampleToneCurve(float scene)
{
   return g_ToneCurve.SampleLevel(g_ToneCurveSampler_s, min(log2(max(scene, 1.0 / 256.0)) * (1.0 / 13.0) + (8.0 / 13.0), 1.0), 0).x;
}

// Tangent (secant) continuation above the curve's inflection, measured on the shipped curve (linear output over linear scene):
// the slope peaks at scene ~1.0 (output 0.17, 17% of white), so continuing there is the brightest choice that never dips
// under vanilla. The bound curve is one of three static tables baked in the exe (time of day doesn't change it), all with their
// peak slope at scene ~0.97; the anchors are reads of the bound one, so each table gets its own continuation.
static const float ToneCurvePivot = 1.0;

float ToneCurvePivotSlope()
{
   static const float probeLow = 0.8;
   static const float probeHigh = 1.25;
   return max((SampleToneCurve(probeHigh) - SampleToneCurve(probeLow)) / (probeHigh - probeLow), 0.0);
}

float3 ToneCurveHDR(float3 scene, float3 vanillaCurve)
{
   // Most pixels sit under the pivot: they skip the continuation's three curve reads
   [branch] if (!any(scene > ToneCurvePivot))
   {
      return vanillaCurve;
   }
   return scene > ToneCurvePivot ? SampleToneCurve(ToneCurvePivot) + ToneCurvePivotSlope() * (scene - ToneCurvePivot) : vanillaCurve;
}

float3 SampleFilmGrain(float2 uv)
{
   float sinA, cosA;
   sincos(g_Frame_Random * -59.0, sinA, cosA);
   float2 centered = float2(uv.x - g_Frame_Random * 59.0, uv.y - g_Frame_Random * 127.0);
   float2 rotated = float2(centered.x * cosA - sinA * centered.y, centered.y * cosA + sinA * centered.x);
   const float2 uvFine = (g_Frame_Random * float2(59.0, 127.0) + rotated) * g_TM_GrainSizeRcp * 0.750187576;
   const float3 grainFine = g_FilmGrainNoise.Sample(g_LinearWrapSampler_s, uvFine).xyz;

   sincos(g_Frame_Random, sinA, cosA);
   centered = float2(uv.x * g_TM_ViewportAspectRatio - g_Frame_Random * 59.0, uv.y - g_Frame_Random * 127.0);
   rotated = float2(centered.x * cosA - sinA * centered.y, centered.y * cosA + sinA * centered.x);
   const float2 uvCoarse = (g_Frame_Random * float2(59.0, 127.0) + rotated) * g_TM_GrainSizeRcp;
   const float3 grainCoarse = g_FilmGrainNoise.Sample(g_LinearWrapSampler_s, uvCoarse).xyz;

   return lerp(grainCoarse, grainFine, g_TM_GrainSecondOctaveStrength);
}

void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float4 o0 : SV_TARGET0)
{
   // The user exposure on top of the adapted one, before the tone curve (SDR and HDR)
   const float3 scene = g_RadianceMap.Sample(g_RadianceMapSampler_s, v1).xyz * g_AdaptedLumBuffer[1] * LumaSettings.GameSettings.Exposure;
   const float3 vanillaCurve = float3(SampleToneCurve(scene.r), SampleToneCurve(scene.g), SampleToneCurve(scene.b));

   float3 bloom = 0.0;
#if TONEMAP_BLOOM
   const float3 dirt = g_BloomDirtinessMap.Sample(g_LinearClampSampler_s, v1).xyz * g_TM_BloomDirtiness + 1.0;
   bloom = g_BloomMap.Sample(g_LinearClampSampler_s, v1).xyz * dirt * LumaSettings.GameSettings.BloomIntensity + g_BloomLensFlareMap.Sample(g_LinearClampSampler_s, v1).xyz;
#endif

   // The vanilla grade, sRGB encoded
   float3 color = saturate(linear_to_sRGB_gamma(saturate(vanillaCurve + bloom)) * g_TM_Brightness);
#if TONEMAP_COLOR_CORRECTION
   color = lerp(color, SampleLUT(g_ColorLut3d, g_LinearClampSampler_s, color, (uint)g_TM_ColorCorrectionLUTDimension3D), LumaSettings.GameSettings.ColorGradingIntensity);
#endif

   if (TONEMAP_TYPE >= 1 && LumaSettings.DisplayMode == 1)
   {
      // The working value: the game's g_TM_Brightness gain on the sRGB-encoded value, as in vanilla, without its clip
      const float3 workingEncoded = linear_to_sRGB_gamma(ToneCurveHDR(scene, vanillaCurve) + bloom, GCT_MIRROR) * g_TM_Brightness;
      float3 workingLinear = gamma_sRGB_to_linear(workingEncoded, GCT_MIRROR);
#if TONEMAP_COLOR_CORRECTION
      LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
      extrapolationData.inputColor = workingEncoded;
      LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
      extrapolationSettings.lutSize = 0;
      extrapolationSettings.inputLinear = false;
      extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
      extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
      extrapolationSettings.extrapolationQuality = 2;
      workingLinear = lerp(workingLinear, SampleLUTWithExtrapolation(g_ColorLut3d, g_LinearClampSampler_s, extrapolationData, extrapolationSettings), LumaSettings.GameSettings.ColorGradingIntensity);
#endif
      // The vanilla grade's chromaticity at the working value's BT.709 relative luminance. Below the curve's pivot and inside the LUT both chains
      // match, so the gain is 1 there. A (near) black vanilla grade stays black.
      float3 gradedLinear = RestoreLuminance(gamma_sRGB_to_linear(color), workingLinear, true, CS_BT709);
      // Contrast (a power around mid gray) and saturation go before DICE
      const float contrast = LumaSettings.GameSettings.Contrast;
      [branch] if (contrast != 1.0)
      {
         gradedLinear *= pow(max(GetLuminance(gradedLinear, CS_BT709), FLT_MIN) / MidGray, contrast - 1.0);
      }
      const float saturation = LumaSettings.GameSettings.Saturation;
      [branch] if (saturation != 1.0)
      {
         gradedLinear = Saturation(gradedLinear, saturation, CS_BT709);
      }
      const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
      const float peakWhite = max(PeakWhiteNits, GamePaperWhiteNits) / sRGB_WhiteLevelNits;
      gradedLinear = DICETonemap(gradedLinear * paperWhite, peakWhite, DefaultDICESettings(DICE_TYPE_BY_CHANNEL_PQ)) / paperWhite;
      color = linear_to_sRGB_gamma(gradedLinear, GCT_MIRROR);
   }

#if TONEMAP_FILM_GRAIN
   // Vanilla lerps towards the grain texture. In HDR only the SDR part of the offset is kept, so highlights aren't pulled to it.
   color += g_TM_GrainStrength * LumaSettings.GameSettings.FilmGrainIntensity * (SampleFilmGrain(v1) - saturate(color));
#endif

#if TONEMAP_VIGNETTE
   const float2 edge = v1 * (1.0 - v1);
   const float vignette = saturate(exp2(log2(saturate(4.0 * edge.x * edge.y)) * g_TM_VignetteParams.x) * g_TM_VignetteParams.y + g_TM_VignetteParams.z);
   color *= vignette;
#endif

   o0 = float4(color, 1.0);
}
