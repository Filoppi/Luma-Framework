// ---- Created with 3Dmigoto v1.3.16 on Wed Jul 22 13:50:03 2026

cbuffer _Globals : register(b0)
{
  float2 invPixelSize : packoffset(c0);
  float4 depthFactors : packoffset(c1);
  float2 fadeParams : packoffset(c2);
  float4 color : packoffset(c3);
  float4 colorMatrix0 : packoffset(c4);
  float4 colorMatrix1 : packoffset(c5);
  float4 colorMatrix2 : packoffset(c6);
  float exponent : packoffset(c7);
  float4 ironsightsDofParams : packoffset(c8);
  uint4 mainTextureDimensions : packoffset(c9);
  float4 combineTextureWeights[2] : packoffset(c10);
  float4 colorScale : packoffset(c12);
  float2 invTexelSize : packoffset(c13);
  float4 downsampleQuarterZOffset : packoffset(c14);
  int sampleCount : packoffset(c15);
  float filterWidth : packoffset(c15.y);
  float mipLevelSource : packoffset(c15.z);
  float convolveSpecularPower : packoffset(c15.w);
  float convolveBlurRadius : packoffset(c16);
  int convolveSampleCount : packoffset(c16.y);
  float3 dofDepthScaleFactors : packoffset(c17);
  float4 radialBlurScales : packoffset(c18);
  float2 radialBlurCenter : packoffset(c19);
  float4 poissonRadialBlurConstants : packoffset(c20);
  float blendFactor : packoffset(c21);
  float3 filmGrainColorScale : packoffset(c21.y);
  float4 filmGrainTextureScaleAndOffset : packoffset(c22);
  float3 depthScaleFactors : packoffset(c23);
  float4 dofParams : packoffset(c24);
  float4 dofParams2 : packoffset(c25);
  float4 dofDebugParams : packoffset(c26);
  float3 bloomScale : packoffset(c27);
  float3 invGamma : packoffset(c28);
  float3 luminanceVector : packoffset(c29);
  float3 vignetteParams : packoffset(c30);
  float4 vignetteScaleAndOffset : packoffset(c31);
  float4 vignetteColor : packoffset(c32);
  float4 chromostereopsisParams : packoffset(c33);
  float4 distortionScaleOffset : packoffset(c34);
}

SamplerState mainTextureSampler_s : register(s0);
SamplerState colorGradingTextureSampler_s : register(s1);
SamplerState tonemapBloomTextureSampler_s : register(s2);
SamplerState filmGrainTextureSampler_s : register(s3);
SamplerState ironsightsDofTextureSampler_s : register(s4);
Texture2D<float4> mainTexture : register(t0);
Texture3D<float4> colorGradingTexture : register(t1);
Texture2D<float4> tonemapBloomTexture : register(t2);
Texture2D<float4> filmGrainTexture : register(t3);
Texture2D<float4> ironsightsDofTexture : register(t4);


// 3Dmigoto declarations
#define cmp -
#define COMMON_TONEMAP 1
#include "./common.hlsl"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // DoF
  r0.xyzw = ironsightsDofTexture.Sample(ironsightsDofTextureSampler_s, v2.xy).xyzw;
  r0.xyz = r0.xyz * r0.xyz;
  r0.xyz = ironsightsDofParams.yyy * r0.xyz;
  r1.x = ironsightsDofParams.x * r0.w;
  r0.w = -r0.w * ironsightsDofParams.x + 1;
  r0.xyz = r1.xxx * r0.xyz;
  r1.xyz = mainTexture.Sample(mainTextureSampler_s, v2.xy).xyz;
  #if TONEMAP_DOF == 1
    r0.xyz = r1.xyz * r0.www + r0.xyz;
  #else
    r0.xyz = r1.xyz;
  #endif

  r1.xyz = tonemapBloomTexture.Sample(tonemapBloomTextureSampler_s, v2.xy).xyz;
  r0.xyz = r1.xyz * bloomScale.xyz * GS.Bloom + r0.xyz;

  r0.xyz = colorScale.xyz * r0.xyz;
  T_Color(r0.xyz);

  // r1.xy = v2.xy * vignetteScaleAndOffset.xy + vignetteScaleAndOffset.zw;
  // r0.w = dot(r1.xy, r1.xy);
  // r0.w = 1 + -r0.w;
  // r0.w = max(0, r0.w);
  // r0.w = log2(r0.w);
  // r0.w = vignetteParams.z * r0.w;
  // r0.w = exp2(r0.w);
  // r0.xyz = r0.xyz * r0.www;
  T_Vignette(v2);

  // r1.xyz = r0.xyz * float3(0.971251667,0.971251667,0.971251667) + float3(0.0578126311,0.0578126311,0.0578126311);
  // r1.xyz = r1.xyz * r0.xyz;
  // r2.xyz = r0.xyz * float3(0.600000501,0.600000501,0.600000501) + float3(0.999998152,0.999998152,0.999998152);
  // r0.xyz = r0.xyz * r2.xyz + float3(0.0599999018,0.0599999018,0.0599999018);
  // r0.xyz = r1.xyz / r0.xyz;
  T_Rolloff();

  // r0.xyz = sqrt(r0.xyz);
  T_Gamma();

  // r0.xyz = r0.xyz * float3(0.96875,0.96875,0.96875) + float3(0.015625,0.015625,0.015625);
  // r0.xyz = colorGradingTexture.Sample(colorGradingTextureSampler_s, r0.xyz).xyz;
  T_LUT();

  // r1.xy = v2.xy * filmGrainTextureScaleAndOffset.xy + filmGrainTextureScaleAndOffset.zw;
  // r0.w = filmGrainTexture.Sample(filmGrainTextureSampler_s, r1.xy).x;
  // r0.w = -0.5 + r0.w;
  // r0.xyz = r0.www * filmGrainColorScale.xyz + r0.xyz;
  T_FilmGrain(v2);

  // o0.w = dot(r0.xyz, float3(0.298999995,0.587000012,0.114));
  // o0.xyz = r0.xyz;
  o0 = T_Out();
  return;
}