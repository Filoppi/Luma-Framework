#ifndef SRC_COMMON_HLSL
#define SRC_COMMON_HLSL

// Silence pow(x, n) issue complaining about negative pow possibly failing
#pragma warning( disable : 3571 )
// Silence for loop issue where multiple int i declarations overlap each other (because hlsl doesn't have stack/scope like c++ thus variables don't pop after their scope dies)
#pragma warning( disable : 3078 )

// These should only ever be included through "Common.hlsl" and never individually
#include "Math.hlsl"
#include "Color.hlsl"
#include "Settings.hlsl"

#ifndef LUT_SIZE
#define LUT_SIZE 16u
#endif
#ifndef LUT_MAX
#define LUT_MAX (LUT_SIZE - 1u)
#endif

// The aspect ratio the game was developed against, in case some effects weren't scaling properly for other aspect ratios.
static const float NativeAspectRatioWidth = 16.0;
static const float NativeAspectRatioHeight = 9.0;
static const float NativeAspectRatio = NativeAspectRatioWidth / NativeAspectRatioHeight;

float3 RestoreLuminance(float3 targetColor, float sourceColorLuminance, bool safe = false, uint colorSpace = CS_DEFAULT)
{
  float targetColorLuminance = GetLuminance(targetColor, colorSpace);
  if (safe)
  {
#if 0 // Disabled as it doesn't seem to help (we'd need to set the threshold to "0.001" (which is too high) for this to pick up the cases where divisions end up denormalizing the number etc)
    if (abs(targetColorLuminance - sourceColorLuminance) <= FLT_EPSILON)
    {
      return targetColor;
    }
#endif
    targetColorLuminance = max(targetColorLuminance, 0.0);
    sourceColorLuminance = max(sourceColorLuminance, 0.0);
#if 1
    return targetColor * (targetColorLuminance <= (FLT_EPSILON * 10.0) ? 0.0 : (sourceColorLuminance / targetColorLuminance)); // Empyrically found threshold
#else
    return targetColor * safeDivision(sourceColorLuminance, targetColorLuminance, 0);
#endif
    
  }
  return targetColor * safeDivision(sourceColorLuminance, targetColorLuminance, 1);
}
float3 RestoreLuminance(float3 targetColor, float3 sourceColor, bool safe = false, uint colorSpace = CS_DEFAULT)
{
  return RestoreLuminance(targetColor, GetLuminance(sourceColor, colorSpace), safe, colorSpace);
}

// Formulas that either use 2.2 or sRGB gamma depending on a global definition.
// Note that converting between linear and gamma space back and forth results in quality loss, especially over very high and very low values.
// 
// In the "POST_PROCESS_SPACE_TYPE != 1" cases, we apply the gamma correction in the very final linearization shader, to make the code simpler
// and make (e.g. UI) gamma blends look like Vanilla.
// 
// Note that these partially ignore "VANILLA_ENCODING_TYPE", they ignore "GAMMA_CORRECTION_RANGE_TYPE" (it acts as if it was 0) and ignore "EARLY_DISPLAY_ENCODING" (it acts as if it's true).
float3 game_gamma_to_linear(float3 Color, bool Mirrored = true)
{
#if POST_PROCESS_SPACE_TYPE == 1 && GAMMA_CORRECTION_TYPE >= 2 && 0 // Disabled for intermediary conversions (fall back to sRGB (this assumes "VANILLA_ENCODING_TYPE" 0)). Moved to final linearization shader
#if 1
  return RestoreLuminance(gamma_sRGB_to_linear(Color, Mirrored ? GCT_MIRROR : GCT_NONE), gamma_to_linear(Color, Mirrored ? GCT_MIRROR : GCT_NONE));
#else // Alternative version (by luminance instead of by channel)
  return RestoreLuminance(gamma_sRGB_to_linear(Color, Mirrored ? GCT_MIRROR : GCT_NONE), gamma_to_linear1(GetLuminance(Color), Mirrored ? GCT_MIRROR : GCT_NONE));
#endif
#endif

#if (POST_PROCESS_SPACE_TYPE == 1 && GAMMA_CORRECTION_TYPE == 1) || VANILLA_ENCODING_TYPE == 1
	return gamma_to_linear(Color, Mirrored ? GCT_MIRROR : GCT_NONE);
#endif

  // any other "GAMMA_CORRECTION_TYPE", any "POST_PROCESS_SPACE_TYPE"
  return gamma_sRGB_to_linear(Color, Mirrored ? GCT_MIRROR : GCT_NONE);
}
// This function undoes any gamma correction we had done
float3 linear_to_game_gamma(float3 Color, bool Mirrored = true)
{
#if POST_PROCESS_SPACE_TYPE == 1 && GAMMA_CORRECTION_TYPE >= 2 && 0 // Disabled for intermediary conversions (fall back to sRGB (this assumes "VANILLA_ENCODING_TYPE" 0)). Moved to final linearization shader
#if 1 // This version of this inverse formula is a little more accurate, though none of the two are a perfect mirror, as the original operation is destructive (and if it's not, it's complicated and slow to accurately revert)
    float3 gammaCorrectedColor = gamma_sRGB_to_linear(linear_to_gamma(Color, Mirrored ? GCT_MIRROR : GCT_NONE), Mirrored ? GCT_MIRROR : GCT_NONE);
#else
    float gammaCorrectedColor = gamma_sRGB_to_linear1(linear_to_gamma1(GetLuminance(Color), Mirrored ? GCT_MIRROR : GCT_NONE), Mirrored ? GCT_MIRROR : GCT_NONE); // "gammaCorrectedLuminance"
#endif
	  return linear_to_sRGB_gamma(RestoreLuminance(Color, gammaCorrectedColor), Mirrored ? GCT_MIRROR : GCT_NONE);
#endif

#if (POST_PROCESS_SPACE_TYPE == 1 && GAMMA_CORRECTION_TYPE == 1) || VANILLA_ENCODING_TYPE == 1
	return linear_to_gamma(Color, Mirrored ? GCT_MIRROR : GCT_NONE);
#endif

  // any other "GAMMA_CORRECTION_TYPE", any "POST_PROCESS_SPACE_TYPE"
  return linear_to_sRGB_gamma(Color, Mirrored ? GCT_MIRROR : GCT_NONE);
}

