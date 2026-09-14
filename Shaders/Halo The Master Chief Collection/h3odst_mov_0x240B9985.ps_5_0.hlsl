// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:27 2026

cbuffer YUVToRGBPS : register(b0)
{
  float4 consta : packoffset(c0);
  float4 crc : packoffset(c1);
  float4 cbc : packoffset(c2);
  float4 adj : packoffset(c3);
  float4 yscale : packoffset(c4);
}

SamplerState samp0_s : register(s0);
SamplerState samp1_s : register(s1);
SamplerState samp2_s : register(s2);
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);
Texture2D<float4> tex2 : register(t2);

#include "./Includes/Common.hlsl"

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = tex2.Sample(samp2_s, v1.xy).x;
  r0.xyz = cbc.xyz * r0.xxx;
  r0.w = tex1.Sample(samp1_s, v1.xy).x;
  r0.xyz = crc.xyz * r0.www + r0.xyz;
  r0.xyz = adj.xyz + r0.xyz;
  r0.w = tex0.Sample(samp0_s, v1.xy).x;
  r0.xyz = r0.www * yscale.xyz + r0.xyz;
  r0.w = 1;
  r0.xyzw = consta.xyzw * r0.xyzw;
  r0 = max(0, r0);
  r0.xyzw = log2(r0.xyzw);
  r0.xyzw = float4(2.20000005,2.20000005,2.20000005,1) * r0.xyzw;
  o0.xyzw = exp2(r0.xyzw);

  o0.w = min(1, o0.w);
  o0.xyz *= HDR_INTSCALING;
  return;
}