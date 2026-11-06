// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x83EEC2CC (projected light).
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
Texture2D<float4> mapLightProjectorMap0 : register(t1);
Texture2D<float4> mapDeferred0 : register(t2);
Texture2D<float4> mapDeferred1 : register(t3);
Texture2D<float4> mapDeferred2 : register(t4);
Texture2D<float4> mapDeferred3 : register(t5);

SamplerState samplerLightModel : register(s0);
SamplerState samplerLightProjectorMap0 : register(s1);
SamplerState samplerDeferred0 : register(s2);
SamplerState samplerDeferred1 : register(s3);
SamplerState samplerDeferred2 : register(s4);
SamplerState samplerDeferred3 : register(s5);

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
  r0.x = dot(r2.xyz, cb2[9].xyz);
  r0.w = r0.x + r0.x;
  r2.xyz = -r0.w * r2.xyz + cb2[9].xyz;

  r3 = mapDeferred0.Sample(samplerDeferred0, r0.yz);
  r0.w = cb1[12].z * r3.x - cb1[12].y;
  r0.w = 1.0 / r0.w;
  r1.xyz = r1.xyz * r0.w;
  r0.w = dot(-r1.xyz, -r1.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r0.w * -r1.xyz;
  r2.x = saturate(dot(r3.xyz, -r2.xyz));

  r3 = mapDeferred2.Sample(samplerDeferred2, r0.yz);
#if TONEMAP_TYPE >= 1
  r3 = saturate(r3);
#endif
  r4 = mapDeferred3.Sample(samplerDeferred3, r0.yz);
#if TONEMAP_TYPE >= 1
  r4 = saturate(r4);
#endif
  r2.y = r3.w;
  r2.y = dot(r2.xy, float2(0.248047, 0.753906));
  r2.x = saturate(r0.x);
  r0.x = saturate(-r0.x);
  r2 = mapLightModel.Sample(samplerLightModel, r2.xy);
  r0.y = r2.w * 1024.0 - 1000.0;
  r0.y = r0.y * r4.y;
  r0.y = exp2(r0.y);
  r0.z = dot(r3.xyz, float3(0.447058, 0.882354, 0.170587));
  r5.xyz = r3.xyz * 2.0 - r0.z;
  r0.z = 1.0 - cb2[3].w;
  r0.z = r4.w * r0.z;
  r0.x = r0.x * r0.z;
  r0.xzw = r5.xyz * r0.x;
  r0.xyz = r4.x * r0.y + r0.xzw;
  r0.xyz = r0.xyz * cb2[19].x;
  r0.xyz = r2.xyz * r3.xyz + r0.xyz;

  r2.xyz = r1.y * float3(cb2[16].x, cb2[16].y, cb2[16].w);
  r1.xyw = r1.x * float3(cb2[15].x, cb2[15].y, cb2[15].w) + r2.xyz;
  r1.xyz = r1.z * float3(cb2[17].x, cb2[17].y, cb2[17].w) + r1.xyw;
  r1.xyz = r1.xyz + float3(cb2[18].x, cb2[18].y, cb2[18].w);
  r1.xy = r1.xy / r1.z;
  r1.xy = r1.xy * cb2[10].xy + cb2[10].zw;
  r1 = mapLightProjectorMap0.SampleLevel(samplerLightProjectorMap0, r1.xy, -2.0);
  r1.xyz = r1.xyz * cb2[3].xyz;
  o0.xyz = r0.xyz * r1.xyz;
  o0.w = 0.0;
}
