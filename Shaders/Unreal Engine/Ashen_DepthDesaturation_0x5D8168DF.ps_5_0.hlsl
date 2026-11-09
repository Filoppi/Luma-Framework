#include "../Includes/Color.hlsl"

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb1 : register(b1)
{
  float4 cb1[4];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[124];
}

void main(
  float4 v0 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  r0.xy = cb0[123].zw * v0.xy;
  r0.z = t0.SampleLevel(s0_s, r0.xy, 0).x;
  r0.xyw = t1.Sample(s1_s, r0.xy).xyz;
  r1.x = r0.z * cb0[57].z + -cb0[57].w;
  r0.z = r0.z * cb0[57].x + cb0[57].y;
  r1.x = 1 / r1.x;
  r0.z = r1.x + r0.z;
  r1.x = -cb1[2].z + r0.z;
  r0.z = saturate(-271441 + r0.z);
  r1.x = cb1[2].w + r1.x;
  r1.x = saturate(r1.x / cb1[3].x);
  r0.z = r1.x + -r0.z;
  r1.x = GetLuminance(r0.xyw); // Luma: fixed BT.601 coeffs (it's still based on gamma space colors though)
  r1.xyz = r1.xxx + -r0.xyw;
  r1.xyz = cb1[2].yyy * r1.xyz;
  r1.xyz = r1.xyz * r0.zzz;
  r0.xyz = cb1[3].yyy * r1.xyz + r0.xyw;
  r0.xyz = cb1[1].xyz + r0.xyz;
#if 1 // Luma
  o0.xyz = r0.xyz;
  o0.w = 0; // Make sure alpha has 0 for the UI
#else
  o0.xyz = max(0.0, r0.xyz);
  o0.w = 1;
#endif
}