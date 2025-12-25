// ---- Created with 3Dmigoto v1.3.16 on Wed Dec 10 12:44:33 2025

cbuffer PerInstanceCB : register(b2)
{
  float4 cb_dwao_bluroffsets0 : packoffset(c0);
  float4 cb_dwao_bluroffsets1 : packoffset(c1);
  float2 cb_dwao_blurparams : packoffset(c2);
}

// Denoiser is unnecessarily strong.
#ifndef BLUR_AMOUNT_MUL
#define BLUR_AMOUNT_MUL 0.1
#endif

SamplerState smp_pointclamp_s : register(s0);
Texture2D<float4> ro_ssao_aodepthbuffer : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : INTERP0,
  float4 v1 : INTERP1,
  float4 v2 : INTERP2,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = ro_ssao_aodepthbuffer.SampleLevel(smp_pointclamp_s, v0.xy, 0).yxzw;
  r1.x = dot(r0.zw, float2(254.007782,0.992217898));
  r2.xyzw = ro_ssao_aodepthbuffer.SampleLevel(smp_pointclamp_s, v0.zw, 0).xyzw;
  r1.y = dot(r2.zw, float2(254.007782,0.992217898));
  r3.xyzw = ro_ssao_aodepthbuffer.SampleLevel(smp_pointclamp_s, v1.xy, 0).xyzw;
  r1.z = dot(r3.zw, float2(254.007782,0.992217898));
  r4.xyzw = ro_ssao_aodepthbuffer.SampleLevel(smp_pointclamp_s, v1.zw, 0).xyzw;
  r1.w = dot(r4.zw, float2(254.007782,0.992217898));
  r5.xyzw = ro_ssao_aodepthbuffer.SampleLevel(smp_pointclamp_s, v2.xy, 0).xyzw;
  r2.z = dot(r5.zw, float2(254.007782,0.992217898));
  r1.xyzw = r2.zzzz + -r1.xyzw;
  r2.z = cb_dwao_blurparams.x * BLUR_AMOUNT_MUL * r2.z;
  r1.xyzw = cmp(r2.zzzz < abs(r1.xyzw));
  r1.xyzw = r1.xyzw ? float4(1,1,1,1) : 0;
  r6.x = r0.y;
  r6.y = r2.x;
  r0.y = r2.y;
  r6.z = r3.x;
  r0.z = r3.y;
  r6.w = r4.x;
  r0.w = r4.y;
  r2.xyzw = -r6.xyzw + r5.xxxx;
  r2.xyzw = r1.xyzw * r2.xyzw + r6.xyzw;
  r2.x = dot(r2.xwzy, float4(0.152469158,0.152469158,0.221841291,0.221841291));
  o0.x = r5.x * 0.251379132 + r2.x;
  r2.xyzw = r5.yyyy + -r0.xyzw;
  r0.xyzw = r1.xyzw * r2.xyzw + r0.xyzw;
  r0.x = dot(r0.xwzy, float4(0.152469158,0.152469158,0.221841291,0.221841291));
  o0.y = r5.y * 0.251379132 + r0.x;
  o0.zw = r5.zw;
  return;
}