// ---- Refactored from 3Dmigoto decompile
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

// VS packs offsets[0,2,4,6,8]: far-neg, near-neg, center, near-pos, far-pos.
// Gaussian sigma=1.0.
static const float kGaussianWeights5[5] =
{
  0.05450,  // v1.xy  (far negative)
  0.24420,  // v1.zw  (near negative)
  0.40261,  // v2.xy  (center)
  0.24420,  // v2.zw  (near positive)
  0.05450,  // v3.xy  (far positive)
};

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  out float4 o0 : SV_TARGET0)
{
  float2 minUV = gauss_minUV.xy;
  float2 maxUV = gauss_maxUV.xy;

  if (LumaData.GameData.IsUpscaling != 0)
  {
    const float2 renderScaleInv = rcp(LumaData.RenderResolutionScale.xy);
    minUV = saturate(minUV * renderScaleInv);
    maxUV = saturate(maxUV * renderScaleInv);
  }

  float3 blurColor = 0.0;
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v1.xy, minUV, maxUV), 0).xyz * kGaussianWeights5[0];
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v1.zw, minUV, maxUV), 0).xyz * kGaussianWeights5[1];
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v2.xy, minUV, maxUV), 0).xyz * kGaussianWeights5[2];
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v2.zw, minUV, maxUV), 0).xyz * kGaussianWeights5[3];
  blurColor += srcSamplerTexture.SampleLevel(srcSampler_s, clamp(v3.xy, minUV, maxUV), 0).xyz * kGaussianWeights5[4];

  o0.xyz = gauss_mix.xyz * blurColor;
  o0.w = 0.0;
}