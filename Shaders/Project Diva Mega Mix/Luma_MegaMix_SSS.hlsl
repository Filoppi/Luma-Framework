// ---- Created with 3Dmigoto v1.3.16 on Tue Sep 02 15:46:05 2025
#include "./Includes/Common.hlsl"
#define cmp -

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);

//////////////////////////////////////////////////

// originally: sss_nprprep_0x086EEB5C
// blits to discard NPR g & b channels
void pre_sss(
    float4 v0: SV_POSITION0,
    float4 v1: TEXCOORD0, // center
    float4 v2: TEXCOORD1, // diag 0 1
    float4 v3: TEXCOORD2, // diag 2 3
    out float4 o0: SV_Target0)
{
    float4 r0, r1;
    uint4 bitmask, uiDest;
    float4 fDest;

    r0 = g_texture.Sample(g_sampler_s, v1.xy); // useful of original is x & w

    // skip downsample stuff
    o0 = r0.xxxw;
    return;

    // // if NPR edge, dont do taps
    // r0.z = cmp(r0.y == 1.000000); 
    // if (r0.z != 0) {
    //   o0.xyz = r0.xxx;
    //   o0.w = 1;
    //   return;
    // }
    //
    // r0.zw = g_texture.Sample(g_sampler_s, v2.xy).xw;
    // r1.x = cmp(r0.y < r0.w);
    // r0.xyzw = r1.xxxx ? r0.zzzw : r0.xxxy;
    // r1.xy = g_texture.Sample(g_sampler_s, v2.zw).xw;
    // r1.z = cmp(r0.w < r1.y);
    // r0.xyzw = r1.zzzz ? r1.xxxy : r0.xyzw;
    // r1.xy = g_texture.Sample(g_sampler_s, v3.xy).xw;
    // r1.z = cmp(r0.w < r1.y);
    // r0.xyzw = r1.zzzz ? r1.xxxy : r0.xyzw;
    // r1.xy = g_texture.Sample(g_sampler_s, v3.zw).xw;
    // r1.z = cmp(r0.w < r1.y);
    // o0.xyzw = r1.zzzz ? r1.xxxy : r0.xyzw;
    // return;
}

//////////////////////////////////////////////////

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0); // (unused)
  // 0.5
  // -0.5
  // 0.5
  // 0.5
  float4 g_texel_size : packoffset(c1);
  // 0.003125 // inv x
  // 0.00555556 // inv y
  // 320 // size x
  // 180 // size y
  float4 g_color : packoffset(c2); // final color multiply tint
  // 1
  // 0.96
  // 1
  // 0 // (unused)
}

