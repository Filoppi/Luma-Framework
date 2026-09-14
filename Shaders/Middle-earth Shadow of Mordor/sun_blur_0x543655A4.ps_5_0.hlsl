// ---- Created with 3Dmigoto v1.3.16 on Sun Jul 12 23:56:43 2026

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

SamplerState sSourceScreenSampler_s : register(s0);
Texture2D<float4> tSourceScreen : register(t0);


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

  r0.xyzw = float4(0,-2.24766374,0,-0.615602672) / k_vSourceBufferSize.xyxy;
  r0.xyzw = v1.xyxy + r0.xyzw;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, r0.zw).xyzw;
  r0.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, r0.xy).xyzw;
  r1.xyzw = float4(0.352086395,0.352086395,0.352086395,0.352086395) * r1.xyzw;
  r0.xyzw = r0.xyzw * float4(0.147913605,0.147913605,0.147913605,0.147913605) + r1.xyzw;
  r1.xyzw = float4(0,0.615602672,0,2.24766374) / k_vSourceBufferSize.xyxy;
  r1.xyzw = v1.xyxy + r1.xyzw;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, r1.xy).xyzw;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, r1.zw).xyzw;
  r0.xyzw = r2.xyzw * float4(0.352086395,0.352086395,0.352086395,0.352086395) + r0.xyzw;
  o0.xyzw = r1.xyzw * float4(0.147913605,0.147913605,0.147913605,0.147913605) + r0.xyzw;
  
  o0 = max(0, o0);
  o0.w = min(1, o0.w);
  return;
}