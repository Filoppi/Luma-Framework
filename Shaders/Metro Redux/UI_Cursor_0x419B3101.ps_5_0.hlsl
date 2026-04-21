#include "../Includes/Common.hlsl"

SamplerState s_clamp_tri_s_s_s_s : register(s5);
Texture2D<float4> t_base : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD2,
  out float4 o0 : SV_Target0)
{
  float4 r0;
  r0.xyzw = t_base.Sample(s_clamp_tri_s_s_s_s, v1.xy).xyzw;
  r0.xyzw = v2.zyxw * r0.xyzw;
  o0.xyz = r0.xyz * r0.www;
  o0.w = r0.w;

  //o0.xyz = linear_to_gamma(o0.xyz);
  //o0 = saturate(o0); // Clamp
}