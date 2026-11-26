// Luma: faithful HLSL translation of Hitman Absolution deferred light pixel shaders 0x6A01F756 and 0x9C3FA046.
// Deferred light: light model LUT lookup, AO-modulated, with specular. Under TONEMAP_TYPE >= 1 the mapDeferred1..3 G-buffer reads are clamped to [0,1] to emulate the vanilla 8-bit UNORM G-buffer; Luma's fp16 G-buffer lets values above 1 leak and produce negative light.
// TONEMAP_TYPE 0 keeps the vanilla math exact.
// 0x6A01F756: non-MSAA G-buffer read with Sample; AO read with Sample; fog density (mapFogDensity) applied to AO under TONEMAP_TYPE >= 1; includes DeferredLightCommon.hlsl.
// 0x9C3FA046: 4x MSAA G-buffer read with Load at the pixel; AO read with Load at half resolution; AO darkness applied with lerp under TONEMAP_TYPE >= 1; no fog density input.

#include "Includes/Common.hlsl"
#include "Includes/DeferredLightSpecular.hlsl"
#if _6A01F756
#include "Includes/DeferredLightCommon.hlsl"
#endif

cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[16];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[18];
};

cbuffer GlobalLightData_cb2 : register(b2)
{
  float4 cb2[8];
};

Texture2D<float4> mapLightModel : register(t0);
#if _6A01F756
Texture2D<float4> mapDeferred0 : register(t1);
Texture2D<float4> mapDeferred1 : register(t2);
Texture2D<float4> mapDeferred2 : register(t3);
Texture2D<float4> mapDeferred3 : register(t4);
Texture2D<float4> mapOcclusion : register(t5);
Texture2D<float4> mapFogDensity : register(t6); // Luma: half-res fog density copy (bound by main.cpp; .w = density), null if none
#else
Texture2DMS<float4, 4> mapDeferred0 : register(t1);
Texture2DMS<float4, 4> mapDeferred1 : register(t2);
Texture2DMS<float4, 4> mapDeferred2 : register(t3);
Texture2DMS<float4, 4> mapDeferred3 : register(t4);
Texture2D<float4> mapOcclusion : register(t5);
#endif

SamplerState samplerLightModel : register(s0);
#if _6A01F756
SamplerState samplerDeferred0 : register(s1);
SamplerState samplerDeferred1 : register(s2);
SamplerState samplerDeferred2 : register(s3);
SamplerState samplerDeferred3 : register(s4);
SamplerState samplerOcclusion : register(s5);
#endif

void main(
  float4 v0 : SV_POSITION,
  float2 v1 : TEXCOORD0,
  float3 v2 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, r4 = 0.0;

  r0 = v1.y * cb4[8];
  r0 = v1.x * cb4[7] + r0;
#if _6A01F756
  r1 = mapDeferred0.Sample(samplerDeferred0, v1.xy);
#else
  r1 = mapDeferred0.Load(int2(v0.xy), 0);
#endif
  r0 = r1.x * cb4[9] + r0;
  r0 = r0 + cb4[10];
  r0.xyz = r0.xyz / r0.w;
  r0.w = dot(r0.xyz, r0.xyz);
  r0.w = rsqrt(r0.w);
  r0.xyz = r0.w * r0.xyz;
  r0.w = dot(v2, v2);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.w * v2;
  r0.xyz = (0.0 != cb4[15].x) ? -r0.xyz : r1.xyz;

#if _6A01F756
  r1 = mapDeferred1.Sample(samplerDeferred1, v1.xy);
#else
  r1 = mapDeferred1.Load(int2(v0.xy), 0);
#endif
#if TONEMAP_TYPE >= 1
  r1 = saturate(r1);
#endif
  r1.xyz = r1.xyz - 0.5;
  r1.xyz = r1.xyz + r1.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r0.w * r1.xyz;
  r0.w = dot(r1.xyz, cb2[7].xyz);
  r2.xyz = r0.w * r1.xyz - cb2[6].xyz;
  r1.x = dot(r1.xyz, cb1[17].xyz);
  r3.x = saturate(r0.w * 0.5);
  r3.y = saturate(dot(r0.xyz, r2.xyz));

#if _6A01F756
  r0 = mapDeferred2.Sample(samplerDeferred2, v1.xy);
#else
  r0 = mapDeferred2.Load(int2(v0.xy), 0);
#endif
#if TONEMAP_TYPE >= 1
  r0 = saturate(r0);
#endif
  r3.z = saturate(r0.w);
#if _6A01F756
  r3.w = dot(r3.yz, float2(0.248047, 0.753906));
#else
  r3.w = dot(r3.yz, float2(0.248046875, 0.75390625));
#endif
  r2 = mapLightModel.Sample(samplerLightModel, r3.xw);
  r1.yzw = cb2[1].xyz - cb2[0].xyz;

#if _6A01F756
  r3 = mapOcclusion.Sample(samplerOcclusion, v1.xy);
#else
  r3 = mapOcclusion.Load(int3(uint2(float2(int2(v0.xy)) * 0.5), 0));
#endif
  r1.yzw = r3.w * r1.yzw + cb2[0].xyz;
  r4.xyz = cb2[3].xyz - cb2[2].xyz;
  r4.xyz = r3.w * r4.xyz + cb2[2].xyz;
  r1.xyz = r4.xyz * r1.x + r1.yzw;
  r4.xyz = cb2[5].xyz - cb2[4].xyz;
  r4.xyz = r3.w * r4.xyz + cb2[4].xyz;
  r3.xyz = r3.xyz * r3.xyz;
#if TONEMAP_TYPE >= 1
#if _6A01F756
  r3.xyz = ApplyFogDensityToAO(r3.xyz, LumaSettings.GameSettings.AODarkness, mapFogDensity, samplerOcclusion, v1.xy);
#else
  r3.xyz = lerp(1.0, r3.xyz, LumaSettings.GameSettings.AODarkness);
#endif
#endif
  r1.xyz = r2.xyz * r4.xyz + r1.xyz;
  r1.w = r2.w * 1024.0 - 1000.0;

#if _6A01F756
  r2 = mapDeferred3.Sample(samplerDeferred3, v1.xy);
#else
  r2 = mapDeferred3.Load(int2(v0.xy), 0);
#endif
#if TONEMAP_TYPE >= 1
  r2 = saturate(r2);
#endif
  r1.w = r1.w * r2.y;
  r1.w = HMADeferredSpecularPower(r1.w);
  r1.w = r2.x * r1.w;
  r2.xyz = r4.xyz * r1.w;
  r0.xyz = r1.xyz * r0.xyz + r2.xyz;
  o0.w = saturate(r0.w - 0.66);
  o0.xyz = r0.xyz * r3.xyz;
}
