// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:23 2026

cbuffer ParametersPS : register(b13)
{
  float4 ___warp_map_xform : packoffset(c0);
  float ___warp_amount : packoffset(c1);
  float4 ___base_map_xform : packoffset(c2);
  float4 ___detail_map_a_xform : packoffset(c3);
  float4 ___detail_mask_a_xform : packoffset(c4);
  float ___detail_fade_a : packoffset(c5);
  float ___detail_multiplier_a : packoffset(c6);
}

cbuffer ScreenPS : register(b0)
{
  float4 screenspace_xform : packoffset(c0);
}

SamplerState UserParameterSampler_warp_map_s : register(s0);
SamplerState UserParameterSampler_base_map_s : register(s1);
SamplerState UserParameterSampler_detail_map_a_s : register(s2);
SamplerState UserParameterSampler_detail_mask_a_s : register(s3);
Texture2D<float4> UserParameterTexture_warp_map : register(t0);
Texture2D<float4> UserParameterTexture_base_map : register(t1);
Texture2D<float4> UserParameterTexture_detail_map_a : register(t2);
Texture2D<float4> UserParameterTexture_detail_mask_a : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v1.xy * ___warp_map_xform.xy + ___warp_map_xform.zw;
  r0.xy = UserParameterTexture_warp_map.Sample(UserParameterSampler_warp_map_s, r0.xy).xy;
  r0.zw = r0.xy * ___warp_amount + v1.xy;
  r0.xy = ___warp_amount * r0.xy;
  r0.xy = r0.xy / screenspace_xform.xy;
  r0.xy = v1.zw + r0.xy;
  r0.xy = r0.xy * ___base_map_xform.xy + ___base_map_xform.zw;
  r1.xyzw = UserParameterTexture_base_map.Sample(UserParameterSampler_base_map_s, r0.xy).xyzw; //color

  r0.xy = r0.zw * ___detail_map_a_xform.xy + ___detail_map_a_xform.zw;
  r0.zw = r0.zw * ___detail_mask_a_xform.xy + ___detail_mask_a_xform.zw;
  r0.z = UserParameterTexture_detail_mask_a.Sample(UserParameterSampler_detail_mask_a_s, r0.zw).w * GS.FilmGrain; 
  r0.z = saturate(___detail_fade_a * r0.z);

  r2.xyzw = UserParameterTexture_detail_map_a.Sample(UserParameterSampler_detail_map_a_s, r0.xy).xyzw;
  r2.xyz = ___detail_multiplier_a * r2.xyz;
  r2.xyzw = float4(-1,-1,-1,-1) + r2.xyzw;
  r0.xyzw = r0.zzzz * r2.xyzw + float4(1, 1, 1, 1);
  // r0.xyz = sRGB_Encode(r0.xyz);

  o0.xyzw = r1.xyzw * r0.xyzw;
  return;
}