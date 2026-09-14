// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:05:18 2026

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
}

SamplerState sBloomResult0Sampler_s : register(s0);
SamplerState sBloomResult1Sampler_s : register(s1);
SamplerState sBloomResult2Sampler_s : register(s2);
Texture2D<float4> tBloomResult0 : register(t0);
Texture2D<float4> tBloomResult1 : register(t1);
Texture2D<float4> tBloomResult2 : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = tBloomResult1.Sample(sBloomResult1Sampler_s, v1.xy).xyzw;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = k_vHDRBloomParams.xxx * r0.xyz;
  r1.xyzw = tBloomResult0.Sample(sBloomResult0Sampler_s, v1.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r0.xyz = r1.xyz * k_vHDRBloomParams.xxx + r0.xyz;
  r1.xyzw = tBloomResult2.Sample(sBloomResult2Sampler_s, v1.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r0.xyz = r1.xyz * k_vHDRBloomParams.xxx + r0.xyz;
  r0.xyz = float3(0.333333343,0.333333343,0.333333343) * r0.xyz;
  r0.w = max(r0.x, r0.y);
  r0.w = max(r0.w, r0.z);
  r0.w = k_vHDRBloomParams.y * r0.w;
  r0.w = 255 * r0.w;
  r0.w = ceil(r0.w);
  r0.w = max(1, r0.w);
  r0.w = 0.00392156886 * r0.w;
  r1.x = k_vHDRBloomParams.x * r0.w;
  o0.w = r0.w;
  o0.xyz = r0.xyz / r1.xxx;

  o0 = max(o0, 0);
  o0.w = min(1, o0.w);
  return;
}