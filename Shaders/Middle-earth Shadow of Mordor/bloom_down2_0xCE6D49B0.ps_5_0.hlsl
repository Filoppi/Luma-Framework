// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:04:43 2026

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
  float4 k_vHDRBloomParams : packoffset(c7);
}

SamplerState sSourceScreenSampler_s : register(s0);
Texture2D<float4> tSourceScreen : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float2 v8 : TEXCOORD7,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v1.zw).xyzw;
  r0.xyz = r0.xyz * r0.www;
  r0.xyz = k_vHDRBloomParams.xxx * r0.xyz;
  r0.xyz = float3(0.0270778369,0.0270778369,0.0270778369) * r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v1.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0159283932,0.0159283932,0.0159283932) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v2.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0424231887,0.0424231887,0.0424231887) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v2.zw).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0612547919,0.0612547919,0.0612547919) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v3.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0815124959,0.0815124959,0.0815124959) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v3.zw).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0999667868,0.0999667868,0.0999667868) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v4.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.112988606,0.112988606,0.112988606) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v4.zw).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.117695794,0.117695794,0.117695794) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v5.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.112988606,0.112988606,0.112988606) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v5.zw).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0999667868,0.0999667868,0.0999667868) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v6.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0815124959,0.0815124959,0.0815124959) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v6.zw).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0612547919,0.0612547919,0.0612547919) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v7.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0424231887,0.0424231887,0.0424231887) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v7.zw).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0270778369,0.0270778369,0.0270778369) + r0.xyz;
  r1.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v8.xy).xyzw;
  r1.xyz = r1.xyz * r1.www;
  r1.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r0.xyz = r1.xyz * float3(0.0159283932,0.0159283932,0.0159283932) + r0.xyz;
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