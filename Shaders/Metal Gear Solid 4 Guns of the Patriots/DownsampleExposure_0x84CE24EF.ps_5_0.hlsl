#include "../Includes/Common.hlsl"

#ifndef ENABLE_IMPROVED_BLUR
#define ENABLE_IMPROVED_BLUR 1
#endif

// For extra stability (still not perfect)
#ifndef ENABLE_HIGH_QUALITY_AUTO_EXPOSURE
#define ENABLE_HIGH_QUALITY_AUTO_EXPOSURE 0
#endif

Texture2D<float4> t0 : register(t0);

// Linear clamp sampler
SamplerState s0_s : register(s0);

// This shader is called 4 times in a row, with a blend mode that is additive on rgb and "max" on a.
// The normalization (/4) of the average luminance is supposedly done on the CPU side.
// The first call is on the bottom left, then top left, then bottom right and then top right (or something like that). Each covers one area, to speed up reads.
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 uv : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
#if 0 // Color/UV output test
  //uv = v0.xy / 32; // Force fullscreen UVs
  o0 = t0.Sample(s0_s, uv).xyzw;
  //o0 = linear_to_gamma(uv.y).x;
  return;
#endif

  float3 colorsSum = 0;
  float maxLuminance = 0.0;
  float sumLuminance = 0.0;

#if ENABLE_IMPROVED_BLUR && ENABLE_HIGH_QUALITY_AUTO_EXPOSURE
  static const int samples = 16;
#elif ENABLE_IMPROVED_BLUR
  // TODO: just generate mips of the source image up to 32x32 or something like that, it'd be faster and a perfect integration/average, completely fixing unstable auto exposure
  // x and y loop iterations are the same as source and target are square.
  static const int samples = 8; // 16 and 32 might be too slow
#else
  static const int samples = 4;
#endif
  static const float sampleStep = 1.0 / float(samples);
#if ENABLE_IMPROVED_BLUR
  // Tile each draw's UV region with one sampling footprint per output pixel.
  float2 uvDx = ddx(uv);
  float2 uvDy = ddy(uv);
  float2 sampleDx = uvDx * sampleStep;
  float2 sampleDy = uvDy * sampleStep;
#endif
  for (int x = 0; x < samples; ++x)
  {
    for (int y = 0; y < samples; ++y)
    {
#if ENABLE_IMPROVED_BLUR
      float2 cellOffset = (float2(x, y) + 0.5) * sampleStep - 0.5;
      // Prevents different draws from reading overlapping areas of the source texture, they are all only considered once. Also prevents reading outside valid areas.
      float2 sampleUV = uv + uvDx * cellOffset.x + uvDy * cellOffset.y;
      // Use the subcell footprint for mip selection instead of the whole output pixel. Texture has no mips.
      float4 encoded = t0.SampleGrad(s0_s, sampleUV, sampleDx, sampleDy);
#else
      // The original code sampled many areas twice and was just overall very bad at averaging the source!
      float2 sampleUV = uv + float2(-0.5 + x * sampleStep, 0.5 - y * sampleStep);
      float4 encoded = t0.Sample(s0_s, sampleUV);
#endif

      // Decode HDR
      float3 color = encoded.rgb / encoded.a * 0.25;

      colorsSum += color;

#if ENABLE_HIGH_QUALITY_AUTO_EXPOSURE
      // Run exposure average in linear space for higher quality.
      // This might change the balance between shadow and light a lot, but with the exception of certain fixed screens, it'd be more accurate.
      color = gamma_to_linear(color, GCT_POSITIVE);
#endif
#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear
      float luminance = GetLuminance(color);
#else
      float luminance = dot(color, float3(0.300000012,0.589999974,0.109999999));
#endif
      maxLuminance = max(maxLuminance, luminance);
      sumLuminance += luminance;
    }
  }

  float averageLuminance = sumLuminance / float(samples * samples);
#if ENABLE_HIGH_QUALITY_AUTO_EXPOSURE
  // Note that the HW blends will still blend in gamma space!
  averageLuminance = linear_to_gamma(averageLuminance, GCT_NONE).x;
  maxLuminance = linear_to_gamma(maxLuminance, GCT_NONE).x;
#endif
  float4 exposureScaling = v1; // Might be tint, unknown. Seems greyscale on rgb usually, with alpha having a different value.

  o0 = exposureScaling * float4(averageLuminance.xxx, maxLuminance);

#if 0 // Color output test (just to see the covered area quickly)
  o0.xyz = colorsSum.rgb / float(samples * samples);
#endif
}
