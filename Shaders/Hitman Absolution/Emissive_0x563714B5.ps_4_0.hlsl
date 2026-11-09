// Luma: faithful HLSL translation of Hitman Absolution emissive material pixel shader 0x563714B5.
// Under TONEMAP_TYPE >= 1 the mapEmissive_01 sample is boosted by LumaSettings.GameSettings.EmissiveHDRBoost (cubic in luminance).
// TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

cbuffer PerObject_cb4 : register(b4)
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
Texture2D<float4> mapEmissive_01 : register(t3);
TextureCube<float4> mapTextureCube_01 : register(t4);

SamplerState samplerDetail_01 : register(s0);
SamplerState samplerTextureNormal_01 : register(s1);
SamplerState samplerDiffuse_01 : register(s2);
SamplerState samplerEmissive_01 : register(s3);
SamplerState samplerTextureCube_01 : register(s4);

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

  r0.xy = v2.xy;
  r0.z = 1.0;
  r1.x = dot(cb4[2].xyz, r0.xyz);
  r1.y = dot(cb4[3].xyz, r0.xyz);
  r0 = mapDetail_01.Sample(samplerDetail_01, r1.xy);
  r0.xy = r0.xy + float2(-0.5, -0.5);
  r0.xy = saturate(r0.xy * cb4[1].z + 0.5);
  r0.z = -r0.x * 2.0 + 1.0;
  r0.w = -r0.y * 2.0 + 1.0;
  r1 = mapTextureNormal_01.Sample(samplerTextureNormal_01, v2.xy);
  r1.xy = r1.xy + float2(-0.5, -0.5);
  r1.xy = saturate(r1.xy * cb4[4].x + 0.5);
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
  float ox = r0.x;
  r0.x = ox * cb1[8].x + r1.x;
  r0.y = ox * cb1[8].z + r1.y;
  r0.w = ox * cb1[8].y + r1.z;
  float oz = r0.z;
  r0.xyz = oz * float3(cb1[10].x, cb1[10].z, cb1[10].y) + float3(r0.x, r0.y, r0.w);
  r0 = mapTextureCube_01.Sample(samplerTextureCube_01, r0.xyz);
  r1 = mapDiffuse_01.Sample(samplerDiffuse_01, v2.xy);
  r0.xyz = r0.xyz * r1.w;
  r0.xyz = r0.xyz * cb4[9].xyz;
  r0.xyz = r0.xyz * cb1[18].xyz;
  r1 = mapEmissive_01.Sample(samplerEmissive_01, v2.xy);
#if TONEMAP_TYPE >= 1
  float eY = GetLuminance(saturate(r1.rgb));
  r1.rgb *= 1.0 + LumaSettings.GameSettings.EmissiveHDRBoost * 3.0 * eY * eY * eY;
#endif
  r1.xyz = r1.xyz * cb4[8].xyz;
  o0.xyz = r1.xyz * cb4[7].xyz + r0.xyz;
  o0.w = 1.0;
}
