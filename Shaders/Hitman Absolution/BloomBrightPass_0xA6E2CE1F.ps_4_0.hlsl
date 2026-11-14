// Luma: HDR-preserving replacement for the bloom bright pass 0xA6E2CE1F.
// TONEMAP_TYPE 0 keeps the vanilla math exactly. With TONEMAP_TYPE >= 1 the upper clamp on the bright pass is removed,
// and the division by alpha is guarded against non-positive values.

#include "Includes/Common.hlsl"
#include "Includes/AutoExposure.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[42];
}

Texture2D<float4> t0 : register(t0); // Scene
Texture2D<float4> t1 : register(t1); // Bloom source

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1,
  out float4 o2 : SV_TARGET2,
  out float4 o3 : SV_TARGET3)
{
  float exposure = GetHMAExposure(cb4[41].z);
  float4 r0 = t0.Sample(s0, v2);
  float4 r1 = t1.Sample(s1, v2);

  float sum = r1.x;
  for (int i = 0; i < 4; i++)
  {
    float2 offsetUV = v2 + cb4[i + 9].xy;
    sum += t1.Sample(s1, offsetUV).x;
  }

  float3 sq = r0.xyz * r0.xyz;
#if TONEMAP_TYPE >= 1
  float3 divided = sq / max(r0.w, 1e-6);
#else
  float3 divided = sq / r0.w;
#endif

  // Selects sq (undivided) when alpha <= 0, like vanilla's predicated divide. Must stay a real select: the old
  // arithmetic form (sel * (sq - divided) + divided) hit inf - inf = NaN when the divide overflowed at alpha == 0.
  float3 r2 = (0.0 >= r0.w) ? sq : divided;
  r2 = r2 * exposure;

#if TONEMAP_TYPE >= 1
  float3 bright = max(0.0, r2 - cb4[41].x * exposure);
#else
  float3 bright = saturate(r2 - cb4[41].x * exposure);
#endif

  float luma = dot(bright, float3(0.298039, 0.588236, 0.113725));
  float3 chroma = bright - luma;

  o0 = r0;
  o1.x = sum * 0.2;
  o1.yzw = r1.yzw;
  o2.xyz = r2;
  o2.w = r1.w;
  o3.xyz = cb4[41].w * chroma + luma;
  o3.w = 0.0;
}
