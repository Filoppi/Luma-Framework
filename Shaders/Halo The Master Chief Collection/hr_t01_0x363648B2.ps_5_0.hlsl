// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:39:42 2026

cbuffer ViewPS : register(b0)
{
  float3 Camera_Position_PS : packoffset(c0);
  float Camera_Position_PS_pad : packoffset(c0.w);
  float4 depth_constants : packoffset(c1);
  bool shadow_mask_enabled : packoffset(c2);
}

cbuffer ExposurePS : register(b1)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer FinalCompositePS : register(b2)
{
  float4 tone_curve_constants : packoffset(c0);
  float4 player_window_constants : packoffset(c1);
  float4 depth_constants2 : packoffset(c2);
  float4x3 color_matrix : packoffset(c3);
  float4 gamma : packoffset(c6);
  float4 noise_params : packoffset(c7);
}

SamplerState GlobalSampler_surface_sampler_s : register(s0);
SamplerState GlobalSampler_bloom_sampler_s : register(s2);
SamplerState GlobalSampler_depth_sampler_s : register(s3);
SamplerState GlobalSampler_blur_sampler_s : register(s4);
SamplerState GlobalSampler_noise_sampler_s : register(s7);
Texture2D<float4> GlobalTexture_surface_sampler : register(t0);
Texture2D<float4> GlobalTexture_bloom_sampler : register(t2);
Texture2D<float4> GlobalTexture_depth_sampler : register(t3);
Texture2D<float4> GlobalTexture_blur_sampler : register(t4);
Texture2D<float4> GlobalTexture_noise_sampler : register(t7);


// 3Dmigoto declarations
#define cmp -
#include "./hr_t.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = GlobalTexture_depth_sampler.Sample(GlobalSampler_depth_sampler_s, v1.xy).x;
  r0.x = r0.x * depth_constants.y + depth_constants.x;
  r0.x = 1 / r0.x;
  r0.x = -depth_constants.z + r0.x;
  r0.x = -depth_constants2.x + abs(r0.x);
  r0.x = max(0, r0.x);
  r0.x = depth_constants.w * r0.x;
  r0.x = min(depth_constants2.y, r0.x);
  r0.x = r0.x * r0.x;
  r0.yzw = GlobalTexture_blur_sampler.Sample(GlobalSampler_blur_sampler_s, v1.xy).xyz;
    r0.yzw = max(r0.yzw, 0);
  r1.xyz = GlobalTexture_surface_sampler.Sample(GlobalSampler_surface_sampler_s, v1.xy).xyz;
  r0.yzw = -r1.xyz * g_exposure.yyy + r0.yzw;
  r1.xyz = g_exposure.yyy * r1.xyz;
  r0.xyz = r0.xxx * r0.yzw + r1.xyz;

  // r1.xyz = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy).xyz;
  {
    float3 C = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy).xyz;
    float3 N = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(0,-1)).xyz;
    float3 S = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(0,1)).xyz;
    float3 W = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(-1,0)).xyz;
    float3 E = GlobalTexture_bloom_sampler.Sample(GlobalSampler_bloom_sampler_s, v2.xy, int2(1, 0)).xyz;
    float3 x = C.xyz;
    x += N.xyz;
    x += S.xyz;
    x += W.xyz;
    x += E.xyz;
    x /= 5;
    r1.xyz = x * (GS.Bloom * BLOOM_MAKEUP);
    r1.xyz = max(0, r1.xyz);
  }
  r0.xyz = r1.xyz * float3(8,8,8) + r0.xyz;

  // Gamma Encode
  r0.xyz = max(r0.xyz, 0);
  r0.xyz = pow(r0.xyz, gamma.z); //1/2.0

  r0.w = 1;
  r1.x = /* saturate */(dot(r0.xyzw, color_matrix._m00_m10_m20_m30));
  r1.y = /* saturate */(dot(r0.xyzw, color_matrix._m01_m11_m21_m31));
  r1.z = /* saturate */(dot(r0.xyzw, color_matrix._m02_m12_m22_m32));

  // HDR Tonemap
  r1.xyz = sRGB_Decode(r1.xyz);
  r1.xyz = Rolloff(r1.xyz);
  r1.xyz = sRGB_Encode(r1.xyz);

  r0.x = GlobalTexture_noise_sampler.Sample(GlobalSampler_noise_sampler_s, FilmGrainUV(v2.zw)).z;
  r0.xy = r0.xx * noise_params.xy + noise_params.zw;
  r0.y *= 0.5 * GS.FilmGrain;
  r0.xyz = r1.xyz * r0.xxx + r0.yyy;
  r0.xyz = max(r0.xyz, 0);

  // o0.w = dot(r0.xyz, float3(0.298999995,0.587000012,0.114));
  o0.w = GetLuminance(r0.xyz, CS_BT709);

  o0.xyz = r0.xyz;
  return;
}