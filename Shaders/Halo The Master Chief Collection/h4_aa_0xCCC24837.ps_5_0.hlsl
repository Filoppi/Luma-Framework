// ---- Created with 3Dmigoto v1.3.16 on Wed Aug 05 18:08:47 2026

cbuffer FXAA : register(b0)
{
  float4 g_externMathValues : packoffset(c0);
  float4 g_innerTapOffsets : packoffset(c1);
  float4 g_outerTapOffsetsOpt : packoffset(c2);
}

SamplerState sourceSampler_sampler_s : register(s0);
Texture2D<float4> sourceSampler_texture : register(t0);


// 3Dmigoto declarations
#define cmp -

#include "./Includes/Common.hlsl"

#if XBOX360_CURVE == 1
#include "./Includes/Curve360.hlsl"
#endif

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  r2.xyzw = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, v1.xy, 0).xyzw;
#if ALLOW_AA == 0
  o0 = r2;
#else
  r0.x = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, v2.xy, 0).w;
  r1.x = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, v2.zy, 0).w;
  r0.z = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, v2.xw, 0).w;
  r0.w = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, v2.zw, 0).w;
  r0.y = 0.00260416674 + r1.x;
  r1.xy = min(r0.xy, r0.zw);
  r1.zw = max(r0.xy, r0.zw);
  r1.x = min(r1.x, r1.y);
  r1.y = max(r1.z, r1.w);

  r1.z = min(r2.w, r1.x);
  r1.w = max(r2.w, r1.y);
  r1.z = r1.w + -r1.z;
  r1.w = 0.125 * r1.y;
  r1.w = max(0.0500000007, r1.w);
  r1.z = cmp(r1.w < r1.z);
  if (r1.z != 0) {
    r3.x = dot(r0.zwxy, g_externMathValues.xxyy);
    r3.y = dot(r0.xzyw, g_externMathValues.xxyy);
    r0.x = dot(r3.xy, r3.xy);
    r0.x = rsqrt(r0.x);
    r0.xyzw = r3.xyxy * r0.xxxx;
    r1.z = min(abs(r0.z), abs(r0.w));
    r3.xyzw = g_externMathValues.zzww * r0.xyzw;
    r3.xyzw = float4(0.125,0.125,0.125,0.125) * r3.xyzw;
    r3.xyzw = r3.xyzw / r1.zzzz;
    r3.xyzw = saturate(float4(0.5,0.5,0.5,0.5) + r3.xyzw);
    r0.xyzw = r0.zwzw * g_innerTapOffsets.xyzw + v1.xyxy;
    r4.xyzw = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, r0.xy, 0).xyzw;
    r0.xyzw = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, r0.zw, 0).xyzw;
    r0.xyzw = float4(0.5,0.5,0.5,0.5) * r0.xyzw;
    r3.xyzw = r3.xyzw * g_outerTapOffsetsOpt.xyxy + v1.zwzw;
    r5.xyzw = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, r3.xy, 0).xyzw;
    r3.xyzw = sourceSampler_texture.SampleLevel(sourceSampler_sampler_s, r3.zw, 0).xyzw;
    r3.xyzw = float4(0.25,0.25,0.25,0.25) * r3.xyzw;
    r0.xyzw = r4.xyzw * float4(0.5,0.5,0.5,0.5) + r0.xyzw;
    r3.xyzw = r5.xyzw * float4(0.25,0.25,0.25,0.25) + r3.xyzw;
    r3.xyzw = r0.xyzw * float4(0.5,0.5,0.5,0.5) + r3.xyzw;
    r1.xy = r3.ww + -r1.xy;
    r1.xy = cmp(float2(0,0) < r1.xy);
    r4.xyzw = r1.yyyy ? r0.xyzw : r3.xyzw;
    o0.xyzw = r1.xxxx ? r4.xyzw : r0.xyzw;
  } else {
    o0.xyzw = r2.xyzw;
  }
#endif

    o0.xyz = max(o0.xyz, 0);
    if (HDR_ENABLED) {
        o0.xyz = RenderIntermediatePass_Decode(o0.xyz);
    }
    
    #if XBOX360_CURVE == 1
        o0.xyz = Curve360::FullCorrect(o0.xyz);
    #endif

    if (HDR_ENABLED) {
        o0.xyz *= HDR_INTSCALING;
        o0.xyz = RenderIntermediatePass_Encode(o0.xyz);
    }

  return;
}