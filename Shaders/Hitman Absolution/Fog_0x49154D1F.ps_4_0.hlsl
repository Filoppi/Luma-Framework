// Luma: Chinatown fog pass 0x49154D1F (fullscreen, blended onto the tonemapped scene RT).
// Vanilla math is kept exactly under TONEMAP_TYPE 0. With TONEMAP_TYPE >= 1 the fog alpha is faded
// toward black where the scene is black, so the fog stops lifting the black floor.

#include "Includes/Common.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[63];
}

Texture2D<float4> t0 : register(t0); // Packed depth (mapTPC0)
Texture2D<float4> t1 : register(t1); // Depth (mapTPC1)
Texture2D<float4> t2 : register(t2); // Scene copy (bound by main.cpp)

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

float4 main(float4 v0 : SV_POSITION, float4 v1 : TEXCOORD1) : SV_TARGET
{
  float4 o0;
  float4 r0, r1, r2, r3, r4, r5;

  r0 = t1.Sample(s1, v1.xy);
  r0.x = -cb4[8].z * r0.x + cb4[8].y;
  r0.x = 1.0 / r0.x;

  // 2x2 bilateral taps of the half-res fog: x from v1.z, y from v1.w (round_ni = floor).
  float fz = floor(v1.z);
  float fw = floor(v1.w);
  float x0 = fz * cb4[61].z + cb4[62].x;
  float x1 = fz * cb4[61].z + cb4[62].z;
  float y0 = fw * cb4[61].w + cb4[62].y;
  float y1 = fw * cb4[61].w + cb4[62].w;

  r1.xyz = float3(x0, y0, x1);
  r0.yzw = float3(x0, y1, x1);

  r2 = t0.Sample(s0, r1.xy);
  r1 = t0.Sample(s0, r1.zy);
  r3.x = dot(r2.xyz, float3(8964.844727, 35.018925, 0.136793));
  r3.y = dot(r1.xyz, float3(8964.844727, 35.018925, 0.136793));

  r4 = t0.Sample(s0, r0.yz);
  r5 = t0.Sample(s0, r0.wz);
  r3.z = dot(r4.xyz, float3(8964.844727, 35.018925, 0.136793));
  r3.w = dot(r5.xyz, float3(8964.844727, 35.018925, 0.136793));

  r0 = exp2(-abs(r3 - r0.x)); // DXBC exp is base 2
  r0 = r0 + 0.0001;

  // frc
  float a = frac(v1.z);
  float b = frac(v1.w);
  r3.x = a;
  r3.y = b;
  r3.z = 1.0 - a;
  r3.w = 1.0 - b;

  r0 = r0 * float4(r3.z, r3.x, r3.z, r3.x);
  r0 = r0 * float4(r3.w, r3.w, r3.y, r3.y);

  r1.x = r0.y * r1.w;
  r1.x = r2.w * r0.x + r1.x;
  r1.x = r4.w * r0.z + r1.x;
  r1.x = r5.w * r0.w + r1.x;

  r0.x = dot(r0, float4(1.0, 1.0, 1.0, 1.0));
  r0.x = r1.x / r0.x;

  o0.w = r0.x * r0.x;
  o0.xyz = cb4[53].xyz;

#if TONEMAP_TYPE >= 1
  // Scene copy: fade the fog alpha toward black where the scene is black.
  // Only for thin fog: in dense smoke (Burning Hotel) the fade made dark objects show through as outlines.
  uint w, h;
  t2.GetDimensions(w, h);
  if (w > 0)
  {
    float3 scene = t2.Load(int3(v0.xy, 0)).rgb;
    float m = max(scene.r, max(scene.g, scene.b));
    o0.w *= lerp(smoothstep(0.0, 0.0625, m), 1.0, smoothstep(0.2, 0.5, o0.w));
  }
#endif

  return o0;
}
