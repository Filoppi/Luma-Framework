#include "../Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Oklab.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[3];
}

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  const bool vanilla = ShouldForceSDR(w1.xy, true) && LumaSettings.DisplayMode == 1;

#if _DD377C05 // This branch of the tonemapper was empty, a texture copy

  float4 sceneColor = t0.SampleLevel(s0_s, w1.xy, 0);
  
  sceneColor.rgb = gamma_to_linear(sceneColor.rgb, GCT_MIRROR);
  FixColorGradingLUTNegativeLuminance(sceneColor.rgb);
  sceneColor.rgb = linear_to_gamma(sceneColor.rgb, GCT_MIRROR);

#else

  float4 sceneColor = t0.SampleLevel(s1_s, w1.xy, 0);

  sceneColor.rgb = gamma_to_linear(sceneColor.rgb, GCT_MIRROR);
  FixColorGradingLUTNegativeLuminance(sceneColor.rgb); // Fix up any possible invalid luminance that might have made it here, before we pass through LUT etc
  sceneColor.rgb = linear_to_gamma(sceneColor.rgb, GCT_MIRROR);

  float saturation = cb0[2].z;
  float brightness = cb0[2].x;
  float contrast = cb0[2].y;

  // The game uses a 256x4 LUT, with the horizontal axis being contrast, and each vertical line being a different color (rgb, 4th axis is unknown, seems never used, probably just to make the texture res even)
