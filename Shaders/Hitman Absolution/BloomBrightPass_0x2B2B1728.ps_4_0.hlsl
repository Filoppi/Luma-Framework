// Luma: HDR-preserving replacement for the Chinatown bloom bright pass 0x2B2B1728.
// Faithful translation of Dump/0x2B2B1728.ps_4_0.cso.
// TONEMAP_TYPE 0 keeps the vanilla math exactly. With TONEMAP_TYPE >= 1 the upper clamp on the bright pass is removed,
// and the division by alpha is guarded against non-positive values.
// Every read of the exposure constant cb4[41].z goes through GetHMAExposure (Includes/AutoExposure.hlsl).

#include "Includes/Common.hlsl"
#include "Includes/AutoExposure.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[42];
}

Texture2D<float4> t0 : register(t0); // Scene

SamplerState s0 : register(s0);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1,
  out float4 o2 : SV_TARGET2)
{
  float4 r0 = t0.Sample(s0, v2);
  o0 = r0;

  float3 sq = r0.xyz * r0.xyz;
#if TONEMAP_TYPE >= 1
  float3 divided = sq / max(r0.w, 1e-6);
#else
  float3 divided = sq / r0.w;
#endif

  // Selects sq (undivided) when alpha <= 0, like vanilla's predicated divide. Must stay a real select: the old
  // arithmetic form (sel * (sq - divided) + divided) hit inf - inf = NaN when the divide overflowed at alpha == 0.
  float3 scaled = (0.0 >= r0.w) ? sq : divided;
  scaled = scaled * GetHMAExposure(cb4[41].z);

  o1.xyz = scaled;
  o1.w = 0.0;

#if TONEMAP_TYPE >= 1
  float3 bright = max(0.0, scaled - cb4[41].x * GetHMAExposure(cb4[41].z));
#else
  float3 bright = saturate(scaled - cb4[41].x * GetHMAExposure(cb4[41].z));
#endif

  float luma = dot(bright, float3(0.298039, 0.588236, 0.113725));
  float3 chroma = bright - luma;

  o2.xyz = cb4[41].w * chroma + luma;
  o2.w = 0.0;
}