cbuffer GaussianCoef : register(b1)
{
  float4 g_param : packoffset(c0);
  // 5 (unused)
  // 0 (unused)
  // 1 radius scale x
  // 1 radius scale y

  float4 g_coef[64] : packoffset(c1);
  // (0-5 is for first unrolled taps)
  // 0.721942 (coeffs rapidly changes! it's scaled by distance calculated CPU-side, where closer = stronger.)
  // 0.998837 (rgb, built-in tinting)
  // 0.945351 (no idea about how multiple characters/objects are handled. most likely can't, and it's also why select clothing (Miku Append) can get tinted like skin...)
  // 0        (w is always unused, wasted ahh)

  // 0.0794974
  // 0.000580729
  // 0.0259216
  // 0

  // 0.0182286
  // 4.26177e-12
  // 3.05691e-05
  // 0

  // 0.00301386
  // 1.18109e-25
  // 4.02395e-10
  // 0

  // 0.000243143
  // 0
  // 5.91249e-17
  // 0

  // 9.55522e-06 
  // 0
  // 9.69697e-26
  // 0

  // 6th index and beyond (stored in sets of 6)
  // 0.0794974
  // 0.000580729
  // 0.0259216
  // 0

  // 0.0400967
  // 1.12853e-06
  // 0.00273864
  // 0

  // 0.0127044
  // 8.28188e-15
  // 3.22966e-06
  // 0

  // 0.0021035
  // 2.29521e-28
  // 4.25135e-11
  // 0

  // 0.0001697
  // 0
  // 6.24661e-18
  // 0

  // 6.669e-06
  // 0
  // 1.0245e-26
  // 0

  // 12th index and beyond
  // 0.0182286
  // 4.26177e-12
  // 3.05691e-05
  // 0

  // 0.0127044
  // 8.28188e-15
  // 3.22966e-06
  // 0

  // 0.00431821
  // 6.07778e-23
  // 3.80872e-09
  // 0

  // 0.000715159
  // 1.68437e-36
  // 5.01359e-14
  // 0

  // 5.76954e-05
  // 0
  // 7.36658e-21
  // 0

  // 2.26736e-06
  // 0
  // 1.20818e-29
  // 0

  // 18th index and beyond
  // 0.00301386
  // 1.18109e-25
  // 4.02395e-10
  // 0

  // 0.0021035
  // 2.29521e-28
  // 4.25135e-11
  // 0

  // 0.000715159
  // 1.68437e-36
  // 5.01359e-14
  // 0

  // 0.000118441
  // 0
  // 6.59961e-19
  // 0

  // 9.55522e-06
  // 0
  // 9.69697e-26
  // 0

  // 3.75508e-07
  // 0
  // 1.59038e-34
  // 0

  // 24th index and beyond
  // 0.000243143
  // 0
  // 5.91249e-17
  // 0

  // 0.0001697
  // 0
  // 6.24661e-18
  // 0

  // 5.76954e-05
  // 0
  // 7.36658e-21
  // 0

  // 9.55522e-06
  // 0
  // 9.69697e-26
  // 0

  // 7.70867e-07
  // 0
  // 1.4248e-32
  // 0

  // 3.02941e-08
  // 0
  // 0
  // 0

  // 30th index and beyond
  // 9.55522e-06
  // 0
  // 9.69697e-26
  // 0

  // 6.669e-06
  // 0
  // 1.0245e-26
  // 0

  // 2.26736e-06
  // 0
  // 1.20818e-29
  // 0

  // 3.75508e-07
  // 0
  // 1.59038e-34
  // 0

  // 3.02941e-08
  // 0
  // 0
  // 0

  // 1.19052e-09
  // 0
  // 0
  // 0

  // (continues with unused 0s)
}

// // get interpolated kernel weight (AI fart)
// float3 CoefAt(int dx, int dy) { return log2(max(g_coef[6 * dy + dx].xyz, 1e-30)); } // log curved, safe
// float3 KernelW(float2 o) // o in original-texel units
// {
//     // 5 sets of 5
//     o = min(abs(o), 5.0);
//     int2 i0 = min((int2)floor(o), 4);
// 
//     // TODO: this is horrible perf not unrolled
//     float2 f = o - i0;
//     float3 a = lerp(CoefAt(i0.x, i0.y),     CoefAt(i0.x + 1, i0.y),     f.x);
//     float3 b = lerp(CoefAt(i0.x, i0.y + 1), CoefAt(i0.x + 1, i0.y + 1), f.x);
//     return exp2(lerp(a, b, f.y));
// }
// 
// float3 KernelWNearest(float2 o)
// {
//     // 5 sets of 5
//     o = min(o, 5.0); // no abs
//     int2 i0 = min((int2)floor(o), 4);
// 
//     return g_coef[6 * i0.y + i0.x].xyz;
// }

float4 SampleSSSInternal(float2 uv) {
  return g_texture.SampleLevel(g_sampler_s, uv, 99); // forces lowest mipmap
}
void SampleSSS(float2 uv, float2 uvCenter, float2 radius, float3 w, inout float3 aC, inout float3 aW)
{
    float4 s = SampleSSSInternal(uvCenter + uv * radius);
    float3 ws = w * s.w; // tinted weight * SSS intensity
    aC += s.xyz * ws;
    aW += ws;
}

// AI fart: appox in-between weights
static float3 gL1, gD1, gD2;
float3 FitW(float t) { return exp2(gL1 + (t - 1.0) * (gD1 + (t - 9.0) * gD2)); }

