#include "../Includes/Common.hlsl"

#ifndef ENABLE_AUTO_HDR
#define ENABLE_AUTO_HDR 1
#endif

Texture2D<float> t2 : register(t2);
Texture2D<float> t1 : register(t1);
Texture2D<float> t0 : register(t0);

SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1;
  r0.x = t1.Sample(s1_s, v1.zw).x;
  r0.xyzw = cb0[1].xyzw * r0.x;
  r1.x = t0.Sample(s0_s, v1.xy).x;
  r0.xyzw = r1.x * cb0[4].xyzw + r0.xyzw;
  r1.x = t2.Sample(s2_s, v1.zw).x;
  r0.xyzw = r1.x * cb0[2].xyzw + r0.xyzw;
  r0.xyzw = cb0[3].xyzw + r0.xyzw;
  o0.xyzw = cb0[0].xyzw * r0.xyzw;

#if ENABLE_AUTO_HDR
  // Luma: add a light AutoHDR pass on videos
  if (LumaSettings.DisplayMode == 1)
  {
    o0.rgb = gamma_to_linear(o0.rgb, GCT_MIRROR);
    o0.rgb = PumboAutoHDR(o0.rgb, 250.0, LumaSettings.GamePaperWhiteNits);
    o0.rgb = linear_to_gamma(o0.rgb, GCT_MIRROR);
  }
#endif
}