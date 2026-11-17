// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0xF3EA45EB.
// Under TONEMAP_TYPE >= 1 the mapDeferred1/2 G-buffer samples are saturated to [0,1] to emulate the vanilla 8-bit UNORM targets (Luma's fp16 leaks values > 1 that make negative light).
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

#if TONEMAP_TYPE >= 1
#define GBUFFER_CLAMP(v) saturate(v)
#else
#define GBUFFER_CLAMP(v) (v)
#endif

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[9];
};

Texture2D<float4> mapDeferred0 : register(t0);
Texture2D<float4> mapDeferred1 : register(t1);
Texture2D<float4> mapDeferred2 : register(t2);

SamplerState samplerDeferred0 : register(s0);
SamplerState samplerDeferred1 : register(s1);
SamplerState samplerDeferred2 : register(s2);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0;

  r0.x = 1.0 / v1.w;
  r0.yzw = r0.x * v2.xyz;
  r1.xy = r0.x * v1.xy;
  r2 = mapDeferred0.Sample(samplerDeferred0, r1.xy);
  r0.x = cb1[12].z * r2.x - cb1[12].y;
  r0.x = 1.0 / r0.x;
  r0.xyz = -r0.x * r0.yzw + cb2[8].xyz;
  r0.w = dot(r0.xyz, r0.xyz);
  r1.z = rsqrt(r0.w);
  r0.w = sqrt(r0.w);
  r2.x = saturate(cb2[1].y * r0.w + cb2[1].x);
  r2.y = saturate(cb2[1].w * r0.w + cb2[1].z);
  r0.w = r2.y * r2.x;
  r2.xyz = r0.w * cb2[3].xyz;
  r0.xyz = r0.xyz * r1.z;
  r3 = GBUFFER_CLAMP(mapDeferred1.Sample(samplerDeferred1, r1.xy));
  r1 = GBUFFER_CLAMP(mapDeferred2.Sample(samplerDeferred2, r1.xy));
  r1.xyz = r2.xyz * r1.xyz;
  r2.xyz = r3.xyz + float3(-0.5, -0.5, -0.5);
  r2.xyz = r2.xyz + r2.xyz;
  r0.w = dot(r2.xyz, r2.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r0.w * r2.xyz;
  r0.x = saturate(dot(r2.xyz, r0.xyz));
  o0.xyz = r0.x * r1.xyz;
  o0.w = 0.0;
}
