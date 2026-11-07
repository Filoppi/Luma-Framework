// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x763EC19C (ps_4_1, MSAA, per-sample).
// Texture2DMS t1 is the depth buffer (.x), which feeds the view-position reconstruction. It is NOT clamped.
// Under TONEMAP_TYPE >= 1 the other MSAA G-buffer reads (t2 normal, t3 color/spec, t4 extra) are saturated right after Load,
// and the rsq/div inputs are guarded (sign-preserving floor of 1e-6) so zero vectors or degenerate depth cannot produce inf/NaN.
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

#if TONEMAP_TYPE >= 1
#define GBUFFER_CLAMP(v) saturate(v)
#define SAFE_MIN(x) max((x), 1e-6)
#else
#define GBUFFER_CLAMP(v) (v)
#define SAFE_MIN(x) (x)
#endif

// Sign-preserving guard for a value used as a divisor (w divide, depth reconstruction denominator).
float SafeDenom(float x)
{
#if TONEMAP_TYPE >= 1
  return (x >= 0.0) ? max(x, 1e-6) : min(x, -1e-6);
#else
  return x;
#endif
}

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[20];
};

Texture2D<float4> mapLightModel : register(t0);
Texture2DMS<float4, 4> mapDepth : register(t1);
Texture2DMS<float4, 4> mapDeferred1 : register(t2);
Texture2DMS<float4, 4> mapDeferred2 : register(t3);
Texture2DMS<float4, 4> mapDeferred3 : register(t4);

SamplerState samplerLightModel : register(s0);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  uint sampleIndex : SV_SampleIndex,
  out float4 o0 : SV_TARGET0)
{
  int2 pixel = int2(v0.xy);

  // View ray scaled by 1 / w (w divide guarded).
  float invW = 1.0 / SafeDenom(v1.w);
  float3 ray = v2 * invW;

  // Depth (t1.x, not clamped). Reconstruction: P = ray / (depth * cb1[12].z - cb1[12].y).
  float depth = mapDepth.Load(pixel, sampleIndex).x;
  float invDepthDen = 1.0 / SafeDenom(depth * cb1[12].z - cb1[12].y);
  float3 pos = ray * invDepthDen;

  // Closest point on the light axis (clamped to the light extent) to the position.
  float3 axis = cb2[4].xyz;
  float axisDist = dot(pos - cb2[8].xyz, axis);
  axisDist = max(axisDist, -cb2[4].w);
  axisDist = min(axisDist, cb2[4].w);
  float3 closest = axis * axisDist + cb2[8].xyz;

  // Direction to the closest point, normalized (rsq input guarded).
  float3 D = closest - pos;
  float distSq = dot(D, D);
  float dist = sqrt(distSq);
  float3 L = D * rsqrt(SAFE_MIN(distSq));

  // Attenuation.
  float2 att = saturate(float2(cb2[0].y, cb2[0].w) * dist + float2(cb2[0].x, cb2[0].z));
  float atten = att.y * att.x;
  float3 lightColor = atten * cb2[3].xyz;

  // Normal (t2), remapped from [0,1] to [-1,1] and normalized.
  float4 normalTex = GBUFFER_CLAMP(mapDeferred1.Load(pixel, sampleIndex));
  float3 nRaw = normalTex.xyz + float3(-0.5, -0.5, -0.5);
  nRaw = nRaw + nRaw;
  float3 N = nRaw * rsqrt(SAFE_MIN(dot(nRaw, nRaw)));

  float NdotL = dot(N, L);
  float NdotL2 = NdotL + NdotL;
  float ndotlSat = saturate(NdotL);
  float3 R = L - N * NdotL2;

  // View direction (rsq input guarded).
  float3 V = -pos * rsqrt(SAFE_MIN(dot(pos, pos)));
  float specTerm = saturate(dot(V, -R));

  // Spec/material (t3) and extra (t4).
  float4 specTex = GBUFFER_CLAMP(mapDeferred2.Load(pixel, sampleIndex));
  float4 extraTex = GBUFFER_CLAMP(mapDeferred3.Load(pixel, sampleIndex));

  // LUT lookup.
  float2 lutUV;
  lutUV.x = ndotlSat;
  lutUV.y = specTerm * 0.248047 + specTex.w * 0.753906;
  float4 lut = mapLightModel.Sample(samplerLightModel, lutUV);

  float expo = lut.w * 1024.0 - 1000.0;
  expo = expo * extraTex.y;
  float p = exp2(expo);
  p = p * extraTex.x;
  p = p * cb2[19].x;

  float3 spec = lut.xyz * specTex.xyz + p;
  float3 result = lightColor * spec;

  o0 = float4(result, 0.0);
}
