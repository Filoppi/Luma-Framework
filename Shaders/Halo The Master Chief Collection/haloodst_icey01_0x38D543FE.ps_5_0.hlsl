// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:29 2026

cbuffer GlobalPS : register(b0)
{
  float ps_global_mip_bias : packoffset(c0);
  float3 ps_global_mip_bias_pad : packoffset(c0.y);
  float2 ps_global_viewport_res : packoffset(c1);
  float2 ps_global_viewport_res_pad : packoffset(c1.z);
  float2 ps_global_viewport_top_left_pixel : packoffset(c2);
  float2 ps_global_viewport_top_left_pixel_pad : packoffset(c2.z);
  float2 ps_global_viewport_res_multipliers : packoffset(c3);
  float2 ps_global_viewport_res_multipliers_pad : packoffset(c3.z);
  float4 ps_global_viewport_bounds_uv : packoffset(c4);
  float2 ps_global_render_resolution : packoffset(c5);
  float2 ps_global_render_resolution_pad : packoffset(c5.z);
  float2 ps_global_render_pixel_size : packoffset(c6);
  float2 ps_global_render_pixel_size_pad : packoffset(c6.z);
  uint ps_global_is_texture_in_viewport_flags : packoffset(c7);
}

cbuffer ParametersPS : register(b13)
{
  float4 ___albedo_color : packoffset(c0);
  float4 ___albedo_color2 : packoffset(c1);
  float4 ___albedo_color3 : packoffset(c2);
  float4 ___base_map_xform : packoffset(c3);
  float4 ___detail_map_xform : packoffset(c4);
  float4 ___color_mask_map_xform : packoffset(c5);
  float4 ___neutral_gray : packoffset(c6);
  float4 ___bump_map_xform : packoffset(c7);
  float4 ___bump_detail_map_xform : packoffset(c8);
}

SamplerState UserParameterSampler_base_map_s : register(s0);
SamplerState UserParameterSampler_detail_map_s : register(s1);
SamplerState UserParameterSampler_color_mask_map_s : register(s2);
SamplerState UserParameterSampler_bump_map_s : register(s3);
SamplerState UserParameterSampler_bump_detail_map_s : register(s4);
Texture2D<float4> UserParameterTexture_base_map : register(t0);
Texture2D<float4> UserParameterTexture_detail_map : register(t1);
Texture2D<float4> UserParameterTexture_color_mask_map : register(t2);
Texture2D<float4> UserParameterTexture_bump_map : register(t3);
Texture2D<float4> UserParameterTexture_bump_detail_map : register(t4);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float v1 : SV_ClipDistance0,
  float4 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD3,
  float3 v6 : TEXCOORD4,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v2.xy * ___color_mask_map_xform.xy + ___color_mask_map_xform.zw;
  r0.xyz = UserParameterTexture_color_mask_map.SampleBias(UserParameterSampler_color_mask_map_s, r0.xy, ps_global_mip_bias).xyz;
  r1.xyzw = ___albedo_color.xyzw * r0.xxxx;
  r2.xyz = ___neutral_gray.xyz;
  r2.w = 1;
  r1.xyzw = r1.xyzw / r2.xyzw;
  r3.xyz = float3(1,1,1) + -r0.xyz;
  r1.xyzw = r3.xxxx + r1.xyzw;
  r4.xyzw = ___albedo_color2.xyzw * r0.yyyy;
  r0.xyzw = ___albedo_color3.xyzw * r0.zzzz;
  r0.xyzw = r0.xyzw / r2.xyzw;
  r2.xyzw = r4.xyzw / r2.xyzw;
  r2.xyzw = r3.yyyy + r2.xyzw;
  r0.xyzw = r3.zzzz + r0.xyzw;
  r1.xyzw = r2.xyzw * r1.xyzw;
  r0.xyzw = r1.xyzw * r0.xyzw;
  r1.xy = v2.xy * ___base_map_xform.xy + ___base_map_xform.zw;
  r1.xyzw = UserParameterTexture_base_map.SampleBias(UserParameterSampler_base_map_s, r1.xy, ps_global_mip_bias).xyzw;
  r2.xy = v2.xy * ___detail_map_xform.xy + ___detail_map_xform.zw;
  r2.xyzw = UserParameterTexture_detail_map.SampleBias(UserParameterSampler_detail_map_s, r2.xy, ps_global_mip_bias).xyzw;
  r1.xyzw = r2.xyzw * r1.xyzw;
  r0.xyzw = r1.xyzw * r0.xyzw;
  r0.xyzw = float4(4.59478998,4.59478998,4.59478998,1) * r0.xyzw;
  o0.xyzw = r0.xyzw;
  o1.w = r0.w;
  r0.xy = v2.xy * ___bump_map_xform.xy + ___bump_map_xform.zw;
  r0.xy = UserParameterTexture_bump_map.SampleBias(UserParameterSampler_bump_map_s, r0.xy, ps_global_mip_bias).xy;
  r0.zw = r0.xy * r0.xy;
  r0.z = r0.z + r0.w;
  r0.z = min(1, r0.z);
  r0.z = 1 + -r0.z;
  r1.z = sqrt(r0.z);
  r0.zw = v2.xy * ___bump_detail_map_xform.xy + ___bump_detail_map_xform.zw;
  r0.zw = UserParameterTexture_bump_detail_map.SampleBias(UserParameterSampler_bump_detail_map_s, r0.zw, ps_global_mip_bias).xy;
  r1.xy = r0.xy + r0.zw;
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx;
  r1.xyz = v4.xyz * r0.yyy;
  r0.xyw = r0.xxx * v5.xyz + r1.xyz;
  r0.xyz = r0.zzz * v3.xyz + r0.xyw;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.xyz * r0.www;
  o1.xyz = r0.xyz * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);

  o0 = saturate(o0);
  o1 = saturate(o1);
  return;
}