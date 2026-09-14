#include "../Includes/Common.hlsl"
#include "Includes/UI.hlsl"

Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float2 w2 : TEXCOORD2,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.x = t0.Sample(s0_s, w2.xy).x;
  r0.y = 0.5;
  r0.xyzw = t2.Sample(s2_s, r0.xy).xyzw;
  r1.x = t1.Sample(s1_s, v2.xy).w;
  r0.w = r1.x * r0.w;
  o0.xyzw = v1.xyzw * r0.xyzw;

  o0 = EmulateHardwareBlendMGS4(v0.xy, o0);
}