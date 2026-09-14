// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 09 21:34:07 2026

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
SamplerState LinearClampSampler_s : register(s1);
Texture2D<float4> HDRScene : register(t0);
Texture2D<float4> UIScene : register(t1);
Texture2D<float> PQEncodeLUT : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  r0.x = 0.100000024 * uiMaxLumScale;
  r0.yzw = HDRScene.Sample(PointSampler_s, v2.xy).xyz;
  // r0.yzw = 2.009233 * r0.yzw; // dumb corrections
  // r0.yzw = pow(r0.yzw, 1.5);

  if (GS.UIBrightnessRatio <= 0) {
    o0.xyz = r0.yzw;
    return;
  }

  r1.x = dot(float3(0.298999995,0.587000012,0.114), r0.yzw);
  r1.y = -uiMaxLumScale + r1.x;
  r1.z = -r1.y / r0.x;
  r1.z = 1.44269502 * r1.z;
  r1.z = exp2(r1.z);
  r1.z = 1 + -r1.z;
  r0.x = r1.z * r0.x;
  r1.z = cmp(0 < r1.y);
  r0.x = r1.z ? r0.x : r1.y;
  r0.x = uiMaxLumScale + r0.x;
  r0.x = r0.x + -r1.x;

  r2.xyzw = UIScene.Sample(PointSampler_s, v2.xy).xyzw;
  r1.y = -r2.w * r2.w + 1;
  r0.x = r1.y * r0.x + r1.x;
  r1.yzw = r0.yzw * r0.xxx;
  r1.yzw = r1.yzw / r1.xxx;
  r0.x = cmp(0 < r1.x);
  r1.xyz = r0.xxx ? r1.yzw : 0;
  r0.xyz = rangeAdj ? r1.xyz : r0.yzw;
  r1.xyz = /* uiMaxLumScale * */GS.UIBrightnessRatio * r2.xyz; // comment out = max is 1
  r1.xyz = r0.xyz * r2.www + r1.xyz;
  r0.xyz = noUIBlend ? r0.xyz : r1.xyz;

  // PQ Encode (doubles BT2020 perchannel tonemap)
//   if (false) {
//     r0.xyz = pow(r0.xyz, 2.2);
// 
//     // color matrix convert (BT709 -> BT2020 + user saturation)
//     r1.x = dot(r0.xyz, mtxColorConvert._m00_m10_m20);
//     r1.y = dot(r0.xyz, mtxColorConvert._m01_m11_m21);
//     r1.z = dot(r0.xyz, mtxColorConvert._m02_m12_m22);
// 
//     r0.xyz = 0.02 * r1.xyz;
//     r0.xyz = pow(r0.xyz, 0.25);
//     r0.xyz = r0.xyz * float3(0.99609375,0.99609375,0.99609375) + float3(0.001953125,0.001953125,0.001953125);
//     r0.w = 0;
//     r1.x = PQEncodeLUT.Sample(LinearClampSampler_s, r0.xw).x;
//     r1.y = PQEncodeLUT.Sample(LinearClampSampler_s, r0.yw).x;
//     r1.z = PQEncodeLUT.Sample(LinearClampSampler_s, r0.zw).x;
//   } else {
//     float p = PeakWhiteNits / GamePaperWhiteNits;
// 
//     r0.xyz = gamma_sRGB_to_linear(r0.xyz);
//     // r0.xyz = r0.xyz / ((r0.xyz / p) + 1);
//     r0.xyz = (r0.xyz * p) * rsqrt(r0.xyz * r0.xyz + p * p);
//     r0.xyz = linear_to_sRGB_gamma(r0.xyz);
// 
//     r1.xyz = r0.xyz;
//   }

  // noise & dither (only for HDR, rather useless)
#if 0
  r0.x = dot(float2(171,231), v0.xy);
  r0.xyz = float3(0.0093457941,0.010309278,0.0149253728) * r0.xxx;
  r0.xyz = frac(r0.xyz);
  r0.xyz = float3(-0.5,-0.5,-0.5) + r0.xyz;
  r0.xyz = noiseIntensity * r0.xyz;
  r0.xyz = r0.xyz * float3(0.000977517106,0.000977517106,0.000977517106) + r1.xyz;
  r0.xyz = enableDithering ? r0.xyz : r1.xyz;
#else
  r0.xyz = r1.xyz;
#endif

  o0.xyz = r0.xyz;

  return;
}