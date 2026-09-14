// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 01:02:03 2026

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

void S(inout float4 x) {
  // x = max(0, x);
  // x.w = min(1, x.w);
}
void S1(inout float4 x) {
  x = max(0, x);
  x.w = min(1, x.w);
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float4 v4 : TEXCOORD3,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD6,
  float4 v8 : TEXCOORD7,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v1.zw).xyzw; S(r0);
  r0.w = saturate(-k_vHDRBloomParams.z + r0.w);
  r1.xyz = r0.xyz * r0.www;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v1.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v2.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v2.zw).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v3.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r3.xyz = r2.xyz * r0.www;
  r0.xyz = r2.xyz * float3(2,2,2) + r0.xyz;
  r1.xyz = r3.xyz * float3(2,2,2) + r1.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v4.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v4.zw).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v5.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v5.zw).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v6.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v6.zw).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v7.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v7.zw).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v8.xy).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r2.xyzw = tSourceScreen.Sample(sSourceScreenSampler_s, v8.zw).xyzw; S(r2);
  r0.w = saturate(-k_vHDRBloomParams.z + r2.w);
  r1.xyz = r2.xyz * r0.www + r1.xyz;
  r0.xyz = r2.xyz + r0.xyz;
  r0.xyz = float3(0.0625,0.0625,0.0625) * r0.xyz;
  r1.xyz = float3(0.0625,0.0625,0.0625) * r1.xyz;
  r2.xyz = k_vHDRBloomParams.xxx * r1.xyz;
  r1.x = dot(r1.xyz, float3(0.300000012,0.589999974,0.109999999));
  r1.x = cmp(0 < r1.x);
  r1.y = max(r2.x, r2.y);
  r1.y = max(r1.y, r2.z);
  r1.y = k_vHDRBloomParams.y * r1.y;
  r1.y = 255 * r1.y;
  r1.y = ceil(r1.y);
  r1.y = max(1, r1.y);
  r3.w = 0.00392156886 * r1.y;
  r1.y = k_vHDRBloomParams.x * r3.w;
  r3.xyz = r2.xyz / r1.yyy;
  r0.w = 0;
  o0.xyzw = r1.xxxx ? r3.xyzw : r0.xyzw;
  
  S1(o0);
  return;
}