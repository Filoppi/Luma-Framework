#include "../Includes/Common.hlsl"
#include "Includes/UI.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[4];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD2,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.xy = v2.xy * float2(2,2) + float2(-1,-1);
  r0.yz = cb0[2].xy * r0.yy;
  r0.xy = r0.xx * cb0[1].xy + r0.yz;
  r0.xy = float2(1,1) + r0.xy;
  r0.xy = r0.xy * float2(0.5,0.5) + cb0[3].xy;
  r0.x = t1.Sample(s1_s, r0.xy).w;
  r0.y = t0.Sample(s0_s, w2.xy).x;
  r0.z = 0.5;
  r1.xyzw = t2.Sample(s2_s, r0.yz).xyzw;
  r1.w = r1.w * r0.x;
  o0.xyzw = v1.xyzw * r1.xyzw;

  o0 = EmulateHardwareBlendMGS4(v0.xy, o0);
}