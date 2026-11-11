#include "../Includes/Common.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Oklab.hlsl"

#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

#ifndef IMPROVED_BLOOM
#define IMPROVED_BLOOM 1
#endif

#ifndef IMPROVED_TONEMAPPING_TYPE
#define IMPROVED_TONEMAPPING_TYPE 1
#endif

#ifndef IMPROVED_COLOR_GRADING_TYPE
#define IMPROVED_COLOR_GRADING_TYPE 0
#endif

#ifndef ENABLE_SDR_COLOR_GRADING
#define ENABLE_SDR_COLOR_GRADING 1
#endif

#ifndef ENABLE_FXAA
#define ENABLE_FXAA 1
#endif

cbuffer _Globals : register(b0)
{
  float4 vPreColorCorrectionMatrix[3] : packoffset(c0);
  float3 vColorScale : packoffset(c3) = {1,1,1};
  float3 vSaturationScale : packoffset(c4) = {1,1,1};
#if _2FF9D5DD
  float2 SimulateHDRParams : packoffset(c5);
  float2 vToneCurvCol2Coord : packoffset(c5.z) = {0.99609375,0.001953125};
#elif _D57D4B9D
  float4 vScreenSize : packoffset(c5) = {1920,1080,1920,1080};
  float4 vSpotParams : packoffset(c6) = {960,540,450,600};
  float fLimbDarkening : packoffset(c7) = {755364.125};
  float fLimbDarkeningWeight : packoffset(c7.y) = {0};
  float2 SimulateHDRParams : packoffset(c7.z);
  float2 vToneCurvCol2Coord : packoffset(c8) = {0.99609375,0.001953125};
#elif _680C78BB
  float4 vSpotParams : packoffset(c5) = {960,540,450,600};
  float fLimbDarkeningWeight : packoffset(c6) = {0};
  float2 SimulateHDRParams : packoffset(c6.y);
  float2 vToneCurvCol2Coord : packoffset(c7) = {0.99609375,0.001953125};
#endif
}

SamplerState smplAdaptedLumLast_s : register(s0); // Point sampler
SamplerState smplScene_s : register(s1); // Linear sampler
SamplerState smplLightShaftLinWork2_s : register(s2); // Linear sampler
#if _680C78BB
SamplerState smplTexLimbDarkening_s : register(s3); // Linear sampler
SamplerState sampToneCurv_s : register(s4); // Linear sampler
#else
SamplerState sampToneCurv_s : register(s3); // Linear sampler
#endif
Texture2D<float> smplAdaptedLumLast_Tex : register(t0); // Exposure
Texture2D<float4> smplScene_Tex : register(t1); // Scene
Texture2D<float4> smplLightShaftLinWork2_Tex : register(t2); // Bloom
#if _680C78BB
Texture2D<float4> smplTexLimbDarkening_Tex : register(t3); // Vignette
Texture2D<float4> sampToneCurv_Tex : register(t4); // LUT (256x1). This is R11G11B10_FLOAT, which isn't really enough to hold 8bit information in the 0-1 range, but given it's a LUT, it's kinda fine
#else
Texture2D<float4> sampToneCurv_Tex : register(t3);
#endif

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  bool forceVanilla = ShouldForceSDR(v1.xy);
  bool doHDR = !forceVanilla && LumaSettings.DisplayMode == 1;

  float4 r0,r1,r2;
  float exposure = smplAdaptedLumLast_Tex.Sample(smplAdaptedLumLast_s, float2(0.25,0.5)).x;
  float4 sceneColor = smplScene_Tex.Sample(smplScene_s, v1.xy).rgba;
  float3 bloomColor = smplLightShaftLinWork2_Tex.Sample(smplLightShaftLinWork2_s, v1.xy).rgb;
#if IMPROVED_BLOOM // Luma: bloom was overly strong (and wide in radius, while low quality in filtering), so reduce it, we have native HDR now
  bloomColor *= forceVanilla ? 1.0 : 0.667;
#endif
  sceneColor.xyz = max(0.0, sceneColor.xyz); // Clamp negative "trash" values
  o0.w = sceneColor.w;
  float3 colorFilter;
  colorFilter.x = dot(sceneColor.xyz, vPreColorCorrectionMatrix[0].xyz);
  colorFilter.y = dot(sceneColor.xyz, vPreColorCorrectionMatrix[1].xyz);
  colorFilter.z = dot(sceneColor.xyz, vPreColorCorrectionMatrix[2].xyz);
  float3 postProcessedColor = colorFilter.xyz * exposure + bloomColor;
  float postProcessedColorLuminance = GetLuminance(vColorScale.xyz * postProcessedColor); // Luma0: fix BT.601 luminance
  postProcessedColor = (postProcessedColor * vColorScale.xyz - postProcessedColorLuminance) * vSaturationScale.xyz + postProcessedColorLuminance;
