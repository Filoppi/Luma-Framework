#include "../Includes/Common.hlsl"
#include "Includes/UI.hlsl"

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;

  r0.xyz = t0.Sample(s0_s, v2.xy).xyz;
  r1.x = r0.x + r0.y;
  r1.x = r1.x + r0.z;
  r1.y = (r1.x != 0.0);
  r1.z = 0.333333333 * r1.x;
  r1.x = r1.y ? r1.z : r1.x;
  r1.y = (0.1 < cb0[0].x);
  r1.x = r1.y ? r1.x : r0.x;
  r1.y = cb0[0].w + r1.x;
  r1.x = (cb0[0].y >= r1.x);
  r1.z = (cb0[0].z < r1.y);
  r1.y = saturate(r1.z ? cb0[0].z : r1.y);
  r0.w = r1.x ? 0 : r1.y;
  o0.xyzw = v1.xyzw * r0.xyzw;
#if 1 // Luma: don't clamp RGB
  o0.w = saturate(o0.w);
#else
  o0.xyzw = saturate(o0.xyzw);
#endif

  // Note: a cheaper way of doing tonemapping or clamping of the UI is to store on alpha whether a pixel is UI or not (when we can?),
  // and clamping at the end in the swapchain copy, but it might not always work.
  o0 = EmulateHardwareBlendMGS4(v0.xy, o0);
}