// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:40:06 2026

cbuffer ExposurePS : register(b0)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer PostProcessPS : register(b1)
{
  float4 pixel_size : packoffset(c0);
  float4 scale : packoffset(c1);
  float4x3 p_postprocess_hue_saturation_matrix : packoffset(c2);
  float4 p_postprocess_contrast : packoffset(c5);
}

cbuffer DownsamplePS : register(b2)
{
  float4 intensity_vector : packoffset(c0);
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
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  // r0.xyz = LocalTexture_source_sampler.Sample(LocalSampler_source_sampler_s, v1.xy).xyz;
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

  r0.xyz = g_exposure.yyy * r0.xyz;
  r1.w = dot(r0.xyz, intensity_vector.xyz);
  r0.w = scale.x * r1.w + scale.y;
  r1.xyz = r0.xyz * r0.www;
  r0.xyzw = float4(0.25,0.25,0.25,0.25) * r1.xyzw;
  o0.xyzw = min(float4(8,8,8,8), r0.xyzw);
  // o0.xyz = pow(o0.xyz, 2.2);

  o0 = max(0, o0);
  return;
}