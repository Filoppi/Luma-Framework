#ifndef BLOOM_TAP_COUNT
#error BLOOM_TAP_COUNT must be defined before including Bloom_BlurGaussianTemplate_0x########.ps_5_0.hlsl
#endif

#ifndef BLOOM_OFFSET_STRIDE
#error BLOOM_OFFSET_STRIDE must be defined before including Bloom_BlurGaussianTemplate_0x########.ps_5_0.hlsl
#endif

#ifndef BLOOM_UV_SEMANTIC
#error BLOOM_UV_SEMANTIC must be defined before including Bloom_BlurGaussianTemplate_0x########.ps_5_0.hlsl
#endif

#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float4 graph_color : packoffset(c0);
  float3 copy_srcColorFactor : packoffset(c1);
  float3 copy_srcColorFactorArray[11] : packoffset(c2);
  float3 highpass_gamma : packoffset(c13);
  float2 highpass_pixelOffset : packoffset(c14);
  float3 highpass_threshold : packoffset(c15);
  float2 gauss_minUV : packoffset(c16);
  float2 gauss_maxUV : packoffset(c16.z);
  float3 gauss_mix : packoffset(c17);
  float4 gauss_weights[65] : packoffset(c18);
  float2 gauss_offsets[65] : packoffset(c83);
  float3 compo_sparkBlend : packoffset(c148);
  float3 compo_oneMinusSparkBlend : packoffset(c149);
  float3 compo_glareGamma : packoffset(c150);
  float4 compo_glareWeights[11] : packoffset(c151);
  float4 compo_glareWeightsSumInv : packoffset(c162);
  float compo_glareBaseBlurMip : packoffset(c163);
  float3 compo_glareSoftAmount : packoffset(c163.y);
  float3 compo_glareSoftExpand : packoffset(c164);
  float3 compo_glareFoggyAmount : packoffset(c165);
  float3 compo_glareFoggyExpand : packoffset(c166);
  float4 compo_vignetteParam0 : packoffset(c167);
  float4 compo_vignetteParam1 : packoffset(c168);
  float4 compo_vignetteParam2 : packoffset(c169);
}

SamplerState srcSampler_s : register(s0);
Texture2D<float4> srcSamplerTexture : register(t0);

static const float INV_LN2 = 1.4426950408889634;

float GetKernelSigma()
{
#if BLOOM_TAP_COUNT == 17
  return 3.0;
#elif BLOOM_TAP_COUNT == 33
  return 5.8;
#else
  return 10.5;
#endif
}

int GetOffsetIndexForTap(int tap)
{
#if BLOOM_TAP_COUNT == 65
  return tap;
#else
  if (tap == 0)
  {
    return 2;
  }

  if (tap == 1)
  {
    return 0;
  }

  return tap * 2;
#endif
}

float GetGaussianWeight(int offsetIndex)
{
  const float centerOffsetIndex = ((float)(BLOOM_TAP_COUNT - 1) * (float)BLOOM_OFFSET_STRIDE) * 0.5;
  const float sigma = GetKernelSigma();
  const float tapDistance = abs((float)offsetIndex - centerOffsetIndex) / (float)BLOOM_OFFSET_STRIDE;
  const float gaussianExponent = -0.5 * (tapDistance * tapDistance) / (sigma * sigma);
  return exp2(gaussianExponent * INV_LN2);
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : BLOOM_UV_SEMANTIC,
  out float4 o0 : SV_TARGET0)
{
  float2 minUV = gauss_minUV.xy;
  float2 maxUV = gauss_maxUV.xy;
  float2 offsetScale = float2(1.0, 1.0);

  if (LumaData.GameData.IsUpscaling != 0)
  {
    const float2 renderScaleInv = rcp(LumaData.RenderResolutionScale.xy);
    minUV = saturate(minUV * renderScaleInv);
    maxUV = saturate(maxUV * renderScaleInv);
    offsetScale = renderScaleInv;
  }

  float3 blurColor = float3(0.0, 0.0, 0.0);
  float weightSum = 0.0;

  [unroll]
  for (int tap = 0; tap < BLOOM_TAP_COUNT; tap++)
  {
    const int offsetIndex = GetOffsetIndexForTap(tap);
    const float2 offsetUV = gauss_offsets[offsetIndex].xy * offsetScale;
    const float2 sampleUV = clamp(v1.xy + offsetUV, minUV, maxUV);
    const float weight = GetGaussianWeight(offsetIndex);
    const float3 sampleColor = srcSamplerTexture.SampleLevel(srcSampler_s, sampleUV, 0).xyz;

    blurColor += sampleColor * weight;
    weightSum += weight;
  }

  blurColor /= max(weightSum, 1e-6);

  o0.xyz = gauss_mix.xyz * blurColor;
  o0.w = 0.0;
}