// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:40:04 2026

cbuffer PostProcessPS : register(b0)
{
  float4 pixel_size : packoffset(c0);
  float4 scale : packoffset(c1);
  float4x3 p_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 p_postprocess_contrast : packoffset(c5);
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
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, v1.xy).xyz;
  // {
  //   int offset = 1;
  //   float3 C = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, v1.xy).xyz;
  //   float3 N = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, v1.xy, int2(0,-offset)).xyz;
  //   float3 S = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, v1.xy, int2(0,offset)).xyz;
  //   float3 W = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, v1.xy, int2(-offset,0)).xyz;
  //   float3 E = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, v1.xy, int2(offset, 0)).xyz;
  //   float3 NW = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, r0.xy, int2(-offset,-offset)).xyz;
  //   float3 NE = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, r0.xy, int2(offset,-offset)).xyz;
  //   float3 SW = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, r0.xy, int2(-offset,offset)).xyz;
  //   float3 SE = LocalTexture_original_sampler.Sample(LocalSampler_original_sampler_s, r0.xy, int2(offset,offset)).xyz;
  //   float3 x = C.xyz;
  //   x += N.xyz;
  //   x += S.xyz;
  //   x += W.xyz;
  //   x += E.xyz;
  //   x /= 5;
  //   r0.xyz = x;
  // }
  r0.xyz = scale.xyz * r0.xyz;
  r1.xyzw = LocalTexture_add_sampler.Sample(LocalSampler_add_sampler_s, v1.xy).xyzw;
  r2.xyzw = float4(8,8,8,8) * r1.xyzw;
  r0.xyz = r2.www * r0.xyz;
  r1.xyz = r0.xyz * float3(8,8,8) + r2.xyz;
  r0.xyzw = float4(4,4,4,32) * r1.xyzw;
  r0.xyzw = min(float4(8,8,8,8), r0.xyzw);
  o0.xyzw = float4(0.03125,0.03125,0.03125,0.03125) * r0.xyzw;
  return;
}