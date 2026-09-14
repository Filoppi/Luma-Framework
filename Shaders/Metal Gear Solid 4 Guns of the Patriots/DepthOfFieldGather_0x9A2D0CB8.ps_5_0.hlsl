#include "../Includes/Common.hlsl"

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

#ifndef ENABLE_IMPROVED_BLUR
#define ENABLE_IMPROVED_BLUR 1
#endif

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

// cb0[4] is the texture area used (e.g. this might start from the top left up to the middle point of the image, taking 1/4 portion) 
cbuffer cb0 : register(b0)
{
  float4 cb0[10];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  
  float2 textureSize;
  t0.GetDimensions(textureSize.x, textureSize.y);
  float2 invTextureSize = rcp(textureSize);

  float2 uvMax = cb0[5].xy;
  // Luma: fix approximate uvMax values (it was hardcoded to 0.499),
  // this caused problems around borders
  if (cb0[8].z == 1.0 && cb0[8].w == 1.0 && cb0[5].x == 0.499 && cb0[5].y == 0.499)
  {
    uvMax = (cb0[4].zw - 0.5) * invTextureSize;
  }
  r0.xy = cb0[8].zw * uvMax;
  r0.zw = v2.xy;

#if ENABLE_IMPROVED_BLUR
  // Measure the actual UV footprint of one output pixel before any divergent
  // control flow or UV clamping. This also accounts for partially used targets:
  // cb0[4].zw describes the active source area, not the destination UV gradient.
  // For this axis-aligned pass the footprint is an exact rectangle.
  float2 outputPixelSize = max(abs(ddx(v2.xy)) + abs(ddy(v2.xy)), invTextureSize);
  float2 outputResolution = rcp(outputPixelSize);
  r1.xyzw = SampleArea(t0, s0_s, r0.zw, textureSize, outputResolution, float2(0,0), r0.xy);
#else
  r0.zw = max(float2(0,0), r0.zw);
  r0.zw = min(r0.zw, r0.xy);
  r1.xyzw = t0.Sample(s0_s, r0.zw).xyzw;
#endif
#if !ENABLE_LUMA // Luma: disabled clamping to SDR
  r1.xyz = saturate(r1.xyz);
#endif
  r2.x = (abs(r1.w) < cb0[2].w);
  if (r2.x != 0) {
    o0.xyz = r1.xyz;
    o0.w = 0;
  } else {
    r2.x = 1 + -cb0[3].z;
    r2.y = cb0[3].z + cb0[3].z;
    r2.z = (1 < cb0[3].y);
    r2.w = 0.159154952 * cb0[3].y;
#if !ENABLE_IMPROVED_BLUR // Original blurring radius (fixed UV offsets, but assumes 16:9)
    r3.xy = float2(1.0 / 1280.0, 1.0 / 720.0) * cb0[9].x;
#else // Luma: fix blur radius
    // Make the UV offsets fixed at any resolution for the same aspect ratio, just adjusts them by aspect ratio.
    // Note: this assumes horizontal FoV scaling even below 16:9 (which might not be the case on some mods)
    float resolutionScale = textureSize.y / 720.0;
    r3.xy = invTextureSize * resolutionScale * cb0[9].x;

    static const float dofScale = 1.0; // Expose if needed
    r3.xy *= dofScale;
#endif
#if ENABLE_IMPROVED_BLUR
    // The spiral has a fixed tap count on the original 720-high sampling grid.
    // At higher resolutions, prefilter each tap over one grid cell as well as
    // the output-pixel footprint, instead of leaving it as a sparse bilinear read.
    // At 7680x2160 with cb0[9].x == 1 this is a 3x3-source-texel box
    // (up to 4x4 intersected texels / four bilinear fetches at fractional UVs).
    // This covers each tap's box; the spiral remains an approximation of the
    // complete DoF kernel, rather than an exhaustive scan of its entire disk.
    float2 gatherResolution = rcp(max(outputPixelSize, abs(r3.xy)));
#endif // ENABLE_IMPROVED_BLUR
    r3.z = cb0[2].z * abs(r1.w);
    r3.w = (cb0[3].y < 0);
    r4.xyz = r1.xyz;
    r4.w = 0;
    r5.xy = float2(1,0);
    // Start the spiral away from zero; this constant also controls radial spacing.
    r5.z = cb0[3].x;
    while (true) {
      r5.w = (r5.z >= cb0[2].x);
      if (r5.w != 0) break;
      r5.w = r4.w * r2.w + cb0[3].w;
      r5.w = frac(r5.w);
      r5.w = -0.5 + r5.w;
      r5.w = abs(r5.w) * r2.y + r2.x;
      r5.w = r2.z ? r5.w : 1;
      sincos(r4.w, r6.x, r7.x);
      r7.y = r6.x;
      r6.xy = r7.xy * r3.xy;
      r5.w = r5.z * r5.w;
      r6.xy = r6.xy * r5.ww + r0.zw;
#if ENABLE_IMPROVED_BLUR
      r6.xyzw = SampleArea(t0, s0_s, r6.xy, textureSize, gatherResolution, float2(0,0), r0.xy);
#else
      r6.xy = max(float2(0,0), r6.xy);
      r6.xy = min(r6.xy, r0.xy);
      r6.xyzw = t0.SampleLevel(s0_s, r6.xy, 0).xyzw;
#endif // ENABLE_IMPROVED_BLUR
#if !ENABLE_LUMA // Luma: disabled clamping to SDR
      r6.xyz = saturate(r6.xyz);
#endif
      r5.w = (r1.w < r6.w);
      r7.x = min(abs(r6.w), r3.z);
      r5.w = r5.w ? r7.x : abs(r6.w);
      r7.xyz = r6.xyz + r4.xyz;
      r6.w = -cb0[3].y + r5.z;
      r7.w = cb0[3].y + r5.z;
      r7.w = r7.w + -r6.w;
      r6.w = -r6.w + r5.w;
      r7.w = 1 / r7.w;
      r6.w = saturate(r7.w * r6.w);
      r7.w = r6.w * -2 + 3;
      r6.w = r6.w * r6.w;
      r6.w = r7.w * r6.w;
      r8.xyz = r4.xyz / r5.xxx;
      r6.xyz = -r8.xyz + r6.xyz;
      r6.xyz = r6.www * r6.xyz + r8.xyz;
      r6.xyz = r6.xyz + r4.xyz;
      r4.xyz = r3.www ? r7.xyz : r6.xyz;
      r5.y = r5.y + r5.w;
      r5.x = 1 + r5.x;
      r4.w = 2.39996314 + r4.w;
      r5.w = cb0[3].x / r5.z;
      r5.z = r5.z + r5.w;
    }
    r0.x = 1 / r5.x;
    o0.xyz = r4.xyz * r0.xxx;
    r0.x = -1 + r5.x;
    o0.w = saturate(r5.y / r0.x);
  }
}