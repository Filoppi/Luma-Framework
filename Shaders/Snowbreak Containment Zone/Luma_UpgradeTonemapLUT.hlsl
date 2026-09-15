#define LUT_SIZE 32
#if !DIMENSIONS_2D
#define LUT_3D 1
#endif

#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 2
#endif

#include "Includes/Common.hlsl"

#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/DICE.hlsl"

#if DIMENSIONS_2D
Texture2D<float4> sourceLUT : register(t0);
RWTexture2D<float4> targetLUT : register(u0);
#else
Texture3D<float4> sourceLUT : register(t0);
RWTexture3D<float4> targetLUT : register(u0);
#endif

SamplerState linearSampler : register(s0);

// Scale UE uses for LUTs, supposedly to avoid clipping on the upper edge (I don't think it's necessary thought)
static const float InOutScale = 1.05;

// Scale LUT input coordinates to acknowledge the half texel offset. Meant for 3D.
float3 EncodedColorToUVZ(float3 color, float3 size = LUT_SIZE)
{
  float3 scale = (size - 1.0) / size;
  float3 bias = 0.5 / size;
  return saturate(color) * scale + bias;
}

float3 DecodeLUTInput(float3 lutEncodedColor)
{
  return ACES::LogToLinear(lutEncodedColor);
}
float3 EncodeLUTInput(float3 color)
{
  return ACES::LinearToLog(color);
}
float3 EncodeLUTOutput(float3 color)
{
#if VANILLA_ENCODING_TYPE == 0
  color = linear_to_sRGB_gamma(color, GCT_MIRROR);
#else
  color = linear_to_gamma(color, GCT_MIRROR);
#endif
  color /= InOutScale;
  return color;
}
float3 DecodeLUTOutput(float3 color)
{
  color *= InOutScale;
#if VANILLA_ENCODING_TYPE == 0
  color = gamma_sRGB_to_linear(color, GCT_MIRROR);
#else
  color = gamma_to_linear(color, GCT_MIRROR);
#endif
  return color;
}

// UE LUT builder stored output in R10G10B10A2_UNORM and that turns NaNs to 0.
// Theoretically we could simply run a quick max with 0, however, that would remove any negative colors,
// which, if we customized LUTs, we could have, due to grading being run in AP1.
float3 SanitizeLUTSample(float3 color)
{
  return IsNaN_Strict(color) ? 0.0 : color;
}
float4 SanitizeLUTSample(float4 color)
{
  return IsNaN_Strict(color) ? 0.0 : color;
}

// TODO: move these to generic funcs!
float4 SampleLUTTexel(int3 pixelCoords)
{
#if DIMENSIONS_2D
  return SanitizeLUTSample(sourceLUT.Load(int3(pixelCoords.x + pixelCoords.z * LUT_SIZE, pixelCoords.y, 0)));
#else
  return SanitizeLUTSample(sourceLUT.Load(int4(pixelCoords, 0)));
#endif
}
float3 SampleLUTColor(float3 encodedColor)
{
#if DIMENSIONS_2D
  encodedColor = saturate(encodedColor);

  // Convert to texel space [0..LUT_MAX]
  float3 tc = encodedColor * LUT_MAX;

  // Blue slice lerp
  float b0 = min(floor(tc.b), LUT_MAX - 1.0); // clamp so b0+1 is valid
  float t  = tc.b - b0;

  // 2D packed dimensions
  float2 dim = float2(LUT_SIZE * LUT_SIZE, LUT_SIZE);

  // UVs at texel centers for slice b0 and b0+1
  float2 uv0 = (float2(tc.r + b0 * LUT_SIZE, tc.g) + 0.5) / dim;
  float2 uv1 = uv0 + float2(1.0 / LUT_SIZE, 0.0); // exactly one slice to the right

  float3 c0 = SanitizeLUTSample(sourceLUT.SampleLevel(linearSampler, uv0, 0).rgb);
  float3 c1 = SanitizeLUTSample(sourceLUT.SampleLevel(linearSampler, uv1, 0).rgb);
  return lerp(c0, c1, t);
#else
  return SanitizeLUTSample(sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(encodedColor), 0).rgb);
