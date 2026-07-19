// ---- Created with 3Dmigoto v1.3.16 on Wed Jul 15 00:19:25 2026
Texture2D<float4> t2 : register(t2);

Texture2D<float4> t1 : register(t1);

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}




// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD1,
  uint v3 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.x = t1.Sample(s0_s, w2.xy).x;
  r0.xyz = cb0[5].xyz * r0.xxx;
  r0.w = t0.Sample(s0_s, v2.xy).x;
  r0.xyz = r0.www * cb0[8].xyz + r0.xyz;
  r0.w = t2.Sample(s0_s, w2.xy).x;
  r0.xyz = r0.www * cb0[6].xyz + r0.xyz;
  r0.xyz = saturate(cb0[7].xyz + r0.xyz);
  o0.xyz = v1.xyz * r0.xyz;
  o0.w = v1.w;

  // Scale to scene paper white 
  if (HDR_ENABLED) {
    o0.xyz = saturate(o0.xyz); // original unorm clamp
    o0.xyz = sRGB_Decode(o0.xyz);
    // o0.xyz = pow(o0.xyz, 2.2);
    o0.xyz *= HDR_INTSCALING;
    o0.xyz = sRGB_Encode(o0.xyz);
  }
  return;
}