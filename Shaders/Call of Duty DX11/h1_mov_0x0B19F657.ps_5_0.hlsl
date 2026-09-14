// ---- Created with 3Dmigoto v1.3.16 on Fri Jun 12 22:04:13 2026
Texture2D<float4> t6 : register(t6);

Texture2D<float4> t5 : register(t5);

Texture2D<float4> t3 : register(t3);

Texture2D<float4> t2 : register(t2);

SamplerState s5_s : register(s5);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s0_s : register(s0);




// 3Dmigoto declarations
#define cmp -
#include "common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : COLOR0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.w = t6.Sample(s5_s, v1.xy).x;
  r1.y = t3.Sample(s2_s, v1.xy).x;
  r1.z = t5.Sample(s3_s, v1.xy).x;
  r1.x = t2.Sample(s0_s, v1.xy).x;
  r1.w = 1;
  r0.y = dot(float4(1,-0.714139998,-0.344139993,0.531215072), r1.xyzw);
  r0.x = dot(float3(1,1.40199995,-0.703749001), r1.xyw);
  r0.z = dot(float3(1,1.77199996,-0.889474511), r1.xzw);
  o0.xyzw = v2.xyzw * r0.xyzw;
  MovPass(o0);

  return;
}