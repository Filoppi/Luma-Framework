// Hitman Absolution emissive material pixel shader (ps_4_0), dither-masked (discard). Both permutations share the body.
// Under TONEMAP_TYPE >= 1 the mapEmissive_01 sample is boosted by EmissiveHDRBoost (cubic in luminance); TONEMAP_TYPE 0 keeps the vanilla math.
// 0x4FF00542: cube map on t4/s4, emissive on t5/s5; cube term tinted by cb4[7], emissive by cb4[9] then cb4[8], final term v6.w.
// 0x6B5CB287: emissive on t4/s4, cube map on t5/s5; cube term tinted by cb4[9], emissive by cb4[8], final term cb4[7].

#include "Includes/Common.hlsl"
#include "Includes/EmissiveBoost.hlsl"
#if _4FF00542
cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[10];
};
#elif _6B5CB287
#endif

cbuffer PerObject_cb0 : register(b0)
{
  float4 cb0[8];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[20];
};

#if _4FF00542
#elif _6B5CB287
cbuffer Globals_cb4 : register(b4)
{
  float4 cb4[10];
};
#endif
Texture3D<float4> mapDitherMask : register(t0);
Texture2D<float4> mapDetail_01 : register(t1);
Texture2D<float4> mapTextureNormal_01 : register(t2);
Texture2D<float4> mapDiffuse_01 : register(t3);
#if _4FF00542
TextureCube<float4> mapTextureCube_01 : register(t4);
Texture2D<float4> mapEmissive_01 : register(t5);
#elif _6B5CB287
Texture2D<float4> mapEmissive_01 : register(t4);
TextureCube<float4> mapTextureCube_01 : register(t5);
#endif

SamplerState samplerDitherMask : register(s0);
SamplerState samplerDetail_01 : register(s1);
SamplerState samplerTextureNormal_01 : register(s2);
SamplerState samplerDiffuse_01 : register(s3);
#if _4FF00542
SamplerState samplerTextureCube_01 : register(s4);
SamplerState samplerEmissive_01 : register(s5);
#elif _6B5CB287
SamplerState samplerEmissive_01 : register(s4);
SamplerState samplerTextureCube_01 : register(s5);
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
#if _4FF00542
  float3 t;
#elif _6B5CB287
#endif

#if _4FF00542
  r0.xyz = v1.y * float3(cb1[5].x, cb1[5].y, cb1[5].w);
  r0.xyz = v1.x * float3(cb1[4].x, cb1[4].y, cb1[4].w) + r0.xyz;
  r0.xyz = v1.z * float3(cb1[6].x, cb1[6].y, cb1[6].w) + r0.xyz;
  r0.xyz = v1.w * float3(cb1[7].x, cb1[7].y, cb1[7].w) + r0.xyz;
#elif _6B5CB287
  r0.xyz = v1.y * cb1[5].xyw;
  r0.xyz = v1.x * cb1[4].xyw + r0.xyz;
  r0.xyz = v1.z * cb1[6].xyw + r0.xyz;
  r0.xyz = v1.w * cb1[7].xyw + r0.xyz;
#endif
  r0.xy = r0.xy / r0.z;
#if _4FF00542
  r0.xy = r0.xy * float2(cb1[19].z, cb1[19].w);
#elif _6B5CB287
  r0.xy = r0.xy * cb1[19].zw;
#endif
  r0.xy = r0.xy * 0.25;
  r0.z = cb0[7].y;
  r0 = mapDitherMask.Sample(samplerDitherMask, r0.xyz);
#if _4FF00542
  if (r0.x + -0.999 < 0.0)
#elif _6B5CB287
  r0.x = r0.x - 0.999;
  if (r0.x < 0.0)
#endif
    discard;
  r0.xy = v2.xy;
  r0.z = 1.0;
  r1.x = dot(cb4[2].xyz, r0.xyz);
  r1.y = dot(cb4[3].xyz, r0.xyz);
  r0 = mapDetail_01.Sample(samplerDetail_01, r1.xy);
  r0.xy = r0.xy + float2(-0.5, -0.5);
#if _4FF00542
  r0.xy = saturate(r0.xy * cb4[1].z + float2(0.5, 0.5));
#elif _6B5CB287
  r0.xy = saturate(r0.xy * cb4[1].z + 0.5);
#endif
  r0.z = -r0.x * 2.0 + 1.0;
  r0.w = -r0.y * 2.0 + 1.0;
  r1 = mapTextureNormal_01.Sample(samplerTextureNormal_01, v2.xy);
  r1.xy = r1.xy + float2(-0.5, -0.5);
#if _4FF00542
  r1.xy = saturate(r1.xy * cb4[4].x + float2(0.5, 0.5));
#elif _6B5CB287
  r1.xy = saturate(r1.xy * cb4[4].x + 0.5);
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
#if _4FF00542
  t = r0.x * float3(cb1[8].x, cb1[8].z, cb1[8].y) + r1.xyz;
  r0.x = t.x;
  r0.y = t.y;
  r0.w = t.z;
  r0.xyz = r0.z * float3(cb1[10].x, cb1[10].z, cb1[10].y) + float3(r0.x, r0.y, r0.w);
#elif _6B5CB287
  float ox = r0.x;
  r0.x = ox * cb1[8].x + r1.x;
  r0.y = ox * cb1[8].z + r1.y;
  r0.w = ox * cb1[8].y + r1.z;
  float oz = r0.z;
  r0.xyz = oz * float3(cb1[10].x, cb1[10].z, cb1[10].y) + float3(r0.x, r0.y, r0.w);
#endif
  r0 = mapTextureCube_01.Sample(samplerTextureCube_01, r0.xyz);
  r1 = mapDiffuse_01.Sample(samplerDiffuse_01, v2.xy);
  r0.xyz = r0.xyz * r1.w;
#if _4FF00542
  r0.xyz = r0.xyz * cb4[7].xyz;
#elif _6B5CB287
  r0.xyz = r0.xyz * cb4[9].xyz;
#endif
  r0.xyz = r0.xyz * cb1[18].xyz;
  r1 = mapEmissive_01.Sample(samplerEmissive_01, v2.xy);
  ApplyEmissiveHDRBoost(r1);
#if _4FF00542
  r1.xyz = r1.xyz * cb4[9].xyz;
#elif _6B5CB287
#endif
  r1.xyz = r1.xyz * cb4[8].xyz;
#if _4FF00542
  o0.xyz = r1.xyz * v6.w + r0.xyz;
#elif _6B5CB287
  o0.xyz = r1.xyz * cb4[7].xyz + r0.xyz;
#endif
  o0.w = 1.0;
}
