// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:07 2025

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  // 0.5
  // -0.5
  // 0.5
  // 0.5
  float4 g_texel_size : packoffset(c1);
  // 0.003125
  // 0.00555556
  // 320
  // 180
  float4 g_color : packoffset(c2);
  // 1
  // 0.96
  // 1
  // 0
}

cbuffer GaussianCoef : register(b1)
{
  float4 g_param : packoffset(c0);
  float4 g_coef[64] : packoffset(c1);
}

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -

#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  r0.xyzw = g_texture.SampleLevel(g_sampler_s, v1.xy, 0).xyzw;
  r1.x = cmp(r0.w < 0.5);
  if (r1.x != 0) {
    o0.xyzw = r0.xyzw;
    return;
  }
  r1.xy = g_param.zw * g_texel_size.xy * GS.SSSRadius; // scaling this up makes SSS wider and stronger

  r0.xyz = r0.xyz * g_coef[0].xyz + float3(9.99999975e-006,9.99999975e-006,9.99999975e-006);
  r2.xyz = g_coef[0].xyz + float3(9.99999975e-006,9.99999975e-006,9.99999975e-006);

  r1.z = 0;
  r3.xy = v1.xy + r1.xz;
  r4.xy = v1.xy + -r1.xz;
  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, 0).xyzw;
    // o0 = r5; return; //debug
  r6.xyz = g_coef[1].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[1].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, 0).xyzw;
  r6.xyz = g_coef[1].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[1].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r4.xy = r4.xy + -r1.xz;
  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, 0).xyzw;
  r6.xyz = g_coef[2].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[2].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, 0).xyzw;
  r6.xyz = g_coef[2].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[2].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug


  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r4.xy = r4.xy + -r1.xz;
  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, 0).xyzw;
  r6.xyz = g_coef[3].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[3].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug


  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, 0).xyzw;
  r6.xyz = g_coef[3].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[3].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r4.xy = r4.xy + -r1.xz;
  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, 0).xyzw;
  r6.xyz = g_coef[4].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[4].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, 0).xyzw;
  r6.xyz = g_coef[4].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[4].xyz * r5.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r3.zw = r4.xy + -r1.xz;
  r4.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, 0).xyzw;
  r5.xyz = g_coef[5].xyz * r4.www;
  r0.xyz = r4.xyz * r5.xyz + r0.xyz;
  r2.xyz = g_coef[5].xyz * r4.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r3.xyzw = g_texture.SampleLevel(g_sampler_s, r3.zw, 0).xyzw;
  r4.xyz = g_coef[5].xyz * r3.www;
  r0.xyz = r3.xyz * r4.xyz + r0.xyz;
  r2.xyz = g_coef[5].xyz * r3.www + r2.xyz;
    // o0.xyz = r0.xyz; return; //debug

  r3.xyz = r0.xyz;
    // o0.xyz = r3.xyz; return; //debug

  r4.xyz = r2.xyz;
    // o0.xyz = r4.xyz; return; //debug

  r5.xyzw = v1.xyxy;
    // o0.xyz = r5.xyz; return; //debug

  r0.w = 6;
  r1.w = 0;

  while (true) {
    r2.w = cmp((int)r1.w >= 5);
    if (r2.w != 0) break;
    r5.xy = r5.xy + r1.zy;
    r5.zw = r5.zw + -r1.zy;
    r6.xyzw = g_texture.SampleLevel(g_sampler_s, r5.xy, 0).xyzw;
    r7.xyz = g_coef[r0.w].xyz * r6.www;
    r6.xyz = r6.xyz * r7.xyz + r3.xyz;
    r7.xyz = g_coef[r0.w].xyz * r6.www + r4.xyz;
    r8.xyzw = g_texture.SampleLevel(g_sampler_s, r5.zw, 0).xyzw;
    r9.xyz = g_coef[r0.w].xyz * r8.www;
    r6.xyz = r8.xyz * r9.xyz + r6.xyz;
    r7.xyz = g_coef[r0.w].xyz * r8.www + r7.xyz;
    r2.w = (int)r0.w + 1;
    r3.xyz = r6.xyz;
    r4.xyz = r7.xyz;
    r8.xyzw = r5.xyxy;
    r9.xyzw = r5.zwzw;
    r0.w = r2.w;
    r3.w = 0;

    while (true) {
      r4.w = cmp((int)r3.w >= 5);
      if (r4.w != 0) break;
      r8.xy = r8.xy + r1.xz;
      r8.zw = r8.zw + -r1.xz;
      r9.xy = r9.xy + r1.xz;
      r9.zw = r9.zw + -r1.xz;
      r10.xyzw = g_texture.SampleLevel(g_sampler_s, r8.xy, 0).xyzw;
      r11.xyz = g_coef[r0.w].xyz * r10.www;
      r10.xyz = r10.xyz * r11.xyz + r3.xyz;
      r11.xyz = g_coef[r0.w].xyz * r10.www + r4.xyz;
      r12.xyzw = g_texture.SampleLevel(g_sampler_s, r8.zw, 0).xyzw;
      r13.xyz = g_coef[r0.w].xyz * r12.www;
      r10.xyz = r12.xyz * r13.xyz + r10.xyz;
      r11.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;
      r12.xyzw = g_texture.SampleLevel(g_sampler_s, r9.xy, 0).xyzw;
      r13.xyz = g_coef[r0.w].xyz * r12.www;
      r10.xyz = r12.xyz * r13.xyz + r10.xyz;
      r11.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;
      r12.xyzw = g_texture.SampleLevel(g_sampler_s, r9.zw, 0).xyzw;
      r13.xyz = g_coef[r0.w].xyz * r12.www;
      r3.xyz = r12.xyz * r13.xyz + r10.xyz;
      r4.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;
      r0.w = (int)r0.w + 1;
      r3.w = (int)r3.w + 1;
    }
    
    r1.w = (int)r1.w + 1;
  }

  r0.xyz = rcp(r4.xyz);
  r0.xyz = r3.xyz * r0.xyz;
  o0.xyz = g_color.xyz * r0.xyz;
  o0.w = 1;
  return;
}