#if ENABLE_LUMA
  const bool extrapolateLUTsMethod = vanilla ? -1 : 1;
  
  float lutWidth;
  float lutHeight;
  t1.GetDimensions(lutWidth, lutHeight);
  float xScale = (lutWidth - 1.f) / lutWidth;
  float xOffset = 1.f / (2.f * lutWidth);
  if (vanilla)
  {
    xScale = 1.0;
    xOffset = 0.0;
  }

  float4 redLUT = sampleLUTWithExtrapolation1D(t1, s1_s, float2((sceneColor.r * xScale) + xOffset, 0.125), extrapolateLUTsMethod).rgba;
  float4 greenLUT = sampleLUTWithExtrapolation1D(t1, s1_s, float2((sceneColor.g * xScale) + xOffset, 0.375), extrapolateLUTsMethod).rgba;
  float4 blueLUT = sampleLUTWithExtrapolation1D(t1, s1_s, float2((sceneColor.b * xScale) + xOffset, 0.625), extrapolateLUTsMethod).rgba;
  float3 gradedSceneColor = float3(redLUT.r, greenLUT.g, blueLUT.b);
  float3 gradedSceneColorLinear;

  // Hues correction
  if (!vanilla)
  {
    float4 redBlackLUT = t1.Sample(s1_s, float2((0 * xScale) + xOffset, 0.125)).rgba;
    float4 greenBlackLUT = t1.Sample(s1_s, float2((0 * xScale) + xOffset, 0.375)).rgba;
    float4 blueBlackLUT = t1.Sample(s1_s, float2((0 * xScale) + xOffset, 0.625)).rgba;
    float3 gradedBlackColorLinear = gamma_to_linear(float3(redBlackLUT.r, greenBlackLUT.g, greenBlackLUT.b));

// Draw purple if the LUT is pre-clipped (e.g. going full black before the min input point, and going full white before the max input point).
// We don't care for the case where LUTs don't reach 1 on the max input point, because LUT extrapolation will take care of the rest.
// And the case where the min input point is raised beyond 0, is already handled below.
#if TEST && 0 // TODO: add min/max scaling to LUTs to unclip them (because this happens!)
    float redBlackPlus1LUT = t1.Load(float3(1, 0, 0)).r;
    float greenBlackPlus1LUT = t1.Load(float3(1, 1, 0)).g;
    float blueBlackPlus1LUT = t1.Load(float3(1, 2, 0)).b;

    float redWhiteMinus1LUT = t1.Load(float3(lutWidth - 2, 0, 0)).r;
    float greenWhiteMinus1LUT = t1.Load(float3(lutWidth - 2, 1, 0)).g;
    float blueWhiteMinus1LUT = t1.Load(float3(lutWidth - 2, 2, 0)).b;

    if (redBlackPlus1LUT <= FLT_MIN || greenBlackPlus1LUT <= FLT_MIN  || blueBlackPlus1LUT <= FLT_MIN || redWhiteMinus1LUT >= (1.0 - FLT_MIN) || greenWhiteMinus1LUT >= (1.0 - FLT_MIN) || blueWhiteMinus1LUT >= (1.0 - FLT_MIN))
    {
      o0 = float4(1, 0, 1, 1);
      return;
    }
#endif

    float lutMidGreyIn = 0.75; // Customize it (bad naming)
    float4 redMidGreyLUT = t1.Sample(s1_s, float2((lutMidGreyIn * xScale) + xOffset, 0.125)).rgba;
    float4 greenMidGreyLUT = t1.Sample(s1_s, float2((lutMidGreyIn * xScale) + xOffset, 0.375)).rgba;
    float4 blueMidGreyLUT = t1.Sample(s1_s, float2((lutMidGreyIn * xScale) + xOffset, 0.625)).rgba;
    float3 gradedMidGreyColorLinear = gamma_to_linear(float3(redMidGreyLUT.r, greenMidGreyLUT.g, blueMidGreyLUT.b));

    gradedSceneColorLinear = gamma_to_linear(gradedSceneColor, GCT_MIRROR);
    FixColorGradingLUTNegativeLuminance(gradedSceneColorLinear.rgb); // Fix up invalid LUT extrapolation colors (it has no concept of luminance given that it works by channel)

    // Fix potentially raised LUT floor
    float3 blackFloorFixColorLinear = gradedSceneColorLinear - (gradedBlackColorLinear * (1.0 - saturate(gradedSceneColorLinear * 10.0)));
    float3 blackFloorFixColorOklab = Oklab::linear_srgb_to_oklab(blackFloorFixColorLinear);
    float3 gradedSceneColorOklab = Oklab::linear_srgb_to_oklab(gradedSceneColorLinear);
    gradedSceneColorOklab.x = lerp(gradedSceneColorOklab.x, blackFloorFixColorOklab.x, 2.0 / 3.0); // Keep the hue and chrominance of the raised/tinted shadow, but restore much of the original shadow level for contrast
    gradedSceneColorLinear = Oklab::oklab_to_linear_srgb(gradedSceneColorOklab);

    float minChrominanceChange = 0.8; // Mostly desaturation for now, we only want the hue shifts (e.g. turning white into yellow)
    float maxChrominanceChange = FLT_MAX; // Setting this to 1 works too, it prevents the clipped color from boosting saturation, however, that's very unlikely to happen
    float3 clippedColorOklab = Oklab::linear_srgb_to_oklab(saturate(gradedSceneColorLinear));
    float hueStrength = 0.0;
    float chrominanceStrength = 0.8 * saturate(clippedColorOklab.x); // Desaturate bright colors more
    gradedSceneColorLinear = RestoreHueAndChrominance(gradedSceneColorLinear, saturate(gradedSceneColorLinear), hueStrength, chrominanceStrength, minChrominanceChange, maxChrominanceChange);
    
    // Restore the highlights color filter on new highlights, this helps avoiding turning highlights to pure white/yellow
    hueStrength = max(saturate(clippedColorOklab.x) - (2.0 / 3.0), 0.0) * 3.0 * 0.8; // Never restore hue to 100%, it messes up
    gradedSceneColorLinear = RestoreHueAndChrominance(gradedSceneColorLinear, gradedMidGreyColorLinear, hueStrength, 0.0);

    // Luma: fixed slightly wrong BT.709 luminance, and calculating it in linear
#if 0 // Linearly blended saturation (doesn't look close to vanilla)
    float gradedSceneColorLuminance = GetLuminance(gradedSceneColorLinear);
    gradedSceneColorLinear = lerp(gradedSceneColorLuminance, gradedSceneColorLinear, pow(saturation, 0.5)); // Do an arbitrary pow on saturation to align it with vanilla
    gradedSceneColor = linear_to_gamma(gradedSceneColorLinear, GCT_MIRROR);
#elif 1 // Keep blending it in gamma, saturation in linear looks very different (even if it'd look better)
    float gradedSceneColorLuminance = linear_to_gamma1(max(GetLuminance(gradedSceneColorLinear), 0.0));
    gradedSceneColor = linear_to_gamma(gradedSceneColorLinear, GCT_MIRROR);
    gradedSceneColor = lerp(gradedSceneColorLuminance, gradedSceneColor, saturation);
#else // ~Vanilla saturation, looks about the same as the improved one above
    gradedSceneColor = linear_to_gamma(gradedSceneColorLinear, GCT_MIRROR);
    float gradedSceneColorLuminance = GetLuminance(gradedSceneColor);
    gradedSceneColor = lerp(gradedSceneColorLuminance, gradedSceneColor, saturation);
#endif

#if _12E5FE2B

  gradedSceneColor *= brightness; // Applying this in linear or gamma space is the same (we need to convert "brightness" to linear too if we want to apply it in linear space)
  
  float contrastMidPoint = 0.5;
#if 1 /// Better matches vanilla, given that it also increases saturation

  gradedSceneColor = ((gradedSceneColor - contrastMidPoint) * contrast) + contrastMidPoint;

#else // Luma modern contrast method that doesn't raise blacks not generate invalid colors

	// Empirical value to match the original game constrast formula look more.
	// This has been carefully researched and applies to both positive and negative contrast.
#if 0 // Keep doing it in gamma space, it should be all right (it will boost saturation too)
  gradedSceneColor = linear_to_gamma(gradedSceneColor, GCT_MIRROR);
  contrastMidPoint = gamma_to_linear1(contrastMidPoint);
#endif
	const float adjustedContrast = pow(contrast, 1.25); // The pow was set to 2 in Starfield Luma but looks neutral at ~1 here.
	// Do abs() to avoid negative power, even if it doesn't make 100% sense, these formulas are fine as long as they look good
	gradedSceneColor = pow(abs(gradedSceneColor) / contrastMidPoint, adjustedContrast) * contrastMidPoint * sign(gradedSceneColor);
#if 0
  gradedSceneColor = gamma_to_linear(gradedSceneColor, GCT_MIRROR);
#endif

#endif

#endif // _12E5FE2B
  }