#endif
}
float3 SampleLUTCoords(float3 uvw)
{
#if DIMENSIONS_2D
  uvw = saturate(uvw);

  // Undo half-texel bias -> texel space [0..LUT_MAX]
  float3 tc = uvw * LUT_SIZE - 0.5;
  tc = clamp(tc, 0.0, LUT_MAX);

  float b0 = min(floor(tc.b), LUT_MAX - 1.0);
  float t  = tc.b - b0;

  float2 dim = float2(LUT_SIZE * LUT_SIZE, LUT_SIZE);

  float2 uv0 = (float2(tc.r + b0 * LUT_SIZE, tc.g) + 0.5) / dim;
  float2 uv1 = uv0 + float2(1.0 / LUT_SIZE, 0.0);

  float3 c0 = SanitizeLUTSample(sourceLUT.SampleLevel(linearSampler, uv0, 0).rgb);
  float3 c1 = SanitizeLUTSample(sourceLUT.SampleLevel(linearSampler, uv1, 0).rgb);
  return lerp(c0, c1, t);
#else
  return SanitizeLUTSample(sourceLUT.SampleLevel(linearSampler, uvw, 0).rgb);
#endif
}

// Find the input value mapped to a lut output (on the grey scale), basically inverts the LUT.
// Both input and output are meant to be in "decoded" lut space, and thus linear.
float3 FindLUTInputForOutput(float targetValue /*= MidGray*/, out float3 slope, out float3 offset)
{
  // Start at texel 0 along the grey diagonal (0,0,0)
  float prevInput = 0.0;
  float3 prevOutput3 = DecodeLUTOutput(SampleLUTTexel(0).rgb); // Input is already encoded with the LUT input encoding, so the steps ("LUT_SIZE") are roughly equal
  float prevOutput = average(prevOutput3); // The best we can do is the average on the grey diagonal here (luminance isn't particularly relevant)

  [loop]
  for (int i = 1; i < LUT_SIZE; ++i)
  {
    // Normalized position for texel i
    float input = (float)i / (float)(LUT_SIZE - 1);
    float3 output3 = DecodeLUTOutput(SampleLUTTexel(i).rgb);
    float output = average(output3);

    // First crossing from below to above (or equal)
    if (output >= targetValue)
    {
      // x = linear input, y = LUT output (already in the same “target” space as targetValue)
      float3 x1 = DecodeLUTInput(prevInput);   // linear input for previous texel
      float3 x2 = DecodeLUTInput(input);       // linear input for current texel
      float3 y1 = prevOutput3;             // output at x1
      float3 y2 = output3;                 // output at x2

      float3 dx = x2 - x1;
      bool3 validSlope = abs(dx) >= 1e-6f;
      // The second case is degenerate: inputs collapsed, just fall back to constant
      slope = validSlope ? ((y2 - y1) / dx) : 0.0;
      offset = validSlope ? (y1 - slope * x1) : targetValue;

      // Interpolate between texel (i-1) and i in *value* space
      float denom = output - prevOutput;
      float alpha = (denom != 0.0) ? ((targetValue - prevOutput) / denom) : 0.0;
      alpha = saturate(alpha);

      // Interpolated normalized coordinate between prev and current coordinates.
      // Assuming the coordinates are encoded with the LUT input encoding,
      // doing an inverse lerp with them should be fairly accurate (we couldn't do any better really).
      float interpolatedInput = lerp(prevInput, input, alpha);
      return DecodeLUTInput(interpolatedInput); // Decode it to return the "linear" value
    }

    // Step forward
    prevInput = input;
    prevOutput3 = output3;
    prevOutput = output;
  }

  // If we could not find the target, return the mid point of the LUT,
  // which statistically is likely to be the closest to the point we are looking for,
  // given that the LUT is likely to be all black or all white in case the target value isn't present.
  // Alternatively we could return black, or white, or directly the target value but that'd be random.
  slope = 1.0;
  offset = 0.0;
#if 1
  return DecodeLUTOutput(SampleLUTCoords(0.5));
#endif
  return targetValue;
}

