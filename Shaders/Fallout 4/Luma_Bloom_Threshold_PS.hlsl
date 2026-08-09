// The original shader 0x5D1B5B1A

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[18];
}

// 3Dmigoto declarations
#define cmp -

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = t1.Sample(s1_s, v1.xy).xyz;
  o0.w = dot(r0.xyz, float3(0.212500006,0.715399981,0.0720999986));

  // Original blur in y direction.
  //r0.xyzw = float4(0,0,0,0);
  //while (true) {
  //  r1.x = cmp((int)r0.w >= 15);
  //  if (r1.x != 0) break;
  //  r1.xy = cb2[r0.w+2].xy + v1.xy;
  //  r1.xyz = t0.Sample(s0_s, r1.xy).xyz;
  //  r1.xyz = -cb2[0].xxx + r1.xyz;
  //  r1.xyz = max(float3(0,0,0), r1.xyz);
  //  r1.xyz = cb2[0].yyy * r1.xyz;
  //  r0.xyz = r1.xyz * cb2[r0.w+2].zzz + r0.xyz;
  //  r0.w = (int)r0.w + 1;
  //}

  // Only apply the threshold.
  r1.xyz = t0.Sample(s0_s, v1.xy).xyz;
  r1.xyz = -cb2[0].xxx + r1.xyz;
  r1.xyz = max(float3(0,0,0), r1.xyz);
  r1.xyz = cb2[0].yyy * r1.xyz;

  o0.xyz = r1.xyz; // Originally, o0.xyz = r0.xyz;
  return;
}