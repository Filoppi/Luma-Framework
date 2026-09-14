// ---- Created with 3Dmigoto v1.3.16 on Sun Jul 12 23:19:42 2026

cbuffer _Globals : register(b0)
{
  float DefaultHeight : packoffset(c0) = {100};
  float DefaultWidth : packoffset(c0.y) = {100};

  struct
  {
    float3 m_Position;
    float2 m_TexCoord;
    float4 m_Color;
  } MaterialVertexDef_Rigid : packoffset(c1);


  struct
  {
    float3 m_Position;
    float2 m_TexCoord;
    float4 m_Color;
    float4 m_Weights;
    float4 m_Indices;
  } MaterialVertexDef_Skeletal : packoffset(c4);

  row_major float4x4 k_mDrawPrimToClip : packoffset(c9);
  float4 k_vBinkConstants : packoffset(c13);
}

SamplerState sDrawPrimVideoBPlaneSampler_s : register(s0);
SamplerState sDrawPrimVideoRPlaneSampler_s : register(s1);
SamplerState sDrawPrimVideoYPlaneSampler_s : register(s2);
Texture2D<float4> tDrawPrimVideoBPlane : register(t0);
Texture2D<float4> tDrawPrimVideoRPlane : register(t1);
Texture2D<float4> tDrawPrimVideoYPlane : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = k_vBinkConstants.xyzw * v1.xyxy;
  r1.y = tDrawPrimVideoRPlane.Sample(sDrawPrimVideoRPlaneSampler_s, r0.zw).x;
  r1.z = tDrawPrimVideoBPlane.Sample(sDrawPrimVideoBPlaneSampler_s, r0.zw).x;
  r1.x = tDrawPrimVideoYPlane.Sample(sDrawPrimVideoYPlaneSampler_s, r0.xy).x;
  r1.w = 1;
  r0.y = dot(float4(1,-0.714139998,-0.344139993,0.531215072), r1.xyzw);
  r0.x = dot(float3(1,1.40199995,-0.703749001), r1.xyw);
  r0.z = dot(float3(1,1.77199996,-0.889474511), r1.xzw);
  o0.xyz = v2.xyz * r0.xyz;
  o0.w = v2.w;

  o0.xyz = min(1, o0.xyz);
  o0.xyz = RenderIntermediatePass(o0.xyz);
  return;
}