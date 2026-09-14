// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 17:28:57 2026

cbuffer ExposurePS : register(b0)
{
  float4 g_exposure : packoffset(c0);
  float4 g_alt_exposure : packoffset(c1);
}

cbuffer ShieldImpactPS : register(b1)
{
  float4 bound_sphere : packoffset(c0);
  float4 shield_dynamic_quantities : packoffset(c1);
  float4 texture_quantities : packoffset(c2);
  float4 plasma1_settings : packoffset(c3);
  float4 plasma2_settings : packoffset(c4);
  float3 overshield_color1 : packoffset(c5);
  float overshield_color1_pad : packoffset(c5.w);
  float3 overshield_color2 : packoffset(c6);
  float overshield_color2_pad : packoffset(c6.w);
  float3 overshield_ambient_color : packoffset(c7);
  float overshield_ambient_color_pad : packoffset(c7.w);
  float3 shield_impact_color1 : packoffset(c8);
  float shield_impact_color1_pad : packoffset(c8.w);
  float3 shield_impact_color2 : packoffset(c9);
  float shield_impact_color2_pad : packoffset(c9.w);
  float3 shield_impact_ambient_color : packoffset(c10);
  float shield_impact_ambient_color_pad : packoffset(c10.w);
  float shield_impact_edge_fade : packoffset(c11);
  float3 shield_impact_edge_fade_pad : packoffset(c11.y);
}

SamplerState LocalSampler_shield_impact_noise_texture1_s : register(s0);
SamplerState LocalSampler_shield_impact_noise_texture2_s : register(s1);
Texture2D<float4> LocalTexture_shield_impact_noise_texture1 : register(t0);
Texture2D<float4> LocalTexture_shield_impact_noise_texture2 : register(t1);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  out float4 o0 : SV_Target0,
  out float4 o1 : SV_Target1)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = texture_quantities.y * shield_dynamic_quantities.x;
  r0.yz = r0.xx * float2(0.0833333358,0.0769230798) + v2.xy;
  r0.xw = -r0.xx * float2(0.0909090936,0.0588235296) + v2.xy;
  r0.xw = texture_quantities.zz * r0.xw;
  r0.x = LocalTexture_shield_impact_noise_texture2.Sample(LocalSampler_shield_impact_noise_texture2_s, r0.xw).x;
  r0.yz = texture_quantities.xx * r0.yz;
  r0.y = LocalTexture_shield_impact_noise_texture1.Sample(LocalSampler_shield_impact_noise_texture1_s, r0.yz).x;
  r0.x = r0.y + -r0.x;
  r0.x = 1 + -abs(r0.x);
  r0.x = log2(r0.x);
  r0.y = plasma2_settings.x * r0.x;
  r0.x = plasma1_settings.x * r0.x;
  r0.x = exp2(r0.x);
  r0.x = -plasma1_settings.z + r0.x;
  r0.x = plasma1_settings.y * r0.x;
  r0.y = exp2(r0.y);
  r0.y = -plasma2_settings.z + r0.y;
  r0.y = plasma2_settings.y * r0.y;
  r0.xy = max(float2(0,0), r0.xy);
  r1.xyz = overshield_color2.xyz * r0.yyy;
  r1.xyz = r0.xxx * overshield_color1.xyz + r1.xyz;
  r0.z = r0.x + r0.y;
  r2.xyz = shield_impact_color2.xyz * r0.yyy;
  r0.xyw = r0.xxx * shield_impact_color1.xyz + r2.xyz;
  r0.z = min(1, r0.z);
  r0.z = 1 + -r0.z;
  r1.xyz = r0.zzz * overshield_ambient_color.xyz + r1.xyz;
  r0.xyz = r0.zzz * shield_impact_ambient_color.xyz + r0.xyw;
  r1.xyz = shield_dynamic_quantities.zzz * r1.xyz;
  r0.xyz = shield_dynamic_quantities.yyy * r0.xyz + r1.xyz;
  r1.x = saturate(v1.w);
  r1.x = log2(r1.x);
  r1.x = shield_impact_edge_fade * r1.x;
  r1.x = exp2(r1.x);
  r0.w = 1;
  r0.xyzw = r1.xxxx * r0.xyzw;
  r0.xyzw = g_exposure.xxxx * r0.xyzw;
  o0.w = g_exposure.w * r0.w;
  o0.xyz = r0.xyz;
  o1.xyz = r0.xyz / g_exposure.yyy;
  o1.w = g_exposure.z * r0.w;

  o0 = max(0, o0);
  if (!HDR_ENABLED) o0.xyz = saturate(o0.xyz);
  else o0.xyz = Neutwo(o0.xyz, 10);
  o0.w = min(o0.w, 1);
  return;
}