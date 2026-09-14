// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:11:38 2026

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
  float4 k_vSourceBufferSize : packoffset(c6) = {0,0,0,0};
}

SamplerState sSourceBufferSampler_s : register(s0);
Texture2D<float4> tSourceBuffer : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = float2(1,1) / k_vSourceBufferSize.xy;
  r0.zw = v1.xy + -r0.xy;
  r1.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r0.zw).xyzw;
  r2.xyzw = r0.xyxy * float4(1,-1,-1,1) + v1.xyxy;
  r0.xy = v1.xy + r0.xy;
  r0.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r0.xy).xyzw;
  r3.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.xy).xyzw;
  r2.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.zw).xyzw;
  r1.xyzw = r3.xyzw + r1.xyzw;
  r1.xyzw = r1.xyzw + r2.xyzw;
  r0.xyzw = r1.xyzw + r0.xyzw;
  o0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
  
  o0 = max(o0, 0);
  o0.w = min(1, o0.w);
  return;
}