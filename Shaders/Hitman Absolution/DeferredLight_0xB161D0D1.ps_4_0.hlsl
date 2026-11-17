// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0xB161D0D1 (two render targets).
// Under TONEMAP_TYPE >= 1 the mapDeferred1 G-buffer sample is saturated to [0,1] to emulate the vanilla 8-bit UNORM targets (Luma's fp16 leaks values > 1 that make negative light).
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

#if TONEMAP_TYPE >= 1
#define GBUFFER_CLAMP(v) saturate(v)
#else
#define GBUFFER_CLAMP(v) (v)
#endif

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[145];
};

Texture2D<float4> mapDeferred0 : register(t0);
Texture2D<float4> mapDeferred1 : register(t1);

SamplerState samplerDeferred0 : register(s0);
SamplerState samplerDeferred1 : register(s1);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
{
  float4 r0 = 0.0, r1 = 0.0;

  r0 = GBUFFER_CLAMP(mapDeferred1.Sample(samplerDeferred1, v2.xy));
  o0.xy = r0.xy;
  r0 = mapDeferred0.Sample(samplerDeferred0, v2.xy);
  r0.x = cb4[8].z * r0.x - cb4[8].y;
  r0.x = -cb4[8].x / r0.x;
  r0.y = saturate(r0.x / cb4[144].x);
  r1.x = -r0.x + cb4[114].x;
  r0.x = r0.y * 255.0;
  r0.y = floor(r0.x);
  o0.w = frac(r0.x);
  o0.z = r0.y * 0.003922;
  r1.y = -r1.x;
  r0.xy = r1.xy + -cb4[114].yy;
  r0.xy = saturate(r0.xy * cb4[114].zw);
  r0.xy = r0.xy * cb4[115].x;
  r0.z = r0.y + r0.x;
  r0.w = 0.0;
  o1 = float4(r0.x, r0.w, r0.w, r0.z);
}
