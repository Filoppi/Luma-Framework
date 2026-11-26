// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shaders 0xB8B6F982 and 0xDF8965BF (no shadows).
// Under TONEMAP_TYPE >= 1 the G-buffer reads (mapDeferred1/2/3) are clamped to [0,1] like the vanilla 8-bit UNORM targets,
// since Luma's fp16 G-buffer lets values > 1 leak in and produce negative light. TONEMAP_TYPE 0 keeps the vanilla math exact.
// 0xB8B6F982: cube light projector (implicit LOD), distance attenuation from cb2[1].
// 0xDF8965BF: 2D light projector (LOD -2, homogeneous projection, 1.0 when lp.z > 0), distance attenuation from cb2[0].

#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"
#include "Includes/DeferredLightCommon.hlsl"


cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[13];
};

cbuffer SharedLightData_cb2 : register(b2)
{
  float4 cb2[20];
};

Texture2D<float4> mapLightModel : register(t0);
#if _B8B6F982
TextureCube<float4> mapLightCubeProjectorMap0 : register(t1);
#else
Texture2D<float4> mapLightProjectorMap0 : register(t1);
#endif
Texture2D<float4> mapDeferred0 : register(t2);
Texture2D<float4> mapDeferred1 : register(t3);
Texture2D<float4> mapDeferred2 : register(t4);
Texture2D<float4> mapDeferred3 : register(t5);

SamplerState samplerLightModel : register(s0);
#if _B8B6F982
SamplerState samplerLightCubeProjectorMap0 : register(s1);
#else
SamplerState samplerLightProjectorMap0 : register(s1);
#endif
SamplerState samplerDeferred0 : register(s2);
SamplerState samplerDeferred1 : register(s3);
SamplerState samplerDeferred2 : register(s4);
SamplerState samplerDeferred3 : register(s5);

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float invW = 1.0 / v1.w;
  float2 uv = v1.xy * invW;
  float3 P = v2.xyz * invW;

  // mapDeferred1: normal (G-buffer read, clamped under TONEMAP_TYPE >= 1).
  float4 gb1 = GBUFFER_CLAMP(mapDeferred1.Sample(samplerDeferred1, uv));
  float3 N = gb1.xyz - 0.5;
  N = N + N;
  N = N * rsqrt(dot(N, N));

  // mapDeferred0: R32 depth (not clamped) -> view depth d and view-space position.
  float depthRaw = mapDeferred0.Sample(samplerDeferred0, uv).x;
  float d = 1.0 / (cb1[12].z * depthRaw - cb1[12].y);
  float3 pos = P * d;

  // Light vector to cb2[8], distance attenuation from cb2[1] (0xB8B6F982) or cb2[0] (0xDF8965BF).
  float3 Lv = cb2[8].xyz - pos;
  float lenSq = dot(Lv, Lv);
  float invLen = rsqrt(lenSq);
  float len = sqrt(lenSq);
#if _B8B6F982
  float2 att = saturate(float2(cb2[1].y * len + cb2[1].x, cb2[1].w * len + cb2[1].z));
  float atten = att.y * att.x;
  float3 attenColor = atten * cb2[3].xyz;
#else
  float2 att = saturate(float2(cb2[0].y * len + cb2[0].x, cb2[0].w * len + cb2[0].z));
  float atten = att.y * att.x;
#endif
  float3 L = Lv * invLen;

  float NdotL = dot(N, L);
  float satNdotL = saturate(NdotL);
  float3 R = -(NdotL + NdotL) * N + L;

  // View direction and specular term.
  float3 V = -pos * rsqrt(dot(pos, pos));
  float spec = saturate(dot(V, -R));

  // mapDeferred2 / mapDeferred3 (G-buffer reads, clamped under TONEMAP_TYPE >= 1).
  float4 gb2 = GBUFFER_CLAMP(mapDeferred2.Sample(samplerDeferred2, uv));
  float4 gb3 = GBUFFER_CLAMP(mapDeferred3.Sample(samplerDeferred3, uv));

  // Light model LUT: x = saturate(N.L), y = packed spec / gb2.w.
  float lutY = spec * 0.248047 + gb2.w * 0.753906;
  float4 lut = mapLightModel.Sample(samplerLightModel, float2(satNdotL, lutY));
  float S = HMADeferredSpecularPower((lut.w * 1024.0 - 1000.0) * gb3.y);
  S = S * gb3.x;
  S = S * cb2[19].x;
  float3 lightTerm = lut.xyz * gb2.xyz + S;

#if _B8B6F982
  // Light cube projector (implicit LOD). Rows use components x,y,z of cb2[15..18].
  float3 lp = pos.y * cb2[16].xyz;
  lp = pos.x * cb2[15].xyz + lp;
  lp = pos.z * cb2[17].xyz + lp;
  lp += cb2[18].xyz;
  float4 proj = mapLightCubeProjectorMap0.Sample(samplerLightCubeProjectorMap0, lp);

  float3 col = proj.rgb * attenColor;
#else
  // Light projector (2D, LOD -2). Homogeneous projection via rows cb2[11..14] and cb2[10] remap.
  float4 lp = pos.y * cb2[12];
  lp = pos.x * cb2[11] + lp;
  lp = pos.z * cb2[13] + lp;
  lp += cb2[14];
  // 1.0 when lp.z > 0, else 0.0.
  float lightMask = (0.0 < lp.z) ? 1.0 : 0.0;
  float2 puv = saturate(lp.xy / lp.w);
  puv = puv * cb2[10].xy + cb2[10].zw;
  float4 proj = mapLightProjectorMap0.SampleLevel(samplerLightProjectorMap0, puv, -2.0);

  float3 col = lightMask * cb2[3].xyz;
  col = atten * col;
  col = proj.rgb * col;
#endif

  o0.xyz = lightTerm * col;
  o0.w = 0.0;
}
