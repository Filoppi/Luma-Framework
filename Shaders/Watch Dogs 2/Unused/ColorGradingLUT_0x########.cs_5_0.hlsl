#define LUT_SIZE 32
#define LUT_3D 1

#include "../Includes/Common.hlsl"

#ifndef HIGH_QUALITY_LUT
#define HIGH_QUALITY_LUT 1
#endif

cbuffer HDRColorGradingLUTs : register(b0)
{
  float4 ColorCorrectColor : packoffset(c0);
  float4 ColorCorrectGain : packoffset(c1);
  float4 ColorCorrectGamma : packoffset(c2);
  float4 ColorCorrectLift : packoffset(c3);
  float4 ColorCorrectOffset : packoffset(c4);
  float ColorCorrectBrightness : packoffset(c5);
  float ColorCorrectContrast : packoffset(c5.y);
  float ColorCorrectSaturation : packoffset(c5.z);
  float HDRColorGradingLUTsWeights[16] : packoffset(c6); // There's one extra of these, seemengly unused
}

Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures00__TexObj__ : register(t0);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures01__TexObj__ : register(t1);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures02__TexObj__ : register(t2);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures03__TexObj__ : register(t3);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures04__TexObj__ : register(t4);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures05__TexObj__ : register(t5);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures06__TexObj__ : register(t6);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures07__TexObj__ : register(t7);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures08__TexObj__ : register(t8);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures09__TexObj__ : register(t9);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures10__TexObj__ : register(t10);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures11__TexObj__ : register(t11);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures12__TexObj__ : register(t12);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures13__TexObj__ : register(t13);
Texture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsTextures14__TexObj__ : register(t14);
RWTexture3D<float4> HDRColorGradingLUTs__HDRColorGradingLUTsBlendedTexture : register(u0);

SamplerState ColorClamp_s : register(s15); // Linear clamp

static float LUTSize = 32.0;
static float LUTMax = LUTSize - 1.0;

float3 EncodeLUTInput(float3 color)
{
  return saturate(log2(abs(color)) * 0.0632478595 + 0.690302372); // LUT Log encoding (Abs+Saturate because it's free)
}
float3 DecodeLUTInput(float3 color)
{
  return exp2((color - 0.690302372) / 0.0632478595); // LUT Log decoding (this decodes the LUT input back, not necessarily the LUT output, which might be linear already)
}

#if 0
// Manual trilinear sample of a 3D texture using UV in [0,1]^3, given we had no sampler here
float3 LinearSample(Texture3D<float4> texture, float3 uvw)
{
  float3 coord = uvw * (LUTMax); // Convert UV to texel space
  float3 baseF = floor(coord);
  uint3 p0 = (uint3)baseF;
  uint3 p1 = min(p0 + 1, (uint3)(LUTMax + 0.5));
  float3 f = coord - baseF; // fractional part (lerp weights)

  // Fetch 8 neighbors
  float3 c000 = texture.Load(int4(p0.x, p0.y, p0.z, 0)).xyz;
  float3 c100 = texture.Load(int4(p1.x, p0.y, p0.z, 0)).xyz;
  float3 c010 = texture.Load(int4(p0.x, p1.y, p0.z, 0)).xyz;
  float3 c110 = texture.Load(int4(p1.x, p1.y, p0.z, 0)).xyz;

  float3 c001 = texture.Load(int4(p0.x, p0.y, p1.z, 0)).xyz;
  float3 c101 = texture.Load(int4(p1.x, p0.y, p1.z, 0)).xyz;
  float3 c011 = texture.Load(int4(p0.x, p1.y, p1.z, 0)).xyz;
  float3 c111 = texture.Load(int4(p1.x, p1.y, p1.z, 0)).xyz;

  // Trilinear interpolation
  float3 c00 = lerp(c000, c100, f.x);
  float3 c10 = lerp(c010, c110, f.x);
  float3 c01 = lerp(c001, c101, f.x);
  float3 c11 = lerp(c011, c111, f.x);

  float3 c0 = lerp(c00, c10, f.y);
  float3 c1 = lerp(c01, c11, f.y);

  return lerp(c0, c1, f.z);
}
#endif

