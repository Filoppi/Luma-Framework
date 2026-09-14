// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 15:06:02 2026

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
SamplerState LocalSampler_weight_sampler_s : register(s1);
Texture2D<float4> LocalTexture_source_sampler : register(t0);
Texture2D<float4> LocalTexture_weight_sampler : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = ps_global_is_texture_in_viewport_flags & int2(1,2);
  r0.zw = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;
  r1.z = 1;
  r2.xyz = float3(0,0,0);
  r1.w = -3;

  while (true) {
    r2.w = cmp(3 < (int)r1.w);
    if (r2.w != 0) break;
    r3.x = (int)r1.w;
    r4.xyz = r2.xyz;
    r2.w = -7;
    while (true) {
      r3.z = cmp(7 < (int)r2.w);
      if (r3.z != 0) break;
      r3.y = (int)r2.w;
      r3.yz = r3.xy * ps_postprocess_pixel_size.xy + v1.xy;
      if (r0.y != 0) {
        r5.xy = r3.yz * ps_global_viewport_res_multipliers.xy + r0.zw;
        r5.xy = max(ps_global_viewport_bounds_uv.xy, r5.xy);
        r5.xy = min(ps_global_viewport_bounds_uv.zw, r5.xy);
      } else {
        r5.xy = r3.yz;
      }
      r3.w = LocalTexture_weight_sampler.Sample(LocalSampler_weight_sampler_s, r5.xy).y;
      if (r0.x != 0) {
        r5.xy = r3.yz * ps_global_viewport_res_multipliers.xy + r0.zw;
        r5.xy = max(ps_global_viewport_bounds_uv.xy, r5.xy);
        r3.yz = min(ps_global_viewport_bounds_uv.zw, r5.xy);
      }
      r1.y = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r3.yz).w; /* r1.y = Neutwo(r1.y, 1); */
      r3.y = 9.99999975e-006 + r1.y;
      r1.x = log2(r3.y);
      r4.xyz = r3.www * r1.xyz + r4.xyz;
      r2.w = (int)r2.w + 2;
    }
    r2.xyz = r4.xyz;
    r1.w = (int)r1.w + 2;
  }

  r0.xy = r2.xy / r2.zz;
  r0.y = 9.99999975e-006 + r0.y;
  r0.y = log2(r0.y);
  r0.z = 1 + -ps_postprocess_scale.x;
  r0.x = r0.x * r0.z;
  o0.xyzw = r0.yyyy * ps_postprocess_scale.xxxx + r0.xxxx;
  return;
}