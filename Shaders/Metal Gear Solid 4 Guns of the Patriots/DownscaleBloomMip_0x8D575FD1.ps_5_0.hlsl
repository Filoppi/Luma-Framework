#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3;
  r0.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
  r0.xyz = r0.xyz / r0.www;
  r0.w = ddx_coarse(v2.x);
  r1.x = v3.x * r0.w;
  r0.w = ddy_coarse(v2.y);
  r1.y = v3.y * r0.w;
  r2.xyzw = r1.xyxy * float4(0.129409522,0.482962906,0.482962906,0.129409522) + v2.xyxy;
  r3.xyzw = t0.Sample(s0_s, r2.xy).xyzw;
  r2.xyzw = t0.Sample(s0_s, r2.zw).xyzw;
  r2.xyz = r2.xyz / r2.www;
  r3.xyz = r3.xyz / r3.www;
  r3.xyz = float3(0.25,0.25,0.25) * r3.xyz;
  r0.xyz = r0.xyz * float3(0.25,0.25,0.25) + r3.xyz;
  r0.xyz = r2.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r2.xyzw = r1.xyxy * float4(0.353553385,-0.353553385,-0.129409522,-0.482962906) + v2.xyxy;
  r1.xyzw = r1.xyxy * float4(-0.482962906,-0.129409522,-0.353553385,0.353553385) + v2.xyxy;
  r3.xyzw = t0.Sample(s0_s, r2.xy).xyzw;
  r2.xyzw = t0.Sample(s0_s, r2.zw).xyzw;
  r2.xyz = r2.xyz / r2.www;
  r3.xyz = r3.xyz / r3.www;
  r0.xyz = r3.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r0.xyz = r2.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r2.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.xyzw = t0.Sample(s0_s, r1.zw).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r2.xyz = r2.xyz / r2.www;
  r0.xyz = r2.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r0.xyz = r1.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r0.xyz = v1.xyz * r0.xyz;
  r0.xyz = float3(0.142857149,0.142857149,0.142857149) * r0.xyz;
  r0.w = max(r0.x, r0.y);
  r1.x = max(0.25, r0.z);
  r0.w = max(r1.x, r0.w);
  r0.w = 1 / r0.w;
  o0.xyz = r0.xyz * r0.w;
  o0.w = 0.25 * r0.w;
#if 0 // Luma: disable unnecessary saturate
  o0.xyz = saturate(o0.xyz);
  o0.w = saturate(o0.w);
#endif
}