float3 SampleLUTPixel(int3 pixelCoords)
{
  float3 color = 0.0; // Default to black if there's no LUT (it doesn't actually try to generate the linear color by default)
  // 3D SDR LUTs presumably loaded from disk, sRGB (linear)
#if _EED9A3FF || _2B8472D5 || _8B8BEC2A || _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures00__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[0];
#endif
#if _2B8472D5 || _8B8BEC2A || _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures01__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[1];
#endif
#if _8B8BEC2A || _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures02__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[2];
#endif
#if _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures03__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[3];
#endif
#if _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures04__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[4];
#endif
#if _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures05__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[5];
#endif
#if _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures06__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[6];
#endif
#if _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures07__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[7];
#endif
#if _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures08__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[8];
#endif
#if _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures09__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[9];
#endif
#if _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures10__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[10];
#endif
#if _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures11__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[11];
#endif
#if _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures12__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[12];
#endif
#if _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures13__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[13];
#endif
#if _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures14__TexObj__.Load(int4(pixelCoords, 0)).rgb * HDRColorGradingLUTsWeights[14];
#endif
  return color;
}

float3 SampleLUT(float3 uvw)
{
  uvw = uvw * (1.0 - (1.0 / LUTSize)) + (0.5 / LUTSize); // Acknowledge half texel offset

  float3 color = 0.0;
#if _EED9A3FF || _2B8472D5 || _8B8BEC2A || _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures00__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[0];
#endif
#if _2B8472D5 || _8B8BEC2A || _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures01__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[1];
#endif
#if _8B8BEC2A || _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures02__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[2];
#endif
#if _919F1537 || _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures03__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[3];
#endif
#if _56F305BB || _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures04__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[4];
#endif
#if _2033D7C9 || _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures05__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[5];
#endif
#if _0A696247 || _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures06__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[6];
#endif
#if _28816DF0 || _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures07__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[7];
#endif
#if _7336D9BE || _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures08__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[8];
#endif
#if _6F668AD1 || _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures09__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[9];
#endif
#if _60118D8B || _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures10__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[10];
#endif
#if _8F54485D || _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures11__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[11];
#endif
#if _B054D156 || _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures12__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[12];
#endif
#if _21774BE1 || _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures13__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[13];
#endif
#if _69D3F6E7
  color += HDRColorGradingLUTs__HDRColorGradingLUTsTextures14__TexObj__.SampleLevel(ColorClamp_s, uvw, 0).rgb * HDRColorGradingLUTsWeights[14];
#endif
  return color;
}

