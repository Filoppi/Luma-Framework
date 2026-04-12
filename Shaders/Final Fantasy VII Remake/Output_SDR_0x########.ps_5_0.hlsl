#include "includes/common.hlsl"
#include "includes/renodx/effects.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/RCAS.hlsl"

// ---- Resource/sampler aliasing to reduce branch duplication ----
#define HAS_BASE_RESOURCES (_F68D39B5 || _51E2B894 || _803889E8 || _D96EF76D || _506D5998 || _BBB9CE42 || _5C2D3A71 || _66162229)

// Texture register aliases
#if _BBB9CE42
  // vignette variant
  #define REG_DITHER    t0
  #define REG_COLOR     t1
  #define REG_VIGNETTE  t2
  #define REG_LUT1      t3
  #define REG_LUT2      t4
  #define REG_UI        t5
#elif _506D5998
  // noise variant
  #define REG_DITHER    t0
  #define REG_COLOR     t1
  #define REG_LUT1      t2
  #define REG_LUT2      t3
  #define REG_NOISE     t4
  #define REG_UI        t5
#elif _5C2D3A71
  // video + monochrome mask
  #define REG_DITHER    t0
  #define REG_COLOR     t1
  #define REG_LUT1      t2
  #define REG_LUT2      t3
  #define REG_MASK      t4
  #define REG_UI        t5
  #define REG_VIDEO     t6
#elif _66162229
  // overlay + monochrome mask
  #define REG_DITHER    t0
  #define REG_COLOR     t1
  #define REG_LUT1      t2
  #define REG_LUT2      t3
  #define REG_MASK      t4
  #define REG_UI        t5
  #define REG_OVERLAY   t6
#else
  // default layout
  #define REG_DITHER    t0
  #define REG_COLOR     t1
  #define REG_LUT1      t2
  #define REG_LUT2      t3
  #define REG_UI        t4
#endif

// Sampler register aliases
#define SAMP_COLOR s0
#if _BBB9CE42
  #define SAMP_VIGNETTE s1
  #define SAMP_LUT      s2
  #define SAMP_UI       s3
#else
  #define SAMP_LUT      s1
  #define SAMP_UI       s2
#endif

// Fog pass aliases
#if (_803889E8 || _D96EF76D)
  #define REG_FOG    t5
  #define SAMP_FOG   s3
#endif

// Video pass aliases
#if _51E2B894
  #define REG_VIDEO  t5
  #define SAMP_VIDEO s3
#elif _803889E8
  #define REG_VIDEO  t6
  #define SAMP_VIDEO s4
#elif _5C2D3A71
  // REG_VIDEO set above to t6
  #define SAMP_VIDEO s3
#endif

// Overlay pass aliases
#if _66162229
  #define SAMP_OVERLAY s3
#endif

// Common declarations using the alias mappings above
#if HAS_BASE_RESOURCES
Texture3D<float4> ditherTex : register(REG_DITHER);
Texture2D<float4> colorTex  : register(REG_COLOR);
Texture3D<float4> lut1Tex   : register(REG_LUT1);
Texture3D<float4> lut2Tex   : register(REG_LUT2);
Texture2D<float4> uiTex     : register(REG_UI);

SamplerState colorSampler : register(SAMP_COLOR);
SamplerState lutSampler   : register(SAMP_LUT);
SamplerState uiSampler    : register(SAMP_UI);
#endif

#if (_803889E8 || _D96EF76D)
Texture2D<float4> fogTex : register(REG_FOG);
SamplerState fogSampler  : register(SAMP_FOG);
#endif

#if (_51E2B894 || _803889E8 || _5C2D3A71)
Texture2D<float4> videoTex : register(REG_VIDEO);
SamplerState videoSampler  : register(SAMP_VIDEO);
#endif

#if _506D5998
Texture2D<float4> noiseTex : register(REG_NOISE);
#endif

#if _BBB9CE42
Texture2D<float4> vignetteTex : register(REG_VIGNETTE);
SamplerState vignetteSampler  : register(SAMP_VIGNETTE);
#endif

