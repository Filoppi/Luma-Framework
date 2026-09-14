// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:27 2026

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
  r1.xy = -ps_postprocess_pixel_size.xy + v1.xy;
  if (r0.x != 0) {
    r1.zw = r1.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r1.zw = max(ps_global_viewport_bounds_uv.xy, r1.zw);
    r1.xy = min(ps_global_viewport_bounds_uv.zw, r1.zw);
  }
  r1.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r1.xy).xyzw; 
  // r1.xyzw = saturate(r1.xyzw);
  r1 = max(r1, 0);

  r2.xyzw = ps_postprocess_pixel_size.xyxy * float4(1,-1,-1,1) + v1.xyxy;
  if (r0.x != 0) {
    r3.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw;
  // r3.xyzw = saturate(r3.xyzw);
  r3 = max(r3, 0);
  
  r1.xyzw = r3.xyzw + r1.xyzw;
  if (r0.x != 0) {
    r2.xy = r2.zw * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r2.zw = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  }
  r2.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.zw).xyzw;
  // r2.xyzw = saturate(r2.xyzw);
  r2 = max(r2, 0);

  r1.xyzw = r2.xyzw + r1.xyzw;
  r2.xy = ps_postprocess_pixel_size.xy + v1.xy;
  if (r0.x != 0) {
    r0.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r0.xy = max(ps_global_viewport_bounds_uv.xy, r0.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r0.xy);
  }
  r0.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyzw;
  // r0.xyzw = saturate(r0.xyzw);
  r0 = max(r0, 0);

  r0.xyzw = r1.xyzw + r0.xyzw;
  r0.xyzw = 0.25 * r0.xyzw;

  if (IsGame_Halo3()) { //TODO: detect if before tonemap, which is when this is useful
    float y = GetLuminance(r0.xyz);
    const float p = 2;
    #if HALO3_BLOOM == 0
      r0.xyz *= safeDivision(Neutwo(r0.xyz, p), y);
      // r0.xyz = saturate(r0.xyz);
    #else
      r0.xyz = Neutwo(r0.xyz, p);
    #endif
    r0.w = Neutwo(r0.w, p);
  }
  r0 = max(0, r0);

  o0 = r0;
  return;
}