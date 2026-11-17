// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0xC986A3EC (ps_4_1, MSAA, shadow + 2D projector, eye at view origin).
// Slots (from the dump): t0 = mapLightModel (LUT, sampled at (sat(N.L), lut_v)), t1 = mapLightProjectorMap0 (2D, SampleLevel lod -2),
// t2 = mapShadowColor0BWhite (R16G16 moments: mean = .x, mean^2 = .y, SampleLevel lod 0),
// t3 = mapDeferred0 (MS depth, .x, NOT clamped), t4 = mapDeferred1 (MS normal), t5 = mapDeferred2 (MS, .xyz + .w), t6 = mapDeferred3 (MS, .x, .y, .w).
// Under TONEMAP_TYPE >= 1: each MS G-buffer read is saturated right after its Load; w divides and rsq inputs that can reach 0 are floored
// (sign-preserving for w divides). The shadow variance already has vanilla's floor (variance + 0.0004 >= 0.0004), so no extra guard is added there.
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"

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

Texture2D<float4> mapLightModel : register(t0);
Texture2D<float4> mapLightProjectorMap0 : register(t1);
Texture2D<float4> mapShadowColor0BWhite : register(t2);
Texture2DMS<float4, 4> mapDeferred0 : register(t3);
Texture2DMS<float4, 4> mapDeferred1 : register(t4);
Texture2DMS<float4, 4> mapDeferred2 : register(t5);
Texture2DMS<float4, 4> mapDeferred3 : register(t6);

SamplerState samplerLightModel : register(s0);
SamplerState samplerLightProjectorMap0 : register(s1);
SamplerState samplerShadowColor0BWhite : register(s2);

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

  // Normal (mapDeferred1, MS): clamp, remap to [-1,1], normalize.
  float3 N = GBUFFER_CLAMP(mapDeferred1.Load(pixel, sampleI)).xyz;
  N = N + float3(-0.5, -0.5, -0.5);
  N = N + N;
  N = N * rsqrt(SAFE_MIN(dot(N, N)));
  float NdotD = dot(N, cb2[9].xyz);
  float3 R = cb2[9].xyz - (NdotD + NdotD) * N;

  // View vector: normalize(-P), eye at view origin.
  float3 V = (-P) * rsqrt(SAFE_MIN(dot(P, P)));
  float RdotV = saturate(dot(V, -R));

  // G-buffers (MS): clamp right after Load.
  float4 g5 = GBUFFER_CLAMP(mapDeferred2.Load(pixel, sampleI));
  float4 g6 = GBUFFER_CLAMP(mapDeferred3.Load(pixel, sampleI));

  // LUT lookup: u = sat(N.L), v = RdotV * 0.248047 + g5.w * 0.753906.
  float lutV = dot(float2(RdotV, g5.w), float2(0.248047, 0.753906));
  float4 lut = mapLightModel.Sample(samplerLightModel, float2(saturate(NdotD), lutV));

  // Exponential term from g6.y and lut.w.
  float E = HMADeferredSpecularPower((lut.w * 1024.0 - 1000.0) * g6.y);

  float D = dot(g5.xyz, float3(0.447058, 0.882354, 0.170587));
  float3 spec2 = g5.xyz * 2.0 - D;
  float Z = saturate(-NdotD) * g6.w * (1.0 - cb2[3].w);
  float3 A = (g6.x * E + spec2 * Z) * cb2[19].x + lut.xyz * g5.xyz;

  // Light-space position: Q = P.x*cb2[15] + P.y*cb2[16] + P.z*cb2[17] + cb2[18]; L = (Q.z, Q.x, Q.y) / Q.w.
  float4 Q = P.x * cb2[15] + P.y * cb2[16] + P.z * cb2[17] + cb2[18];
  float3 L = float3(Q.z, Q.x, Q.y) / SAFE_W(Q.w);

  // Falloff from the two clamped cone/box terms.
  float3 s1 = saturate(cb2[7].xyz * L.yzx - cb2[5].xyz);
  float3 s2 = saturate(cb2[6].xyz * L.yzx - cb2[4].xyz);
  float3 S = s1 + s2;
  float falloff = 1.0 - min(sqrt(dot(S, S)), 1.0);

  // Projector lookup (t1 is not MS, so unchanged).
  float2 projUV = float2(L.y * cb2[10].x + cb2[10].z, L.z * cb2[10].y + cb2[10].w);
  float3 proj = mapLightProjectorMap0.SampleLevel(samplerLightProjectorMap0, projUV, -2.0).xyz;
  float3 lightCol = falloff * (proj * cb2[3].xyz);

  // Shadow: moments from t2 (not MS, unchanged), variance with vanilla's 0.0004 floor.
  float litZ = saturate(L.x);
  float2 moments = mapShadowColor0BWhite.SampleLevel(samplerShadowColor0BWhite, L.yz, 0.0).xy;
  float m = moments.x;
  float dd = m - litZ;
  float variance = saturate(-m * m + moments.y);
  float shadowRsq = rsqrt(min(variance + 0.0004, 1.0));
  float shadowS = saturate(dd * cb2[19].y * shadowRsq + 1.0);
  float shadowF = cb2[3].w * (1.0 - shadowS) + shadowS;

  float3 lit = shadowF * lightCol;
  o0.xyz = A * lit;
  o0.w = 0.0;
}
