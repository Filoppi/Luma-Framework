// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:23 2026

cbuffer ParametersPS : register(b13)
{
  float4 ___albedo_color : packoffset(c0);
  float4 ___base_map_xform : packoffset(c1);
  float4 ___detail_map_xform : packoffset(c2);
  float ___self_illum_intensity : packoffset(c3);
  float4 ___self_illum_map_xform : packoffset(c4);
  float4 ___self_illum_color : packoffset(c5);
  float3 ___edge_fade_center_tint : packoffset(c6);
  float3 ___edge_fade_edge_tint : packoffset(c7);
  float ___edge_fade_power : packoffset(c8);
}

cbuffer ExposurePS : register(b0)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer MiscPS : register(b1)
{
  float2 texture_size : packoffset(c0);
  float2 texture_size_pad : packoffset(c0.z);
  float4 dynamic_environment_blend : packoffset(c1);
  float4 p_render_debug_mode : packoffset(c2);
  bool actually_calc_albedo : packoffset(c3);
  bool p_lightmap_compress_constant_using_dxt : packoffset(c3.y);
}

SamplerState UserParameterSampler_self_illum_map_s : register(s2);
Texture2D<float4> UserParameterTexture_self_illum_map : register(t2);
Texture2D<float4> normal_texture : register(t17);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float v1 : SV_ClipDistance0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  float4 v6 : TEXCOORD6,
  float4 v7 : TEXCOORD7,
  float3 v8 : COLOR0,
  float3 v9 : COLOR1,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = dot(v6.xyz, v6.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = v6.xyz * r0.xxx;
  if (actually_calc_albedo != 0) {
    r1.xyz = v3.xyz;
  } else {
    r2.xy = (int2)v0.xy;
    r2.zw = float2(0,0);
    r2.xyz = normal_texture.Load(r2.xyz).xyz;
    r1.xyz = r2.xyz * float3(2,2,2) + float3(-1,-1,-1);
  }
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = sqrt(r0.w);
  r1.xyz = r1.xyz / r0.www;
  r0.x = dot(r0.xyz, r1.xyz);
  r0.yz = v2.xy * ___self_illum_map_xform.xy + ___self_illum_map_xform.zw;
  r0.yzw = UserParameterTexture_self_illum_map.Sample(UserParameterSampler_self_illum_map_s, r0.yz).xyz;
  r0.yzw = ___self_illum_color.xyz * r0.yzw;
  r0.yzw = ___self_illum_intensity * r0.yzw;
  r0.yzw = g_alt_exposure.xxx * r0.yzw;
  r0.x = log2(abs(r0.x));
  r0.x = ___edge_fade_power * r0.x;
  r0.x = exp2(r0.x);
  r1.xyz = -___edge_fade_edge_tint.xyz + ___edge_fade_center_tint.xyz;
  r1.xyz = r0.xxx * r1.xyz + ___edge_fade_edge_tint.xyz;
  r0.xyz = r1.xyz * r0.yzw;
  r0.xyz = v8.xyz * r0.xyz;
  r0.xyz = g_exposure.xxx * r0.xyz;
  r0.xyz = max(float3(0,0,0), r0.xyz);
  o1.xyz = r0.xyz / g_exposure.yyy;
  o1.xyz = Neutwo(o1.xyz, 1.26);
  o0.xyz = r0.xyz;
  o0.w = 0;
  o1.w = 0;
  return;
}