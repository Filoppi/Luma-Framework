// Luma: faithful HLSL translation of Hitman Absolution pixel shader 0x6F46E816 (ps_4_0).
// The lamp tube and lit windows are rendered by the cube-map term (t3). Under TONEMAP_TYPE >= 1
// that cube-map colour is boosted before the cb4[7] tint. TONEMAP_TYPE 0 keeps the exact vanilla math.

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
TextureCube<float4> mapTextureCube_01 : register(t3);

SamplerState samplerDetail_01 : register(s0);
SamplerState samplerTextureNormal_01 : register(s1);
SamplerState samplerDiffuse_01 : register(s2);
SamplerState samplerTextureCube_01 : register(s3);

void main(
  float4 v0 : SV_POSITION,
  float3 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD2,
  float3 v3 : TEXCOORD3,
  float3 v4 : TEXCOORD4,
  float3 v5 : TEXCOORD5,
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
#if TONEMAP_TYPE >= 1
  float cY = GetLuminance(saturate(r0.rgb));
  r0.rgb *= 1.0 + LumaSettings.GameSettings.EmissiveHDRBoost * 8.0 * smoothstep(0.2, 0.5, cY);
#endif
  r0.xyz = r0.xyz * cb4[7].xyz;
  r1 = mapDiffuse_01.Sample(samplerDiffuse_01, v2.xy);
  o0.xyz = r0.xyz * r1.w;
  o0.w = 1.0;
}
