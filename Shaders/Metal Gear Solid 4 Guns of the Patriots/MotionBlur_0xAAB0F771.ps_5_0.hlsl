#include "../Includes/Common.hlsl"

#ifndef ENABLE_MOTION_BLUR
#define ENABLE_MOTION_BLUR 1
#endif

// Use 16 samples instead of 4 to reduce stepping along the blur trail.
#ifndef ENABLE_HIGH_QUALITY_MOTION_BLUR
#define ENABLE_HIGH_QUALITY_MOTION_BLUR 0
#endif

Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}

// Draws an additive layer of motion blur, representing the movement of the image
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
#if !ENABLE_MOTION_BLUR
  o0 = 0;
  return;
#endif

  float4 r0,r1,r2,r3,r4;
  int4 r1i;
  r0.xy = max(float2(0,0), v1.xy);
  r0.xy = min(cb0[8].zw, r0.xy);
  r0.z = t1.Sample(s1_s, r0.xy).x;
  r0.w = -cb0[5].x + r0.z;
  r0.w = saturate(cb0[5].y * r0.w);
  r1.xy = r0.xy * float2(2,2) + float2(-1,-1);
  r1.xy = r1.xy * r0.zz;
  r1.z = cb0[4].x * r0.z + cb0[4].y;
  r2.xyzw = cb0[1].xyzw * r1.yyyy;
  r2.xyzw = r1.xxxx * cb0[0].xyzw + r2.xyzw;
  r2.xyzw = r1.zzzz * cb0[2].xyzw + r2.xyzw;
  r2.xyzw = r0.zzzz * cb0[3].xyzw + r2.xyzw;
  r1.z = r2.z / r2.w;
  r1.w = (r1.z >= 0);
  r1.z = (r1.z < 0.5);
  r1.z = r1.z ? r1.w : 0;
  r1.w = max(0.00100000005, r2.w);
  r2.xy = r2.xy / r1.ww;
  r1.xy = r1.xy / r0.zz;
  r1.xy = r2.xy + -r1.xy;
  r0.z = 0.5 * cb0[4].z;
  r1.xy = r1.xy * r0.zz;
  r1.xy = r1.zz ? r1.xy : 0;
#if 1 // Luma: fix aspect ratio scaling
  float2 sourceSize;
  t0.GetDimensions(sourceSize.x, sourceSize.y);
  float aspectRatio = sourceSize.x / sourceSize.y;
  r1.zw = float2(aspectRatio, 1) * r1.xy;
#else
  r1.zw = float2(1.77777779, 1) * r1.xy;
#endif
  r0.z = dot(r1.zw, r1.zw);
  r0.z = sqrt(r0.z);
  r1.z = min(cb0[5].z, r0.z);
  r1.w = (0 < r0.z);
  r1.z = r1.z / r0.z;
  r1.z = r1.w ? r1.z : 1;
  r1.xy = r1.xy * r1.zz;
  r1.xy = cb0[9].xx * r1.xy;
  r2.xyz = t0.Sample(s0_s, r0.xy).xyz;
#if ENABLE_HIGH_QUALITY_MOTION_BLUR
  static const int sampleCount = 16;
#else
  static const int sampleCount = 4;
#endif
  // Keep the original first and last sample positions as the sample count changes.
  r1.xy *= 0.75 / float(sampleCount - 1);
  r3.xyz = r2.xyz;
  r1.z = 1.0;
  r1i.w = 1;
  while (true) {
    if (r1i.w >= sampleCount) break;
    r2.w = (float)r1i.w;
    r4.xy = r1.xy * r2.ww + r0.xy;
    r4.xy = max(float2(0,0), r4.xy);
    r4.xy = min(cb0[8].zw, r4.xy);
    r2.w = t1.Sample(s1_s, r4.xy).x;
    r2.w = -cb0[5].x + r2.w;
    r2.w = saturate(cb0[5].y * r2.w);
    r4.xyz = t0.Sample(s0_s, r4.xy).xyz;
    r3.xyz = r2.www * r4.xyz + r3.xyz;
    r1.z = r2.w + r1.z;
    r1i.w++;
  }
  o0.xyz = r3.xyz / r1.z;
  r0.x = saturate(cb0[4].w * r0.z);
  o0.w = r0.x * r0.w;
}