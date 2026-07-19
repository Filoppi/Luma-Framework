// ---- Created with 3Dmigoto v1.3.16 on Tue Jul 14 19:39:24 2026
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

// https://www.desmos.com/calculator/stnfdhk9t1
float Rolloff(float x, float invPeak, float invExposure) {return DivideSafe(x, x * invPeak + invExposure, 0);}
float3 Rolloff(float3 x, float invPeak, float invExposure) {return DivideSafe(x, x * invPeak + invExposure, 0);}

float RolloffComplete(float x, float contrast, float invPeak, float invExposure) {
  x = pow(x, contrast);
  return DivideSafe(x, x * invPeak + invExposure, 0);
}
float3 RolloffComplete(float3 x, float contrast, float invPeak, float invExposure) {
  x = pow(x, contrast);
  return DivideSafe(x, x * invPeak + invExposure, 0);
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  uint v3 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  // color
  r0.xy = v1.xy / v1.ww;
  r0.xy = min(cb2[22].zw, r0.xy);
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
 
  // flashbang mask?
  r1.xyz = (int3)r0.xyz & int3(0x7f800000,0x7f800000,0x7f800000);
  r1.xyz = cmp((int3)r1.xyz != int3(0x7f800000, 0x7f800000, 0x7f800000));
  r0.xyz = r1.xyz ? r0.xyz : float3(65000,65000,65000);

//   // pow
//   // r0.xyz = pow(r0.xyz, cb3[0].x);
//   #define POW_MODE 1
//   #if POW_MODE == 0
//     // reduced
//     r0.xyz = pow(r0.xyz, lerp(cb3[0].x , min(1.42, cb3[0].x), 0.8)); 
//   #elif POW_MODE == 1
//     // reduced & low passed
//     {
//       float3 o = r0.xyz;
//       float lg = lerp(cb3[0].x, min(1.42, cb3[0].x), 0.8);
//       float ug = lerp(cb3[0].x, min(1.248, cb3[0].x), 0.8);
//       float3 l = pow(r0.xyz, lg);
//       float3 u = pow(r0.xyz, ug);
//       float s = saturate(GetLuminance(l) * 3);
//       float3 s3 = saturate(l * 3);
//       // r0.xyz = lerp(l, u, 0.8 * s);
//       r0.xyz = PerceptualLerp(l, u, s * 0.8, (lerp(lg, ug, s) + 1) * 0.8); 
//       // r0.xyz = pow(r0.xyz, (1/1.05));
//     }
//   #endif
// 
//   // Reinhard Coeffs
//   r0.w = t1.Load(1).x /* DVS2 */; //inv peak (this can go negative, meaning inv tonemap ahh asymptote for highlights...)
//   r1.x = t1.Load(2).x /* DVS1 */; //inv exposure/contrast
// 
//   // r1.xyz = r0.xyz * r0.www + r1.xxx;
//   // r1.xyz = max(float3(9.99999975e-006,9.99999975e-006,9.99999975e-006), r1.xyz);
//   // r0.xyz = r0.xyz / r1.xyz;
//   #if 0
//     // original improved, unclamped
//     r0.w = max(r0.w, 0.0); //NO INVERSE TONEMAP AHH
//     r0.xyz = Rolloff(r0.xyz, r0.w, r1.x);
//     r0.xyz *= (80.f / 500.f);
//   #else
//     // replacement
//     r0.xyz *= DivideSafe(1, r1.x, 0) * (80.f / 500.f);
//     // r0.xyz *= (Rolloff(0.18, max(r0.w, 0.0), r1.x) / 0.18) * (80.f / 500.f);
//     r0.xyz = Neupow(r0.xyz, HDR_PEAK, 2 * GS.WhiteClip);
//   #endif

//   // pow and Reinhard
//   // r0.w = t1.Load(1).x; //useless
//   r1.x = t1.Load(2).x;
//   r0.xyz = pow(r0.xyz, cb3[0].x);
//   r0.xyz *= DivideSafe(1, r1.x, 0);
//   r0.xyz = pow(r0.xyz, 1/cb3[0].x);
//   {
//     float3 o = r0.xyz;
//     float lg = lerp(cb3[0].x, min(1.42, cb3[0].x), 0.8);
//     float ug = lerp(cb3[0].x, min(1.248, cb3[0].x), 0.8);
//     float3 l = pow(r0.xyz, lg);
//     float3 u = pow(r0.xyz, ug);
//     float s = saturate(GetLuminance(l) * 3);
//     float3 s3 = saturate(l * 3);
//     r0.xyz = PerceptualLerp(l, u, s * 0.8, (lerp(lg, ug, s) + 1) * 0.8); 
//   }
//   r0.xyz *= (80.f / 500.f);
//   r0.xyz = HDRTonemap(r0.xyz, HDR_PEAK);

  // pow and Reinhard
  // r0.w = t1.Load(1).x; //useless
  r1.x = t1.Load(2).x;
  r0.xyz *= DivideSafe(1, pow(r1.x, 0.6), 0);
  r0.xyz = pow(r0.xyz, pow(cb3[0].x, 0.6));
  r0.xyz *= (80.f / 500.f);
  r0.xyz = HDRTonemap(r0.xyz, HDR_PEAK);

  r0.w = max3(r0.x, r0.y, r0.z);
  r0.w = max(9.99999975e-006, r0.w);
  r0.xyz = r0.xyz / r0.www;
  r0.xyz = pow(r0.xyz, cb3[0].y);
  r1.xyz = float3(1,1,1) + -r0.xyz;
  r1.w = -cb3[0].w * cb3[1].x + r0.w;
  r1.w = max(0, r1.w);
  r1.w = cb3[0].z * r1.w;
  r0.xyz = r1.www * r1.xyz + r0.xyz;
  r0.xyz = r0.xyz * r0.www;

  // clean
  r0.xyz = max(r0.xyz, 0);
  r0.xyz = min(r0.xyz, cb3[1].x); //usually 125, idk

  // gamma
  // r0.xyz = r0.xyz / cb3[1].xxx; //normalize 0-cb3[1].x (makes influence even more deadly)
  // r0.xyz = pow(r0.xyz, cb3[1].y);
  // r0.xyz = cb3[1].xxx * r0.xyz;

  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}