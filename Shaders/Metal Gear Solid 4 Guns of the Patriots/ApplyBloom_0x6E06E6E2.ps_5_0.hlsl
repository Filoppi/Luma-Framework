#include "../Includes/Common.hlsl"

Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[8];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD1,
  float2 v3 : TEXCOORD2,
  float2 w3 : TEXCOORD3,
  float2 v4 : TEXCOORD4,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xyz = t1.Sample(s1_s, w2.xy).xyz;
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r0.x = saturate(dot(cb0[7].xyz, r0.xyz));
  r0.yzw = t2.Sample(s2_s, v3.xy).xyz;
  r0.yzw = r0.yzw * float3(2,2,2) + float3(-1,-1,-1);
  r0.y = saturate(dot(cb0[7].xyz, r0.yzw));
  r0.x = r0.x + r0.y;
  r0.yzw = t3.Sample(s3_s, w3.xy).xyz;
  r0.yzw = r0.yzw * float3(2,2,2) + float3(-1,-1,-1);
  r0.y = saturate(dot(cb0[7].xyz, r0.yzw));
  r0.x = r0.x + r0.y;
  r0.x = 0.333333343 * r0.x;
  r0.x = r0.x * r0.x;
  r0.yzw = t0.Sample(s0_s, v2.xy).xyz;
#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear!
  r1.x = GetLuminance(r0.yzw);
#else
  r1.x = dot(r0.yzw, float3(0.300000012,0.589999974,0.109999999));
#endif
  r1.y = -cb0[5].w + r1.x;
  r1.x = saturate(r1.y / r1.x);
  r1.xyz = r1.xxx * r0.yzw;
  r1.xyz = r1.xyz * r0.xxx;
  r0.xyz = r1.xyz * cb0[5].xxx + r0.yzw;
  o0.xyz = v1.xyz * r0.xyz;
  o0.w = 1;
}