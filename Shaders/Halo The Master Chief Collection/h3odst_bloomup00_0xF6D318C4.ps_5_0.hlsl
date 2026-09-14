// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:45 2026

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

SamplerState LocalSampler_original_sampler_s : register(s0);
SamplerState LocalSampler_add_sampler_s : register(s1);
Texture2D<float4> LocalTexture_original_sampler : register(t0);
Texture2D<float4> LocalTexture_add_sampler : register(t1);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = ps_global_is_texture_in_viewport_flags & int2(1,2);
  r0.zw = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;
  if (r0.x != 0) {
    r1.xy = v1.xy * ps_global_viewport_res_multipliers.xy + r0.zw;
    r1.xy = max(ps_global_viewport_bounds_uv.xy, r1.xy);
    r1.xy = min(ps_global_viewport_bounds_uv.zw, r1.xy);
  } else {
    r1.xy = v1.xy;
  }
  r1.xyz = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, r1.xy).xyz;
  if (r0.y != 0) {
    r0.xy = v1.xy * ps_global_viewport_res_multipliers.xy + r0.zw;
    r0.xy = max(ps_global_viewport_bounds_uv.xy, r0.xy);
    r0.xy = min(ps_global_viewport_bounds_uv.zw, r0.xy);
  } else {
    r0.xy = v1.xy;
  }
  r0.xyzw = LocalTexture_add_sampler.Sample(LocalSampler_add_sampler_s, r0.xy).xyzw;
  r1.xyz = ps_postprocess_scale.xyz * r1.xyz;
  o0.xyz = r1.xyz * r0.www + r0.xyz;
  o0.w = r0.w;
  return;
}