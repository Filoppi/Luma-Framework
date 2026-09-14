// ---- Created with 3Dmigoto v1.3.16 on Wed Sep 09 21:33:16 2026

cbuffer cbToneMap : register(b1)
{
  float3 g_ToneMapInvSceneLumScale : packoffset(c0);
  float4 g_ReinhardParam : packoffset(c1);
  float4 g_ToneMapParam : packoffset(c2);
  float4 g_ToneMapSceneLumScale : packoffset(c3);
  float4 g_AdaptParam : packoffset(c4);
  float4 g_AdaptCenterWeight : packoffset(c5);
  float4 g_BrightPassThreshold : packoffset(c6);
  float4 g_GlareLuminance : packoffset(c7);
  float4 g_BloomBoostColor : packoffset(c8);
  float4 g_vBloomFinalColor : packoffset(c9);
  float4 g_vBloomScaleParam : packoffset(c10);
  float4x3 g_mtxColorMultiplyer : packoffset(c11);
  float4 g_vChromaticAberrationRG : packoffset(c14);
  float4 g_vChromaticAberrationB : packoffset(c15);
  bool4 g_bEnableFlags : packoffset(c16);
  float4 g_vFeedBackBlurParam : packoffset(c17);
  float4 g_vVignettingParam : packoffset(c18);
  float4 g_vHDRDisplayParam : packoffset(c19);
  float4 g_vChromaticAberrationShapeParam : packoffset(c20);
  float4 g_vScreenSize : packoffset(c21);
  float4 g_vSampleDistanceAdjust : packoffset(c22);
  uint4 g_vMaxSampleCount : packoffset(c23);
  float4 g_vScenePreExposure : packoffset(c24);
  float4 g_vCameraParam : packoffset(c25);
}

SamplerState SS_ClampLinear_s : register(s1);
Texture2D<float4> g_SourceTexture : register(t0);
Texture2D<float4> g_ToneMapTableTexture : register(t1);
Texture3D<float4> g_ColorGradingLUTTexture : register(t2);
Texture2D<float4> g_GlareAccTexture : register(t3);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"
#include "../Includes/JzAzBz.hlsl"
#include "../Includes/Color.hlsl"

#define LUT_SIZE 16u
#define LUT_3D 1
#include "../Includes/ColorGradingLUT.hlsl"