#if _5C2D3A71 || _66162229
// Use the LUT sampler for the mask to avoid duplicate sampler declarations
Texture2D<float>  noiseTex : register(REG_MASK);
#define maskSampler lutSampler
#endif

#if _66162229
Texture2D<float4> overlayTex : register(REG_OVERLAY);
SamplerState      overlaySampler : register(SAMP_OVERLAY);
#endif

Texture2D<float2> dummyFloat2Texture : register(t8);

cbuffer cb1 : register(b1)
{
  float4 cb1[140];
}

cbuffer cb0 : register(b0)
{
  float4 cb0[39];
}

#define cmp -

float3 SampleLUT(Texture3D<float4> lut, float3 color)
{
    float4 r0;
    uint w, h, d;
    lut.GetDimensions(w, h, d);
    r0.w = (w == 32 && h == 32 && d == 32) ? 32 : 0;
    r0.z = r0.w ? 31 : 0;
    r0.xy = r0.ww ? float2(0.03125,0.015625) : float2(1,0.5);
    r0.w = r0.x * r0.z;
    float3 coord = color.xyz * r0.www + r0.yyy;

    return lut.SampleLevel(lutSampler, coord, 0).xyz;
}

#if _51E2B894 || _803889E8 || _5C2D3A71
float3 SampleVideoTexture(float2 pos, float2 v0)
{
    float4 r0,r1,r3,r4,r5,r6;
    float3 color;
    r1.z = cmp(cb0[24].y != 0.000000);
    r3.xy = saturate(cb0[24].xz);
    color.xyz = videoTex.SampleLevel(videoSampler, pos.xy, 0).xyz;
    color.xyz = color.xyz * r3.yyy;
    if (r1.z != 0) {
      r4.xzw = float3(0.00999999978,0.00999999978,0.00999999978) * color.xyz;
      r4.xzw = max(float3(0,0,0), r4.xzw);
      r4.xzw = log2(r4.xzw);
      r4.xzw = float3(0.159301758,0.159301758,0.159301758) * r4.xzw;
      r4.xzw = exp2(r4.xzw);
      r5.xyzw = r4.xxzz * float4(18.8515625,18.6875,18.8515625,18.6875) + float4(0.8359375,1,0.8359375,1);
      r1.zw = rcp(r5.yw);
      r1.zw = r5.xz * r1.zw;
      r1.zw = log2(r1.zw);
      r1.zw = float2(78.84375,78.84375) * r1.zw;
      r1.zw = exp2(r1.zw);
      r5.xy = min(float2(1,1), r1.zw);
      r1.zw = r4.ww * float2(18.8515625,18.6875) + float2(0.8359375,1);
      r1.w = rcp(r1.w);
      r1.z = r1.z * r1.w;
      r1.z = log2(r1.z);
      r1.z = 78.84375 * r1.z;
      r1.z = exp2(r1.z);
      r5.z = min(1, r1.z);
      // LUT2 via helper to match existing pipeline
      r4.xyz = SampleLUT(lut2Tex, r5.xyz);
      r5.xyz = cmp(r4.xyz < float3(0.0404499993,0.0404499993,0.0404499993));
      r6.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r4.xyz;
      r4.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r4.xyz;
      r4.xyz = float3(0.947867334,0.947867334,0.947867334) * r4.xyz;
      r4.xyz = log2(r4.xyz);
      r4.xyz = float3(2.4000001,2.4000001,2.4000001) * r4.xyz;
      r4.xyz = exp2(r4.xyz);
      r4.xyz = saturate(r5.xyz ? r6.xyz : r4.xyz);
      r4.xyz = log2(r4.xyz);
      r4.xyz = cb0[27].xxx * r4.xyz;
      color.xyz = exp2(r4.xyz);
    }
    if (LumaSettings.GameSettings.custom_film_grain_strength != 0) {
      color.xyz = renodx::effects::ApplyFilmGrain(
          color.xyz,
          v0.xy,
          LumaSettings.GameSettings.custom_random,
          LumaSettings.GameSettings.custom_film_grain_strength * 0.03f,
          1.f);
    }
    return color.xyz;
}
#endif

