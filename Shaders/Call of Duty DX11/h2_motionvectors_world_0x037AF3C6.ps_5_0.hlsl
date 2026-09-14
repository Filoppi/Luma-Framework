// ---- Created with 3Dmigoto v1.3.16 on Fri Jun 19 01:51:22 2026
Texture2D<float4> t2 : register(t2);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[28];
}




// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.zw = float2(0,0);

  r0.z = t2.SampleLevel(s0_s, v1.xy, 0).x;

  // motion vectors for viewmodel is just the world behind it but 1000x, unusable...
  if (r0.z >= 0.9) {
    o0.xy = float2(0,0);
    return;
  }

  r0.xy = v1.xy;
  r0.w = 1;
  r1.x = dot(r0.xyzw, cb2[6].xyzw);
  r1.y = dot(r0.xyzw, cb2[7].xyzw);
  r0.x = dot(r0.xyzw, cb2[8].xyzw);
  r0.xy = r1.xy / r0.xx;
  r0.xy = v1.xy + -r0.xy;
  r0.xy = cb2[27].xy * r0.xy;

  r0.zw = float2(0.25,0.25) * abs(r0.xy);
  r0.zw = min(float2(1,1), r0.zw);
  r1.xy = float2(-4,-4) + abs(r0.xy);
  r0.xy = cmp(r0.xy >= float2(0,0));
  r1.xy = saturate(float2(0.027777778,0.027777778) * r1.xy);
  r1.xy = sqrt(r1.xy);
  r0.zw = -r1.xy + r0.zw;
  r0.zw = r0.zw * float2(0.5,0.5) + r1.xy;
  r0.xy = r0.xy ? r0.zw : -r0.zw;

  o0.xy = r0.xy;
  return;
}