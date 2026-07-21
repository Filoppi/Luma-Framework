// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:51 2026
Buffer<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[53];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[4];
}

// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

float3 PerceptualLerp(float3 colorA, float3 colorB, float alpha, float gamma = 2.2)
{
	float luminanceA = GetLuminance(colorA.rgb);
	float luminanceB = GetLuminance(colorB.rgb);
	float gammaLuminanceA = pow(max(luminanceA, 0.f), 1.0 / gamma); // Linear->Gamma
	float gammaLuminanceB = pow(max(luminanceB, 0.f), 1.0 / gamma); // Linear->Gamma
	float targetLuminance = pow(lerp(gammaLuminanceA, gammaLuminanceB, alpha), gamma); // Blend in gamma space

	float3 colorLerped = lerp(colorA, colorB, alpha);
	float sourceLuminance = GetLuminance(colorLerped.rgb);
	if (sourceLuminance != 0.f) colorLerped.rgb *= max(targetLuminance / sourceLuminance, 0.f); // If any of the two luminance is negative, clip to black
	return colorLerped;
}

float3 RenoDX_Contrast(float3 x, float contrast, float mid_gray = 0.18f) {
  return pow(max(0, x / mid_gray), contrast) * mid_gray;
}

// https://www.desmos.com/calculator/stnfdhk9t1
float Rolloff(float x, float contrast, float invPeak, float invExposure) {
  x = pow(x, contrast);
  return safeDivision(x, x * invPeak + invExposure, 0);
}
float3 Rolloff(float3 x, float contrast, float invPeak, float invExposure) {
  x = pow(x, contrast);
  return safeDivision(x, x * invPeak + invExposure, 0);
}

float RolloffLocalMax(float contrast, float invPeak, float invExposure) {
  float c = contrast;
  float a = invPeak;
  float b = invExposure;
  return pow((b * c - b) / (a * c + a), rcp(c)); //2nd derivative = 0
}
float RolloffSlope(float x, float contrast, float invPeak, float invExposure) {
  return (invExposure * contrast * pow(x, contrast - 1)) / pow((invPeak * pow(x, contrast) + invExposure), 2); //1st derivative @ x
}
float3 RolloffExt(float3 x, float contrast, float invPeak, float invExposure) {
  float3 xBack = x;

  // extend
  float thres = RolloffLocalMax(contrast, invPeak, invExposure);
  float3 lower = Rolloff(x, contrast, invPeak, invExposure);
  float3 higher = RolloffSlope(max(thres, 0.000001), contrast, invPeak, invExposure) * (x - thres) + Rolloff(thres, contrast, invPeak, invExposure);
  x = x < thres ? lower : higher;

  // HDR per channel
  x = Neupow(x, HDR_PEAK, 2.2 * GS.WhiteClip);
  // x = ExponentialRollOff(x, thres, HDR_PEAK, 100 / GS.WhiteClip);

  // correct HDR perchannel;
  {
    float3 sdr = lower;
    sdr = UCS_Encode(sdr);
    x = UCS_Encode(x);
    x = RestoreHueAndChrominanceUcs(x, sdr, GS.BlowoutCorrection, 0, 0);
    x = UCS_Decode(x);

    // clean under/over shoot
    x = max(x, 0);
    x = min(HDR_PEAK, x);
  }

  return x;
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  uint v3 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // color
  r0.xy = v1.xy / v1.ww;
  r0.xy = min(cb2[22].zw, r0.xy);
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;

  // idk mask
  r1.xyz = (int3)r0.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
  r1.xyz = cmp((int3)r1.xyz != int3(0x7f800000,0x7f800000,0x7f800000));
  r0.xyz = r1.xyz ? r0.xyz : float3(65000,65000,65000);

  // rolloff
  float contrast = cb3[0].x;
  float invPeak = t1.Load(1).x; //aka white clip
  float invExposure = t1.Load(2).x;
  if (HDR_ENABLED) {
    r0.xyz = RolloffExt(r0.xyz, contrast, invPeak, invExposure);
  } else {
    r0.xyz = Rolloff(r0.xyz, contrast, invPeak, invExposure);
  }

  // r0.xyz = t1.Load(1).x - DVS1; //debug identify invPeak
  // r0.xyz = t1.Load(2).x - DVS1; //debug identify invExposure

  // max channel saturation / white path
  r0.w = max(r0.y, r0.z);
  r0.w = max(r0.x, r0.w);

  r0.w = max(9.99999975e-006, r0.w); 
  r0.xyz = r0.xyz / r0.www; //to 1
  
  r0.xyz = pow(r0.xyz, cb3[0].y); //global saturation
  // only use white path in SDR! this is also disabled in original HDR
  if (!HDR_ENABLED) { 
    r1.xyz = float3(1,1,1) + -r0.xyz; //SDR centric
    r1.w = -cb3[0].w * cb3[1].x + r0.w;
    r1.w = max(0, r1.w);
    r1.w = cb3[0].z * r1.w;
    r0.xyz = r1.www * r1.xyz + r0.xyz;
  }

  r0.xyz = r0.xyz * r0.www;

  // clamp
  r0.xyz = max(float3(0,0,0), r0.xyz);
  if (!HDR_ENABLED) r0.xyz = min(cb3[1].xxx, r0.xyz);

  // Modded sRGB Encode
  if (!HDR_ENABLED) {
    r0.xyz = log2(r0.xyz);
    r0.xyz = cb3[1].yyy * r0.xyz; //user settings
    r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r0.xyz;
    r0.xyz = exp2(r0.xyz);
    r1.xyz = exp2(r1.xyz);
    r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
    r2.xyz = cmp(r0.xyz < float3(0.00313080009,0.00313080009,0.00313080009));
    r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
    r0.xyz = r2.xyz ? r0.xyz : r1.xyz;
  }

  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}

// // ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:51 2026
// Buffer<float4> t1 : register(t1);
// 
// Texture2D<float4> t0 : register(t0);
// 
// SamplerState s0_s : register(s0);
// 
// cbuffer cb2 : register(b2)
// {
//   float4 cb2[53];
// }
// 
// cbuffer cb3 : register(b3)
// {
//   float4 cb3[4];
// }
// 
// // 3Dmigoto declarations
// #define cmp -
// #include "./common.hlsl"
// 
// void main(
//   float4 v0 : SV_POSITION0,
//   float4 v1 : TEXCOORD0,
//   float2 v2 : TEXCOORD1,
//   uint v3 : SV_IsFrontFace0,
//   out float4 o0 : SV_Target0)
// {
//   float4 r0,r1,r2;
//   uint4 bitmask, uiDest;
//   float4 fDest;
// 
//   r0.xy = v1.xy / v1.ww;
//   r0.xy = min(cb2[22].zw, r0.xy);
//   r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
// 
//   r1.xyz = (int3)r0.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
//   r1.xyz = cmp((int3)r1.xyz != int3(0x7f800000,0x7f800000,0x7f800000));
//   r0.xyz = r1.xyz ? r0.xyz : float3(65000,65000,65000);
// 
//   r0.xyz = pow(r0.xyz, cb3[0].x); //contrast
//   r0.w = t1.Load(1).x; // inv peak
//   r1.x = t1.Load(2).x; // inv exposure
//   r1.xyz = r0.xyz * r0.www + r1.xxx;
//   r1.xyz = max(float3(9.99999975e-006,9.99999975e-006,9.99999975e-006), r1.xyz);
//   r0.xyz = r0.xyz / r1.xyz;
// 
//   r0.w = max(r0.y, r0.z);
//   r0.w = max(r0.x, r0.w);
//   r0.w = max(9.99999975e-006, r0.w);
//   r0.xyz = r0.xyz / r0.www;
//   r0.xyz = log2(r0.xyz);
//   r0.xyz = cb3[0].yyy * r0.xyz;
//   r0.xyz = exp2(r0.xyz);
//   r1.xyz = float3(1,1,1) + -r0.xyz;
//   r1.w = -cb3[0].w * cb3[1].x + r0.w;
//   r1.w = max(0, r1.w);
//   r1.w = cb3[0].z * r1.w;
//   r0.xyz = r1.www * r1.xyz + r0.xyz;
//   r0.xyz = r0.xyz * r0.www;
// 
//   r0.xyz = max(float3(0,0,0), r0.xyz);
//   r0.xyz = min(cb3[1].xxx, r0.xyz);
//   
//   // sRGB Encode
//   r0.xyz = log2(r0.xyz);
//   r0.xyz = cb3[1].yyy * r0.xyz; //user settings
//   r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r0.xyz;
//   r0.xyz = exp2(r0.xyz);
//   r1.xyz = exp2(r1.xyz);
//   r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
//   r2.xyz = cmp(r0.xyz < float3(0.00313080009,0.00313080009,0.00313080009));
//   r0.xyz = float3(12.9200001,12.9200001,12.9200001) * r0.xyz;
//   r0.xyz = r2.xyz ? r0.xyz : r1.xyz;
//   // o0.xyz = linear_to_sRGB_gamma(r0.xyz);
// 
//   o0.xyz = r0.xyz;
//   o0.w = 1;
//   return;
// }