#else // !ENABLE_LUMA
  // Note: Vanilla was using a nearest neighbor sampler, which butchered detail beyond 8bit (that's why the LUT input colors (UVs) didn't the half texel offset acknowledged).
  float4 redLUT = t1.Sample(s0_s, float2(sceneColor.r, 0.125)).rgba;
  float4 greenLUT = t1.Sample(s0_s, float2(sceneColor.g, 0.375)).rgba;
  float4 blueLUT = t1.Sample(s0_s, float2(sceneColor.b, 0.625)).rgba;
  float3 gradedSceneColor = float3(redLUT.r, greenLUT.g, blueLUT.b);
  float gradedSceneColorLuminance = dot(gradedSceneColor, float3(0.22,0.707,0.071)); // Wrong luminance vector, doesn't sum up to 1 either
  gradedSceneColor = lerp(gradedSceneColorLuminance, gradedSceneColor, saturation);
  
#if _12E5FE2B
  gradedSceneColor *= brightness; // Game brightness slider, goes from 0.1 to 2.0, possibly affected by other things too (causes raw clipping in native SDR, but in Luma it's ok in SDR and HDR, still, it's better left at default as it works in gamma space). It's also weird that brightness was done before contrast, as the contrast center doesn't shift...
  gradedSceneColor = ((gradedSceneColor - 0.5) * contrast) + 0.5; // Note that this will generate invalid colors and colors beyond the Rec.709 gamut
#endif // _12E5FE2B
#endif // ENABLE_LUMA

#endif // _DD377C05

#if _DD377C05
  float3 gradedSceneColor = sceneColor.rgb;
  float3 gradedSceneColorLinear;
#endif

#if ENABLE_LUMA
  gradedSceneColorLinear = gamma_to_linear(gradedSceneColor, GCT_MIRROR);
#if defined(ENABLE_COLOR_GRADING) && !ENABLE_COLOR_GRADING
  float3 sceneColorLinear = gamma_to_linear(sceneColor.rgb, GCT_MIRROR);
  float colorGradingStrength = 0.0;
  gradedSceneColorLinear = lerp(sceneColorLinear, gradedSceneColorLinear, colorGradingStrength);
#endif

  const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
  const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  if (vanilla)
  {
    gradedSceneColorLinear = saturate(gradedSceneColorLinear); // This isn't even needed really
  }
#if 0 // TODO: disabled as videos are HDR to begin with so they can happily go through the other path HDR path below! We can clean the c++ side code.
  // AutoHDR on videos
  else if (LumaData.CustomData1)
  {
    gradedSceneColorLinear = PumboAutoHDR(gradedSceneColorLinear, lerp(sRGB_WhiteLevelNits, 400.0, LumaData.CustomData3), LumaSettings.GamePaperWhiteNits);
  }
