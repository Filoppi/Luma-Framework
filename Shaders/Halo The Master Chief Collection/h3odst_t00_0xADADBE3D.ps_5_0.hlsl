// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:47 2026

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

cbuffer PostProcessPS : register(b1)
{
  float4 ps_postprocess_pixel_size : packoffset(c0);
  float4 ps_postprocess_scale : packoffset(c1);
  float4x3 ps_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 ps_postprocess_contrast : packoffset(c5);
}

cbuffer FinalCompositePS : register(b2)
{
  float4 intensity : packoffset(c0);
  float4 tone_curve_constants : packoffset(c1);
  float4 player_window_constants : packoffset(c2);
  float4 screenspace_sampler_xform : packoffset(c3);
  float4 health_constants : packoffset(c4);
  float4 health_mult_base : packoffset(c5);
  float4 health_mult_scale : packoffset(c6);
  float4 health_add_base : packoffset(c7);
  float4 health_add_scale : packoffset(c8);
  float4 shield_constants : packoffset(c9);
  float4 shield_mult_base : packoffset(c10);
  float4 shield_mult_scale : packoffset(c11);
  float4 shield_add_base : packoffset(c12);
  float4 shield_add_scale : packoffset(c13);
}

SamplerState LocalSampler_surface_sampler_s : register(s0);
SamplerState LocalSampler_bloom_sampler_s : register(s2);
SamplerState LocalSampler_shield_sampler_s : register(s7);
Texture2D<float4> LocalTexture_surface_sampler : register(t0);
Texture2D<float4> LocalTexture_bloom_sampler : register(t2);
Texture2D<float4> LocalTexture_shield_sampler : register(t7);


// 3Dmigoto declarations
#define cmp -
#include "./h3odst_t.hlsl"

//https://github.com/halohlsl/Halo3ODST-Shader-Source/blob/master/hlsl/final_composite_base.hlsl
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = ps_global_is_texture_in_viewport_flags & int3(1,4,128);
  r1.xy = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;
  if (r0.x != 0) {
    r0.xw = v1.xy * ps_global_viewport_res_multipliers.xy + r1.xy;
    r0.xw = max(ps_global_viewport_bounds_uv.xy, r0.xw);
    r0.xw = min(ps_global_viewport_bounds_uv.zw, r0.xw);
  } else {
    r0.xw = v1.xy;
  }
  r2.xyz = LocalTexture_surface_sampler.Sample(LocalSampler_surface_sampler_s, r0.xw).xyz;
  r2.xyz = ColorInBlowout(r2.xyz);

  if (r0.y != 0) {
    r0.xy = v1.xy * ps_global_viewport_res_multipliers.xy + r1.xy;
    r0.xy = max(ps_global_viewport_bounds_uv.xy, r0.xy);
    r0.xy = min(ps_global_viewport_bounds_uv.zw, r0.xy);
  } else {
    r0.xy = v1.xy;
  }
  r0.xyw = LocalTexture_bloom_sampler.Sample(LocalSampler_bloom_sampler_s, r0.xy).xyz * GS.Bloom;
  r0.xyw = r2.xyz + r0.xyw;
  r1.zw = v1.xy * shield_constants.zw + float2(0.5,0.5);
  r1.zw = -shield_constants.zw * float2(0.5,0.5) + r1.zw;
  if (r0.z != 0) {
    r1.xy = r1.zw * ps_global_viewport_res_multipliers.xy + r1.xy;
    r1.xy = max(ps_global_viewport_bounds_uv.xy, r1.xy);
    r1.zw = min(ps_global_viewport_bounds_uv.zw, r1.xy);
  }
  r0.z = LocalTexture_shield_sampler.Sample(LocalSampler_shield_sampler_s, r1.zw).w;
  r0.z = saturate(-shield_constants.x + r0.z);
  r1.xyz = shield_mult_scale.xyz * r0.zzz + shield_mult_base.xyz;
  r2.xyz = shield_add_scale.xyz * r0.zzz + shield_add_base.xyz;
  r0.xyz = r0.xyw * r1.xyz + r2.xyz;

  r0.w = 1;
  r1.x = dot(r0.xyzw, ps_postprocess_hue_saturation_matrix._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, ps_postprocess_hue_saturation_matrix._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, ps_postprocess_hue_saturation_matrix._m02_m12_m22_m32);

  r0.x = dot(r1.xyz, 0.333000004);
  r0.y = cmp(0 < r0.x);
  // r0.z = pow(r0.x, ps_postprocess_contrast.x);
  r0.z = ContrastPower(r1.xyz, r0.x, ps_postprocess_contrast.x);
  r0.x = r0.z / r0.x;
  r0.xzw = r1.xyz * r0.xxx;
  r0.xyz = r0.yyy ? r0.xzw : r1.xyz;


  SetColor(r0.xyz);

  // r0.xyz = min(tone_curve_constants.xxx, r0.xyz);
  // r1.xyz = r0.xyz * tone_curve_constants.www + tone_curve_constants.zzz;
  // r1.xyz = r1.xyz * r0.xyz + tone_curve_constants.yyy;
  // r0.xyz = r1.xyz * r0.xyz;

  Rolloff(tone_curve_constants);
  
  r0.xyz = tmi.x;
  r0.w = dot(r0.xyz, float3(0.298999995,0.587000012,0.114)); // luma for aa
  o0.w = sqrt(r0.w);

  GammaOut();
  o0.xyz = tmi.x;
  return;
}