// Luma per pass or per frame data
cbuffer LumaData : register(LUMA_DATA_CB_INDEX)
{
  // GPU has "32 32 32 32 | break" bits alignment on memory, so to not break any "float2", we need all the float/uint/int before them to be in groups of 2 (because we are using a unified struct).
  struct
  {
    // These can be used as non generic (pass specific) data (even a float through asfloat())
    uint CustomData1;
    uint CustomData2;
    uint CustomData3;
    
    uint FrameIndex;
    
    float2 RenderResolutionScale;
    // This can be used instead of "CV_ScreenSize" in passes where "CV_ScreenSize" would have been
    // replaced with 1 because DLSS SR upscaled the image earlier in the rendering.
    float2 PreviousRenderResolutionScale;
    
#if GAME_PREY
    // Camera jitters in NCD space (based on the rendering resolution, but relative to the output resolution full range UVs, so apply these before "CV_HPosScale.xy")
    // (not in projection matrix space, so they don't need to be divided by the rendering resolution). You might need to multiply this by 0.5 and invert the horizontal axis before using it, if it's targeting UV space.
    float2 CameraJitters;
    // Previous frame's camera jitters in NCD space (relative to its own resolution).
    float2 PreviousCameraJitters;
#if 0 // Disabled in code too
    row_major float4x4 ViewProjectionMatrix;
    row_major float4x4 PreviousViewProjectionMatrix;
#endif
    // Same as the one on "PostAA" "AA" but fixed to include jitters as well
    row_major float4x4 ReprojectionMatrix;
#endif
  } LumaData : packoffset(c0);
}

// AdvancedAutoHDR pass to generate some HDR brightess out of an SDR signal.
// This is hue conserving and only really affects highlights.
// "SDRColor" is meant to be in "SDR range" (linear), as in, a value of 1 matching SDR white (something between 80, 100, 203, 300 nits, or whatever else)
// https://github.com/Filoppi/PumboAutoHDR
float3 PumboAutoHDR(float3 SDRColor, float _PeakWhiteNits, float _PaperWhiteNits, float ShoulderPow = 2.75f)
{
	const float SDRRatio = max(GetLuminance(SDRColor), 0.f);
	// Limit AutoHDR brightness, it won't look good beyond a certain level.
	// The paper white multiplier is applied later so we account for that.
	const float AutoHDRMaxWhite = max(min(_PeakWhiteNits, PeakWhiteNits) / _PaperWhiteNits, 1.f);
	const float AutoHDRExtraRatio = pow(saturate(SDRRatio), ShoulderPow) * (AutoHDRMaxWhite - 1.f);
	const float AutoHDRTotalRatio = SDRRatio + AutoHDRExtraRatio;
	return SDRColor * safeDivision(AutoHDRTotalRatio, SDRRatio, 1);
}

