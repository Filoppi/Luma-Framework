// ---- Created with 3Dmigoto v1.4.1 on Mon May  4 16:47:20 2026

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
Texture2D<float4> srcSampler2Texture : register(t0);
Texture2D<float4> srcSampler3Texture : register(t1);
Texture2D<float4> srcSampler4Texture : register(t2);
Texture2D<float4> srcSampler5Texture : register(t3);
Texture2D<float4> srcSampler6Texture : register(t4);
Texture2D<float4> srcSampler7Texture : register(t5);
Texture2D<float4> srcSampler8Texture : register(t6);
Texture2D<float4> srcSampler9Texture : register(t7);
Texture2D<float4> srcSampler10Texture : register(t8);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = srcSampler10Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r1.xyz = srcSampler9Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[10].xyz + r1.xyz;
  r1.xyz = srcSampler8Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[9].xyz + r1.xyz;
  r1.xyz = srcSampler7Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[8].xyz + r1.xyz;
  r1.xyz = srcSampler6Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[7].xyz + r1.xyz;
  r1.xyz = srcSampler5Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[6].xyz + r1.xyz;
  r1.xyz = srcSampler4Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[5].xyz + r1.xyz;
  r1.xyz = srcSampler3Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[4].xyz + r1.xyz;
  r1.xyz = srcSampler2Texture.SampleLevel(srcSampler_s, v1.xy, 0).xyz;
  r0.xyz = r0.xyz * copy_srcColorFactorArray[3].xyz + r1.xyz;
  r0.xyz = copy_srcColorFactorArray[2].xyz * r0.xyz;
  o0.xyz = min(float3(40000,40000,40000), r0.xyz);
  o0.w = 0;
  return;
}