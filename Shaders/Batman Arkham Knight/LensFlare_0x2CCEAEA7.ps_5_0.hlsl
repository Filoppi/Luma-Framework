Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[21];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[28];
}

// 3Dmigoto declarations
#define cmp -

#ifndef DISABLE_LENS_FLARE_AND_LENS_DIRT
#define DISABLE_LENS_FLARE_AND_LENS_DIRT 0
#endif

void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD5,
  float4 v4 : TEXCOORD6,
  float4 v5 : TEXCOORD8,
  out float4 o0 : SV_TARGET0,
  out float4 o1 : SV_TARGET1)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = t1.SampleBias(s1_s, v0.xy, 0).x;
  r0.xyz = v4.xyz * r0.xxx;
  r0.xyz = v4.www * r0.xyz;
  r1.xy = v3.xy / v3.ww;
  r1.xy = r1.xy * cb1[19].xy + cb1[19].wz;
  r2.xyz = t0.SampleBias(s0_s, r1.xy, 0).xyz;
  r2.xyz = r2.xyz * float3(0.5,0.5,0.5) + float3(0.5,0.5,0.5);
  r0.xyz = r2.xyz * r0.xyz;
  r0.xyz = r0.xyz * v4.xyz + cb0[23].xyz;
  r1.z = 1 + -v5.w;
  r0.w = 1;
  r0.xyzw = r1.zzzz * r0.xyzw;
  r1.z = saturate(v4.w);
  r0.xyzw = r1.zzzz * r0.xyzw;
  t2.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r1.zw = uiDest.xy;
  r1.zw = (uint2)r1.zw;
  r1.xy = r1.xy * r1.zw;
  r1.xy = (uint2)r1.xy;
  r1.zw = float2(0,0);
  r1.x = t2.Load(r1.xyz).x;
  r1.x = min(0.999999881, r1.x);
  r1.x = r1.x * cb1[20].z + -cb1[20].w;
  r1.x = 1 / r1.x;
  r1.x = cmp(v3.z < r1.x);
  r1.x = r1.x ? 1.000000 : 0;
  r1.y = -cb0[27].x * r1.x + 1;
  r1.x = cb0[27].x * r1.x;
  o1.xyzw = r1.xxxx * r0.xyzw; // What is this used for?

  #if DISABLE_LENS_FLARE_AND_LENS_DIRT
  o0 = 0.0;
  #else
  o0.xyzw = r1.yyyy * r0.xyzw;
  #endif

  return;
}