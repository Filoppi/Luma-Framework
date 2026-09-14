// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 09 21:34:19 2026

cbuffer DisplayMappingData : register(b0)
{
  float outputGammaForSDR : packoffset(c0);
  int noUIBlend : packoffset(c0.y);
  int rangeAdj : packoffset(c0.z);
  int enableDithering : packoffset(c0.w);
  float noiseIntensity : packoffset(c1);
  float noiseScale : packoffset(c1.y);
  float uiMaxLumScale : packoffset(c1.z);
  float uiMaxLumScaleRecp : packoffset(c1.w);
  float uiMaxNitsNormalizedLinear : packoffset(c2);
  float4x3 mtxColorConvert : packoffset(c3);
}

SamplerState PointSampler_s : register(s0);
Texture2D<float4> HDRScene : register(t0);
Texture2D<float4> UIScene : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  r0.xyz = HDRScene.Sample(PointSampler_s, v2.xy).xyz;
  r1.xyzw = UIScene.Sample(PointSampler_s, v2.xy).xyzw;

  if (GS.UIBrightnessRatio <= 1 && GS.UIBrightnessRatio > 0) {
    r1.xyz = pow(r1.xyz, 2.2);
    r1.xyz *= GS.UIBrightnessRatio;
    r1.xyz = pow(r1.xyz, 1/2.2);
    r0.xyz = r0.xyz * r1.www + r1.xyz;
  }

  r0.xyz = log2(r0.xyz);
  r0.xyz = outputGammaForSDR * r0.xyz;
  o0.xyz = exp2(r0.xyz);
  return;
}