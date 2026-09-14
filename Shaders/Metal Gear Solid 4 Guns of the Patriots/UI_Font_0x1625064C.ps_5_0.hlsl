#include "../Includes/Common.hlsl"
#include "Includes/UI.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0;
  r0.w = t0.SampleBias(s0_s, v2.xy, -1).x;
  r0.w = saturate(r0.w);
  r0.xyz = float3(1,1,1);
  o0.xyzw = v1.xyzw * r0.xyzw;

  o0 = EmulateHardwareBlendMGS4(v0.xy, o0);
}