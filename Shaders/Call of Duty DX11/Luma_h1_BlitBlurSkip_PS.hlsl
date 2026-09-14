#include "./common.hlsl"
Texture2D<float4> t4 : register(t4);
SamplerState s4_s : register(s4);
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  o0.xyzw = t4.Sample(s4_s, v1.xy).xyzw;
  return;
}