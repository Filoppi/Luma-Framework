// Luma: HDR-preserving replacement for the lens flare pass 0xD743DF0F.
// TONEMAP_TYPE 0 keeps the vanilla math exactly. With TONEMAP_TYPE >= 1 the four threshold subtractions
// (saturate(x - threshold)) become max(0, x - threshold), so bright flare sources are no longer clamped to 1.

#include "Includes/Common.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[50];
}

Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // Flare source

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

float3 FlareThreshold(float3 x, float t)
{
#if TONEMAP_TYPE >= 1
  return max(0.0, x - t);
#else
  return saturate(x - t);
#endif
}

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float2 d = v2 - 0.5;

  float2 uvA = d * -0.75 + 0.5;
  float2 uvB = d * -0.15 + 0.5;
  float2 uvC = d * -3.25 + 0.5;
  float2 uvD = d * -1.55 + 0.5;

  float4 a = t1.Sample(s1, uvA);
  float4 b = t1.Sample(s1, uvB);
  float3 bT = FlareThreshold(b.xyz, cb4[49].w);
  float3 aT = FlareThreshold(a.xyz, cb4[49].z) * cb4[47].xyz;

  float4 dSample = t1.Sample(s1, uvD);
  float4 cSample = t1.Sample(s1, uvC);
  float3 cT = FlareThreshold(cSample.xyz, cb4[49].x);
  float3 dT = FlareThreshold(dSample.xyz, cb4[49].y);

  float3 acc = dT * cb4[46].xyz + aT;
  acc = bT * cb4[48].xyz + acc;
  acc = cT * cb4[45].xyz + acc;

  float4 scene = t0.Sample(s0, v2);
  o0.xyz = scene.xyz * cb4[42].x + acc;
  o0.w = scene.w * cb4[42].x;
}
