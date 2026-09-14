#include "../Includes/Common.hlsl"

#ifndef ENABLE_SCREEN_DISTORTION
#define ENABLE_SCREEN_DISTORTION 1
#endif

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[5];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float2 sourceSize;
  t0.GetDimensions(sourceSize.x, sourceSize.y);
#if !ENABLE_SCREEN_DISTORTION
  v2 = v0.xy / sourceSize;
  o0 = t0.Sample(s0_s, v2);
  return;
#endif

  // Luma: fix screen distortion increasing in UW.
  const float nativeAspectRatio = 16.0 / 9.0;
  float aspectRatio = sourceSize.x / sourceSize.y;
  float horizontalDistortionScale = nativeAspectRatio / max(aspectRatio, nativeAspectRatio);

  float4 r0,r1,r2;
  r0.x = 1 + -v2.x;
  r0.y = -v2.y + r0.x;
  r0.x = -v2.x + v2.y;
  r0.zw = cb0[0].yy * r0.xy;
  r1.xy = v2.xy * float2(2,2) + float2(-1,-1);
  r0.zw = r0.zw * float2(0.5,0.5) + r1.xy;
  r0.xy = r0.xy * cb0[0].yy + r1.xy;
  r0.xy = r0.xy * cb0[1].wz + float2(1,1);
  r0.xy = r0.xy * float2(0.5,0.5) + cb0[1].xy;
  r0.x = v2.x + (r0.x - v2.x) * horizontalDistortionScale;
  r1.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xy = r0.zw * cb0[2].wz + float2(1,1);
  r0.xy = r0.xy * float2(0.5,0.5) + cb0[2].xy;
  r0.x = v2.x + (r0.x - v2.x) * horizontalDistortionScale;
  r0.xyz = t0.Sample(s0_s, r0.xy).xyz;
  r0.xyz = float3(0.5,0.5,0.5) * r0.xyz;
  r0.xyz = r1.xyz * float3(0.5,0.5,0.5) + r0.xyz;
  r1.xyz = cb0[4].xyz * cb0[4].www;
  r2.xyz = cb0[3].xyz * cb0[3].www;
  o0.xyz = r0.xyz * r1.xyz + r2.xyz;
  o0.w = cb0[0].x;
}
