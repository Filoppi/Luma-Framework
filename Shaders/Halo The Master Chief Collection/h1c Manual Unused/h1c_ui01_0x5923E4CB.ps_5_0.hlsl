// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 16:25:29 2026

cbuffer _cb0 : register(b0)
{
  float4 gradient_min_color : packoffset(c0);
  float4 gradient_max_color : packoffset(c1);
  float4 flash_color : packoffset(c2);
  float4 background_color : packoffset(c3);
  float4 tint_color : packoffset(c4);
  float4 alpha_min : packoffset(c5);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS1_s : register(s1);
Texture2D<float4> Texture0 : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = Texture0.Sample(TexS1_s, v3.xy).zw;
  r0.xy = float2(-0.00100000005,0.5) + r0.yx;
  r0.x = cmp(r0.x < 0);
  r0.y = saturate(-flash_color.w + r0.y);
  r0.y = cmp(r0.y >= 0.5);
  if (r0.x != 0) discard;
  r0.xz = Texture0.Sample(TexS0_s, v3.xy).zw;
  r0.w = gradient_max_color.w * r0.x;
  r0.w = saturate(8 * r0.w);
  r1.x = 1 + -r0.w;
  r1.yzw = gradient_max_color.xyz * r0.www;
  r1.xyz = saturate(r1.xxx * gradient_min_color.xyz + r1.yzw);
  r0.x = gradient_min_color.w + -r0.x;
  r0.x = saturate(4 * r0.x);
  r0.x = saturate(-r0.x * 2 + 1);
  r1.xyz = saturate(r0.xxx * flash_color.xyz + r1.xyz);
  r1.xyz = r0.yyy ? background_color.xyz : r1.xyz;
  r0.x = r0.y ? background_color.w : tint_color.w;
  r1.xyz = r1.xyz * r0.zzz;
  r0.y = saturate(alpha_min.w + -r0.z);
  o0.xyz = tint_color.xyz * r1.xyz;

  r0.z = 1 + -r0.x;
  r0.y = r0.z * r0.y;
  r0.y = r0.y / alpha_min.w;
  o0.w = r0.x + r0.y;

  o0.xyz = UIScaling(o0.xyz);
  return;
}