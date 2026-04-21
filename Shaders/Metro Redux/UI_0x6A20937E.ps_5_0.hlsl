#include "../Includes/Common.hlsl"

SamplerState s_clamp_tri_s : register(s5);
Texture2D<float4> t_base : register(t0);

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  r0.xyzw = float4(-0.0009765625,-0.0009765625,0.0009765625,-0.0009765625) + v1.xyxy;
  r0.xy = t_base.SampleBias(s_clamp_tri_s, r0.xy, -0.5).xy;
  r0.zw = t_base.SampleBias(s_clamp_tri_s, r0.zw, -0.5).xy;
  r0.xyzw = r0.xxxy + r0.zzzw;
  r1.xyzw = float4(-0.0009765625,0.0009765625,0.0009765625,0.0009765625) + v1.xyxy;
  r1.xy = t_base.SampleBias(s_clamp_tri_s, r1.xy, -0.5).xy;
  r1.zw = t_base.SampleBias(s_clamp_tri_s, r1.zw, -0.5).xy;
  r0.xyzw = r1.xxxy + r0.xyzw;
  r0.xyzw = r0.xyzw + r1.zzzw;
  r1.xyzw = r0.zzzw * float4(0.25,0.25,0.25,0.25) + float4(-0.100000001,-0.100000001,-0.100000001,-0.100000001);
  r0.xyzw = float4(0.25,0.25,0.25,0.25) * r0.xyzw;
  r1.xyzw = saturate(float4(1.25000012,1.25000012,1.25000012,1.25000012) * r1.xyzw);
  r2.xyzw = r1.zzzw * float4(-2,-2,-2,-2) + float4(3,3,3,3);
  r1.xyzw = r1.xyzw * r1.xyzw;
  r1.xyzw = r2.xyzw * r1.xyzw + -r0.zzzw;
  r2.xy = ddx_coarse(v1.xy);
  r2.zw = ddy_coarse(v1.xy);
  r2.xy = max(abs(r2.xy), abs(r2.zw));
  r2.x = max(r2.x, r2.y);
  r2.x = 1 / r2.x;
  r2.x = saturate(r2.x * 0.001953125 + -1);
  r0.xyzw = r2.xxxx * r1.xyzw + r0.xyzw;
  r1.xyzw = v2.zyxw * r0.zzzz;
  r0.xyzw = r0.xyzw * v2.zyxw + -r1.xyzw;
  r2.x = dot(v2.zyx, float3(0.300000012,0.479999989,0.219999999));
  r0.xyzw = r2.xxxx * r0.xyzw + r1.xyzw;
  o0.xyz = r0.xyz * r0.www;
  o0.w = r0.w;

  //o0.xyz = linear_to_gamma(o0.xyz);
  //o0 = saturate(o0); // Clamp
}