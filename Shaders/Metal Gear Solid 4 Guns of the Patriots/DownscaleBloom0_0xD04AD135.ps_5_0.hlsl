#include "../Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  r0.xyzw = cb0[0].xyzw + v1.xyxy;
  r1.xyzw = t0.Sample(s0_s, r0.zw).xyzw;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyz = r0.xyz / r0.www;
  r1.xyz = r1.xyz / r1.www;
  r1.xyz = float3(0.25,0.25,0.25) * r1.xyz;
  r0.xyz = r0.xyz * float3(0.25,0.25,0.25) + r1.xyz;
  r1.xyzw = -cb0[0].zwxy + v1.xyxy;
  r2.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.xyzw = t0.Sample(s0_s, r1.zw).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r2.xyz = r2.xyz / r2.www;
  r0.xyz = r2.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r0.xyz = r1.xyz * float3(0.25,0.25,0.25) + r0.xyz;
  r0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  r1.xyz = v2.xxx * r0.xyz;
  r2.xyz = r1.xyz * v2.yyy + float3(1,1,1);
  r1.xyz = r2.xyz * r1.xyz;
  r2.xyz = r0.xyz * v2.xxx + float3(1,1,1);
#if 1 // Luma: fix Rec.601 luminance// TODO: calculate in linear space
  r0.x = GetLuminance(r0.xyz);
#else
  r0.x = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
#endif
  r0.x = -v3.w + r0.x;
  r0.x = max(0, r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r1.xyz = r1.xyz / r2.xyz;
  r1.xyz = saturate(r1.xyz * v4.yyy + -v4.zzz);
  r0.xyz = r1.xyz * v4.www + r0.xyz;
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