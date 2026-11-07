// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x3994DDCA.
// Under TONEMAP_TYPE >= 1 the mapDeferred1/2/3 G-buffer samples are saturated to [0,1] to emulate the vanilla 8-bit UNORM targets (Luma's fp16 leaks values > 1 that make negative light).
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"

#if TONEMAP_TYPE >= 1
#define GBUFFER_CLAMP(v) saturate(v)
#else
#define GBUFFER_CLAMP(v) (v)
#endif

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[16];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[18];
};

cbuffer GlobalLightData_cb2 : register(b2)
{
  float4 cb2[8];
};

Texture2D<float4> mapLightModel : register(t0);
Texture2D<float4> mapDeferred0 : register(t1);
Texture2D<float4> mapDeferred1 : register(t2);
Texture2D<float4> mapDeferred2 : register(t3);
Texture2D<float4> mapDeferred3 : register(t4);
Texture2D<float4> mapOcclusion : register(t5);

SamplerState samplerLightModel : register(s0);
SamplerState samplerDeferred0 : register(s1);
SamplerState samplerDeferred1 : register(s2);
SamplerState samplerDeferred2 : register(s3);
SamplerState samplerDeferred3 : register(s4);
SamplerState samplerOcclusion : register(s5);

void main(
  float4 v0 : SV_POSITION,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0;

  r0 = v1.y * cb4[8];
  r0 = v1.x * cb4[7] + r0;
  r1 = mapDeferred0.Sample(samplerDeferred0, v1.xy);
  r0 = r1.x * cb4[9] + r0;
  r0 = r0 + cb4[10];
  r0.xyz = r0.xyz / r0.w;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.w * r0.xyz;
  r0.w = dot(v2.xyz, v2.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.w * v2.xyz;
  r0.xyz = (0.0 != cb4[15].x) ? -r0.xyz : r1.xyz;
  r1 = GBUFFER_CLAMP(mapDeferred1.Sample(samplerDeferred1, v1.xy));
  r1.xyz = r1.xyz + float3(-0.5, -0.5, -0.5);
  r1.xyz = r1.xyz + r1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.w * r1.xyz;
  r0.w = dot(r1.xyz, cb2[7].xyz);
  r2.xyz = r0.w * r1.xyz - cb2[6].xyz;
  r1.x = dot(r1.xyz, cb1[17].xyz);
  r1.xyz = cb2[2].xyz * r1.x + cb2[0].xyz;
  r3.x = saturate(r0.w * 0.5);
  r3.y = saturate(dot(r0.xyz, r2.xyz));
  r0 = GBUFFER_CLAMP(mapDeferred2.Sample(samplerDeferred2, v1.xy));
  r3.z = saturate(r0.w);
  r3.w = dot(r3.yz, float2(0.248047, 0.753906));
  r2 = mapLightModel.Sample(samplerLightModel, r3.xw);
  r1.xyz = r2.xyz * cb2[4].xyz + r1.xyz;
  r1.w = r2.w * 1024.0 + -1000.0;
  r2 = GBUFFER_CLAMP(mapDeferred3.Sample(samplerDeferred3, v1.xy));
  r1.w = r1.w * r2.y;
  r1.w = HMADeferredSpecularPower(r1.w);
  r1.w = r2.x * r1.w;
  r2.xyz = r1.w * cb2[4].xyz;
  r0.xyz = r1.xyz * r0.xyz + r2.xyz;
  o0.w = saturate(r0.w + -0.66);
  r1 = mapOcclusion.Sample(samplerOcclusion, v1.xy);
  r1.xyz = r1.xyz * r1.xyz;
  o0.xyz = r0.xyz * r1.xyz;
}