// LUMA FT: functions to convert an SDR color (optionally in gamma space) to an HDR one (optionally linear * paper white).
// This should be used for any color that writes on the color buffer (or back buffer) from tonemapping on.
// The "IsAfterDisplayTransfer" flag tells this function whether we are post "PostAAComposites" (which would optionally be the first pass space to store output in gamma space based on "POST_PROCESS_SPACE_TYPE" for Luma)
float3 SDRToHDR(float3 Color, bool InGammaSpace = true, bool IsAfterDisplayTransfer = false, bool IsUI = false)
{
  bool OutLinearSpace = bool(POST_PROCESS_SPACE_TYPE == 1) || (bool(POST_PROCESS_SPACE_TYPE >= 2) && !IsAfterDisplayTransfer);
  if (OutLinearSpace)
  {
    if (InGammaSpace)
    {
      Color.rgb = game_gamma_to_linear(Color.rgb);
      InGammaSpace = false;
    }
    const float paperWhite = (IsUI ? UIPaperWhiteNits : GamePaperWhiteNits) / sRGB_WhiteLevelNits;
    Color.rgb *= paperWhite;
  }
  else
  {
    // We do not scale by game paper white here, as gamma space buffers are stored with the SDR white level,
    // but we scale the UI by its relative ratio.
    if (IsUI)
    {
      // Linearize for the brightness multiplication
      if (InGammaSpace)
      {
        Color.rgb = game_gamma_to_linear(Color.rgb);
        InGammaSpace = false;
      }
      const float UIRelativePaperWhite = UIPaperWhiteNits / GamePaperWhiteNits;
      Color.rgb *= UIRelativePaperWhite;
    }
    if (!InGammaSpace)
    {
      Color.rgb = linear_to_game_gamma(Color.rgb);
    }
  }
	return Color;
}
float4 SDRToHDR(float4 Color, bool InGammaSpace = true, bool IsAfterDisplayTransfer = false, bool IsUI = false)
{
	return float4(SDRToHDR(Color.rgb, InGammaSpace, IsAfterDisplayTransfer, IsUI), Color.a);
}

// LUMA FT: added these functions to decode and re-encode the "back buffer" from any range to a range that roughly matched SDR linear space
float3 EncodeBackBufferFromLinearSDRRange(float3 color, bool UI = false)
{
  bool InLinearSpace = bool(POST_PROCESS_SPACE_TYPE == 1) || (bool(POST_PROCESS_SPACE_TYPE >= 2) && !UI);
  if (InLinearSpace)
  {
    const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
    return color * paperWhite;
  }
  else
  {
  	return linear_to_game_gamma(color);
  }
}
float3 DecodeBackBufferToLinearSDRRange(float3 color, bool UI = false)
{
  bool InLinearSpace = bool(POST_PROCESS_SPACE_TYPE == 1) || (bool(POST_PROCESS_SPACE_TYPE >= 2) && !UI);
  if (InLinearSpace)
  {
    const float paperWhite = GamePaperWhiteNits / sRGB_WhiteLevelNits;
    return color / paperWhite;
  }
  else
  {
  	return game_gamma_to_linear(color);
  }
}

// Partially mirrors "DrawLUTTexture()".
// PassType:
//  0 Generic
//  1 TAA
bool ShouldSkipPostProcess(float2 PixelPosition, uint PassType = 0)
{
#if TEST_MOTION_BLUR_TYPE || TEST_SMAA_EDGES
  return true;
#endif // TEST_MOTION_BLUR_TYPE || TEST_SMAA_EDGES
#if TEST_TAA_TYPE
  if (PassType != 1) { return true; }
#endif // TEST_TAA_TYPE
#if DRAW_LUT
	const uint LUTMinPixel = 0;
	uint LUTMaxPixel = LUT_MAX;
	uint LUTSizeMultiplier = 1;
  uint PixelScale = DRAW_LUT_TEXTURE_SCALE;
#if ENABLE_LUT_EXTRAPOLATION
	LUTSizeMultiplier = 2;
	LUTMaxPixel += LUT_SIZE * (LUTSizeMultiplier - 1);
	PixelScale = round(pow(PixelScale, 1.f / LUTSizeMultiplier));
#endif // ENABLE_LUT_EXTRAPOLATION

	const uint LUTPixelSideSize = LUT_SIZE * LUTSizeMultiplier;
	const uint2 LUTPixelPosition2D = round((PixelPosition / (float)PixelScale) - 0.5);
	const uint3 LUTPixelPosition3D = uint3(LUTPixelPosition2D.x % LUTPixelSideSize, LUTPixelPosition2D.y, LUTPixelPosition2D.x / LUTPixelSideSize);
	if (!any(LUTPixelPosition3D < LUTMinPixel) && !any(LUTPixelPosition3D > LUTMaxPixel))
	{
    return true;
  }
#endif // DRAW_LUT
  return false;
}

