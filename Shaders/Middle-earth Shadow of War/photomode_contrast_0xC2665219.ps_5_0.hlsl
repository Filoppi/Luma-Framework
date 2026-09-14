// ---- Created with 3Dmigoto v1.3.16 on Thu Jul 16 19:31:42 2026
Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb2 : register(b2)
{
  float4 cb2[53];
}

cbuffer cb3 : register(b3)
{
  float4 cb3[1];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[26];
}




// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"
#include "../../Includes/Math.hlsl"

float3 RenoDX_Contrast(float3 x, float contrast, float mid_gray = 0.18f) {
  return pow(max(0, x / mid_gray), contrast) * mid_gray;
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  uint v3 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  // r0.x = 1.5 + -cb3[0].x;
  // r0.x = 1 / r0.x;
  // r0.y = cmp(0.5 >= cb3[0].x);
  // r0.z = 0.5 + cb3[0].x;
  // r0.x = r0.y ? r0.z : r0.x;

  r0.yz = min(cb2[22].zw, v1.zw);
  r0.yzw = t0.Sample(s0_s, r0.yz).xyz;

//   r0.yzw = saturate(r0.yzw / cb0[25].xxx); //establishes threshold, default 6.25
//   r1.xyz = -r0.yzw * float3(2,2,2) + float3(2,2,2);
// 
//   r1.xyz = pow(r1.xyz, r0.x);
// 
//   r1.xyz = float3(2,2,2) + -r1.xyz;
//   r1.xyz = float3(0.5,0.5,0.5) * r1.xyz;
//   r2.xyz = r0.yzw + r0.yzw;
//   r0.y = dot(r0.yzw, float3(0.300000012,0.589999974,0.109999999));
//   r0.y = cmp(r0.y < 0.5);
//   r0.xzw = r2.xyz * r0.xxx;
//   r0.xzw = 0.5 * r0.xzw;
//   r0.xyz = saturate(r0.yyy ? r0.xzw : r1.xyz);
//   r0.xyz = cb0[25].xxx * r0.xyz;

  r0.xyz = r0.yzw;
  r0.xyz = RenoDX_Contrast(r0.xyz, cb3[0].x * 2, 0.1);

  o0.xyz = r0.xyz;
  return;
}