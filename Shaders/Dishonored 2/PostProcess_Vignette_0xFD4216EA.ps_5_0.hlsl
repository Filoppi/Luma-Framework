#include "Includes/GameCBuffers.hlsl"
#include "../Includes/Common.hlsl"

cbuffer PerInstanceCB : register(b2)
{
  float4 cb_color : packoffset(c0);
  float4 cb_color_adjust : packoffset(c1);
  float2 cb_localtime : packoffset(c2);
  uint2 cb_postfx_luminance_exposureindex : packoffset(c2.z);
  float2 cb_opacity_tilling : packoffset(c3);
  float2 cb_opacity_panningspeed : packoffset(c3.z);
  float cb_material_seed : packoffset(c4);
  float cb_postfx_screenquadfade : packoffset(c4.y);
  float cb_postfx_post_tonemap : packoffset(c4.z);
  float cb_particlealphatest : packoffset(c4.w);
  float cb_opacity_rotationspeed : packoffset(c5);
}

SamplerState smp_linearclamp_s : register(s0);
Texture2D<float4> ro_fx_opacity : register(t0);

#define cmp -

void main(
  float4 v0 : INTERP0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  r0.xy = cb_opacity_panningspeed.xy * cb_localtime.xx;
  r0.z = cb_opacity_rotationspeed * cb_localtime.x;
  sincos(r0.z, r1.x, r2.x);
  r0.zw = float2(-0.5,-0.5) + v0.xy;
  r1.yz = r0.zw * r2.xx;
  r0.w = r0.w * r1.x + r1.y;
  r0.z = -r0.z * r1.x + r1.z;
  r1.xy = float2(0.5,0.5) + r0.wz;
  r0.xy = r1.xy * cb_opacity_tilling.xy + r0.xy;
  r0.x = ro_fx_opacity.Sample(smp_linearclamp_s, r0.xy).x;
  r0.y = -0.00196078443 + r0.x;
  r0.x = cb_postfx_screenquadfade * r0.x * LumaSettings.GameSettings.VignetteStrength;
  r0.y = cmp(r0.y < 0);
  if (r0.y != 0) discard;
  r0.yzw = float3(-1,-1,-1) + cb_color_adjust.xyz;
  o0.xyz = r0.xxx * r0.yzw + float3(1,1,1);
  o0.w = r0.x;
}