void ApplyDithering(inout float3 color, float2 uv, bool gammaSpace = true, float paperWhite = 1.0, uint bitDepth = DITHERING_BIT_DEPTH, float time = 0, bool useTime = false)
{
  // LUMA FT: added in/out encoding
  color /= paperWhite;
  float3 lastLinearColor = color;
  //TODO LUMA: use log10 gamma or HDR10 PQ, it should match human perception more accurately
  if (!gammaSpace)
  {
    color = linear_to_game_gamma(color); // Just use the same gamma function we use across the code, to keep it simple
  }
  float3 lastGammaColor = color;

  uint ditherRatio; // LUMA FT: added dither bith depth support, 8 bit might be too much for 16 bit HDR
  // Optimized (static) branches
  if (bitDepth == 8) { ditherRatio = 255; }
  else if (bitDepth == 10) { ditherRatio = 1023; }
  else { ditherRatio = uint(round(pow(2, bitDepth) - 1.0)); }

  float3 rndValue;
	// Apply dithering in sRGB space to minimize quantization artifacts
	// Use a triangular distribution which gives a more uniform noise by avoiding low-noise areas
  if (useTime)
  {
    const float tr = frac(time / 1337.7331) + 0.5; // LUMA FT: added "time" randomization to avoid dithering being fixed per pixel over time
    rndValue = NRand3(uv, tr) + NRand3(uv + 0.5789, tr) - 1.0;
  }
  else
  {
    rndValue = NRand3(uv) + NRand3(uv + 0.5789) - 1.0; // LUMA FT: fixed this from subtracting 0.5 to 1 so it's mirrored and doesn't just raise colors
  }
#if TEST_DITHERING
  color += rndValue;
#else // TEST_DITHERING
  color += rndValue / ditherRatio;
#endif // TEST_DITHERING

  if (!gammaSpace)
  {
#if HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
    color = lastLinearColor + (game_gamma_to_linear(color) - game_gamma_to_linear(lastGammaColor));
#else // HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
    color = game_gamma_to_linear(color);
#endif // HIGH_QUALITY_POST_PROCESS_SPACE_CONVERSIONS
  }
  color *= paperWhite;
}

// Fix up sharpening/blurring when done on HDR images in post processing. In SDR, the source color could only be between 0 and 1,
// so the halos (rings) that can appear around rapidly changing colors were limited, but in HDR lights can go much brighter so the halos got noticeable with default settings.
// This might also help with overly strong sharpening in SDR too.
// This might be best avoided on blur passes unless shown to be needed.
// This should work with any "POST_PROCESS_SPACE_TYPE" setting.
// Note that sharpening can always generated invalid luminances (I think), so that should be accounted for.
float3 FixUpSharpeningOrBlurring(float3 postSharpeningColor, float3 preSharpeningColor)
{
#if ENABLE_SHARPENING
    // Either set it to 0.5, 0.75 or 1 to make results closer to SDR (this makes more sense when done in gamma space, but also works in linear space).
    // Lower values slightly diminish the effect of sharpening, but further avoid halos issues.
    static const float sharpeningMaxColorDifference = 0.5;
    postSharpeningColor.rgb = clamp(postSharpeningColor.rgb, preSharpeningColor - sharpeningMaxColorDifference, preSharpeningColor + sharpeningMaxColorDifference);
    
#if 0 // Not necessary until proven otherwise, the whole shader code base works in r g b individually so even if we had an invalid luminance, it'd be fine (it will likely be clipped on output anyway)
    postSharpeningColor.rgb = max(postSharpeningColor.rgb, min(preSharpeningColor.rgb, 0)); // Don't allow scRGB colors to go below the min we previously had
#endif
#endif // ENABLE_SHARPENING
  	return postSharpeningColor;
}

float2 RemapUV(float2 UV, float2 sourceResolution, float2 targetResolution)
{
  // First remap from a "+half source texel uv offset to 1-half source texel uv offset" range to a 0-1 range, then re-map acknowleding the half target texel uv offset.
  UV -= 0.5 / sourceResolution;
  UV *= (sourceResolution / (sourceResolution - 1.0)) * ((targetResolution - 1.0) / targetResolution); // Unified over one line to avoid shifting the UV range too many times
  UV += 0.5 / targetResolution;
  return UV;
}

// "resolutionsScale" is the "direct" resolution multiplier (e.g. 0.5 means 50% rendering resolution)
float2 RemapUVFromScale(float2 UV, float2 resolutionScale /*= CV_HPosScale.xy*/, float2 sourceResolution /*= CV_ScreenSize.xy*/)
{
  // Avoid "degrading" the quality if the resolution scale is 1
  return resolutionScale == 1 ? UV : RemapUV(UV, sourceResolution, sourceResolution / resolutionScale);
}

#endif // SRC_COMMON_HLSL