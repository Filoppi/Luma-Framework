// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:07:03 2026

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
  float4 k_vScreenBufferSize : packoffset(c6) = {0,0,0,0};
  row_major float4x4 k_mPreviousMotionTransform : packoffset(c7);
  float4 k_vMotionBlurParams : packoffset(c11);
  float3 k_vScene_ZRange : packoffset(c12);
}

SamplerState sSourceBufferSampler_s : register(s0);
Texture2D<float4> tSourceBuffer : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = k_vScreenBufferSize.zw * float2(-7.5,-7.5) + w1.xy;
  r0.zw = float2(0,0);
  r1.y = r0.x;
  r2.y = r0.y;
  r1.xz = float2(0,0);
  while (true) {
    r1.w = cmp((int)r1.z >= 16);
    if (r1.w != 0) break;
    r3.xy = r0.zw;
    r3.z = r1.x;
    r2.x = r1.y;
    r1.w = 0;
    while (true) {
      r2.z = cmp((int)r1.w >= 16);
      if (r2.z != 0) break;
      r4.xy = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.xy).xy;
      r2.zw = float2(-0.5,-0.5) + r4.xy;
      r4.z = dot(r2.zw, r2.zw);
      r2.z = cmp(r3.z < r4.z);
      r3.xyz = r2.zzz ? r4.xyz : r3.xyz;
      r2.x = k_vScreenBufferSize.z + r2.x;
      r1.w = (int)r1.w + 1;
    }
    r0.zw = r3.xy;
    r1.x = r3.z;
    r1.y = r2.x;
    r2.y = k_vScreenBufferSize.w + r2.y;
    r1.z = (int)r1.z + 1;
  }
  o0.xy = r0.zw;
  o0.zw = float2(0,0);
  return;
}