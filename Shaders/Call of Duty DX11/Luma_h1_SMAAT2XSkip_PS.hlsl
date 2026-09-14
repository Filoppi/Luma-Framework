// ---- Created with 3Dmigoto v1.3.16 on Wed Jun 17 21:09:44 2026
Texture2D<float4> t7 : register(t7); //motion vectors

Texture2D<float4> t6 : register(t6); //edges

Texture2D<float4> t5 : register(t5); //prev prev?

Texture2D<float4> t3 : register(t3); //prev

Texture2D<float4> t2 : register(t2); //curr

SamplerState s6_s : register(s6);

SamplerState s5_s : register(s5);

SamplerState s3_s : register(s3);

SamplerState s2_s : register(s2);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[35];
}

// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float3 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  // blit
  r2.xyz = t2.SampleLevel(s0_s, v1.xy, 0).xyz;
  o0 = r2.xyz;
  o1 = 0;

  return;
}