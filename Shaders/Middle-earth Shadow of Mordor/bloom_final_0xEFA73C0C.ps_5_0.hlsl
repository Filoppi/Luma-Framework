// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:05:59 2026

cbuffer _Globals : register(b0)
{
  float DefaultHeight : packoffset(c0) = {100};
  float DefaultWidth : packoffset(c0.y) = {100};

  struct
  {
    float2 m_Position;
  } MaterialVertexDef_Rigid : packoffset(c1);


  struct
  {
    float2 m_Position;
    float4 m_Weights;
    float4 m_Indices;
  } MaterialVertexDef_Skeletal : packoffset(c2);

  bool bHalfPrecision : packoffset(c5) = false;
  bool bUsePS3CompilerArgs : packoffset(c5.y) = true;
  float4 k_vHDRBloomParams : packoffset(c6);
  float4 k_vHDRImageWeight : packoffset(c7);
}

SamplerState sBloomResultSampler_s : register(s0);
SamplerState sRadialBlurResultSampler_s : register(s1);
Texture2D<float4> tBloomResult : register(t0);
Texture2D<float4> tRadialBlurResult : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = tBloomResult.Sample(sBloomResultSampler_s, v1.xy).xyzw;
  r0.xyz = r0.xyz /* * SI.bloom */ * r0.www;
  r0.xyz = k_vHDRBloomParams.xxx * r0.xyz;
  r1.xyz = k_vHDRImageWeight.www * r0.xyz;
  r2.xyz = tRadialBlurResult.Sample(sRadialBlurResultSampler_s, v1.xy).xyz;
  r1.xyz = max(r2.xyz, r1.xyz);
  o0.xyz = r0.xyz * k_vHDRImageWeight.yyy + r1.xyz;
  o0.w = k_vHDRImageWeight.x;

  o0 = max(o0, 0);
  o0.w = min(1, o0.w);
  return;
}