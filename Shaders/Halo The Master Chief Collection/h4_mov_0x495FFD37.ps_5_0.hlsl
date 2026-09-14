// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:39:48 2026

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
SamplerState samp3_s : register(s3);
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);
Texture2D<float4> tex2 : register(t2);
Texture2D<float4> tex3 : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = tex0.Sample(samp0_s, v1.xy).x;
  r0.y = tex1.Sample(samp1_s, v1.zw).x;
  r0.z = tex2.Sample(samp2_s, v1.zw).x;
  r1.xyz = cbc.xyz * r0.zzz;
  r0.yzw = crc.xyz * r0.yyy + r1.xyz;
  r0.yzw = adj.xyz + r0.yzw;
  r0.xyz = r0.xxx * yscale.xyz + r0.yzw;
  r1.x = cmp(0 < consta.z);
  if (r1.x != 0) {
    r0.w = tex3.Sample(samp3_s, v1.xy).x;
  } else {
    r0.w = 1;
  }
  o0.xyzw = consta.xyyw * r0.xyzw;
  o0.xyz = RenderIntermediatePass(o0.xyz);
  return;
}