#endif
  // TODO: try Reinhard for all. it seems next to identical?
  else if (LumaSettings.DisplayMode == 1)
  {
    // The game doesn't have many bright highlights, the dynamic range is relatively low, this helps alleviate that.
    // All values found empyrically
    float normalizationPoint = 0.02;
    float fakeHDRIntensity = 0.4;
    
    float3 fakeHDRColor = FakeHDR(gradedSceneColorLinear, normalizationPoint, fakeHDRIntensity, 0.0); // TODO: try boosting saturation here
    
    // Boost saturation
    float highlightsSaturationIntensity = 0.25; // Anything more is deep fried.
    float luminanceTonemap = saturate(Reinhard::ReinhardRange(GetLuminance(gradedSceneColorLinear), MidGray, -1.0, 1.0, false).x);
    fakeHDRColor = Oklab::linear_srgb_to_oklab(fakeHDRColor);
    fakeHDRColor.yz *= lerp(1.0, max(pow(luminanceTonemap, 1.0 / DefaultGamma) + 0.5, 1.0), highlightsSaturationIntensity); // Arbitrary formula
	  fakeHDRColor = Oklab::oklab_to_linear_srgb(fakeHDRColor);
    
    gradedSceneColorLinear = lerp(gradedSceneColorLinear, fakeHDRColor, LumaData.CustomData3);

#if 1 // More perceptually accurate and better expands into BT.2020
    gradedSceneColorLinear = Oklab::linear_srgb_to_oklab(gradedSceneColorLinear);
    gradedSceneColorLinear.yz *= 1.0 + LumaData.CustomData4;
    gradedSceneColorLinear = Oklab::oklab_to_linear_srgb(gradedSceneColorLinear);
#else
    gradedSceneColorLinear = Saturation(gradedSceneColorLinear, 1.0 + LumaData.CustomData4);
#endif

    bool perChannel = true; // There's little difference in this game beside is some very bright scenes (e.g. lava), and per channel looks a lot closer to vanilla
    DICESettings settings = DefaultDICESettings(perChannel ? DICE_TYPE_BY_CHANNEL_PQ : DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    settings.Mirrored = true; // Do DICE mirrored to better handle excessive saturation through negative numbers (it generally makes no difference)
    gradedSceneColorLinear = DICETonemap(gradedSceneColorLinear * paperWhite, peakWhite, settings) / paperWhite;
    
#if GAMUT_MAPPING_TYPE == 0
    float desaturationAmount = 0.5;
    float darkeningAmount = 0.5;
    gradedSceneColorLinear = BT2020_To_BT709(CorrectOutOfRangeColor(BT709_To_BT2020(gradedSceneColorLinear), true, false, desaturationAmount, darkeningAmount, peakWhite / paperWhite, CS_BT2020));
#endif

#if ENABLE_DITHERING // HDR only, it's hard to see it in SDR
    static const float HDR10_MaxWhite = HDR10_MaxWhiteNits / sRGB_WhiteLevelNits;
    gradedSceneColorLinear = Linear_to_PQ(gradedSceneColorLinear * paperWhite / HDR10_MaxWhite, GCT_MIRROR);
    ApplyDithering(gradedSceneColorLinear, w1.xy, true, 1.0, DITHERING_BIT_DEPTH, LumaSettings.FrameIndex, true); // 9 bit dither seems to be the best balance
    gradedSceneColorLinear = PQ_to_Linear(gradedSceneColorLinear, GCT_MIRROR) / paperWhite * HDR10_MaxWhite;
#endif
  }
  else
  {
    float shoulderStart = LumaData.CustomData1 ? 0.75 : MidGray; // On lava mid grey looks great, in other places 0.75 or so might do.
    float3 gradedSceneColorLinearLuminanceTM = RestoreLuminance(gradedSceneColorLinear, Reinhard::ReinhardRange(GetLuminance(gradedSceneColorLinear), shoulderStart, -1.0, peakWhite / paperWhite, false).x, true);
    float3 gradedSceneColorLinearChannelTM = Reinhard::ReinhardRange(gradedSceneColorLinear, shoulderStart, -1.0, peakWhite / paperWhite, false);
#if 0 // Test: SDR clip
    gradedSceneColorLinear = saturate(gradedSceneColorLinear);
#elif 1 // Most accurate to Vanilla, however also inherits some of its defects, but the art is made for it, at least when we are constraned to the SDR range
    gradedSceneColorLinear = gradedSceneColorLinearChannelTM;
#elif 1 // Looks average on lava
    gradedSceneColorLinear = gradedSceneColorLinearLuminanceTM;
#else // Looks terrible on lava
    // Restore per channel chrominance on luminance TM
    gradedSceneColorLinear = RestoreHueAndChrominance(gradedSceneColorLinearLuminanceTM, gradedSceneColorLinearChannelTM, 0.0, 0.8);
#endif

#if GAMUT_MAPPING_TYPE == 0 && 0 // Looks bad with "RestoreHueAndChrominance". Not needed with "gradedSceneColorLinearChannelTM".
    float desaturationAmount = DVS1; // 0.5
    float darkeningAmount = DVS2; // 0.5
    gradedSceneColorLinear = CorrectOutOfRangeColor(gradedSceneColorLinear, true, true, desaturationAmount, darkeningAmount, peakWhite / paperWhite);
#endif
  }

#if UI_DRAW_TYPE == 2
  gradedSceneColorLinear *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
#endif // UI_DRAW_TYPE == 2

  gradedSceneColor = linear_to_gamma(gradedSceneColorLinear, GCT_MIRROR);
#endif // ENABLE_LUMA
  
  o0.xyz = gradedSceneColor;
  o0.w = sceneColor.a;
}