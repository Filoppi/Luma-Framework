// Luma: faithful HLSL translation of Hitman Absolution pixel shader 0xD8080EE4 (ps_4_0).
// Sphere-map reflection material. Under TONEMAP_TYPE >= 1 the final colour gets an HDR soft cap on its
// max channel: vanilla's HDR sphere-map (rgb * 2^(16 * a * cb4[10].x)) reached ~6.6x paper white on guns
// and chrome, and vanilla clipped it in the tonemap. TONEMAP_TYPE 0 keeps the exact vanilla math.

#include "Includes/Common.hlsl"

cbuffer PerObject_cb4 : register(b4)
{
  float4 cb4[12];
};

cbuffer SharedPerViewData_cb1 : register(b1)
{
  float4 cb1[19];
};

Texture2D<float4> mapTextureNormal_01 : register(t0);
Texture2D<float4> mapSpecular_01 : register(t1);
Texture2D<float4> mapDiffuse_01 : register(t2);
Texture2D<float4> mapTextureLOD_01 : register(t3);

SamplerState samplerTextureNormal_01 : register(s0);
SamplerState samplerSpecular_01 : register(s1);
SamplerState samplerDiffuse_01 : register(s2);
SamplerState samplerTextureLOD_01 : register(s3);

void main(
  float4 v0 : SV_POSITION,
  float3 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  float3 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  float3 v5 : TEXCOORD4,
  float v6 : TEXCOORD5,
  out float4 o0 : SV_TARGET0)
{
  float4 r0 = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0;

  r0 = mapDiffuse_01.Sample(samplerDiffuse_01, v2.xy);
  r0.w = r0.w + cb4[5].z;
  r0.xyz = r0.xyz + float3(-1.0, -1.0, -1.0);
  r0.xyz = cb4[9].xyz * r0.xyz + float3(1.0, 1.0, 1.0);
  if (r0.w < 0.0) discard;

  r1 = mapTextureNormal_01.Sample(samplerTextureNormal_01, v2.xy);
  r1.xy = r1.xy + float2(-0.5, -0.5);
  r1.xy = saturate(r1.xy * cb4[0].z + 0.5);
  r1.xy = r1.xy + float2(-0.5, -0.5);
  r1.xy = r1.xy + r1.xy;
  r2.xyz = r1.y * v5.xyz;
  r2.xyz = v4.xyz * r1.x + r2.xyz;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = min(r0.w, 1.0);
  r0.w = 1.0 - r0.w;
  r0.w = sqrt(r0.w);
  r1.xyz = v3.xyz * r0.w + r2.xyz;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r2.xyz = r0.w * r1.xyz;
  r1.xy = r1.xy * float2(-0.5, -0.5) + float2(0.5, 0.5);
  r0.w = dot(-v1.xyz, -v1.xyz);
  r0.w = rsqrt(r0.w);
  r3.xyz = r0.w * -v1.xyz;
  r0.w = saturate(dot(r3.xyz, r2.xyz));
  r0.w = 1.0 - r0.w;
  r0.w = log2(r0.w);
  r0.w = r0.w * cb4[11].x;
  r0.w = exp2(r0.w);
  r1.z = cb4[11].z - cb4[11].y;
  r0.w = r0.w * r1.z + cb4[11].y;

  r2 = mapSpecular_01.Sample(samplerSpecular_01, v2.xy);
  r1.z = 1.0 - r2.x;
  r2.xyz = r2.xyz * r2.xyz;
  r1.z = log2(r1.z);
  r1.z = r1.z * cb4[8].x;
  r1.z = exp2(r1.z);
  r1.z = r1.z * cb4[7].w + cb4[8].y;
  r1 = mapTextureLOD_01.SampleLevel(samplerTextureLOD_01, r1.xy, r1.z);
  r1.xyz = r1.xyz * cb4[7].xyz;
  r1.w = r1.w * cb4[10].x;
  r1.w = r1.w * 16.0;
  r1.w = exp2(r1.w);
  r0.xyz = r0.xyz * r1.xyz;
  r0.xyz = r0.xyz * cb4[6].xyz;
  r0.xyz = r1.w * r0.xyz;
  r0.xyz = r0.w * r0.xyz;
  r0.w = 1.0 - v6.x;
  r0.xyz = r0.xyz * r0.w;
  r0.xyz = r0.xyz * r2.xyz;
  o0.xyz = r0.xyz * cb1[18].xyz;
  o0.w = 1.0;

#if TONEMAP_TYPE >= 1
  // HDR soft cap on the max channel: identity up to the knee (0.5), asymptotes to maxValue (1.5): below the scene's other highlights so the adaptive display mapping doesn't peak on the gun.
  float m = max(o0.r, max(o0.g, o0.b));
  const float knee = 0.5;
  const float maxValue = 1.5;
  if (m > knee)
  {
    float e = m - knee;
    o0.xyz *= (knee + e / (1.0 + e / (maxValue - knee))) / m;
  }
#endif
}