void sss0(
  float4 v0 : SV_POSITION0, // pixel position
  float2 v1 : TEXCOORD0, // uv center
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1; // completely unused later

  // // debug
  // o0.xyzw = 2;
  // return;

  // center (usually r == g == b monochrome, but can be tinted) (rgb is SSS surface w/ specular & shadow map, which SSS blur will expand outwards) (alpha denotes SSS intensity)
  r0.xyzw = g_texture.SampleLevel(g_sampler_s, v1.xy, 0).xyzw; // highest mip for shadow map detail

  // cutoff: low/no SSS
  if (r0.w < 0.5) {
    o0.xyz = r0.xyz;
    return;
  }

  // radius
  r1.xy = g_param.zw * g_texel_size.xy * GS.SSSRadius; // scaling this up makes SSS wider & more apparent

#if SSS0_NOT_NATIVE_SHADER == 0 && CUSTOM_SSS_HQBLUR == 1
  // accumulators: color and weight
  float3 centerW = g_coef[0].xyz + 1e-5; // safe
  float3 aC = r0.xyz * centerW;
  float3 aW = centerW;

  // loops
//   const float subsamples = 2; // within the 5 taps, how many sub-samples? // TODO: this becomes disgusting n^2!!! consider downsample blur, but then we'll lose shadowmap quality... AHHHH
//   [unroll] for (float y = -5 * subsamples; y <= 5 * subsamples; y++)
//   {
//     [unroll] for (float x = -5 * subsamples; x <= 5 * subsamples; x++)
//     {
//       if (x == 0 && y == 0) continue; // skip center
// 
//       // sub-sample
//       float2 o = float2(x, y) / subsamples;
// 
//       // weight for sub-sample
//       float3 w = KernelW(o);
//       if (max(w.x, max(w.y, w.z)) < 1e-5) continue; // skip low weights
// 
//       // sample
//       float4 s = g_texture.SampleLevel(g_sampler_s, v1 + o * r1.xy, 0);
// 
//       // accumulate
//       float3 ws = w * s.w; // tinted weight * SSS intensity
//       aC += s.xyz * ws;
//       aW += ws;
//   }

//   // TODO: this is not like vanilla
//   [unroll] for (float y = 0; y <= 5; y++)
//   {
//     [unroll] for (float x = 0; x <= 5; x++)
//     {
//       if (x == 0 && y == 0) continue; // skip center
// 
//       // sub-sample
//       float2 o = float2(x, y);
// 
//       // weight for sub-sample
//       float3 w = KernelWNearest(o);
//       w /= GS.SSSRadius;
//       if (max(w.x, max(w.y, w.z)) < 1e-5) continue; // skip low weights
// 
//       // samples
//       SampleSSS(float2( o.x,  o.y), v1, r1.xy, w, aC, aW);
//       SampleSSS(float2(-o.x, -o.y), v1, r1.xy, w, aC, aW);
//       SampleSSS(float2( o.x, -o.y), v1, r1.xy, w, aC, aW);
//       SampleSSS(float2(-o.x,  o.y), v1, r1.xy, w, aC, aW);
// 
//       // "in-between" samples
//       #if 0
//         const float2 s0 = r1.xy * 0.5;
//         const float3 w0 = w * 0.22;
//         SampleSSS(float2( o.x,  o.y), v1, s0, w0, aC, aW);
//         SampleSSS(float2(-o.x, -o.y), v1, s0, w0, aC, aW);
//         SampleSSS(float2( o.x, -o.y), v1, s0, w0, aC, aW);
//         SampleSSS(float2(-o.x,  o.y), v1, s0, w0, aC, aW);
//       #elif 0
//         const float2 s0 = r1.xy * 2;
//         const float3 w0 = w * 0.66;
//         SampleSSS(float2( o.x,  o.y), v1, s0, w0, aC, aW);
//         SampleSSS(float2(-o.x, -o.y), v1, s0, w0, aC, aW);
//         SampleSSS(float2( o.x, -o.y), v1, s0, w0, aC, aW);
//         SampleSSS(float2(-o.x,  o.y), v1, s0, w0, aC, aW);
// 
//         const float2 s1 = r1.xy * 3;
//         const float3 w1 = w * 0.33;
//         SampleSSS(float2( o.x,  o.y), v1, s1, w1, aC, aW);
//         SampleSSS(float2(-o.x, -o.y), v1, s1, w1, aC, aW);
//         SampleSSS(float2( o.x, -o.y), v1, s1, w1, aC, aW);
//         SampleSSS(float2(-o.x,  o.y), v1, s1, w1, aC, aW);
//       #endif
//     }
//   }

  // AI fart: appox in-between weights
  float3 L1 = log2(max(g_coef[1].xyz, 1e-30)); // log curved
  float3 L3 = log2(max(g_coef[3].xyz, 1e-30));
  float3 L5 = log2(max(g_coef[5].xyz, 1e-30));
  gL1 = L1;
  gD1 = (L3 - L1) / 8.0;
  gD2 = ((L5 - L3) / 16.0 - gD1) / 24.0;

  /* [unroll] */ for (int y = 0; y <= 5; y++)
  /* [unroll] */ for (int x = 0; x <= 5; x++)
  {
    if (x == 0 && y == 0) continue;
    float3 g = g_coef[6 * y + x].xyz; // weight
    if (max(g.x, max(g.y, g.z)) < 1e-5) continue; // skip low weights
    float3 corr = min(g / max(FitW(x * x + y * y), 1e-30), 4.0); // pin fit to table at the cell center

    for (int sy = -1; sy <= 1; sy += 2)
    for (int sx = -1; sx <= 1; sx += 2)
    {
      if ((x == 0 && sx < 0) || (y == 0 && sy < 0)) continue; // skip same sample
      for (int b = -1; b <= 1; b += 2)
      for (int a = -1; a <= 1; a += 2)
      {
        float2 p = float2(sx * x + a * 0.25, sy * y + b * 0.25);
        float3 w = FitW(dot(p, p)) * corr * 0.25;
        SampleSSS(p, v1, r1.xy, w, aC, aW);
      }
    }
  }

  aC /= aW; // normalize
  o0.xyz = aC * g_color.xyz; // tint
#else
  // accumulators: color and weight
  r0.xyz = r0.xyz * g_coef[0].xyz + float3(9.99999975e-006,9.99999975e-006,9.99999975e-006); // init accumulated color
  r2.xyz = g_coef[0].xyz + float3(9.99999975e-006,9.99999975e-006,9.99999975e-006); // init accumulated weight

  int sLevel = 0;

  // tap 1 & 2: horizontal +/-
  r1.z = 0;
  r3.xy = v1.xy + r1.xz;
  r4.xy = v1.xy + -r1.xz;
  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r3.xy).xyzw;
  r6.xyz = g_coef[1].xyz * r5.www; // tinted weight * SSS intensity
  r0.xyz = r5.xyz * r6.xyz + r0.xyz; // new color * weight + accumulated color
  r2.xyz = g_coef[1].xyz * r5.www + r2.xyz; // tinted weight * SSS intensity + accumulated weight

  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r4.xy).xyzw;
  r6.xyz = g_coef[1].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[1].xyz * r5.www + r2.xyz;

  // tap 3 & 4: 
  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r4.xy = r4.xy + -r1.xz;
  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r3.xy).xyzw;
  r6.xyz = g_coef[2].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[2].xyz * r5.www + r2.xyz;

  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r4.xy).xyzw;
  r6.xyz = g_coef[2].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[2].xyz * r5.www + r2.xyz;

  // tap 5 & 6: 
  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r4.xy = r4.xy + -r1.xz;
  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r3.xy).xyzw;
  r6.xyz = g_coef[3].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[3].xyz * r5.www + r2.xyz;

  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r4.xy).xyzw;
  r6.xyz = g_coef[3].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[3].xyz * r5.www + r2.xyz;

  // tap 7 & 8: 
  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r4.xy = r4.xy + -r1.xz;
  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r3.xy).xyzw;
  r6.xyz = g_coef[4].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[4].xyz * r5.www + r2.xyz;

  // r5.xyzw = g_texture.SampleLevel(g_sampler_s, r4.xy, sLevel).xyzw;
  r5.xyzw = SampleSSSInternal(r4.xy).xyzw;
  r6.xyz = g_coef[4].xyz * r5.www;
  r0.xyz = r5.xyz * r6.xyz + r0.xyz;
  r2.xyz = g_coef[4].xyz * r5.www + r2.xyz;

  // tap 9 & 10: 
  r3.z = v1.y;
  r3.xy = r3.xz + r1.xz;
  r3.zw = r4.xy + -r1.xz;
  // r4.xyzw = g_texture.SampleLevel(g_sampler_s, r3.xy, sLevel).xyzw;
  r4.xyzw = SampleSSSInternal(r3.xy).xyzw;
  r5.xyz = g_coef[5].xyz * r4.www;
  r0.xyz = r4.xyz * r5.xyz + r0.xyz;
  r2.xyz = g_coef[5].xyz * r4.www + r2.xyz;

  // r3.xyzw = g_texture.SampleLevel(g_sampler_s, r3.zw, sLevel).xyzw;
  r3.xyzw = SampleSSSInternal(r3.zw).xyzw;
  r4.xyz = g_coef[5].xyz * r3.www;
  r0.xyz = r3.xyz * r4.xyz + r0.xyz;
  r2.xyz = g_coef[5].xyz * r3.www + r2.xyz;

  // set
  r3.xyz = r0.xyz; // accumulated color
  r4.xyz = r2.xyz; // accumulated weight

  // init sample uvs
  r5.xyzw = v1.xyxy;

  // loop
  r0.w = 6;
  [unroll] for (int i = 0; i < 5; i++) { 
    r5.xy = r5.xy + r1.zy;
    r5.zw = r5.zw + -r1.zy;

    // r6.xyzw = g_texture.SampleLevel(g_sampler_s, r5.xy, sLevel).xyzw;
    r6.xyzw = SampleSSSInternal(r5.xy).xyzw;
    r7.xyz = g_coef[r0.w].xyz * r6.www;
    r6.xyz = r6.xyz * r7.xyz + r3.xyz;
    r7.xyz = g_coef[r0.w].xyz * r6.www + r4.xyz;

    // r8.xyzw = g_texture.SampleLevel(g_sampler_s, r5.zw, sLevel).xyzw;
    r8.xyzw = SampleSSSInternal(r5.zw).xyzw;
    r9.xyz = g_coef[r0.w].xyz * r8.www;
    r6.xyz = r8.xyz * r9.xyz + r6.xyz;
    r7.xyz = g_coef[r0.w].xyz * r8.www + r7.xyz;

    r0.w = (int)r0.w + 1; // weights index++

    // accumulated color & weights
    r3.xyz = r6.xyz;
    r4.xyz = r7.xyz;

    r8.xyzw = r5.xyxy;
    r9.xyzw = r5.zwzw;
    [unroll] for (int j = 0; j < 5; j++) {
      r8.xy = r8.xy + r1.xz;
      r8.zw = r8.zw + -r1.xz;
      r9.xy = r9.xy + r1.xz;
      r9.zw = r9.zw + -r1.xz;

      // r10.xyzw = g_texture.SampleLevel(g_sampler_s, r8.xy, sLevel).xyzw;
      r10.xyzw = SampleSSSInternal(r8.xy).xyzw;
      r11.xyz = g_coef[r0.w].xyz * r10.www;
      r10.xyz = r10.xyz * r11.xyz + r3.xyz;
      r11.xyz = g_coef[r0.w].xyz * r10.www + r4.xyz;

      // r12.xyzw = g_texture.SampleLevel(g_sampler_s, r8.zw, sLevel).xyzw;
      r12.xyzw = SampleSSSInternal(r8.zw).xyzw;
      r13.xyz = g_coef[r0.w].xyz * r12.www;
      r10.xyz = r12.xyz * r13.xyz + r10.xyz;
      r11.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;

      // r12.xyzw = g_texture.SampleLevel(g_sampler_s, r9.xy, sLevel).xyzw;
      r12.xyzw = SampleSSSInternal(r9.xy).xyzw;
      r13.xyz = g_coef[r0.w].xyz * r12.www;
      r10.xyz = r12.xyz * r13.xyz + r10.xyz;
      r11.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;

      // r12.xyzw = g_texture.SampleLevel(g_sampler_s, r9.zw, sLevel).xyzw;
      r12.xyzw = SampleSSSInternal(r9.zw).xyzw;
      r13.xyz = g_coef[r0.w].xyz * r12.www;
      r3.xyz = r12.xyz * r13.xyz + r10.xyz;
      r4.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;

      r0.w = (int)r0.w + 1; // weights index++
    }
  }

  // finalize
  r0.xyz = r3.xyz / r4.xyz; // accumulated color / accumulated weight
  r0.xyz *= g_color.xyz; // tint

  o0.xyz = r0.xyz;
  return;
