// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shader 0x7BB100E4 (ps_4_1, MSAA, LUT + cube projector).
// Slots: t2 = mapDeferred0 is the depth buffer (its .x feeds position reconstruction, NOT clamped).
// t3 = mapDeferred1 (normal), t4 = mapDeferred2 (color/material, .xyzw) and t5 = mapDeferred3 (.xy) are MSAA G-buffer reads:
// saturated right after Load under TONEMAP_TYPE >= 1. Non-MS textures (t0 LUT, t1 cube projector) are unchanged.
// Under TONEMAP_TYPE >= 1 the w divides and rsq inputs that can reach 0 are floored (sign-preserving for w).
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
TextureCube<float4> mapLightCubeProjectorMap0 : register(t1);
Texture2DMS<float4, 4> mapDeferred0 : register(t2);
Texture2DMS<float4, 4> mapDeferred1 : register(t3);
Texture2DMS<float4, 4> mapDeferred2 : register(t4);
Texture2DMS<float4, 4> mapDeferred3 : register(t5);

SamplerState samplerLightModel : register(s0);
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

  // G-buffer normal (mapDeferred1, MS): clamp, remap to [-1,1], normalize.
  float3 n = GBUFFER_CLAMP(mapDeferred1.Load(pixel, sampleI)).xyz;
  n = n + float3(-0.5, -0.5, -0.5);
  n = n + n;
  n = n * rsqrt(SAFE_MIN(dot(n, n)));

  // View ray: v2 / v1.w.
  float invW = 1.0 / SAFE_W(v1.w);
  float3 viewRay = invW * v2;

  // Depth (mapDeferred0 .x, not clamped) and view-space position.
  float depth = mapDeferred0.Load(pixel, sampleI).x;
  float invZ = 1.0 / SAFE_W(cb1[12].z * depth - cb1[12].y);
  float3 P = viewRay * invZ;
  float3 toEye = cb2[8].xyz - P;

  // Distance and normalized eye vector.
  float lenSq = dot(toEye, toEye);
  float invLen = rsqrt(SAFE_MIN(lenSq));
  float len = sqrt(lenSq);
  float3 V = toEye * invLen;

  // Distance attenuation.
  float2 atten2 = saturate(float2(cb2[1].y * len + cb2[1].x, cb2[1].w * len + cb2[1].z));
  float3 lightColor = (atten2.y * atten2.x) * cb2[3].xyz;

  // Reflection and light cone.
  float ndotv = dot(n, V);
  float ndotvX2 = ndotv + ndotv;
  float ndotvSat = saturate(ndotv);
  float3 R = V - ndotvX2 * n;

  float pLenSq = dot(-P, -P);
  float invPLen = rsqrt(SAFE_MIN(pLenSq));
  float3 Pn = -P * invPLen;
  float lightCone = saturate(dot(Pn, -R));

  // G-buffer color/material (mapDeferred2, MS): clamp.
  float4 gb2 = GBUFFER_CLAMP(mapDeferred2.Load(pixel, sampleI));

  // LUT lookup (t0 is not MS, so unchanged).
  float lutV = dot(float2(lightCone, gb2.w), float2(0.248047, 0.753906));
  float4 lut = mapLightModel.Sample(samplerLightModel, float2(ndotvSat, lutV));

  // Specular/material term from mapDeferred3 (MS, .xy): clamp.
  float2 specTex = GBUFFER_CLAMP(mapDeferred3.Load(pixel, sampleI).xy);
  float r0z = lut.w * 1024.0 - 1000.0;
  float falloff = HMADeferredSpecularPower(r0z * specTex.y) * specTex.x * cb2[19].x;
  float3 lit = lut.xyz * gb2.xyz + falloff;

  // Light-space position and cube projector (t1 is not MS, so unchanged).
  float3 B = P.x * cb2[15].xyz + P.y * cb2[16].xyz + P.z * cb2[17].xyz + cb2[18].xyz;
  float3 cube = mapLightCubeProjectorMap0.Sample(samplerLightCubeProjectorMap0, B).xyz;

  lit = lit * (cube * lightColor);
  o0.xyz = lit;
  o0.w = 0.0;
}
