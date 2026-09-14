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

cbuffer DownsamplePS : register(b2)
{
  float4 intensity_vector : packoffset(c0);
}

SamplerState LocalSampler_bloom_sampler_s : register(s0);
SamplerState LocalSampler_source_sampler_s : register(s1);
Texture2D<float4> LocalTexture_bloom_sampler : register(t0);
Texture2D<float4> LocalTexture_source_sampler : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

float3 SampleBloom(float2 uv, float4 r0) {
  float4 r2, r3;
  if (r0.x != 0) {
    r3.xy = uv * ps_global_viewport_res_multipliers.xy + r0.zw;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    uv = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, uv).xyz;
  return r3.xyz;
}

float3 SampleBloom1(float2 uv, float4 r0) {
  float4 r2, r3;
  if (r0.x != 0) {
    r3.xy = uv * ps_global_viewport_res_multipliers.xy + r0.zw;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    uv = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyz = LocalTexture_bloom_sampler.Sample(LocalSampler_bloom_sampler_s, uv).xyz;
  return r3.xyz;
}

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;
  const float o = 1;

  r0.xy = ps_global_is_texture_in_viewport_flags & int2(1,2);
  r0.zw = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;

  // TODO: Luma insert downsampling gaussian blur
  r1.xy = -ps_postprocess_pixel_size.xy + v1.xy;
  if (r0.y != 0) {
    r1.zw = r1.xy * ps_global_viewport_res_multipliers.xy + r0.zw;
    r1.zw = max(ps_global_viewport_bounds_uv.xy, r1.zw);
    r1.xy = min(ps_global_viewport_bounds_uv.zw, r1.zw);
  }
  r1.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r1.xy).xyz;
  r1.xyz = float3(9.99999994e-009,9.99999994e-009,9.99999994e-009) + r1.xyz;
  r2.xyzw = ps_postprocess_pixel_size.xyxy * float4(1,-1,-1,1) + v1.xyxy;
  if (r0.y != 0) {
    r3.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.zw;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyz;
  r1.xyz = r3.xyz + r1.xyz;
  if (r0.y != 0) {
    r2.xy = r2.zw * ps_global_viewport_res_multipliers.xy + r0.zw;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r2.zw = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  }
  r2.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.zw).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r2.xy = ps_postprocess_pixel_size.xy + v1.xy;
  if (r0.y != 0) {
    r2.zw = r2.xy * ps_global_viewport_res_multipliers.xy + r0.zw;
    r2.zw = max(ps_global_viewport_bounds_uv.xy, r2.zw);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r2.zw);
  }
  r2.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r2.xy).xyz;
  r1.xyz = r2.xyz + r1.xyz;
  r1.xyz = float3(0.25,0.25,0.25) * r1.xyz;

  // r1.xyz = 0;
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, 0) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -1) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  1) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, 0) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  1, 0) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  1, o *  1) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, o * -1) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  1, o * -1) + v1.xy, r0);
  // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, o *  1) + v1.xy, r0);
  // r1.xyz *= 1.f / (1 + 2 + 2 + 2 + 2);

  // clamp
  if (!HDR_ENABLED) r1.xyz = saturate(r1.xyz);
  else r1.xyz = Neutwo(r1.xyz, 2);/* anchoredCInfinityShoulder(r1.xyz, 2, 1, 1); */

  r2.w = dot(r1.xyz, intensity_vector.xyz);
  r0.y = ps_postprocess_scale.y * r2.w;
  r1.w = -ps_postprocess_scale.x + r2.w;
  r0.y = max(r1.w, r0.y);
  r0.y = r0.y / r2.w;
  r2.xyz = r1.xyz * r0.yyy;
    r2.xyz = max(0, r2.xyz);

  float4 r0Back = r0;
  r1.xy = float2(0.5,0.5) + v0.xy;
  r1.xy = ps_postprocess_pixel_size.xy * r1.xy;
  if (r0.x != 0) {
    r0.xy = r1.xy * ps_global_viewport_res_multipliers.xy + r0.zw;
    r0.xy = max(ps_global_viewport_bounds_uv.xy, r0.xy);
    r1.xy = min(ps_global_viewport_bounds_uv.zw, r0.xy);
  }
  r0.xyzw = LocalTexture_bloom_sampler.Sample(LocalSampler_bloom_sampler_s, r1.xy).xyzw;

  // r0.xyz += SampleBloom1(ps_postprocess_pixel_size.xy * (0.5 * float2(o * -3, 0) + v0.xy), r0Back);
  // r0.xyz += SampleBloom1(ps_postprocess_pixel_size.xy * (0.5 * float2(o * -2, 0) + v0.xy), r0Back);
  // r0.xyz += SampleBloom1(ps_postprocess_pixel_size.xy * (0.5 * float2(o * -1, 0) + v0.xy), r0Back);
  // r0.xyz += SampleBloom1(ps_postprocess_pixel_size.xy * (0.5 * float2(o * -1, 0) + v0.xy), r0Back);
  // r0.xyz += SampleBloom1(ps_postprocess_pixel_size.xy * (0.5 * float2(o *  2, 0) + v0.xy), r0Back);
  // r0.xyz += SampleBloom1(ps_postprocess_pixel_size.xy * (0.5 * float2(o *  3, 0) + v0.xy), r0Back);
  // r0.xyz *= 1.f / (1 + 6);
  // // r0.xyz = sqrt(r0.xyz);
  // // r0.xyz *= r0.xyz;

  o0.xyzw = max(r2.xyzw, r0.xyzw);
  // o0.xyzw = r0.xyzw;
  // o0.xyzw = r2.xyzw;

  // o0.xyz = (r2.xyz + r0.xyz) / 2;
  // o0.w = max(r2.w, r0.w);
  return;
}