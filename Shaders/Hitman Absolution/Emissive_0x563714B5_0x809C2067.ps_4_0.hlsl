// Hitman Absolution emissive material pixel shader (ps_4_0), no dither-mask discard. Both permutations share the body.
// Under TONEMAP_TYPE >= 1 the mapEmissive_01 sample is boosted by EmissiveHDRBoost (cubic in luminance); TONEMAP_TYPE 0 keeps the vanilla math.
// 0x563714B5: cb4 in PerObject_cb4, emissive on t3/s3, cube map on t4/s4; cube term tinted by cb4[9], final term cb4[7].
// 0x809C2067: cb4 in Globals_cb4, cube map on t3/s3, emissive on t4/s4; cube term tinted by cb4[7], final term v6.w.

#include "Includes/Common.hlsl"
#include "Includes/EmissiveBoost.hlsl"

#if _563714B5
cbuffer PerObject_cb4 : register(b4)
#elif _809C2067
cbuffer Globals_cb4 : register(b4)
#endif
{
  float4 cb4[10];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[19];
};

Texture2D<float4> mapDetail_01 : register(t0);
Texture2D<float4> mapTextureNormal_01 : register(t1);
Texture2D<float4> mapDiffuse_01 : register(t2);
#if _563714B5
Texture2D<float4> mapEmissive_01 : register(t3);
TextureCube<float4> mapTextureCube_01 : register(t4);
#elif _809C2067
TextureCube<float4> mapTextureCube_01 : register(t3);
Texture2D<float4> mapEmissive_01 : register(t4);
#endif

SamplerState samplerDetail_01 : register(s0);
SamplerState samplerTextureNormal_01 : register(s1);
SamplerState samplerDiffuse_01 : register(s2);
#if _563714B5
SamplerState samplerEmissive_01 : register(s3);
SamplerState samplerTextureCube_01 : register(s4);
#elif _809C2067
SamplerState samplerTextureCube_01 : register(s3);
SamplerState samplerEmissive_01 : register(s4);
#endif

void main(
  float4 v0 : SV_POSITION,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  float4 v6 : TEXCOORD6,
  float4 v7 : TEXCOORD7,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0, r1 = 0.0;
#if _563714B5
#elif _809C2067
  float3 t;
#endif

  r0.xy = v2.xy;
  r0.z = 1.0;
  r1.x = dot(cb4[2].xyz, r0.xyz);
  r1.y = dot(cb4[3].xyz, r0.xyz);
  r0 = mapDetail_01.Sample(samplerDetail_01, r1.xy);
  r0.xy = r0.xy + float2(-0.5, -0.5);
#if _563714B5
  r0.xy = saturate(r0.xy * cb4[1].z + 0.5);
#elif _809C2067
  r0.xy = saturate(r0.xy * cb4[1].z + float2(0.5, 0.5));
#endif
  r0.z = -r0.x * 2.0 + 1.0;
  r0.w = -r0.y * 2.0 + 1.0;
  r1 = mapTextureNormal_01.Sample(samplerTextureNormal_01, v2.xy);
  r1.xy = r1.xy + float2(-0.5, -0.5);
#if _563714B5
  r1.xy = saturate(r1.xy * cb4[4].x + 0.5);
#elif _809C2067
  r1.xy = saturate(r1.xy * cb4[4].x + float2(0.5, 0.5));
#endif
  r1.xy = r1.xy + float2(-0.5, -0.5);
  r0.xy = r0.zw * abs(r1.xy) + r0.xy;
  r0.xy = r1.xy + r0.xy;
  r0.xy = r0.xy + float2(-0.5, -0.5);
  r0.xy = r0.xy + r0.xy;
  r1.xyz = r0.y * v5.xyz;
  r1.xyz = v4.xyz * r0.x + r1.xyz;
  r0.x = dot(r0.xy, r0.xy);
  r0.x = min(r0.x, 1.0);
  r0.x = -r0.x + 1.0;
  r0.x = sqrt(r0.x);
  r0.xyz = v3.xyz * r0.x + r1.xyz;
  r0.w = dot(v1.xyz, r0.xyz);
  r0.w = r0.w + r0.w;
  r0.xyz = r0.xyz * -r0.w + v1.xyz;
  r1.xyz = r0.y * float3(cb1[9].x, cb1[9].z, cb1[9].y);
#if _563714B5
  float ox = r0.x;
  r0.x = ox * cb1[8].x + r1.x;
  r0.y = ox * cb1[8].z + r1.y;
  r0.w = ox * cb1[8].y + r1.z;
  float oz = r0.z;
  r0.xyz = oz * float3(cb1[10].x, cb1[10].z, cb1[10].y) + float3(r0.x, r0.y, r0.w);
#elif _809C2067
  t = r0.x * float3(cb1[8].x, cb1[8].z, cb1[8].y) + r1.xyz;
  r0.x = t.x;
  r0.y = t.y;
  r0.w = t.z;
  r0.xyz = r0.z * float3(cb1[10].x, cb1[10].z, cb1[10].y) + float3(r0.x, r0.y, r0.w);
#endif
  r0 = mapTextureCube_01.Sample(samplerTextureCube_01, r0.xyz);
  r1 = mapDiffuse_01.Sample(samplerDiffuse_01, v2.xy);
  r0.xyz = r0.xyz * r1.w;
#if _563714B5
  r0.xyz = r0.xyz * cb4[9].xyz;
#elif _809C2067
  r0.xyz = r0.xyz * cb4[7].xyz;
#endif
  r0.xyz = r0.xyz * cb1[18].xyz;
  r1 = mapEmissive_01.Sample(samplerEmissive_01, v2.xy);
  ApplyEmissiveHDRBoost(r1);
#if _563714B5
#elif _809C2067
  r1.xyz = r1.xyz * cb4[9].xyz;
#endif
  r1.xyz = r1.xyz * cb4[8].xyz;
#if _563714B5
  o0.xyz = r1.xyz * cb4[7].xyz + r0.xyz;
#elif _809C2067
  o0.xyz = r1.xyz * v6.w + r0.xyz;
#endif
  o0.w = 1.0;
}
