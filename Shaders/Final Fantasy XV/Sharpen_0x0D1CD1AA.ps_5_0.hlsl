#include "Includes/Common.hlsl"
#include "../Includes/RCAS.hlsl"

cbuffer cbSharpen : register(b0)
{
  float4 g_gauss_weights[16] : packoffset(c0);
  float4 g_gauss_offsets[16] : packoffset(c16);
  float g_sharpenAmount : packoffset(c32);
}

cbuffer cbSharpenModify : register(b1)
{
  float g_sharpenAmountMultiplier;
}

SamplerState pointSampler_s : register(s0);
SamplerState linearSampler_s : register(s1);
Texture2D<float4> g_gauss_srcSamplerTexture : register(t0);
Texture2D<float4> g_sharpen_srcSamplerTexture : register(t1);
Texture2D<float2> dummyFloat2Texture : register(t2); // LUMA

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float3 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // r0.xy = g_gauss_offsets[1].xy + v1.xy;
  // r0.xyz = g_gauss_srcSamplerTexture.SampleLevel(linearSampler_s, r0.xy, 0).xyz;
  // r1.xyz = float3(1,1,1) + r0.xyz;
  // r0.xyz = r0.xyz / r1.xyz;
  // r0.xyz = g_gauss_weights[1].xyz * r0.xyz;
  // r1.xy = g_gauss_offsets[0].xy + v1.xy;
  // r1.xyz = g_gauss_srcSamplerTexture.SampleLevel(linearSampler_s, r1.xy, 0).xyz;
  // r2.xyz = float3(1,1,1) + r1.xyz;
  // r1.xyz = r1.xyz / r2.xyz;
  // r0.xyz = r1.xyz * g_gauss_weights[0].xyz + r0.xyz;
  // r1.xy = g_gauss_offsets[2].xy + v1.xy;
  // r1.xyz = g_gauss_srcSamplerTexture.SampleLevel(linearSampler_s, r1.xy, 0).xyz;
  // r2.xyz = float3(1,1,1) + r1.xyz;
  // r1.xyz = r1.xyz / r2.xyz;
  // r0.xyz = r1.xyz * g_gauss_weights[2].xyz + r0.xyz;
  r1.xyz = g_sharpen_srcSamplerTexture.SampleLevel(pointSampler_s, v1.xy, 0).xyz;
  o0.xyz = RCAS(v0.xy, 0, 0x7FFFFFFF, LumaSettings.GameSettings.Sharpness, g_sharpen_srcSamplerTexture, dummyFloat2Texture, 1.0, true, float4(r1.rgb, 1.0)).rgb;

  // r2.xyz = float3(1,1,1) + r1.xyz;
  // r1.xyz = r1.xyz / r2.xyz;
  // r0.xyz = r1.xyz + -r0.xyz;
  // r0.xyz = r0.xyz * g_sharpenAmount * g_sharpenAmountMultiplier + r1.xyz;
  // r2.xyz = float3(1,1,1) + -r0.xyz;
  // r2.xyz = max(float3(9.99999975e-06,9.99999975e-06,9.99999975e-06), r2.xyz);
  // r0.xyz = r0.xyz / r2.xyz;
  // r2.xyz = float3(1,1,1) + -r1.xyz;
  // r2.xyz = max(float3(9.99999975e-06,9.99999975e-06,9.99999975e-06), r2.xyz);
  // r1.xyz = r1.xyz / r2.xyz;
  // r2.xyz = float3(0.899999976,0.899999976,0.899999976) * r1.xyz;
  // r1.xyz = float3(1.10000002,1.10000002,1.10000002) * r1.xyz;
  // r0.xyz = max(r2.xyz, r0.xyz);
  // o0.xyz = min(r0.xyz, r1.xyz);
  return;
}