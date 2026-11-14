// Luma: Hitman Absolution half-res smoke density pass 0x8B9419D7.
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 the ray is clipped to the
// smoke volume's core region and the density is integrated over the inside part only, so smoke is
// still drawn where the visible surface lies outside the volume but the view ray crosses it.

#include "Includes/Common.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[94];
}

Texture2D<float4> t0 : register(t0); // Packed depth
Texture3D<float4> t1 : register(t1); // 3D noise

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

// 4 taps of the 3D noise around p (same taps and swizzle as vanilla).
float SmokeDensity(float3 p)
{
  float4 tA = t1.Sample(s1, p * 2.0 + cb4[58].xyz);
  float4 tB = t1.Sample(s1, p * 4.0 + cb4[59].xyz);
  float4 tC = t1.Sample(s1, p);
  float4 tD = t1.Sample(s1, p * 8.0 + cb4[60].xyz);
  // Vanilla swizzles the first tap (t1.yxzw): its .x lands in n.y.
  float4 n = float4(tC.x, tA.x, tB.x, tD.x);
  float dn = dot(n, cb4[56]);
  return dn * dn + cb4[55].w;
}

// Largest t such that a * (t * v) - b <= 0 holds, if the constraint bounds t. Otherwise a large value.
float BoundT(float a, float v, float b)
{
  float av = a * v;
  return (av > 1e-8) ? (b / av) : 1e30;
}

float4 main(float4 v0 : SV_POSITION, float2 v1 : TEXCOORD0, float4 v2 : TEXCOORD1, float3 v3 : TEXCOORD2) : SV_TARGET
{
  float4 o0;

  float depth = t0.Sample(s0, v1.xy).x;
  float lin = 1.0 / (cb4[8].y - cb4[8].z * depth);

  float3 wpos = lin * v2.xyz + cb4[1].xyz;
  float dens = SmokeDensity(wpos);

  float d = lin * v2.w;
  float distAlpha = 1.0 - exp2(-dens * saturate(d * cb4[54].y - cb4[54].z) * d);

  float3 q = lin * v3.xyz;
  float3 e = saturate(cb4[66].xyz * q - cb4[64].xyz) + saturate(q - cb4[65].xyz);
  float x = min(dot(e, e), 1.0) * cb4[91].x + cb4[93].x;
  float falloff = sqrt(saturate(1.0 + x)) - sqrt(saturate(1.0 + x)) * sqrt(saturate(1.0 - x));

  o0.w = falloff * distAlpha;

  // Depth packing (3 bytes of saturate(lin * 0.000333)), exactly vanilla.
  float s = saturate(lin * 0.000333);
  float a0 = floor(s * 255.999985);
  float b0 = floor(s * 65535.996094);
  float y1 = -a0 * 0.003906 + s;
  float z1 = -b0 * 0.000015 + s;
  float3 fr = frac(float3(s, y1 * 256.0, z1 * 65536.0));
  float3 pk = fr * 1.003922;
  o0.xyz = float3(pk.x - fr.y * 0.003922, pk.y - fr.z * 0.003922, pk.z);

#if TONEMAP_TYPE >= 1
  // Core region: per component, cb4[66]*q - cb4[64] <= 0 and q - cb4[65] <= 0 with q = t * v3.
  bool outsideCore = any(cb4[64].xyz < 0.0) || any(cb4[65].xyz < 0.0);
  if (!outsideCore)
  {
    float tCore = 1e30;
    tCore = min(tCore, BoundT(cb4[66].x, v3.x, cb4[64].x));
    tCore = min(tCore, BoundT(cb4[66].y, v3.y, cb4[64].y));
    tCore = min(tCore, BoundT(cb4[66].z, v3.z, cb4[64].z));
    tCore = min(tCore, BoundT(1.0, v3.x, cb4[65].x));
    tCore = min(tCore, BoundT(1.0, v3.y, cb4[65].y));
    tCore = min(tCore, BoundT(1.0, v3.z, cb4[65].z));

    float teff = min(lin, tCore);
    float3 wposE = teff * v2.xyz + cb4[1].xyz;
    float densE = SmokeDensity(wposE);
    float dE = teff * v2.w;
    float alphaE = 1.0 - exp2(-densE * saturate(dE * cb4[54].y - cb4[54].z) * dE);
    o0.w = max(o0.w, alphaE);
  }
#endif

  return o0;
}