#if DIMENSIONS_2D
[numthreads(8, 8, 1)]
#else
[numthreads(8, 8, 8)]
#endif
void main(uint3 DispatchThreadId : SV_DispatchThreadID)
{
  uint3 pixelPos = DispatchThreadId;
  
#if DIMENSIONS_2D
  if (pixelPos.x >= ((uint)LUT_SIZE * (uint)LUT_SIZE) || pixelPos.y >= (uint)LUT_SIZE)
    return;
#else
  if (pixelPos.x >= (uint)LUT_SIZE || pixelPos.y >= (uint)LUT_SIZE || pixelPos.z >= (uint)LUT_SIZE)
    return;
#endif

#if DIMENSIONS_2D
  const uint r = pixelPos.x % (uint)LUT_SIZE;
  const uint b = pixelPos.x / (uint)LUT_SIZE;
  const uint g = pixelPos.y;
  float3 originalHDRColor = DecodeLUTInput(float3(r, g, b) / LUT_MAX);

  uint2 pixelPos2D = pixelPos.xy;
  pixelPos = uint3(r, g, b); // Set the 3D one
#else
  float3 originalHDRColor = DecodeLUTInput((float3)pixelPos / (float)LUT_MAX);

#if 0 // Test: Passthrough
  targetLUT[pixelPos.xyz] = SanitizeLUTSample(sourceLUT.Load(int4(pixelPos.xyz, 0)));
  return;
#endif
#endif

  float4 tonemappedColor = SampleLUTTexel(pixelPos);
  tonemappedColor.rgb = DecodeLUTOutput(tonemappedColor.rgb);

  float3 upgradedColor = tonemappedColor.rgb;

#if TONEMAP_TYPE == 0 // Vanilla (supposedly SDR)

  // Do nothing

#elif TONEMAP_TYPE == 1 // Lame AutoHDR method (just for testing really)

  upgradedColor.rgb = PumboAutoHDR(upgradedColor.rgb, FLT_MAX, LumaSettings.GamePaperWhiteNits, 3.333, 0.333);

#elif TONEMAP_TYPE == 2 // Extrapolate HDR out of SDR

  float midGrayOut = MidGray;
  float3 midGreySlope = 1.0;
  float3 midGreyOffset = 0.0;
  // "midGrayIn" is theoretically always the same as "midGrayOut" in UE as it guarantees fixed 0.18 in and out (at least for the filmic tonemapper, not the SDR LUTs)
  float3 midGrayIn = FindLUTInputForOutput(midGrayOut, midGreySlope, midGreyOffset); // TODO: do once for all threads if we are using a compute shader?

#if 0
  // TODO: 3D only for now!!! Anyway, probably not needed
  float4 black = sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(0.0), 0.0);
  float4 midGrey = sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(0.5), 0.0); // Is 0.5 really mid grey here? It is if the LUT was SDR, kinda.
  float4 white = sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(1.0), 0.0);
  float4 red = sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(float3(1, 0, 0)), 0.0);
  float4 green = sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(float3(0, 1, 0)), 0.0);
  float4 blue = sourceLUT.SampleLevel(linearSampler, EncodedColorToUVZ(float3(0, 0, 1)), 0.0);