#if _D57D4B9D
  r1.xy = v1.xy * vScreenSize.zw - vSpotParams.xy;
  r0.w = dot(r1.xy, r1.xy);
  r1.x = fLimbDarkening + r0.w;
  r0.w = sqrt(r0.w);
  r0.w = -vSpotParams.z + r0.w;
  r1.x = fLimbDarkening / r1.x;
  r1.x = r1.x * r1.x;
  r1.xyz = r1.x * postProcessedColor;
  r1.w = (0 >= r0.w);
  r0.w = saturate(vSpotParams.w / r0.w);
  r0.w = r1.w ? 1 : r0.w;
  r1.xyz = r1.xyz * r0.w;
  r1.xyz = fLimbDarkeningWeight * r1.xyz;
  r0.w = 1 - fLimbDarkeningWeight;
  postProcessedColor = postProcessedColor * r0.w + r1.xyz; // TODO: check if this raises blacks and fix it otherwise
#elif _680C78BB && ENABLE_VIGNETTE
  r1.xy = v1.xy * vSpotParams.xy + vSpotParams.zw;
  r1.xyz = smplTexLimbDarkening_Tex.Sample(smplTexLimbDarkening_s, r1.xy).xyz;
  r1.xyz = saturate(fLimbDarkeningWeight * r1.xyz);
  r1.xyz = 1.0 - r1.xyz;
  postProcessedColor *= r1.xyz;
#endif // _D57D4B9D

  float3 originalPostProcessedColor = postProcessedColor;
  if (doHDR)
  {
#if IMPROVED_TONEMAPPING_TYPE >= 2
    // Tonemap by luminance to avoid hue shifts
    postProcessedColor = GetLuminance(postProcessedColor);
#elif IMPROVED_TONEMAPPING_TYPE >= 1
    postProcessedColor = BT709_To_BT2020(postProcessedColor);
#endif // IMPROVED_TONEMAPPING_TYPE
  }

  // Uncharted 2 Hable tonemapper:
  const float A = 0.22;  // Shoulder Strength
  const float B = 0.30;  // Linear Strength
  const float C = 0.10;  // Linear Angle
  const float D = 0.20;  // Toe Strength
  const float E = 0.01;  // Toe Numerator
  const float F = 0.30;  // Toe Denominator
  const float W = 11.2;  // Linear White
  // Note: "SimulateHDRParams.x" should be matching "1.0 / Tonemap_Uncharted2_Eval(W, A, B, C, D, E, F)"
  // Note: the final e/f division was hardcoded to 0.0333 instead of 0.033333333333... as it should have been according to the other parameters. This meant that 0 didn't map to 0, so we corrected that.
  float3 vanillaTonemappedColor;
  float3 tonemappedColor = Uncharted2::Tonemap_Uncharted2_Extended(postProcessedColor, false, vanillaTonemappedColor, 0, MidGray, 1, SimulateHDRParams.x, A, B, C, D, E, F);

  if (doHDR)
  {
#if IMPROVED_TONEMAPPING_TYPE >= 2

    // We only allow this in HDR because in SDR by channel often looks better, and also it fits into the 0-1 range
    tonemappedColor = RestoreLuminance(originalPostProcessedColor, tonemappedColor.x);

#if IMPROVED_TONEMAPPING_TYPE >= 3 // Optional. This slightly reduces gamut in case the LUT added contrast or shifted colors, so it's best not used.
    vanillaTonemappedColor = RestoreLuminance(originalPostProcessedColor, vanillaTonemappedColor.x);
#else
    vanillaTonemappedColor = Tonemap_Uncharted2_Eval(originalPostProcessedColor, A, B, C, D, E, F) * SimulateHDRParams.x;
#endif // IMPROVED_TONEMAPPING_TYPE

#elif IMPROVED_TONEMAPPING_TYPE >= 1 // Tonemap in BT.2020 in HDR if we are doing per channel

    tonemappedColor = BT2020_To_BT709(tonemappedColor);
    vanillaTonemappedColor = Tonemap_Uncharted2_Eval(originalPostProcessedColor, A, B, C, D, E, F) * SimulateHDRParams.x;
    
#endif // IMPROVED_TONEMAPPING_TYPE
  }

#if ENABLE_SDR_COLOR_GRADING
  // 256x1 1D LUT (mostly driving contrast). They don't seem to raise the black floor (ever? at least not usually) so they don't need any kind of correction.
  float3 lutEncodedInput = vanillaTonemappedColor;
  