// #if _5C2D3A71 || _66162229
// // Generic monochrome mask effect (Rec.709 luma). Mask in REG_MASK using LUT sampler slot.
// float3 ApplyMonochromeEffect(float3 color, float2 pos)
// {
//     float mask = saturate(maskTex.SampleLevel(maskSampler, pos, 0).x);
//     float luma = dot(color, float3(0.212599993, 0.715200007, 0.0722000003));
//     return lerp(color, luma.xxx, mask);
// }
// #endif

// #if _66162229
// // Overlay in REG_OVERLAY. Matches decompiled blend: saturate(color * a + rgb) with a scaled by cb0[23].x.
// float3 ApplyOverlay(float3 color, float2 pos)
// {
//     float4 ov = overlayTex.SampleLevel(overlaySampler, pos, 0);
//     float s = saturate(cb0[23].x);
//     ov = s * (ov - float4(0, 0, 0, 1)) + float4(0, 0, 0, 1);
//     return saturate(color * ov.w + ov.xyz);
// }
// #endif

#if _803889E8 || _D96EF76D || _66162229
float3 ApplyOverlay(float3 color, Texture2D<float4> overlayTex, SamplerState overlaySampler, float2 pos)
{
  float4 r0,r1,r2,r3;
  r2.xyz = color;
  r3.xyzw = overlayTex.SampleLevel(overlaySampler, pos.xy, 0).xyzw;
  r0.w = saturate(cb0[23].x);
  r3.xyzw = float4(-0,-0,-0,-1) + r3.xyzw;
  r3.xyzw = r0.wwww * r3.xyzw + float4(0,0,0,1);
  r2.xyz = saturate(r2.xyz * r3.www + r3.xyz);
  return r2.xyz;
}
#endif

