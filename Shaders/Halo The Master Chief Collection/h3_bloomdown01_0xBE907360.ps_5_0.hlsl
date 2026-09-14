// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:35 2026

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

SamplerState LocalSampler_source_sampler_s : register(s0);
Texture2D<float4> LocalTexture_source_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = ps_global_is_texture_in_viewport_flags & 1;
  r0.yz = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;
  r1.xyzw = ps_postprocess_pixel_size.xyxy * float4(-2,-2,0,-2) + v1.xyxy;
  if (r0.x != 0) {
    r2.xy = r1.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r1.xy = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  }
  r2.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r1.xy).xyzw; /* r2.xyz = sRGB_Encode(r2.xyz); */
  r2.xyzw = r2.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + float4(9.99999994e-009,9.99999994e-009,9.99999994e-009,9.99999994e-009);
  if (r0.x != 0) {
    r1.xy = r1.zw * ps_global_viewport_res_multipliers.xy + r0.yz;
    r1.xy = max(ps_global_viewport_bounds_uv.xy, r1.xy);
    r1.zw = min(ps_global_viewport_bounds_uv.zw, r1.xy);
  }
  r1.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r1.zw).xyzw; /* r1.xyz = sRGB_Encode(r1.xyz); */
  r1.xyzw = r1.xyzw * float4(0.125,0.125,0.125,0.125) + r2.xyzw;
  r2.xyzw = ps_postprocess_pixel_size.xyxy * float4(2,-2,-2,0) + v1.xyxy;
  if (r0.x != 0) {
    r3.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw; /* r3.xyz = sRGB_Encode(r3.xyz); */
  r1.xyzw = r3.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + r1.xyzw;
  if (r0.x != 0) {
    r2.xy = r2.zw * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r2.zw = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  }
  r2.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.zw).xyzw; /* r2.xyz = sRGB_Decode(r2.xyz); */
  r1.xyzw = r2.xyzw * float4(0.125,0.125,0.125,0.125) + r1.xyzw;
  if (r0.x != 0) {
    r2.xy = v1.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  } else {
    r2.xy = v1.xy;
  }
  r2.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw; /* r2.xyz = sRGB_Encode(r2.xyz); */
  r1.xyzw = r2.xyzw * float4(0.25,0.25,0.25,0.25) + r1.xyzw;
  r2.xyzw = ps_postprocess_pixel_size.xyxy * float4(2,0,-2,2) + v1.xyxy;
  if (r0.x != 0) {
    r3.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw; /* r3.xyz = sRGB_Encode(r3.xyz); */
  r1.xyzw = r3.xyzw * float4(0.125,0.125,0.125,0.125) + r1.xyzw;
  if (r0.x != 0) {
    r2.xy = r2.zw * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r2.zw = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  }
  r2.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.zw).xyzw; /* r2.xyz = sRGB_Encode(r2.xyz); */
  r1.xyzw = r2.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + r1.xyzw;
  r2.xy = ps_postprocess_pixel_size.xy * float2(0,2) + v1.xy;
  if (r0.x != 0) {
    r2.zw = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.zw = max(ps_global_viewport_bounds_uv.xy, r2.zw);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r2.zw);
  }
  r2.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw; /* r2.xyz = sRGB_Encode(r2.xyz); */
  r1.xyzw = r2.xyzw * float4(0.125,0.125,0.125,0.125) + r1.xyzw;
  r2.xy = ps_postprocess_pixel_size.xy * float2(2,2) + v1.xy;
  if (r0.x != 0) {
    r0.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r0.xy = max(ps_global_viewport_bounds_uv.xy, r0.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r0.xy);
  }
  r0.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw; /* r0.xyz = sRGB_Encode(r0.xyz); */
  // o0.xyz = sRGB_Encode(o0.xyz);
  // o0.xyz = pow(o0.xyz, 2.6);
  // o0.xyz = sRGB_Decode(o0.xyz);
  r0.xyzw = r0.xyzw * float4(0.0625,0.0625,0.0625,0.0625) + r1.xyzw;

  if (IsGame_Halo3()) r0.xyz *= 1.146; // bloom only

  // Halo3: w (luma) is used by autoexposure
  o0 = max(r0, 0); 
  return;
}