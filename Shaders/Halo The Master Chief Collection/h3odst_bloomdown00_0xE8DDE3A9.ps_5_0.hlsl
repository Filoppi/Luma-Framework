// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:56 2026

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

cbuffer ExposurePS : register(b1)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer PostProcessPS : register(b2)
{
  float4 ps_postprocess_pixel_size : packoffset(c0);
  float4 ps_postprocess_scale : packoffset(c1);
  float4x3 ps_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 ps_postprocess_contrast : packoffset(c5);
}

cbuffer DownsamplePS : register(b3)
{
  float4 intensity_vector : packoffset(c0);
}

SamplerState LocalSampler_dark_source_sampler_s : register(s0);
Texture2D<float4> LocalTexture_dark_source_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

float3 SampleBloom(float2 uv, float4 r0) {
  float4 r2, r3;
  if (r0.x != 0) {
    r3.xy = uv * ps_global_viewport_res_multipliers.xy + r0.yz;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    uv = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyz = LocalTexture_dark_source_sampler.Sample(LocalSampler_dark_source_sampler_s, uv).xyz;
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

  r0.x = ps_global_is_texture_in_viewport_flags & 1;
  r0.yz = ps_global_render_pixel_size.xy * ps_global_viewport_top_left_pixel.xy;

  r1.xy = -ps_postprocess_pixel_size.xy + v1.xy;
  if (r0.x != 0) {
    r1.zw = r1.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r1.zw = max(ps_global_viewport_bounds_uv.xy, r1.zw);
    r1.xy = min(ps_global_viewport_bounds_uv.zw, r1.zw);
  }
  r1.xyz = LocalTexture_dark_source_sampler.Sample(LocalSampler_dark_source_sampler_s, r1.xy).xyz;
  float3 b0 = r1.xyz;
  r1.xyz = float3(9.99999994e-009,9.99999994e-009,9.99999994e-009) + r1.xyz;

  r2.xyzw = ps_postprocess_pixel_size.xyxy * float4(1, -1, -1, 1) + v1.xyxy;
  if (r0.x != 0) {
    r3.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r3.xy = max(ps_global_viewport_bounds_uv.xy, r3.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r3.xy);
  }
  r3.xyz = LocalTexture_dark_source_sampler.Sample(LocalSampler_dark_source_sampler_s, r2.xy).xyz;
  float3 b1 = r3.xyz;
  r1.xyz = r3.xyz + r1.xyz;

  if (r0.x != 0) {
    r2.xy = r2.zw * ps_global_viewport_res_multipliers.xy + r0.yz;
    r2.xy = max(ps_global_viewport_bounds_uv.xy, r2.xy);
    r2.zw = min(ps_global_viewport_bounds_uv.zw, r2.xy);
  }
  r2.xyz = LocalTexture_dark_source_sampler.Sample(LocalSampler_dark_source_sampler_s, r2.zw).xyz;
  float3 b2 = r2.xyz;
  r1.xyz = r2.xyz + r1.xyz;

  r2.xy = ps_postprocess_pixel_size.xy + v1.xy;
  if (r0.x != 0) {
    r0.xy = r2.xy * ps_global_viewport_res_multipliers.xy + r0.yz;
    r0.xy = max(ps_global_viewport_bounds_uv.xy, r0.xy);
    r2.xy = min(ps_global_viewport_bounds_uv.zw, r0.xy);
  }
  r0.xyz = LocalTexture_dark_source_sampler.Sample(LocalSampler_dark_source_sampler_s, r2.xy).xyz;
  float3 b3 = r0.xyz;
  r0.xyz = r1.xyz + r0.xyz;

  r0.w = 1.00294113 * r0.x;
  r1.xyzw = cmp(r0.xxxy < float4(0.250244379,0.500488758,1.00097752,0.250244379));
  r2.xyz = r0.xxx * float3(0.501470566,0.250735283,0.125367641) + float3(0.125490203,0.250980407,0.501960814);
  r0.x = r1.z ? r2.y : r2.z;
  r0.x = r1.y ? r2.x : r0.x;
  r0.x = r1.x ? r0.w : r0.x;
  r1.x = r0.x * r0.x;

  r0.x = 1.00294113 * r0.y;
  r2.xyzw = cmp(r0.yyzz < float4(0.500488758,1.00097752,0.250244379,0.500488758));
  r3.xyz = r0.yyy * float3(0.501470566,0.250735283,0.125367641) + float3(0.125490203,0.250980407,0.501960814);
  r0.y = r2.y ? r3.y : r3.z;
  r0.y = r2.x ? r3.x : r0.y;
  r0.x = r1.w ? r0.x : r0.y;
  r1.y = r0.x * r0.x;

  r0.x = 1.00294113 * r0.z;
  r0.y = cmp(r0.z < 1.00097752);
  r3.xyz = r0.zzz * float3(0.501470566,0.250735283,0.125367641) + float3(0.125490203,0.250980407,0.501960814);
  r0.y = r0.y ? r3.y : r3.z;
  r0.y = r2.w ? r3.x : r0.y;
  r0.x = r2.z ? r0.x : r0.y;
  r1.z = r0.x * r0.x;

//   const float o = 2;
//   r1.xyz = 0;
// 
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, 0) + v1.xy, r0);
// 
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -3) + v1.xy, r0);
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -2) + v1.xy, r0);
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -1) + v1.xy, r0);
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  1) + v1.xy, r0);
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  2) + v1.xy, r0);
//   r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  3) + v1.xy, r0);
// 
//   // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -3, 0) + v1.xy, r0);
//   // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -2, 0) + v1.xy, r0);
//   // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, 0) + v1.xy, r0);
//   // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, 0) + v1.xy, r0);
//   // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  2, 0) + v1.xy, r0);
//   // r1.xyz += SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  3, 0) + v1.xy, r0);
//   
//   r1.xyz *= 4.f / (1 + 6 /* + 6 */);
//   r1.xyz *= r1.xyz;
// 
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, 0) + v1.xy, r0));
// // 
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -3) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -2) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o * -1) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  1) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  2) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(0, o *  3) + v1.xy, r0));
// // 
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -3, 0) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -2, 0) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, 0) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(o * -1, 0) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  2, 0) + v1.xy, r0));
// //   r1.xyz = max(r1.xyz, SampleBloom(ps_postprocess_pixel_size.xy * float2(o *  3, 0) + v1.xy, r0));
// 
//   r1.xyz *= 4.f;
//   r1.xyz *= r1.xyz;


  r0.xyz = g_exposure.yyy * r1.xyz;
  r0.w = dot(r0.xyz, intensity_vector.xyz);
  r1.x = ps_postprocess_scale.y * r0.w;
  r1.y = -ps_postprocess_scale.x + r0.w;
  r1.x = max(r1.x, r1.y);
  r1.x = r1.x / r0.w;
  o0.xyz = r1.xxx * r0.xyz; 
    o0.xyz = max(0, o0.xyz);
  o0.w = r0.w;

  // if (IsGame_Halo3ODST()) {
    o0.xyz = Neutwo(o0.xyz, 32);
    o0.w = Neutwo(o0.w, 32); //luma for auto exposure
  // }
  // r1.xyz = sqrt(r1.xyz);

  return;
}