#if IMPROVED_COLOR_GRADING_TYPE >= 1
  float3 lutMin = 0.0;
  float3 lutMax = 1.0;
  if (doHDR)
  {
    Find1DLUTClippingEdges(sampToneCurv_Tex, 256, 0.0375, 0, 0, 0, lutMin, lutMax); // TODO: this is quite expensive, move to Vertex Shader? Also it seems to never be needed in the game as LUTs aren't clipped...
#if DEVELOPMENT // Print purple when LUTs present clipping, it doesn't seem to ever happen
    if (any(lutMin != 0.0) || any(lutMax != 1.0))
    {
      o0 = float4(1, 0, 1, 1);
      return;
    }
#endif
  }
  // Compress the input to be 100% within the LUT clipping range
  lutEncodedInput = remap(lutEncodedInput, 0, 1, lutMin, lutMax);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
  
  lutEncodedInput = lutEncodedInput * vToneCurvCol2Coord.x + vToneCurvCol2Coord.y;
  float3 colorGradedColor;
  colorGradedColor.r = sampToneCurv_Tex.Sample(sampToneCurv_s, float2(lutEncodedInput.r, 0.5)).r;
  colorGradedColor.g = sampToneCurv_Tex.Sample(sampToneCurv_s, float2(lutEncodedInput.g, 0.5)).g;
  colorGradedColor.b = sampToneCurv_Tex.Sample(sampToneCurv_s, float2(lutEncodedInput.b, 0.5)).b;

#if IMPROVED_COLOR_GRADING_TYPE >= 1
  // Re-expand the LUT output from its clipping range, to the full range, preventing colors from clipping. This possibly generates a wider gamut too.
  colorGradedColor = remap(colorGradedColor, lutMin, lutMax, 0, 1);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1

  // We can simply re-apply the contrast scale per channel from SDR to HDR, it should be enough (display mapping is later anyway)
  if (doHDR)
  {
#if IMPROVED_COLOR_GRADING_TYPE >= 3

#if 0 // Luma: do reproject grading by luminance, not by channel. This keeps its contrast change but avoids the hue shifts from it. The downside is that any tint is missed, and some levels have it (or well, a tint as a consequence of applying a different contrast between channels).

    colorGradedColor = GetLuminance(vanillaTonemappedColor) != 0.0 ? (tonemappedColor * (GetLuminance(colorGradedColor) / GetLuminance(vanillaTonemappedColor))) : colorGradedColor;

#else // Luma: re-project SDR grading on HDR ungraded image with oklab (should look good but it might create problems with blue shadow) (for example fire looks nicer with this)

    float3 colorGradedColorOkab = Oklab::linear_srgb_to_oklab(colorGradedColor);
    float3 vanillaTonemappedColorOkab = Oklab::linear_srgb_to_oklab(vanillaTonemappedColor);
    float3 tonemappedColorOkab = Oklab::linear_srgb_to_oklab(tonemappedColor);
    // Note: this could flip hues but it's probably fine most of the times... // TODO: fix... this causes nans and broken colors. The sun in the main menu background for example.
#if 1
    tonemappedColorOkab = (abs(vanillaTonemappedColorOkab) <= FLT_EPSILON) ? (tonemappedColorOkab * (colorGradedColorOkab / vanillaTonemappedColorOkab)) : colorGradedColorOkab;
#else
    tonemappedColorOkab += colorGradedColorOkab - vanillaTonemappedColorOkab;
#endif
    tonemappedColorOkab[0] = max(tonemappedColorOkab[0], 0.0); // Clamp lightness (can't go below 0)
    colorGradedColor = Oklab::oklab_to_linear_srgb(tonemappedColorOkab);

#endif

#else // IMPROVED_COLOR_GRADING_TYPE <= 2

    // This game had quite limited range in the linear rendering, so it's fine to simply reproject the grading to the HDR tonemapping.
    // Below mid grey or so "tonemappedColor" and "vanillaTonemappedColor" should match so this is really not a problem,
    // and works even if the gradedcolor was additive around 0 (raised blacks).
    colorGradedColor = vanillaTonemappedColor != 0.0 ? (MultiplyExtendedGamutColor(tonemappedColor, colorGradedColor / vanillaTonemappedColor)) : colorGradedColor;

#endif // IMPROVED_COLOR_GRADING_TYPE >= 3

#if IMPROVED_COLOR_GRADING_TYPE >= 2 // Note: theoretically this should be a separate setting than oklab above
    colorGradedColor = lerp(colorGradedColor, RestoreLuminance(colorGradedColor, tonemappedColor), sqr(saturate(GetLuminance(colorGradedColor) / MidGray))); // Ignore grading contrast changes beyond mid grey, it create an ugly contrasty look that also dims highlights way too much (actually sometimes it makes them brighter too)
#endif // IMPROVED_COLOR_GRADING_TYPE >= 2
  }

  o0.rgb = colorGradedColor;
#else // !ENABLE_SDR_COLOR_GRADING
  o0.rgb = doHDR ? tonemappedColor : vanillaTonemappedColor;
#endif // ENABLE_SDR_COLOR_GRADING

#if 0 // Test whether black maps to back (it doesn't matter if white maps to white, as the peak isn't 1 anymore in HDR)
  o0.rgb = sampToneCurv_Tex.Sample(sampToneCurv_s, float2(/*1.0 - */vToneCurvCol2Coord.y, 0.5)).rgb;
#endif

#if ENABLE_FXAA // Overwrite the alpha with luminance (it doesn't seem like it was ever used by the UI, and anyway it was already set to the pre-tonemapping luminance it seems)
  o0.a = linear_to_gamma(GetLuminance(o0.rgb), GCT_POSITIVE).x;
#endif
}