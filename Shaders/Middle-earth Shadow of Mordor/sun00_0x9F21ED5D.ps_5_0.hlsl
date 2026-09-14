// ---- Created with 3Dmigoto v1.3.16 on Sun Jul 12 23:54:54 2026

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
  float4 k_vRadialBlurLight : packoffset(c6) = {0,0,0,0};
  float4 k_vRadialBlurParams : packoffset(c7) = {0.5,1,0.899999976,1};
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
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = k_vRadialBlurLight.xy + -v1.xy;
  r0.xy = k_vRadialBlurParams.xx * r0.xy;
  r0.z = dot(r0.xy, r0.xy);
  r0.z = sqrt(r0.z);
  r0.z = saturate(r0.z * 2 + -1);
  r0.z = 1 + -r0.z;
  r0.z = k_vRadialBlurParams.w * r0.z;
  r1.xyz = tSourceScreen.Sample(sSourceScreenSampler_s, v1.xy).xyz;
  r2.xyz = r1.xyz;
  r3.xy = v1.xy;
  r0.w = k_vRadialBlurParams.y;
  r1.w = 0;
  while (true) {
    r2.w = cmp((int)r1.w >= 48);
    if (r2.w != 0) break;
    r3.xy = r0.xy * float2(0.020833334,0.020833334) + r3.xy;
    r4.xyz = tSourceScreen.Sample(sSourceScreenSampler_s, r3.xy).xyz;
    r2.w = k_vRadialBlurParams.z * r0.w;
    r2.xyz = r4.xyz * r0.www + r2.xyz;
    r1.w = (int)r1.w + 1;
    r0.w = r2.w;
  }
  r2.xyz = /* saturate */ max(0, r2.xyz); 
  o0.xyz = r2.xyz * r0.zzz;
  o0.w = 1;
  return;
}