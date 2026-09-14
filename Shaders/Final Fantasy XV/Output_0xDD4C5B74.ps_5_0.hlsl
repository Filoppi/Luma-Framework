// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 02 01:21:35 2025
#include "Includes/Common.hlsl"


cbuffer _Globals : register(b0)
{
  float gamma : packoffset(c0);
  float pqScale : packoffset(c0.y);
}

SamplerState samplerSrc0_s : register(s0);
Texture2D<float4> samplerSrc0Texture : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = samplerSrc0Texture.Sample(samplerSrc0_s, v1.xy).xyzw;
  // r0.xyz = max(float3(0,0,0), r0.xyz);
  o0.w = r0.w;
  // r1.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r0.xyz;
  // r1.xyz = float3(0.947867334,0.947867334,0.947867334) * r1.xyz;
  // r1.xyz = log2(r1.xyz);
  // r1.xyz = float3(2.4000001,2.4000001,2.4000001) * r1.xyz;
  // r1.xyz = exp2(r1.xyz);
  // r2.xyz = cmp(float3(0.0392800011,0.0392800011,0.0392800011) >= r0.xyz);
  // r0.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r0.xyz;
  // r0.xyz = r2.xyz ? r0.xyz : r1.xyz;
  r0.xyz = gamma_sRGB_to_linear(r0.xyz, GCT_MIRROR);
  float3 color = r0.xyz;

  // r0.xyz = log2(r0.xyz);
  // r0.xyz = gamma * r0.xyz;
  // r0.xyz = exp2(r0.xyz);

  // gamma correction instead of custom gamma
  // r0.xyz = GammaCorrection(r0.xyz, 2.4);
  
  // color expansion
  // r0.w = 0.587700009 * r0.y;
  // r0.w = r0.x * 1.66050005 + -r0.w;
  // r1.x = -r0.z * 0.072800003 + r0.w;
  // r0.w = 0.100599997 * r0.y;
  // r0.w = r0.x * -0.0182000007 + -r0.w;
  // r1.z = r0.z * 1.11870003 + r0.w;
  // r0.x = dot(r0.xy, float2(-0.124600001,1.13300002));
  // r1.y = -r0.z * 0.0083999997 + r0.x;

  // r1.rgb = BT709_To_BT2020(r1.rgb);
  // r1.rgb = max(0.f, r1.rgb);
  // r1.rgb = BT2020_To_BT709(r1.rgb);

  // r1.rgb = max(r1.rgb, 0.f)

  // o0.xyz = pqScale * r1.xyz;

  // scale with game nits

  // o0.rgb = r1.rgb * 203.f / 80.f;
  // o0.rgb = r1.rgb;
  o0.rgb = BT2020_To_BT709(color);

  return;
}