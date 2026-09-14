  // ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 18:08:43 2026

cbuffer EngineViewPS : register(b0)
{
  float4x4 ps_camera_to_world_matrix : packoffset(c0);
  float3x3 ps_worldspace_normal_axis : packoffset(c4);
  float ps_worldspace_normal_axis_pad : packoffset(c6.w);
  float4 ps_view_exposure : packoffset(c7);
  float4 ps_view_self_illum_exposure : packoffset(c8);
  float4 ps_shadow_direction : packoffset(c9);
  float4 ps_analytical_light_direction : packoffset(c10);
  float4 ps_constant_shadow_alpha : packoffset(c11);
  float4 ps_tiling_vpos_offset : packoffset(c12);
  float4 ps_tiling_resolvetexture_xform : packoffset(c13);
  float4 ps_tiling_reserved1 : packoffset(c14);
  float4 ps_tiling_reserved2 : packoffset(c15);
  float4 ps_debug_ambient_intensity : packoffset(c16);
}

cbuffer PostProcessPS : register(b1)
{
  float4 ps_pixel_size : packoffset(c0);
  float4 ps_scale : packoffset(c1);
  float4 ps_intensity : packoffset(c2);
  float4x3 ps_hue_saturation_matrix : packoffset(c3);
  float4 ps_contrast : packoffset(c6);
}

cbuffer FinalCompositeSharedPS : register(b2)
{
  float4 ps_player_window_constants : packoffset(c0);
  float4 ps_depth_constants[2] : packoffset(c1);
}

cbuffer FinalCompositeFunctionsPS : register(b3)
{
  float4 ps_color_grading_scale_offset[2] : packoffset(c0);
  float4 ps_color_grading_half_texel_offset[2] : packoffset(c2);
  float4 ps_filmic_tone_curve_params[5] : packoffset(c4);
  bool ps_apply_color_matrix : packoffset(c9);
  bool ps_apply_contrast : packoffset(c9.y);
}

SamplerState ps_surface_sampler_sampler_s : register(s0);
SamplerState ps_bloom_sampler_sampler_s : register(s2);
SamplerState ps_depth_sampler_sampler_s : register(s3);
SamplerState ps_blur_sampler_sampler_s : register(s4);
SamplerState ps_color_grading_0Sampler_s : register(s8);
Texture2D<float4> ps_surface_sampler_texture : register(t0);
Texture2D<float4> ps_bloom_sampler_texture : register(t2);
Texture2D<float4> ps_depth_sampler_texture : register(t3);
Texture2D<float4> ps_blur_sampler_texture : register(t4);
Texture3D<float4> ps_color_grading_0Texture : register(t8);


// 3Dmigoto declarations
#define cmp -
#include "./h4_t.hlsl"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = ps_depth_sampler_texture.Sample(ps_depth_sampler_sampler_s, v1.xy).x;
  r0.x = r0.x * ps_depth_constants[0].y + ps_depth_constants[0].x;
  r0.x = 1 / r0.x;
  r0.xy = ps_depth_constants[1].xz * r0.xx + ps_depth_constants[1].yw;
  r0.xy = max(float2(0,0), r0.xy);
  r0.xy = min(ps_depth_constants[0].zw, r0.xy);
  r0.x = dot(r0.xy, r0.xy);
  r0.yzw = ps_blur_sampler_texture.Sample(ps_blur_sampler_sampler_s, v1.xy).xyz;
  r1.xyz = ps_surface_sampler_texture.Sample(ps_surface_sampler_sampler_s, v1.xy).xyz;
  r1.xyz = max(0, r1.xyz); //safe
  r1.xyz = ps_view_exposure.yyy * r1.xyz;
  r0.yzw = r0.yzw * ps_view_exposure.yyy + -r1.xyz;
  r0.xyz = r0.xxx * r0.yzw + r1.xyz;
  r1.xyz = ps_bloom_sampler_texture.Sample(ps_bloom_sampler_sampler_s, v1.xy).xyz * GS.Bloom;
  r1.xyz = max(0, r1.xyz); //safe
  r0.xyz = r1.xyz + r0.xyz;
  r0.w = 1;
  r1.x = dot(r0.xyzw, ps_hue_saturation_matrix._m00_m10_m20_m30);
  r1.y = dot(r0.xyzw, ps_hue_saturation_matrix._m01_m11_m21_m31);
  r1.z = dot(r0.xyzw, ps_hue_saturation_matrix._m02_m12_m22_m32);
  r0.xyz = ps_apply_color_matrix ? r1.xyz : r0.xyz;
  r0.w = dot(r0.xyz, float3(0.333000004,0.333000004,0.333000004));
  r0.w = log2(r0.w);
  r0.w = ps_contrast.w * r0.w;
  r0.w = exp2(r0.w);
  r1.xyz = r0.xyz * r0.www;

  TexTuple lut = {ps_color_grading_0Texture, ps_color_grading_0Sampler_s};
  SetColor(r0.xyz);
  CalcPeakDeltaFromLut(lut, ps_color_grading_scale_offset, ps_color_grading_half_texel_offset);

  // r0.xyz = ps_apply_color_matrix ? r1.xyz : r0.xyz;
  // r1.xyz = r0.xyz * ps_filmic_tone_curve_params[0].xyz + ps_filmic_tone_curve_params[1].xyz;
  // r1.xyz = r1.xyz * r0.xyz;
  // r2.xyz = r0.xyz * ps_filmic_tone_curve_params[2].xyz + ps_filmic_tone_curve_params[3].xyz;
  // r0.xyz = r0.xyz * r2.xyz + ps_filmic_tone_curve_params[4].xyz;
  // r0.xyz = r1.xyz / r0.xyz;
  Rolloff(ps_filmic_tone_curve_params);

  // r0.xyz = sqrt(r0.xyz);
  // r0.xyz = r0.xyz * ps_color_grading_scale_offset[0].xyz + ps_color_grading_half_texel_offset[0].xyz;
  // o0.xyzw = ps_color_grading_0Texture.SampleLevel(ps_color_grading_0Sampler_s, r0.xyz, 0).xyzw;
  LUTGamma(lut, ps_color_grading_scale_offset, ps_color_grading_half_texel_offset);
  r0.xyz = tmi.x;
  r0.w = tmi.w;

  o0.xyzw = r0.xyzw;
  return;
}