void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  bool isHDR = g_bEnableFlags.z != 0;

  // color + bloom
  r0.xyz = g_GlareAccTexture.SampleLevel(SS_ClampLinear_s, v1.xy, 0).xyz;
  r0.xyz = g_GlareLuminance.xxx * r0.xyz;

  r1.xyz = g_SourceTexture.SampleLevel(SS_ClampLinear_s, v1.xy, 0).xyz;
  r1.xyz = g_ToneMapSceneLumScale.xyz * r1.xyz;

  r0.xyz = r1.xyz * v1.zzz + r0.xyz;
  // o0.xyz = pow(r0.xyz, 1/2.2); return; // debug
  // r0.xyz = v1.x; // debug

  // Reinhard
  float p = HDR_PEAK;
  // p /= max3(mul(float4(1,1,1,1), g_mtxColorMultiplyer)); // peak delta
  // o0.xyz = pow(p, 1/2.2); return; // debug
  if (!isHDR) {
    r0.xyz = r0.xyz / (0.2 + r0.xyz); // LUT encoding (generates a lot of white clip...)
    r0.w = 0;
    r1.x = g_ToneMapTableTexture.SampleLevel(SS_ClampLinear_s, r0.xw, 0).x;
    r1.y = g_ToneMapTableTexture.SampleLevel(SS_ClampLinear_s, r0.yw, 0).x;
    r1.z = g_ToneMapTableTexture.SampleLevel(SS_ClampLinear_s, r0.zw, 0).x;
  } else {
    float3 sdr = ReinhardSekiro(r0.xyz, g_ReinhardParam, 0, false);

    // float thres = ReinhardSekiroHDRExtThres(g_ReinhardParam);
    // float slope = ReinhardSekiroVelocity(thres, g_ReinhardParam);
    // float output = ReinhardSekiro(thres, g_ReinhardParam, 0, false);
    float thres  = g_ToneMapTableTexture.Load(int3(uint2(0,0),0)).x;
    float slope  = g_ToneMapTableTexture.Load(int3(uint2(1,0),0)).x;
    float output = g_ToneMapTableTexture.Load(int3(uint2(2,0),0)).x;
    r1.xyz = LinearPiecewiseExtension(sdr, r0.xyz, thres, slope, output);

    float e = rcp(g_ToneMapParam.y);
    r1.xyz = pow(r1.xyz, e); //TODO: usually 1, but this should be low passed

    #if TONEMAP_BT2020 == 1
      r1.xyz = BT709_To_BT2020(r1.xyz);
    #endif
    // r1.xyz = Neutwo(r1.xyz, p);
    r1.xyz = Reinhard(r1.xyz, p); //TODO: adv tonemap (though LUT would bottleneck).
    #if TONEMAP_BT2020 == 1
      r1.xyz = BT2020_To_BT709(r1.xyz);
    #endif
  }

  // o0.xyz = pow(r1.xyz, 1/2.2); return; //debug
  
  // color matrix color grading (slight saturation and whatnot)
  r1.w = 1;
  r0.xyz = mul(r1.xyzw, g_mtxColorMultiplyer);
  r0.xyz = max(0, r0.xyz);

  // vignette
  r1.xy = v1.xy * float2(2,2) + float2(-1,-1);
  r1.xy = g_vVignettingParam.xy * r1.xy; // scale
  r0.w = dot(r1.xy, r1.xy); // dist
  r0.w = sqrt(r0.w); // dist
  r0.w = saturate(r0.w * g_vVignettingParam.z + g_vVignettingParam.w); // scale/factor
  r0.w = 1 + -r0.w;
  r1.x = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r1.x * r0.w;
  r0.w = r0.w * r0.w;
  r0.w = r0.w * r0.w;
  float vignetteMultiplier = r0.w;
  r0.xyz = r0.xyz * vignetteMultiplier;

  // o0.xyz = pow(r0.xyz, 1/2.2); return; //debug

  // compress
  float3 colorU = r0.xyz;
  float3 colorN = colorU;
  if (isHDR) {
    float y = max3(colorN) /* GetLuminance(colorN) */; //TODO: idk which is better. LUT ruins everything anyways...
    // float y1 = Neutwo(y, DVS1, p);
    float y1 = Neupow(y, 1, p, 4);
    colorN *= safeDivision(y1, y);
  }

  // pow contrast / gamma encode (usually 1/2.2)
  r0.xyz = pow(r0.xyz, g_ToneMapParam.z);

  // LUT (builder lerps 2 16x into this 16x 3D)
  // r0.xyz = r0.xyz * float3(0.9375,0.9375,0.9375) + float3(0.03125,0.03125,0.03125);
  // r1.xyz = g_ColorGradingLUTTexture.Sample(SS_ClampLinear_s, r0.xyz).xyz; 
  r1.xyz = SampleLUT(g_ColorGradingLUTTexture, SS_ClampLinear_s, r0.xyz, LUT_SIZE, true);

  // decompress
  if (isHDR) {
    r1.xyz = pow(r1.xyz, 2.2); // gamma decode
    r1.xyz = RestorePostProcess(colorU, colorN, r1.xyz, 0, true);
    r1.xyz = max(r1.xyz, 0);
    r1.xyz = pow(r1.xyz, 1/2.2); // gamma encode
  }

//   // HDR (inv tonemap by luminance)
//   if (isHDR) {
//     // gamma decode
//     r0.w = 1 / g_ToneMapParam.z;
//     r1.xyz = pow(r1.xyz, r0.w);
// 
//     r2.xyz = 1 + -r1.xyz;
//     r2.xyz = max(0.00999999978, r2.xyz); // safe
//     r2.xyz = r1.xyz / r2.xyz;
//     r0.w = 1 / g_ReinhardParam.x;
// 
//     r2.xyz = pow(r2.xyz, r0.w);
// 
//     r0.w = dot(r2.xyz, float3(0.298909992,0.586610019,0.114480004));
//     r1.w = pow(r0.w, g_ReinhardParam.x);
// 
//     r2.x = 1 + r1.w;
//     r1.w = r1.w / r2.x;
// 
//     r2.x = -1 + r0.w;
//     r2.x = r2.x * 0.0526315793 + 1;
//     r2.x = pow(r2.x, g_ReinhardParam.x);
//     r2.y = 1 + r2.x;
//     r2.x = r2.x / r2.y;
//     r2.x = -0.5 + r2.x;
//     r2.x = r2.x * 19 + 0.5;
// 
//     r0.w = cmp(1 < r0.w);
//     r0.w = r0.w ? r2.x : r1.w;
//     
//     r2.xyz = r1.xyz * r0.www;
// 
//     r0.w = dot(r1.xyz, float3(0.298909992,0.586610019,0.114480004)); //y
//     r0.w = 9.99999975e-005 + r0.w; //safe
//     r1.xyz = r2.xyz / r0.www;
// 
//     r1.xyz = g_vHDRDisplayParam.yyy * r1.xyz; // brightness/exposure
// 
//     // r1.xyz = pow(r1.xyz, 1.0 / 3.3); // WTH is this?
//     // r1.xyz *= 0.49770236 * 2.009233;
//     // r1.xyz = pow(r1.xyz, 1.5);
// 
//     // r1.xyz = pow(r1.xyz, 1 / 2.2); // gamma encode
//     r1.xyz = pow(r1.xyz, g_ToneMapParam.z);
//   }

  o0.xyz = r1.xyz;
  o0.w = 1;
  return;
}