// TODOFT5: move to generic code and use for Just Cause 3 as well. Do a similar function for tonemapping...
// Find the input value mapped to a lut output (on the grey scale), basically inverts the LUT.
// Both input and output are meant to be in "decoded" lut space, and thus linear.
float3 FindLUTInputForOutput(float targetValue /*= MidGray*/, out float3 slope, out float3 offset)
{
  // If the target value is <= 0.5, search from 0 up, otherwise search from 1 down
  bool flipDirection = targetValue > 0.5;

  // Start at texel 0 along the grey diagonal (0,0,0)
  float prevInput = flipDirection ? 1.0 : 0.0;
  float3 prevOutput3 = SampleLUTPixel(flipDirection ? (LUT_SIZE - 1) : 0); // Input is already encoded with the LUT input encoding, so the steps ("LUT_SIZE") are roughly equal
  float prevOutput = average(prevOutput3); // The best we can do is the average on the grey diagonal here (luminance isn't particularly relevant)

  [loop]
  for (int i = (flipDirection ? (LUT_SIZE - 2) : 1); flipDirection ? (i >= 0) : (i < LUT_SIZE); flipDirection ? --i : ++i)
  {
    // Normalized position for texel i
    float input = (float)i / (float)(LUT_SIZE - 1);
    float3 output3 = SampleLUTPixel(i);
    float output = average(output3);

    // First crossing (or matching)
    bool checkPrevious = false; // Not needed unless the LUT starts from a value above our target, or goes up and down. Both are unlikely.
    bool hit = flipDirection
      ? ((!checkPrevious || prevOutput >= targetValue) && output <= targetValue)
      : ((!checkPrevious || prevOutput <= targetValue) && output >= targetValue);
    if (hit)
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
  // Alternatively we could return black, or white, or directly the target value but that'd be random, or expose a fallback value in the func params.
  slope = 1.0;
  offset = 0.0;
  return SampleLUT(0.5);
}

// Linear in and sRGB gamma space out (though this is later sampled as sRGB/linear, which isn't good for SDR gamma space encoded LUTs, given the input would be log space)
// The permutations are as follows (n of luts):
// 0xAC50585B 0
// 0xEED9A3FF 1
// 0x2B8472D5 2
// 0x8B8BEC2A 3
// 0x919F1537 4
// 0x56F305BB 5
// 0x2033D7C9 6
// 0x0A696247 7
// 0x28816DF0 8
// 0x7336D9BE 9
// 0x6F668AD1 10
// 0x60118D8B 11
// 0x8F54485D 12
// 0xB054D156 13
// 0x21774BE1 14
// 0x69D3F6E7 15
[numthreads(8, 8, 1)]
void main(uint3 vThreadID : SV_DispatchThreadID)
{
  float3 color = SampleLUTPixel(vThreadID.xyz);
  
#if !defined(_AC50585B) || !_AC50585B // Avoids warning, this case is meant to be all black anyway

#if HIGH_QUALITY_LUT
  float midGrayOut = MidGray;
  float3 midGreySlope = 1.0;
  float3 midGreyOffset = 0.0;
  float3 midGrayIn = FindLUTInputForOutput(midGrayOut, midGreySlope, midGreyOffset);
#else
  float midGrayIn = MidGray; // Values between 0.15 and 0.25 look good // TODO: improve... nonsense, only mid grey out matters. Also we should ideally take the slope too.
  float3 midGrayOut = SampleLUT(EncodeLUTInput(midGrayIn)); // Do this per channel, in case the LUT also did color filtering (we'd want to project it on the raw color too)
#endif // HIGH_QUALITY_LUT

  float3 rawColor = DecodeLUTInput(vThreadID.xyz / LUTMax);

  // Luma: Blend or Branch with untonemapped above mid grey
  // This is recomputed every frame, so we can always branch
  if (LumaSettings.DisplayMode == 1)
  {
#if HIGH_QUALITY_LUT // Better formula, contiguous gradients
    rawColor = (rawColor * midGreySlope) + midGreyOffset;
    float3 colorRatio = rawColor != 0.0 ? (color / rawColor) : 1.0;
    color = color >= midGrayOut ? rawColor : color;

#if 0 // Expand colors below mid grey
    // Find the LUT change ratio in shadow (below mid gray) in the original color space (BT.709),
    // and reproject it in BT.2020, it will slightly shift colors,
    // but as long as this is a tonemapping LUT that applied some filmic TM on shadow (crushing them and expanding their saturation/contrast),
    // it should look nicer when expanded.

    // To tell if the LUT is just a TM LUT, we can sample 0.18 0.18 0.18 from it, and see if all channels output is the same. If not, lower the expansion rate, given that it's also a grading lut, and reprojecting it in BT.2020 would shift the results too much.
    float3 gain = midGrayIn != 0.0 ? (midGrayOut / midGrayIn) : 0.0; // Per channel scale
    float minGain = min3(gain);
    float maxGain = max3(gain);
    float lutGrayUniformity;
    if (maxGain != 0.0) // 0 when channels differ as much as possible, 1 when they’re identical
      lutGrayUniformity = saturate(1.0 - ((maxGain - minGain) / maxGain));
    else
      lutGrayUniformity = 0.0;
    
    // Don't do expansion in BT.2020 if the LUT raised colors, given that would have been likely to reduce "saturation" (the color range), so doing it in BT.2020 is possibly even more detrimental
    float3 expansionRatio = colorRatio >= 1.0 ? 0.0 : lutGrayUniformity; // TODO: make sure this doesn't cause steps in gradients, otherwise turn the branch into some lerp
    float3 expandedColor = lerp(color, BT2020_To_BT709(BT709_To_BT2020(rawColor) * colorRatio), expansionRatio * DVS7); // TODO: remove DVS!!! Also flip the "colorRatio >= 1.0" condition above???
    color = lerp(expandedColor, color, pow(saturate(color / midGrayOut), 2.0));
#endif
#else // !HIGH_QUALITY_LUT
    // Remap the raw color to the same average brightness
    rawColor *= midGrayOut / midGrayIn;
    color = lerp(color, rawColor, pow(saturate(color / midGrayOut), 2.0));
#endif // HIGH_QUALITY_LUT
  }

#endif // !_AC50585B

  float3 shadowRaise = saturate(1.0 - color) * ColorCorrectLift.xyz; // Raise shadow // Luma: added saturate for HDR safety
#if 1
  color = EmulateShadowOffset(color, shadowRaise); // TODO: try
#else
  color += shadowRaise;
#endif
  color *= ColorCorrectGain.xyz;

#if 0 // Luma: fixed negative values support (below too, given that now the pow is abs*sign)
  color = max(0.0, color);
#endif
  color = pow(abs(color), max(9.99999997e-007, ColorCorrectGamma.xyz)) * sign(color); // Usually neutral (1)
#if 1 // TODO: test
  color = EmulateShadowOffset(color, ColorCorrectOffset.xyz);
#else
  color += ColorCorrectOffset.xyz;
#endif
  color += ColorCorrectBrightness; // TODO: check if this is usually 0, otherwise fix it too

  float luminance = GetLuminance(color);
  color = lerp(luminance, color, ColorCorrectSaturation + 1.0); // Saturation is independent from the color space

  float contrastMidPoint = MidGray;
#if 0
	// Contrast adjustment (shift the colors from 0<->1 to (e.g.) -0.5<->0.5 range, multiply and shift back).
	// The higher the distance from the contrast middle point, the more contrast will change the color.
	// This generates negative colors for contrast > 1, and these could have invalid luminances.
	color = ((color - contrastMidPoint) * ColorCorrectContrast) + contrastMidPoint;
#else // Luma: improve contrast formula (HDR friendly)
	// Empirical value to match the original game constrast formula look more.
	// This has been carefully researched and applies to both positive and negative contrast.
	float adjustedContrastIntensity = sqrt(ColorCorrectContrast);
  // Run contrast in BT.2020 to generate more BT.2020 colors and "clip" less
  if (LumaSettings.DisplayMode == 1)
  {
    color = BT709_To_BT2020(color);
  }
	// Do abs() to avoid negative power, even if it doesn't make 100% sense, these formulas are fine as long as they look good
	color = pow(abs(color) / contrastMidPoint, adjustedContrastIntensity) * contrastMidPoint * sign(color);
  if (LumaSettings.DisplayMode == 1)
  {
    color = BT2020_To_BT709(color);
  }
#endif

  color *= saturate(ColorCorrectColor.xyz);

#if 0 // We upgrade the LUT to R16G16B16A16_FLOAT, so we don't need to convert to sRGB manually anymore (writing to a UAV doesn't do that automatically)
  color = linear_to_sRGB_gamma(color, GCT_MIRROR); // Luma: fixed negative values support
#elif 0 // Luma: encode in log to have a higher perceptual quality, better HDR support and also to have the same encoding in and out, which prevents the LUT from causing broken gradients (banding). Update: actually, not, outputting linear and skewing the input coordinates is better.
  color = EncodeLUTInput(color);
#endif
#if 0 // Test extra saturation to make sure gamut isn't clamped to BT.709
  color = Saturation(color, 1.0 + DVS10);
#endif
  HDRColorGradingLUTs__HDRColorGradingLUTsBlendedTexture[vThreadID.xyz].xyzw = float4(color, 0);
}