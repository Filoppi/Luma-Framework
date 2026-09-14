// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:12:10 2026

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

  float4 k_vSourceBufferSize : packoffset(c5) = {0,0,0,0};
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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = float4(1,1,1,1) / k_vSourceBufferSize.xyxy;
  r1.xyzw = r0.zwzw * float4(-0.548137248,-0.548137248,0.548137248,-0.548137248) + v1.xyxy;
  r0.xyzw = r0.xyzw * float4(-0.548137248,0.548137248,0.548137248,0.548137248) + v1.xyxy;
  r2.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r1.zw).xyzw;
  r1.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r1.xy).xyzw;
  r2.xyzw = float4(0.25,0.25,0.25,0.25) * r2.xyzw;
  r1.xyzw = r1.xyzw * float4(0.25,0.25,0.25,0.25) + r2.xyzw;
  r2.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r0.xy).xyzw;
  r0.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r0.zw).xyzw;
  r1.xyzw = r2.xyzw * float4(0.25,0.25,0.25,0.25) + r1.xyzw;
  o0.xyzw = r0.xyzw * float4(0.25,0.25,0.25,0.25) + r1.xyzw;

  o0 = max(o0, 0);
  o0.w = min(1, o0.w);
  return;
}