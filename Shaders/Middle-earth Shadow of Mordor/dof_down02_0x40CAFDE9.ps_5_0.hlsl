// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:12:55 2026

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
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = 1 / k_vSourceBufferSize.x;
  r1.xyzw = r0.xxxx * float4(-2.44915605,-6.3703742,-0.657535493,-4.40918207) + v1.xxxx;
  r0.xyzw = r0.xxxx * float4(4.40918207,0.657535493,6.3703742,2.44915605) + v1.xxxx;
  r2.xz = r1.yw;
  r2.yw = v1.yy;
  r3.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.zw).xyzw;
  r2.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.xy).xyzw;
  r3.xyzw = float4(0.103677981,0.103677981,0.103677981,0.103677981) * r3.xyzw;
  r2.xyzw = r2.xyzw * float4(0.0430062301,0.0430062301,0.0430062301,0.0430062301) + r3.xyzw;
  r1.yw = v1.yy;
  r3.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r1.xy).xyzw;
  r1.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r1.zw).xyzw;
  r2.xyzw = r3.xyzw * float4(0.18147929,0.18147929,0.18147929,0.18147929) + r2.xyzw;
  r1.xyzw = r1.xyzw * float4(0.17183651,0.17183651,0.17183651,0.17183651) + r2.xyzw;
  r2.xz = r0.yw;
  r2.yw = v1.yy;
  r3.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.xy).xyzw;
  r2.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r2.zw).xyzw;
  r1.xyzw = r3.xyzw * float4(0.17183651,0.17183651,0.17183651,0.17183651) + r1.xyzw;
  r1.xyzw = r2.xyzw * float4(0.18147929,0.18147929,0.18147929,0.18147929) + r1.xyzw;
  r0.yw = v1.yy;
  r2.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r0.xy).xyzw;
  r0.xyzw = tSourceBuffer.Sample(sSourceBufferSampler_s, r0.zw).xyzw;
  r1.xyzw = r2.xyzw * float4(0.103677981,0.103677981,0.103677981,0.103677981) + r1.xyzw;
  o0.xyzw = r0.xyzw * float4(0.0430062301,0.0430062301,0.0430062301,0.0430062301) + r1.xyzw;

  o0 = max(o0, 0);
  o0.w = min(1, o0.w);
  return;
}