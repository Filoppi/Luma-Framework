// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x77CF04D2 (ps_4_1, MSAA, shadow + cube projector).
// Slots: t2 = mapDeferred0 is the depth buffer (its .x feeds position reconstruction, NOT clamped).
// t3 = mapDeferred1 (normal) and t4 = mapDeferred2 (color) are MSAA G-buffer reads: saturated right after Load under TONEMAP_TYPE >= 1.
// Non-MS textures (t0 shadow map, t1 cube projector) are unchanged.
// Under TONEMAP_TYPE >= 1 the w divides and rsq inputs that can reach 0 are floored (sign-preserving for w).
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

#if TONEMAP_TYPE >= 1
#define GBUFFER_CLAMP(v) saturate(v)
#define SAFE_MIN(x) max((x), 1e-6)
#define SAFE_W(x) (((x) < 0.0) ? min((x), -1e-6) : max((x), 1e-6))
#else
#define GBUFFER_CLAMP(v) (v)
#define SAFE_MIN(x) (x)
#define SAFE_W(x) (x)
#endif

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[20];
};

Texture2D<float4> mapShadowColor0BBlack : register(t0);
TextureCube<float4> mapLightCubeProjectorMap0 : register(t1);
Texture2DMS<float4, 4> mapDeferred0 : register(t2);
Texture2DMS<float4, 4> mapDeferred1 : register(t3);
Texture2DMS<float4, 4> mapDeferred2 : register(t4);

SamplerState samplerShadowColor0BBlack : register(s0);
SamplerState samplerLightCubeProjectorMap0 : register(s1);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  uint sampleIndex : SV_SampleIndex,
  out float4 o0 : SV_TARGET0)
{
  int2 pixel = int2(v0.xy);
  int sampleI = (int)sampleIndex;

  // View ray: v2 / v1.w.
  float invW = 1.0 / SAFE_W(v1.w);
  float3 viewRay = v2 * invW;

  // Depth (mapDeferred0 .x, not clamped) and view-space position.
  float depth = mapDeferred0.Load(pixel, sampleI).x;
  float invZ = 1.0 / SAFE_W(cb1[12].z * depth - cb1[12].y);
  float3 P = viewRay * invZ;
  float3 toEye = cb2[8].xyz - P;

  // Light-space position. Cbuffer rows are used in x,z,y order, as in the dump.
  float3 B;
  B.x = P.x * cb2[15].x + P.y * cb2[16].x + P.z * cb2[17].x + cb2[18].x;
  B.y = P.x * cb2[15].z + P.y * cb2[16].z + P.z * cb2[17].z + cb2[18].z;
  B.z = P.x * cb2[15].y + P.y * cb2[16].y + P.z * cb2[17].y + cb2[18].y;

  // Shadow map lookup (t0 is not MS, so unchanged).
  float3 shadowPos = B * cb2[4].xyz;
  float2 shadowUV = shadowPos.xy / SAFE_W(shadowPos.z);
  shadowUV = shadowUV * 0.5 + 0.5;
  float2 moments = mapShadowColor0BBlack.SampleLevel(samplerShadowColor0BBlack, shadowUV, 0.0).xy;

  // Variance term: sat(m.y - m.x * m.x + 0.0004) clamped to 1, then rsq.
  float variance = saturate(-moments.x * moments.x + moments.y);
  float shadowRsq = rsqrt(SAFE_MIN(min(variance + 0.0004, 1.0)));

  // Distance from eye and normalized eye vector.
  float lenSq = dot(toEye, toEye);
  float invLen = rsqrt(SAFE_MIN(lenSq));
  float len = sqrt(lenSq);
  float3 V = toEye * invLen;

  float shadowDelta = moments.x - len * cb2[8].w;

  // Distance attenuation.
  float2 atten2 = saturate(float2(cb2[1].y * len + cb2[1].x, cb2[1].w * len + cb2[1].z));
  float3 lightColor = (atten2.y * atten2.x) * cb2[3].xyz;

  // Cube projector (t1 is not MS, so unchanged). Sampled at (B.x, B.z, B.y) as in the dump.
  float3 cube = mapLightCubeProjectorMap0.Sample(samplerLightCubeProjectorMap0, float3(B.x, B.z, B.y)).xyz;
  float3 lit = cube * lightColor;

  // Shadow factor.
  float shadowTerm = saturate(shadowDelta * cb2[19].y * shadowRsq + 1.0) - 1.0;
  float spot = saturate(cb2[0].y * dot(V, cb2[9].xyz) + cb2[0].x);
  float t = spot * shadowTerm + 1.0;
  float factor = cb2[3].w * (1.0 - t) + t;
  lit = factor * lit;

  // G-buffer normal (mapDeferred1, MS): clamp, remap to [-1,1], normalize.
  float3 n = GBUFFER_CLAMP(mapDeferred1.Load(pixel, sampleI)).xyz;
  n = n + float3(-0.5, -0.5, -0.5);
  n = n + n;
  n = n * rsqrt(SAFE_MIN(dot(n, n)));

  // G-buffer color (mapDeferred2, MS): clamp.
  float3 colorTex = GBUFFER_CLAMP(mapDeferred2.Load(pixel, sampleI)).xyz;
  lit = lit * colorTex;

  float ndotl = saturate(dot(n, V));
  o0.xyz = ndotl * lit;
  o0.w = 0.0;
}