#endif

  // Luma: Blend or Branch with untonemapped above mid grey
  // This is recomputed every frame, so we can always branch
  if (LumaSettings.DisplayMode == 1)
  {
    float3 midGrayProgress = saturate(tonemappedColor.rgb / midGrayOut);
    float maxMidGrayProgress = max3(midGrayProgress);
    float3 midGrayTowhiteProgress = saturate((max(tonemappedColor.rgb, midGrayOut) - midGrayOut) / (1.0 - midGrayOut));
    float maxMidGrayTowhiteProgress = max3(midGrayTowhiteProgress);

    // Match the slope on of untonemapped with tonemapped on mid grey.
    // This will also re-apply the same hue shifts (at least the ones around on mid grey...).
    float3 remappedHDRColor = (originalHDRColor * midGreySlope) + midGreyOffset;

#if 1

    const float3 originalRemappedHDRColor = remappedHDRColor;

    bool debug = false;
    float colorGradingIntensity = debug ? DVS1 : 1.0; // At 0, we pass through the raw scene color, at 1, we do the full SDR->HDR grading remastering
    
    // Keep the vanilla shadow levels, per channel, it's already saturated and constrasty in UE generally, and we can't really expand it without distorting colors.
    remappedHDRColor = lerp(tonemappedColor.rgb, remappedHDRColor, sqrt(midGrayProgress)); // Note use sqr or neutral if we wanted to delay the passage to the "nuetral" HDR color here, possibly meaning either more or less saturated shadow

    // The more we are above mid grey, the more we restore the original hue, but from the "halved" tonemapped color,
    // so theren't no weird hue shift or desaturation from strong highlights.
    float tonemapHalveScale = lerp(1.0, lerp(0.667, 1.0, saturate((LumaSettings.GameSettings.HDRHighlightsHuePreservation - 0.75) / (1.0 - 0.75))) * (debug ? DVS2 : 1.0), sqrt(maxMidGrayProgress)); // Lower seems to break gradients. The value is fixed though the optimal value might depend by game or by scene.
    float3 hueSourceTonemappedColor = DecodeLUTOutput(SampleLUTColor(EncodeLUTInput(originalHDRColor * tonemapHalveScale))) / tonemapHalveScale;
    //hueSourceTonemappedColor = lerp(tonemappedColor.rgb, hueSourceTonemappedColor, sqrt(maxMidGrayProgress) /*sqrt(saturate(tonemappedColor.rgb))*/); // We'd do the same lerp twice, we already do it above. Also it's probably bad to do it by channel.
    float hueRestorationAmount = LumaSettings.GameSettings.HDRHighlightsHuePreservation;
    float shadowChrominanceRestorationAmount = 1.0 - sqrt(maxMidGrayProgress); // We always do it to improve blends and preserve chrominance in shadow
    remappedHDRColor = RestoreHueAndChrominance(remappedHDRColor, hueSourceTonemappedColor, hueRestorationAmount, shadowChrominanceRestorationAmount);

    // The more we are above mid grey, the more we restore the original chrominance (saturation),
    // but as we reach 1, we lower the restoration intensity, because in SDR highlights often coverge to white (too much).
    // We don't go too high to leave enough HDR "saturation" on highlights.
    float maxHighlightsDiscoloration = LumaSettings.GameSettings.HDRHighlightsChrominancePreservation; // TODO: Make per game presets!
    remappedHDRColor = RestoreHueAndChrominance(remappedHDRColor, tonemappedColor.rgb, 0.0, sqrt(maxMidGrayTowhiteProgress) * maxHighlightsDiscoloration);

    remappedHDRColor = lerp(originalRemappedHDRColor, remappedHDRColor, colorGradingIntensity);

#if 0 // Blend with "tonemappedColor.rgb" already handled above, though some shadow seems to break here (likely UCS problems), so ... blend again instead!!!
    upgradedColor = remappedHDRColor;
#elif 1 // Uniform blend
    upgradedColor = lerp(tonemappedColor.rgb, remappedHDRColor, sqr(maxMidGrayProgress)); // Do a square to further delay the transition to the HDR color
#else // Per channel blend used to look better, but it breaks gradients given that we operated with UCS
    upgradedColor = lerp(tonemappedColor.rgb, remappedHDRColor, sqr(midGrayProgress));
#endif

#else // Bad implementation: this will create steps as the LUT's tonemapper isn't fully per channel, find the mid grey in/out for red/green/magenta/yellow etc, and then blend to the closest ones based on the current color cube
    
    upgradedColor = tonemappedColor.rgb >= midGrayOut ? remappedHDRColor : tonemappedColor.rgb;
    
#endif
  }

#else // Untonemapped

  upgradedColor = originalHDRColor;

#endif

  if (LumaSettings.DisplayMode == 1)
  {
    upgradedColor = Saturation(upgradedColor, LumaSettings.GameSettings.HDRChrominance);

#if TONEMAP_TYPE == 2
    const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
    const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
    DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
    settings.DesaturationVsDarkeningRatio = 0.5; // TODO: tweak all
    upgradedColor = DICETonemap(upgradedColor * paperWhite, peakWhite, settings) / paperWhite;
#endif
  }

  upgradedColor.rgb = EncodeLUTOutput(upgradedColor.rgb);

#if DIMENSIONS_2D
  //pixelPos.xyz = ConditionalConvert3DTo2DLUTCoordinates(pixelPos);
  targetLUT[pixelPos2D.xy] = float4(upgradedColor, tonemappedColor.a);
#else
  targetLUT[pixelPos.xyz] = float4(upgradedColor, tonemappedColor.a);
#endif
}