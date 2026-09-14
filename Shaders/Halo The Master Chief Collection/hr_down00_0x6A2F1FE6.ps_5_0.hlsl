// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:39:56 2026

cbuffer PostProcessPS : register(b0)
{
  float4 pixel_size : packoffset(c0);
  float4 scale : packoffset(c1);
  float4x3 p_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 p_postprocess_contrast : packoffset(c5);
}

cbuffer Kernel5PS : register(b1)
{
  float4 kernel[5] : packoffset(c0);
}

SamplerState LocalSampler_target_sampler_s : register(s0);
Texture2D<float4> LocalTexture_target_sampler : register(t0);


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

  r0.xy = kernel[1].xy * pixel_size.xy + v1.xy;
  r0.xyzw = LocalTexture_target_sampler.Sample(LocalSampler_target_sampler_s, r0.xy).xyzw;
  r0.xyzw = kernel[1].zzzz * r0.xyzw;
  r0.xyzw = float4(8,8,8,8) * r0.xyzw;
  r1.xy = kernel[0].xy * pixel_size.xy + v1.xy;
  r1.xyzw = LocalTexture_target_sampler.Sample(LocalSampler_target_sampler_s, r1.xy).xyzw;
  r1.xyzw = kernel[0].zzzz * r1.xyzw;
  r0.xyzw = r1.xyzw * float4(8,8,8,8) + r0.xyzw;
  r1.xy = kernel[2].xy * pixel_size.xy + v1.xy;
  r1.xyzw = LocalTexture_target_sampler.Sample(LocalSampler_target_sampler_s, r1.xy).xyzw;
  r1.xyzw = kernel[2].zzzz * r1.xyzw;
  r0.xyzw = r1.xyzw * float4(8,8,8,8) + r0.xyzw;
  r1.xy = kernel[3].xy * pixel_size.xy + v1.xy;
  r1.xyzw = LocalTexture_target_sampler.Sample(LocalSampler_target_sampler_s, r1.xy).xyzw;
  r1.xyzw = kernel[3].zzzz * r1.xyzw;
  r0.xyzw = r1.xyzw * float4(8,8,8,8) + r0.xyzw;
  r1.xy = kernel[4].xy * pixel_size.xy + v1.xy;
  r1.xyzw = LocalTexture_target_sampler.Sample(LocalSampler_target_sampler_s, r1.xy).xyzw;
  r1.xyzw = kernel[4].zzzz * r1.xyzw;
  r0.xyzw = r1.xyzw * float4(8,8,8,8) + r0.xyzw;
  r0.xyzw = scale.xyzw * r0.xyzw;
  r0.xyzw = float4(4,4,4,4) * r0.xyzw;
  // r0.xyzw = min(float4(8,8,8,8), r0.xyzw);
  o0.xyzw = float4(0.03125,0.03125,0.03125,0.03125) * r0.xyzw;
  return;
}