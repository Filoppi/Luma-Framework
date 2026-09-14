// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 09 21:34:00 2026

cbuffer CB_AA : register(b0)
{
  float4 g_vScreenSize : packoffset(c0);
  float4 g_vFilterParam : packoffset(c1);
  float4 g_vScreenSizeHalf : packoffset(c2);
  float4x4 g_mCurToPrevScreen : packoffset(c3);
  float4 g_vOrignalVelocityEncode : packoffset(c7);
  float4 g_vFeedBackParam : packoffset(c8);
  float2 g_vSharpenRange : packoffset(c9);
  float g_SharpenIntensity : packoffset(c9.z);
  float g_SubPixelFeedbackFactor : packoffset(c9.w);
  float4 g_FeedbackWeightScale[4] : packoffset(c10);
  float g_PrevRelativeExposure : packoffset(c14);
  float dmy : packoffset(c14.y);
  float2 dmy2 : packoffset(c14.z);
}

SamplerState SS_ClampLinear_s : register(s3);
Texture2D<float4> SourceTex : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  // TODO: RCAS?
  
  r0.xyz = SourceTex.SampleLevel(SS_ClampLinear_s, v1.xy, 0, int2(1, 0)).xyz;
  r0.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r0.xyz;
  r0.xyz = log2(r0.xyz);
  r1.xyz = SourceTex.SampleLevel(SS_ClampLinear_s, v1.xy, 0, int2(0, -1)).xyz;
  r1.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r1.xyz;
  r1.xyz = log2(r1.xyz);
  r2.xyz = SourceTex.SampleLevel(SS_ClampLinear_s, v1.xy, 0, int2(0, 0)).xyz;
  r2.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r2.xyz;
  r2.xyz = log2(r2.xyz);
  r1.xyz = r2.xyz * float3(4,4,4) + -r1.xyz;
  r0.xyz = r1.xyz + -r0.xyz;
  r1.xyz = SourceTex.SampleLevel(SS_ClampLinear_s, v1.xy, 0, int2(-1, 0)).xyz;
  r1.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r1.xyz;
  r1.xyz = log2(r1.xyz);
  r0.xyz = -r1.xyz + r0.xyz;
  r1.xyz = SourceTex.SampleLevel(SS_ClampLinear_s, v1.xy, 0, int2(0, 1)).xyz;
  r1.xyz = float3(9.99999975e-005,9.99999975e-005,9.99999975e-005) + r1.xyz;
  r1.xyz = log2(r1.xyz);
  r0.xyz = -r1.xyz + r0.xyz;
  r0.xyz = max(g_vSharpenRange.xxx, r0.xyz);
  r0.xyz = min(g_vSharpenRange.yyy, r0.xyz);
  r0.xyz = /* DVS1 */g_SharpenIntensity * r0.xyz + r2.xyz; //TODO: user
  r0.xyz = exp2(r0.xyz);
  r0.xyz = float3(-9.99999975e-005,-9.99999975e-005,-9.99999975e-005) + r0.xyz;
  o0.xyz = max(float3(0,0,0), r0.xyz);
  o0.w = 1;
  return;
}