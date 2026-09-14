// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 09 21:32:51 2026

cbuffer cbcol : register(b0)
{
  float4 ConstColor : packoffset(c0);
}

cbuffer cbParam : register(b1)
{
  bool g_bEnableGamma : packoffset(c0);
}

SamplerState SS_WrapLinear_s : register(s0);
Texture2D<float4> g_Tex : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = g_Tex.Sample(SS_WrapLinear_s, v2.xy).xyzw;

  // SDR path only gamma encode (prob brightness slider too)
  // r1.xyz = log2(r0.xyz);
  // r1.xyz = float3(0.454545468,0.454545468,0.454545468) * r1.xyz;
  // r1.xyz = exp2(r1.xyz);
    r1.xyz = pow(r0.xyz, 1.0 / 2.2);
    
  r0.xyz = g_bEnableGamma ? r1.xyz : r0.xyz;
  r1.xyzw = ConstColor.xyzw * v1.xyzw;
  
  o0.xyzw = r1.xyzw * r0.xyzw;
  return;
}