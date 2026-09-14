// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:10 2026

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

cbuffer DisplacementPS : register(b1)
{
  float4 ps_displacement_screen_constants : packoffset(c0);
  float4x4 ps_displacement_previous_view_projection : packoffset(c1);
  float4x4 ps_displacement_screen_to_world : packoffset(c5);
}

cbuffer DisplacementMotionBlurPS : register(b2)
{
  int ps_displacement_num_taps : packoffset(c0);
  int3 ps_displacement_num_taps_pad : packoffset(c0.y);
  float4 ps_displacement_misc_values : packoffset(c1);
  float4 ps_displacement_blur_max_and_scale : packoffset(c2);
  float4 ps_displacement_crosshair_center : packoffset(c3);
  bool ps_displacement_do_distortion : packoffset(c4);
}

SamplerState LocalSampler_displacement_sampler_s : register(s0);
SamplerState LocalSampler_ldr_buffer_s : register(s1);
Texture2D<float4> LocalTexture_displacement_sampler : register(t0);
Texture2D<float4> LocalTexture_ldr_buffer : register(t1);
Texture2D<float4> LocalTexture_distortion_depth_buffer : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"


void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;
  r0.zw = -ps_displacement_crosshair_center.xy + v1.zw;
  r0.z = dot(r0.zw, r0.zw);
  r0.z = ps_displacement_misc_values.w * r0.z;
  r0.z = min(1, r0.z);
  if (ps_displacement_do_distortion != 0) {
    r1.xy = ps_displacement_screen_constants.zw + ps_displacement_screen_constants.zw;
    r1.zw = LocalTexture_displacement_sampler.Sample(LocalSampler_displacement_sampler_s, v1.xy).xy;
    r1.zw = float2(-0.501960814,-0.501960814) + r1.zw;
    r1.xy = r1.xy * r1.zw;
    r1.xy = r1.xy * float2(0.5,0.5) + v1.xy;
  } else {
    r1.xy = v1.xy;
  }
  r0.w = 1 / ps_displacement_misc_values.x;
  r1.zw = r1.xy * ps_global_viewport_res_multipliers.xy + r0.xy;
  r1.zw = max(ps_global_viewport_bounds_uv.xy, r1.zw);
  r1.zw = min(ps_global_viewport_bounds_uv.zw, r1.zw);
  r2.xyzw = LocalTexture_ldr_buffer.Sample(LocalSampler_ldr_buffer_s, r1.zw).xyzw;
  r1.z = 1 + -r2.w;
  r0.z = r1.z * r0.z;
  r1.z = cmp(0.00999999978 < r0.z);
  if (r1.z != 0) {
    r3.xy = (int2)v0.xy;
    r3.zw = float2(0,0);
    r1.z = LocalTexture_distortion_depth_buffer.Load(r3.xyz).x;
    r3.xyzw = ps_displacement_screen_to_world._m01_m11_m21_m31 * v1.wwww;
    r3.xyzw = v1.zzzz * ps_displacement_screen_to_world._m00_m10_m20_m30 + r3.xyzw;
    r3.xyzw = r1.zzzz * ps_displacement_screen_to_world._m02_m12_m22_m32 + r3.xyzw;
    r3.xyzw = ps_displacement_screen_to_world._m03_m13_m23_m33 + r3.xyzw;
    r4.xyz = ps_displacement_previous_view_projection._m01_m11_m31 * r3.yyy;
    r4.xyz = r3.xxx * ps_displacement_previous_view_projection._m00_m10_m30 + r4.xyz;
    r3.xyz = r3.zzz * ps_displacement_previous_view_projection._m02_m12_m32 + r4.xyz;
    r3.xyz = r3.www * ps_displacement_previous_view_projection._m03_m13_m33 + r3.xyz;
    r1.zw = r3.xy / r3.zz;
    r1.zw = v1.zw + -r1.zw;
    r1.zw = ps_displacement_blur_max_and_scale.zw * r1.zw;
    r1.zw = min(ps_displacement_blur_max_and_scale.xy, r1.zw);
    r1.zw = max(-ps_displacement_blur_max_and_scale.xy, r1.zw);
    r4.xy = r1.xy;
    r2.w = 0;
    r3.xyzw = float4(0,0,0,0);
    while (true) {
      r4.z = cmp((int)r3.w >= ps_displacement_num_taps);
      if (r4.z != 0) break;
      r4.xy = r0.ww * r1.zw * GS.MotionBlur + r4.xy;
      r4.zw = r4.xy * ps_global_viewport_res_multipliers.xy + r0.xy;
      r4.zw = max(ps_global_viewport_bounds_uv.xy, r4.zw);
      r4.zw = min(ps_global_viewport_bounds_uv.zw, r4.zw);
      r5.xyzw = LocalTexture_ldr_buffer.SampleLevel(LocalSampler_ldr_buffer_s, r4.zw, 0).xyzw;
      r4.z = 1 + -r5.w;
      r3.xyz = r5.xyz * r4.zzz + r3.xyz;
      r2.w = r4.z + r2.w;
      r3.w = (int)r3.w + 1;
    }
  } else {
    r3.xyz = float3(0,0,0);
    r2.w = 0;
  }
  r0.x = cmp(0 < r2.w);
  r1.xyz = r3.xyz / r2.www;
  r1.xyz = r0.xxx ? r1.xyz : r3.xyz;
  r0.x = r2.w * r0.z;
  r0.x = r0.x * r0.w;
  r0.yzw = r1.xyz + -r2.xyz;
  o0.xyz = r0.xxx * r0.yzw + r2.xyz;
  o0.w = 0;
  return;
}