#if _506D5998 || _5C2D3A71 || _66162229
float SampleNoiseTexture(float3 color, float4 v0)
{
  float4 r0,r1,r2,r3,r4,r5;
  r0.w = cb0[34].z + -cb0[34].x;
  r0.w = max(0, r0.w);
  r0.w = 0.00026041668 * r0.w;
  r0.w = min(1, r0.w);
  r1.z = saturate(cb0[29].x);
  r3.xy = trunc(v0.xy);
  r3.yz = float2(0.618034005,0.618034005) * r3.xy;
  r1.w = dot(r3.yz, r3.yz);
  r1.w = sqrt(r1.w);
  sincos(r1.w, r4.x, r5.x);
  r1.w = r4.x / r5.x;
  r1.w = r1.w * r3.x;
  r1.w = frac(r1.w);
  r1.w = max(1.00000001e-07, r1.w);
  r2.w = frac(cb0[29].y);
  r2.w = 24 * r2.w;
  r2.w = floor(r2.w);
  r3.x = r2.w * 63.1312447 + r1.w;
  r3.x = frac(r3.x);
  r3.x = r3.x * 2 + -1;
  r2.w = 1 + r2.w;
  r1.w = r2.w * 63.1312447 + r1.w;
  r1.w = frac(r1.w);
  r1.w = r1.w * 2 + -1;
  r2.w = cmp(abs(r1.w) < abs(r3.x));
  r1.w = r2.w ? r3.x : r1.w;
  r1.z = 0.5 * r1.z;
  r2.w = 1 + -abs(r1.w);
  r0.w = r0.w * r0.w;
  r2.w = log2(r2.w);
  r0.w = r2.w * r0.w;
  r0.w = exp2(r0.w);
  r0.w = 1 + -r0.w;
  r0.w = r1.z * r0.w;
  r1.z = cmp(0 < r1.w);
  r1.w = cmp(r1.w < 0);
  r1.z = (int)-r1.z + (int)r1.w;
  r1.z = (int)r1.z;
  r0.w = r0.w * r1.z + 1;
  r3.x = saturate(0.5 * r0.w);
  r0.w = dot(color.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.z = cmp(r0.w < 0.00313080009);
  r1.w = 12.9200001 * r0.w;
  r0.w = log2(r0.w);
  r0.w = 0.416666657 * r0.w;
  r0.w = exp2(r0.w);
  r0.w = r0.w * 1.05499995 + -0.0549999997;
  r3.y = r1.z ? r1.w : r0.w;
  r1.zw = r3.xy * float2(0.96875,0.96875) + float2(0.015625,0.015625);
  return noiseTex.SampleLevel(lutSampler, r1.zw, 0).x;
}
#endif

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11;
  uint4 bitmask, uiDest;
  float4 fDest;
  float2 resolution;
  if (LumaData.GameData.DrewUpscaling) {
    resolution = LumaData.GameData.OutputResolution.xy;
  } else {
    resolution = cb0[31].xy;
  }

  r0.xy = cmp(cb0[34].xy < v0.xy);
  r0.zw = cmp(v0.xy < cb0[34].zw);
  r0.xy = r0.zw ? r0.xy : 0;
  r0.x = r0.y ? r0.x : 0;
  if ((cb0[34].x < v0.x && v0.x < cb0[34].z)
      && (cb0[34].y < v0.y && v0.y < cb0[34].w)) {
    int2 v0xy = (int2)v0.xy;
    int2 cb034xy = (int2)cb0[34].xy;
    r0.xy = (int2)v0.xy;
    r1.xy = (int2)cb0[34].xy;
    // r1.xy = (int2)(r0.xy) + -r1.xy;
    // r1.xy = (int2)r1.xy;
    r1.xy = float2(0.5, 0.5) + int2(v0.xy - cb034xy);
    r1.zw = cb0[35].zw * r1.xy;
    r2.xy = cb0[35].xy * cb0[35].wz;
    r1.xy = r1.xy * cb0[35].zw + float2(-0.5,-0.5);
    r2.xy = float2(0.5625,1.77777779) * r2.xy;
    r2.xy = min(float2(1,1), r2.xy);
    r1.xy = r1.xy * r2.xy + float2(0.5,0.5);
    r1.zw = r1.zw * resolution.xy + cb0[30].xy;
    r1.zw = cb0[0].zw * r1.zw;

    float2 pixelPos = r1.xy;
    float2 uvCoord = r1.zw;

#if !UI_COMPOSITION_PASS
    r2.xyz = colorTex.SampleLevel(colorSampler, uvCoord, 0).xyz;
    if (LumaData.GameData.DrewUpscaling && LumaSettings.GameSettings.can_sharpen != 0.f) {
      r2.xyz = RCAS(int2(v0.xy), 0, 0x7FFFFFFF, LumaSettings.GameSettings.custom_sharpness_strength, colorTex, dummyFloat2Texture, 1.f, true , float4(r2.xyz, 1.0f)).rgb;
    }
    float3 colorSample = r2.xyz;
    // Original shader multiplies by 0.01 before PQ encoding
    float3 pqColor = Linear_to_PQ(colorSample * 0.01f);

    r3.xyz = SampleLUT(lut1Tex, pqColor.xyz);

    r3.xyz = SampleLUT(lut2Tex, r3.xyz);

    r2.xyz = saturate(r2.xyz);
    r2.xyz = log2(r2.xyz);
    r2.xyz = float3(0.454545438,0.454545438,0.454545438) * r2.xyz;
    r2.xyz = exp2(r2.xyz);
    r4.xyz = cmp(r2.xyz < float3(0.100000001,0.100000001,0.100000001));
    r5.xyz = float3(0.699999988,0.699999988,0.699999988) * r2.xyz;
    r6.xyz = cmp(r2.xyz < float3(0.200000003,0.200000003,0.200000003));
    r7.xyz = r2.xyz * float3(0.899999976,0.899999976,0.899999976) + float3(-0.0199999996,-0.0199999996,-0.0199999996);
    r8.xyz = cmp(r2.xyz < float3(0.300000012,0.300000012,0.300000012));
    r9.xyz = r2.xyz * float3(1.10000002,1.10000002,1.10000002) + float3(-0.0599999987,-0.0599999987,-0.0599999987);
    r10.xyz = cmp(r2.xyz < float3(0.5,0.5,0.5));
    r11.xyz = r2.xyz * float3(1.14999998,1.14999998,1.14999998) + float3(-0.075000003,-0.075000003,-0.075000003);
    r2.xyz = r10.xyz ? r11.xyz : r2.xyz;
    r2.xyz = r8.xyz ? r9.xyz : r2.xyz;
    r2.xyz = r6.xyz ? r7.xyz : r2.xyz;
    r2.xyz = r4.xyz ? r5.xyz : r2.xyz;
    r0.w = cmp(0 < cb0[38].x);
    r2.xyz = r0.www ? r2.xyz : r3.xyz;

    if (LumaSettings.GameSettings.custom_film_grain_strength != 0.f)
    {
      r2.xyz = renodx::effects::ApplyFilmGrain(
          r2.xyz,
          v0.xy / cb0[34].zw,
          LumaSettings.GameSettings.custom_random,
          LumaSettings.GameSettings.custom_film_grain_strength * 0.03f,
          1.f);
    }

    float3 gradedColor = r2.xyz;

    r3.xyz = cmp(r2.xyz < float3(0.0404499993,0.0404499993,0.0404499993));
    r4.xyz = float3(0.0773993805,0.0773993805,0.0773993805) * r2.xyz;
    r2.xyz = float3(0.0549999997,0.0549999997,0.0549999997) + r2.xyz;
    r2.xyz = float3(0.947867334,0.947867334,0.947867334) * r2.xyz;
    r2.xyz = log2(r2.xyz);
    r2.xyz = float3(2.4000001,2.4000001,2.4000001) * r2.xyz;
    r2.xyz = exp2(r2.xyz);
    r2.xyz = r3.xyz ? r4.xyz : r2.xyz;
    r2.xyz = log2(abs(r2.xyz));
    r2.xyz = cb0[27].xxx * r2.xyz;
    r2.xyz = exp2(r2.xyz);
    r3.xyz = r2.xyz;
    r0.w = 1;
#if _D96EF76D
    r3.xyz = ApplyOverlay(r3.xyz, fogTex, fogSampler, pixelPos.xy);
#endif
    
#if _BBB9CE42
    float2 vignetteUV = cb0[21].xy * pixelPos.xy;
    vignetteUV = max(cb0[22].xy, vignetteUV);
    vignetteUV = min(cb0[22].zw, vignetteUV);
    float4 vignetteSample = vignetteTex.SampleLevel(vignetteSampler, vignetteUV, 0).xyzw;
    r0.w = saturate(cb0[20].x);
    vignetteSample.xyzw = float4(-0,-0,-0,-1) + vignetteSample;
    vignetteSample.xyzw = r0.wwww * vignetteSample.xyzw + float4(0,0,0,1);
    r0.w = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
    r1.z = 1 + -vignetteSample.w;
    r0.w = r1.z * r0.w;
    vignetteSample.xyz = vignetteSample.xyz * r0.www;
    r3.xyz = saturate(r2.xyz * vignetteSample.www + vignetteSample.xyz);
    r2.xyz = r3.xyz;
    r0.w = 1;
#endif

#if _66162229
    // Apply overlay, then monochrome mask (t6 overlay, t4 mask)
    r2.xyz = ApplyOverlay(r2.xyz, overlayTex, overlaySampler, pixelPos.xy);
    float2 uv = trunc(v0.xy);
    float noise = SampleNoiseTexture(r2.xyz, v0);
    r2.xyz = r2.xyz * noise;
    // r2.xyz = ApplyMonochromeEffect(r2.xyz, pixelPos.xy);
    r3.xyz = r2.xyz;
    r0.w = 1;
#endif

#if _51E2B894 || _803889E8 || _5C2D3A71
    float3 videoColor = SampleVideoTexture(pixelPos.xy, v0.xy / cb0[34].zw);
    r3.xyz = videoColor + -r2.xyz;
    float2 blendFactor = saturate(cb0[24].xz);
    r2.xyz = r3.xyz * blendFactor.x + r2.xyz;
#if _803889E8
    r2.xyz = ApplyOverlay(r2.xyz, fogTex, fogSampler, pixelPos.xy);
#endif
#if _5C2D3A71
    // Apply monochrome mask using t4 (same sampler slot as LUT)
    float noise = SampleNoiseTexture(r2.xyz, v0);
    r2.xyz = r2.xyz * noise;
#endif
    r3.xyz = r2.xyz;
    r0.w = 1;
#endif

#if _506D5998
    float noise = SampleNoiseTexture(r3.xyz, v0);
    r2.xyz = r2.xyz * noise; // pre-apply noise to tonemapped result
    r3.xyz = r2.xyz;
    r0.w = 1;
#endif

#if TONEMAP_PASS
    // --- Pass 1 output (hudless tonemapped SDR, linear [0,1]) ---
    o0.xyz = r2.xyz;
    o0.w = 1;
    return;
#endif // TONEMAP_PASS
#else // UI_COMPOSITION_PASS
    // --- Pass 2 input: read hudless tonemapped from colorTex (rebound by C++ to intermediate RT) ---
    r2.xyz = colorTex.SampleLevel(colorSampler, uvCoord, 0).xyz;
    r3.xyz = r2.xyz;
    r0.w = 1;
#endif // !UI_COMPOSITION_PASS

    r1.xyzw = uiTex.SampleLevel(uiSampler, pixelPos, 0).xyzw;
    r2.w = dot(r3.xyz, float3(0.212599993,0.715200007,0.0722000003));
    r2.xyz = r2.xyz * r0.www + -r2.www;
    r2.xyz = cb0[25].xxx * r2.xyz + r2.www;
    r0.w = cb0[25].y * r1.w;
    r1.xyz = saturate(r2.xyz * r0.www + r1.xyz);
    r2.xyz = cmp(r1.xyz < float3(0.00313080009,0.00313080009,0.00313080009));
    r3.xyz = float3(12.9200001,12.9200001,12.9200001) * r1.xyz;
    r1.xyz = log2(r1.xyz);
    r1.xyz = float3(0.416666657,0.416666657,0.416666657) * r1.xyz;
    r1.xyz = exp2(r1.xyz);
    r1.xyz = r1.xyz * float3(1.05499995,1.05499995,1.05499995) + float3(-0.0549999997,-0.0549999997,-0.0549999997);
    r1.xyz = r2.xyz ? r3.xyz : r1.xyz;
    float3 color = r1.xyz;
    // if (LumaSettings.GameSettings.custom_film_grain_strength == 0.f)
    {
      r0.z = asuint(cb1[139].z) << 3;
      r0.xyz = (int3)r0.xyz & int3(63,63,63);
      r0.w = 0;
      r0.x = ditherTex.Load(r0.xyzw).x;
      r0.x = r0.x * 2 + -1;
      r0.y = cmp(0 < r0.x);
      r0.z = cmp(r0.x < 0);
      r0.y = (int)-r0.y + (int)r0.z;
      r0.y = (int)r0.y;
      r0.x = 1 + -abs(r0.x);
      r0.x = sqrt(r0.x);
      r0.x = 1 + -r0.x;
      r0.x = r0.y * r0.x;
      r0.yzw = r1.xyz * float3(2,2,2) + float3(-1,-1,-1);
      r0.yzw = float3(-0.992156863,-0.992156863,-0.992156863) + abs(r0.yzw);
      r0.yzw = cmp(r0.yzw < float3(0,0,0));
      r2.xyz = r0.xxx * float3(0.00392156886,0.00392156886,0.00392156886) + r1.xyz;
      color.xyz = saturate(r0.yzw ? r2.xyz : r1.xyz);
    }

#if DEVELOPMENT && EARLY_DISPLAY_ENCODING
    color.xyz = gamma_to_linear(color.xyz, GCT_MIRROR, 2.2f);
#endif

    o0.xyz = color.xyz;
    o0.w = 1;
  } else {
    o0.xyzw = float4(0,0,0,0);
  }
  return;
}