// ---- Created with 3Dmigoto v1.3.16 on Sat Jun 20 23:08:37 2026
Texture2D<float4> t4 : register(t4);

SamplerState s4_s : register(s4);

cbuffer cb4 : register(b4)
{
  float4 cb4[68];
}

cbuffer cb2 : register(b2)
{
  float4 cb2[33];
}

// 3Dmigoto declarations
#define cmp -
#include "common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xy = max(cb2[32].xy, v1.xy);
  r0.xy = min(cb2[32].zw, r0.xy);
  r0.xyz = t4.Sample(s4_s, r0.xy).xyz;
  
  // r1.xyz = cmp(cb4[0].xxx >= r0.xyz);
  // r2.xyz = r1.xyz ? float3(0,0,0) : float3(1,1,1);
  // r1.xyz = r1.xyz ? float3(1,1,1) : 0;
  // r3.xyz = cb4[1].xxx * r2.xyz;
  // r3.xyz = r1.xyz * cb4[2].xxx + r3.xyz;
  // r4.xyz = cb4[1].zzz * r2.xyz;
  // r4.xyz = r1.xyz * cb4[2].zzz + r4.xyz;
  // r3.xyz = r0.xyz * r3.xyz + r4.xyz;
  // r4.xyz = cb4[1].yyy * r2.xyz;
  // r2.xyz = cb4[1].www * r2.xyz;
  // r2.xyz = r1.xyz * cb4[2].www + r2.xyz;
  // r1.xyz = r1.xyz * cb4[2].yyy + r4.xyz;
  // r0.xyz = r0.xyz * r1.xyz + r2.xyz;
  // r0.xyz = saturate(r3.xyz / r0.xyz);

//   // Setup
//   float3 colorU, colorT;
// 
//   // SDR
//   float3 lower = MobiusRolloff(x, cb4[2]); //when thres = \inf
//   float3 upper = MobiusRolloff(x, cb4[1]); //when thres = 0
//   colorT = x < cb4[0].x ? lower : upper; //piecewise point/threshold
//   colorT = ClampByMaxChannel(colorT, 1); //clean
//   colorT = max(colorT, 0); //clean
//   // colorT = saturate(colorT); //clean
//   float colorTMax = max(colorT.x, max(colorT.y, colorT.z));
// 
//   // HDR
//   float4 c = cb4[0].x > 0 ? cb4[2] : cb4[1]; //use lower unless threshold is 0
//   float slope_at_piecewise = MobiusRolloffDerivative(cb4[0].x, c);
//   float output_at_piecewise = MobiusRolloff(cb4[0].x, c);
//   float output_at_piecewise_safe = max(output_at_piecewise, 0.0001);
//   float3 lower_hdr = colorT; //lower from SDR
//   float3 upper_hdr = slope_at_piecewise * (x - cb4[0].x) + output_at_piecewise; //mx + b
//   colorU = x < cb4[0].x ? lower_hdr : upper_hdr;
//   colorU = max(colorU, 0); //clean
// 
//   // HDR out
//   o0.w = GetLuminance(colorU, CS_BT709);
// 
//   // HDR Blowout
//   {
//     // Forced blowout
//     colorU = Rolloff_HDRRolloff(colorU, output_at_piecewise_safe); 
// 
//     // 100% steal
//     float colorTy = GetLuminance(colorT, CS_BT709);
//     float colorUy = GetLuminance(colorU, CS_BT709);
//     // colorU *= safeDivision(colorTy, colorUy, 1); //DISABLED! We want untonemapped color here!
//     colorT = colorU;
//     
//     // // UCS Steal //TODO: remove
//     // colorU = UCS_ToUCS(colorU);
//     // colorT = UCS_ToUCS(colorT);
//     // colorT = RestoreHueAndChrominanceUcs(colorT, colorU, 0.45, 0.8, 0); 
//     // colorT = UCS_FromUCS(colorT); //and colorU is unused after this
//   }
//   r0.xyz = colorT;
  RolloffResult r = Rolloff_Complete(r0.xyz, cb4[0].x, cb4[1], cb4[2]);
  r0.xyz = r.color;
  o0.w = r.y;

  r0.xyz = pow(r0.xyz, cb4[3].z);

  o0.x = dot(r0.xyz, cb4[65].xyz);
  o0.y = dot(r0.xyz, cb4[66].xyz);
  o0.z = dot(r0.xyz, cb4[67].xyz);
  o0.xyz = max(o0.xyz, 0); //clean

  return;
}