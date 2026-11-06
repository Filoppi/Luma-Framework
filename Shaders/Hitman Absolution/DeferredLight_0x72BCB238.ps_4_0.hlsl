// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x72BCB238.
// Under TONEMAP_TYPE >= 1 the mapDeferred1..3 G-buffer reads are clamped to [0,1] to emulate the vanilla 8-bit UNORM G-buffer; Luma's fp16 G-buffer lets values above 1 leak and produce negative light.
// TONEMAP_TYPE 0 keeps the vanilla math exact.

#include "Includes/Common.hlsl"

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[20];
};

Texture2D<float4> mapLightModel : register(t0);
Texture2D<float4> mapDeferred0 : register(t1);
Texture2D<float4> mapDeferred1 : register(t2);
Texture2D<float4> mapDeferred2 : register(t3);
Texture2D<float4> mapDeferred3 : register(t4);

SamplerState samplerLightModel : register(s0);
SamplerState samplerDeferred0 : register(s1);
SamplerState samplerDeferred1 : register(s2);
SamplerState samplerDeferred2 : register(s3);
SamplerState samplerDeferred3 : register(s4);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, r4 = 0.0, r5 = 0.0;

  r0.x = 1.0 / v1.w;
  r0.yz = r0.x * v1.xy;
  r1.xyz = r0.x * v2.xyz;

  r2 = mapDeferred1.Sample(samplerDeferred1, r0.yz);
#if TONEMAP_TYPE >= 1
  r2 = saturate(r2);
#endif
  r2.xyz = r2.xyz - 0.5;
  r2.xyz = r2.xyz + r2.xyz;
  r0.x = dot(r2.xyz, r2.xyz);
  r0.x = rsqrt(r0.x);
  r2.xyz = r0.x * r2.xyz;

  r3 = mapDeferred0.Sample(samplerDeferred0, r0.yz);
  r0.x = cb1[12].z * r3.x - cb1[12].y;
  r0.x = 1.0 / r0.x;
  r3.xyz = -r0.x * r1.xyz + cb2[8].xyz;
  r1.xyz = r1.xyz * r0.x;
  r0.x = dot(r3.xyz, r3.xyz);
  r0.w = rsqrt(r0.x);
  r0.x = sqrt(r0.x);
  r4.x = saturate(cb2[1].y * r0.x + cb2[1].x);
  r4.y = saturate(cb2[1].w * r0.x + cb2[1].z);
  r0.x = r4.y * r4.x;
  r4.xyz = r0.x * cb2[3].xyz;
  r3.xyz = r0.w * r3.xyz;
  r5.x = dot(r2.xyz, r3.xyz);
  r0.x = r5.x + r5.x;
  r5.x = saturate(r5.x);
  r2.xyz = -r0.x * r2.xyz + r3.xyz;
  r0.x = dot(-r1.xyz, -r1.xyz);
  r0.x = rsqrt(r0.x);
  r1.xyz = r0.x * -r1.xyz;
  r1.x = saturate(dot(r1.xyz, -r2.xyz));

  r2 = mapDeferred2.Sample(samplerDeferred2, r0.yz);
#if TONEMAP_TYPE >= 1
  r2 = saturate(r2);
#endif
  r0 = mapDeferred3.Sample(samplerDeferred3, r0.yz);
#if TONEMAP_TYPE >= 1
  r0 = saturate(r0);
#endif
  r1.y = r2.w;
  r5.y = dot(r1.xy, float2(0.248047, 0.753906));
  r1 = mapLightModel.Sample(samplerLightModel, r5.xy);
  r0.z = r1.w * 1024.0 - 1000.0;
  r0.y = r0.z * r0.y;
  r0.y = exp2(r0.y);
  r0.x = r0.y * r0.x;
  r0.x = r0.x * cb2[19].x;
  r0.xyz = r1.xyz * r2.xyz + r0.x;
  o0.xyz = r4.xyz * r0.xyz;
  o0.w = 0.0;
}
