// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:39:50 2026

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


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = v1.xy * scale.xy + scale.zw;
  // r0.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, r0.xy).xyz;
  {
    int o = 2;
    float3 C = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy).xyz;
    float3 N = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(0,-o)).xyz;
    float3 S = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(0,o)).xyz;
    float3 W = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(-o,0)).xyz;
    float3 E = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(o, 0)).xyz;

    float3 N1 = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(0,-o*2)).xyz;
    float3 S1 = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(0,o*2)).xyz;
    float3 W1 = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(-o*2,0)).xyz;
    float3 E1 = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy, int2(o*2, 0)).xyz;

    float3 x = C.xyz;
    x += N.xyz;
    x += S.xyz;
    x += W.xyz;
    x += E.xyz;
    x += N1.xyz;
    x += S1.xyz;
    x += W1.xyz;
    x += E1.xyz;
    x /= 1 + 4 + 4;
    r0.xyz = x;
  }

  r0.xyz = max(r0.xyz, 0);
  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}