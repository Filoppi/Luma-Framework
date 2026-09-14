// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:08:53 2026

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
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = k_vScreenBufferSize.zw * float2(-16,-16) + w1.xy;
  r0.zw = float2(0,0);
  r1.y = r0.x;
  r2.y = r0.y;
  r1.xz = float2(0,0);
  while (true) {
    r1.w = cmp((int)r1.z >= 3);
    if (r1.w != 0) break;
    r2.zw = cmp((int2)r1.zz == int2(0,2));
    r1.w = (int)r1.z;
    r3.y = -1 + r1.w;
    r1.w = (int)r2.w | (int)r2.z;
    r4.xy = r0.zw;
    r4.z = r1.x;
    r2.x = r1.y;
    r2.z = 0;
    while (true) {
      r2.w = cmp((int)r2.z >= 3);
      if (r2.w != 0) break;
      r5.xy = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.xy).xy;
      r3.zw = float2(-0.5,-0.5) + r5.xy;
      r5.z = dot(r3.zw, r3.zw);
      r6.xy = cmp((int2)r2.zz == int2(0,2));
      r2.w = (int)r6.y | (int)r6.x;
      r2.w = r2.w ? r1.w : 0;
      r4.w = (int)r2.z;
      r3.x = -1 + r4.w;
      r4.w = cmp(r4.z < r5.z);
      r3.x = dot(r3.zw, r3.xy);
      r3.x = cmp(r3.x < 0);
      r3.x = r3.x ? r4.w : 0;
      r3.xzw = r3.xxx ? r5.xyz : r4.xyz;
      r5.xyz = r4.www ? r5.xyz : r4.xyz;
      r4.xyz = r2.www ? r3.xzw : r5.xyz;
      r2.x = k_vScreenBufferSize.z * 16 + r2.x;
      r2.z = (int)r2.z + 1;
    }
    r0.zw = r4.xy;
    r1.x = r4.z;
    r1.y = r2.x;
    r2.y = k_vScreenBufferSize.w * 16 + r2.y;
    r1.z = (int)r1.z + 1;
  }
  o0.xy = r0.zw;
  o0.zw = float2(0,0);
  return;
}