#endif
}

//   // for loop
//   r0.w = 6; // initial index for weights look up g_coef[]
//   r1.w = 0; // i
//   while (true) {
//     // max i yet?
//     r2.w = cmp((int)r1.w >= 5);
//     if (r2.w != 0) break;
//
//     r5.xy = r5.xy + r1.zy;
//     r5.zw = r5.zw + -r1.zy;
//     r6.xyzw = g_texture.SampleLevel(g_sampler_s, r5.xy, 0).xyzw;
//     r7.xyz = g_coef[r0.w].xyz * r6.www;
//     r6.xyz = r6.xyz * r7.xyz + r3.xyz;
//     r7.xyz = g_coef[r0.w].xyz * r6.www + r4.xyz;
//     r8.xyzw = g_texture.SampleLevel(g_sampler_s, r5.zw, 0).xyzw;
//     r9.xyz = g_coef[r0.w].xyz * r8.www;
//     r6.xyz = r8.xyz * r9.xyz + r6.xyz;
//     r7.xyz = g_coef[r0.w].xyz * r8.www + r7.xyz;
//
//     // r2.w = (int)r0.w + 1;
//     // r0.w = r2.w;
//     r0.w = (int)r0.w + 1; // weights index++
//
//     // accumulated color & weights
//     r3.xyz = r6.xyz;
//     r4.xyz = r7.xyz;
//
//     r8.xyzw = r5.xyxy;
//     r9.xyzw = r5.zwzw;
//     r3.w = 0; // j
//     while (true) {
//       // max j yet?
//       r4.w = cmp((int)r3.w >= 5);
//       if (r4.w != 0) break;
//
//       r8.xy = r8.xy + r1.xz;
//       r8.zw = r8.zw + -r1.xz;
//       r9.xy = r9.xy + r1.xz;
//       r9.zw = r9.zw + -r1.xz;
//       r10.xyzw = g_texture.SampleLevel(g_sampler_s, r8.xy, 0).xyzw;
//       r11.xyz = g_coef[r0.w].xyz * r10.www;
//       r10.xyz = r10.xyz * r11.xyz + r3.xyz;
//       r11.xyz = g_coef[r0.w].xyz * r10.www + r4.xyz;
//
//       r12.xyzw = g_texture.SampleLevel(g_sampler_s, r8.zw, 0).xyzw;
//       r13.xyz = g_coef[r0.w].xyz * r12.www;
//       r10.xyz = r12.xyz * r13.xyz + r10.xyz;
//       r11.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;
//
//       r12.xyzw = g_texture.SampleLevel(g_sampler_s, r9.xy, 0).xyzw;
//       r13.xyz = g_coef[r0.w].xyz * r12.www;
//       r10.xyz = r12.xyz * r13.xyz + r10.xyz;
//       r11.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;
//
//       r12.xyzw = g_texture.SampleLevel(g_sampler_s, r9.zw, 0).xyzw;
//       r13.xyz = g_coef[r0.w].xyz * r12.www;
//       r3.xyz = r12.xyz * r13.xyz + r10.xyz;
//       r4.xyz = g_coef[r0.w].xyz * r12.www + r11.xyz;
//
//       r0.w = (int)r0.w + 1; // weights index++
//       r3.w = (int)r3.w + 1; // j++
//     }
//
//     r1.w = (int)r1.w + 1; // i++
//   }





