// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:39:44 2026

cbuffer PostProcessPS : register(b0)
{
  float4 pixel_size : packoffset(c0);
  float4 scale : packoffset(c1);
  float4x3 p_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 p_postprocess_contrast : packoffset(c5);
}

SamplerState LocalSampler_source_sampler_s : register(s0);
Texture2D<float4> LocalTexture_source_sampler : register(t0);


// 3Dmigoto declarations
#define cmp -

// (results also used by bloom)
void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = scale.xy * v1.xy;
  // o0.xyzw = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy).xyzw;

  // 960x540 -> 288x180
  float4 C = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy).xyzw;
  float3 N = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(0,-1)).xyz;
  float3 S = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(0,1)).xyz;
  float3 W = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(-1,0)).xyz;
  float3 E = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(1, 0)).xyz;

  // float3 NW = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(-1,-1)).xyz;
  // float3 NE = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(1,-1)).xyz;
  // float3 SW = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(-1,1)).xyz;
  // float3 SE = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy, int2(1,1)).xyz;

  float3 x = C.xyz;

  x += N.xyz;
  x += S.xyz;
  x += W.xyz;
  x += E.xyz;

  // x += NW.xyz;
  // x += NE.xyz;
  // x += SW.xyz;
  // x += SE.xyz;

  x /= 1 + 4 /* + 4 */;

  o0 = float4(x, C.w);
  return;
}