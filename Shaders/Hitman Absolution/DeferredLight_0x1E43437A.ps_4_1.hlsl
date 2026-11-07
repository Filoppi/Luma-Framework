#include "Includes/Common.hlsl"

cbuffer SharedPerViewData : register(b1) { float4 cb1[18]; };
cbuffer SharedLightData : register(b2) { float4 cb2[8]; };
cbuffer GlobalsData : register(b4) { float4 cb4[16]; };
Texture2D<float4> t0 : register(t0);
Texture2DMS<float4> t1 : register(t1);
Texture2DMS<float4> t2 : register(t2);
Texture2DMS<float4> t3 : register(t3);
Texture2DMS<float4> t4 : register(t4);
Texture2D<float4> t5 : register(t5);
SamplerState s0 : register(s0);

void main(float4 v0 : SV_POSITION, float2 v1 : TEXCOORD0, float3 v2 : TEXCOORD1, uint sampleIndex : SV_SampleIndex, out float4 o0 : SV_TARGET0)
{
  int2 pixel = int2(v0.xy);
  int sampleI = int(sampleIndex);

  float3 rayN = v2 * rsqrt(dot(v2, v2));
  bool flipRay = cb4[15].x != 0.0;

  float depth = t1.Load(pixel, sampleI).x;
  float4 p = v1.y * cb4[8];
  p = v1.x * cb4[7] + p;
  p = depth * cb4[9] + p;
  p = p + cb4[10];
#if TONEMAP_TYPE >= 1
  float w = (p.w < 0.0) ? min(p.w, -1e-6) : max(p.w, 1e-6);
  float3 posView = p.xyz / w;
#else
  float3 posView = p.xyz / p.w;
#endif
  float posLenSq = dot(posView, posView);
#if TONEMAP_TYPE >= 1
  posLenSq = max(posLenSq, 1e-6);
#endif
  float3 posDir = posView * rsqrt(posLenSq);
  float3 viewDir = flipRay ? -posDir : rayN;

  float3 n = t2.Load(pixel, sampleI).xyz;
#if TONEMAP_TYPE >= 1
  n = saturate(n);
#endif
  n = n - 0.5;
  n = n + n;
  float nLenSq = dot(n, n);
#if TONEMAP_TYPE >= 1
  nLenSq = max(nLenSq, 1e-6);
#endif
  n = n * rsqrt(nLenSq);
  float ndotl = dot(n, cb2[7].xyz);
  float3 reflectedDir = ndotl * n - cb2[6].xyz;
  float nDotUp = dot(n, cb1[17].xyz);
  float3 light = nDotUp * cb2[2].xyz + cb2[0].xyz;

  float4 color = t3.Load(pixel, sampleI);
#if TONEMAP_TYPE >= 1
  color = saturate(color);
#endif
  float spec = saturate(dot(viewDir, reflectedDir));
  float lutY = dot(float2(spec, saturate(color.w)), float2(0.248047, 0.753906));
  float4 lut = t0.Sample(s0, float2(saturate(ndotl * 0.5), lutY));
  light = lut.xyz * cb2[4].xyz + light;

  float2 specIn = t4.Load(pixel, sampleI).xy;
#if TONEMAP_TYPE >= 1
  specIn = saturate(specIn);
#endif
  float specPow = exp2((lut.w * 1024.0 - 1000.0) * specIn.y);
  uint4 occCoord = uint4(float4(pixel, 0.0, 0.0) * 0.5);
  float3 occ = t5.Load(int3(occCoord.xyz)).xyz;
  occ = occ * occ;
  float specTerm = specIn.x * specPow;
  float3 lit = light * color.xyz + specTerm * cb2[4].xyz;
  o0.w = saturate(color.w - 0.66);
  o0.xyz = lit * occ;
}
