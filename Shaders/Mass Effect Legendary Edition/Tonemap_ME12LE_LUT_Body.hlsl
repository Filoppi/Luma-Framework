// Shared ME1LE/ME2LE stage-1 body for twelve LUT-graded permutations, selected by TM_HAS_MOTIONBLUR, TM_HAS_GRAIN,
// TM_HAS_FILMIC and the ME1LE vignette overrides. ME1LE has no filmic axis; with the axes fixed the two games'
// non-filmic paths are identical, verified bit-exact against the ME1LE bytecode. The analytic shaders 0xAAE8755A
// (ME1LE) and 0xCC76075F (ME2LE) use the shared analytic body instead.
//   ME2LE: 0x2754F750 = MB         0x1536C5B5 = MB + grain      0x940979D8 = MB + filmic
//          0x75BFAFBC = MB + grain + filmic
//          0xD077D06B = (bare LUT) 0x8E0C0DBB = grain           0x222186F8 = filmic
//          0xEC890842 = grain + filmic
//   ME1LE: 0x151FE4CA = MB + grain 0x69F03340 = MB              0x109F3B6E = grain
//          0x8C8E8CA2 = (bare LUT)
//
// Non-filmic grading is transcribed from 0x2754F750 and filmic grading from 0x222186F8; their register-level
// swizzles differ, are each internally rotation-consistent, and must be preserved. The body emits linear
// gradedHDR and native gamma sdrGamma for the shared DICE/output tail.

// clang-format off
#include "Includes/Common.hlsl"      // Defines game settings; keep first.
#include "../Includes/Color.hlsl"    // Transfer and color helpers.
#include "../Includes/DICE.hlsl"     // Display-peak tonemap.
#include "../Includes/Reinhard.hlsl" // ReinhardRange, used by the grade proxy.
#include "Includes/Tonemap_MELE_HDRConfig.hlsli"   // HDR reconstruction constants.
#include "Includes/Tonemap_MELE_ExpExtended.hlsli" // Continued scene curve and its validated work input.
#include "Includes/Tonemap_MELE_HDRBridge.hlsli"   // Max-channel grade proxy; needs Reinhard above.
// clang-format on

#ifndef TM_HAS_MOTIONBLUR
#define TM_HAS_MOTIONBLUR 1
#endif
#ifndef TM_HAS_GRAIN
#define TM_HAS_GRAIN 1
#endif
#ifndef TM_HAS_FILMIC
#define TM_HAS_FILMIC 0
#endif

// Decompiler artifact kept so the verbatim transcription compiles unchanged.
#define cmp -

// Texture and sampler registers follow this fixed order:
//   [depth (MB)] scene dof near far bloom lut [velocity (MB)] [noise (grain)] [filmic]
#if TM_HAS_MOTIONBLUR
#define R_DEPTH   t0
#define R_SCENE   t1
#define R_DOF     t2
#define R_DOFNEAR t3
#define R_DOFFAR  t4
#define R_BLOOM   t5
#define R_LUT     t6
#define R_VEL     t7
#define R_NOISE   t8
#if TM_HAS_GRAIN
#define R_FILMIC t9
#else
#define R_FILMIC t8
#endif
#define S_DEPTH   s0
#define S_SCENE   s1
#define S_DOF     s2
#define S_DOFNEAR s3
#define S_DOFFAR  s4
#define S_BLOOM   s5
#define S_LUT     s6
#define S_VEL     s7
#define S_NOISE   s8
#if TM_HAS_GRAIN
#define S_FILMIC s9
#else
#define S_FILMIC s8
#endif
#define CO_NOISEOFFSET c39
#define CO_FILMGRAIN   c40
#else
#define R_SCENE   t0
#define R_DOF     t1
#define R_DOFNEAR t2
#define R_DOFFAR  t3
#define R_BLOOM   t4
#define R_LUT     t5
#define R_NOISE   t6
#if TM_HAS_GRAIN
#define R_FILMIC t7
#define S_FILMIC s7
#else
#define R_FILMIC t6
#define S_FILMIC s6
#endif
#define S_SCENE        s0
#define S_DOF          s1
#define S_DOFNEAR      s2
#define S_DOFFAR       s3
#define S_BLOOM        s4
#define S_LUT          s5
#define S_NOISE        s6
#define CO_NOISEOFFSET c7
#define CO_FILMGRAIN   c8
#endif

cbuffer _Globals : register(b0)
{
   float4 PackedParameters : packoffset(c0);
   float4 InputTextureSize : packoffset(c1);
   float4 MinMaxBlurClamp : packoffset(c2);
   float4 DOFKernelParams : packoffset(c3);
   float4 BloomTintAndScreenBlendThreshold : packoffset(c4);
   float4 GammaColorScaleAndInverse : packoffset(c5);
   float4 GammaOverlayColor : packoffset(c6);
#if TM_HAS_MOTIONBLUR
   float4 RenderTargetClampParameter : packoffset(c7);
   float4 MotionBlurMaskScaleAndBias : packoffset(c8);
   float4x4 ScreenToWorld : packoffset(c9);
   float4x4 PrevViewProjMatrix : packoffset(c13);
   float4 StaticVelocityParameters : packoffset(c17);
   float4 DynamicVelocityParameters : packoffset(c18);
   float StepOffsetsOpaque[5] : packoffset(c19);
   float StepWeightsOpaque[5] : packoffset(c24);
   float StepOffsetsTranslucent[5] : packoffset(c29);
   float StepWeightsTranslucent[5] : packoffset(c34);
#endif
#if TM_HAS_GRAIN
   float4 NoiseTextureOffset : packoffset(CO_NOISEOFFSET);
   float FilmGrain_Scale : packoffset(CO_FILMGRAIN);
#endif
}

#if TM_HAS_MOTIONBLUR
Texture2D<float4> SceneDepthTexture : register(R_DEPTH);
#endif
Texture2D<float4> SceneColorTexture : register(R_SCENE);
Texture2D<float4> DOFTexture : register(R_DOF);
Texture2D<float4> DOFBlurredNear : register(R_DOFNEAR);
Texture2D<float4> DOFBlurredFar : register(R_DOFFAR);
Texture2D<float4> BlurredImageSeperateBloom : register(R_BLOOM);
Texture2D<float4> ColorGradingLUT : register(R_LUT);
#if TM_HAS_MOTIONBLUR
Texture2D<float4> VelocityBuffer : register(R_VEL);
#endif
#if TM_HAS_GRAIN
Texture2D<float4> NoiseTexture : register(R_NOISE);
#endif
#if TM_HAS_FILMIC
Texture2D<float4> smpFilmicLUT : register(R_FILMIC);
#endif

#if TM_HAS_MOTIONBLUR
SamplerState SceneDepthTextureSampler_s : register(S_DEPTH);
#endif
SamplerState SceneColorTextureSampler_s : register(S_SCENE);
SamplerState DOFTextureSampler_s : register(S_DOF);
SamplerState DOFBlurredNearSampler_s : register(S_DOFNEAR);
SamplerState DOFBlurredFarSampler_s : register(S_DOFFAR);
SamplerState BlurredImageSeperateBloomSampler_s : register(S_BLOOM);
SamplerState ColorGradingLUTSampler_s : register(S_LUT);
#if TM_HAS_MOTIONBLUR
SamplerState VelocityBufferSampler_s : register(S_VEL);
#endif
#if TM_HAS_GRAIN
SamplerState NoiseTextureSampler_s : register(S_NOISE);
#endif
#if TM_HAS_FILMIC
SamplerState smpFilmicLUTSampler_s : register(S_FILMIC);
#endif

// Native ME1LE/ME2LE SDR grade transcribed from live CSOs. main() runs it once on the untouched native value in every
// Display Mode and, in HDR, once more on the grade proxy; HDR keeps the native result's RGB ratios and takes only
// luminance from the reconstruction. Preserve register-level swizzles; the filmic 1D LUT stays inline in main().
float3 MELE_ME12LE_GradeChain(float3 c)
{
   float4 r0, r1, r2;
#if TM_HAS_FILMIC
   r1.xyz = c;
   // Native 16-slice LUT interpolation for filmic variants.
   r0.yzw = float3(15, 0.05859375, 0.9375) * r1.xyz;
   r0.y = floor(r0.y);
   r1.x = r1.x * 15 + -r0.y;
   r0.x = r0.y * 0.0625 + r0.z;
   r0.xyzw = float4(0.001953125, 0.03125, 0.064453125, 0.03125) + r0.xwxw;
   r1.yzw = ColorGradingLUT.Sample(ColorGradingLUTSampler_s, r0.xy).xyz;
   r0.xyz = ColorGradingLUT.Sample(ColorGradingLUTSampler_s, r0.zw).xyz;
   r0.xyz = r0.xyz + -r1.yzw;
   r0.xyz = r1.xxx * r0.xyz + r1.yzw;
#else
   r0.xyz = c;
   // Native highlight desaturation; keep swizzles.
   r1.xyz = float3(0.993047416, 0.98082906, 0.980000436) * r0.xyz;
   r0.w = dot(r0.yzx, float3(0.333000004, 0.333000004, 0.333000004));
   r0.w = cmp(1.10000002 < r0.w);
   r1.w = r0.w ? 1.000000 : 0;
   r2.x = dot(r1.yzx, float3(0.300000012, 0.589999974, 0.109999999));
   r2.xyz = -r0.xyz * float3(0.993047416, 0.98082906, 0.980000436) + r2.xxx;
   r1.xyz = r2.xyz * float3(0.5, 0.5, 0.5) + r1.xyz;
   r0.w = r0.w ? 0 : 1;
   r0.xyz = r0.www * r0.xyz;
   r0.xyz = r1.www * r1.xyz + r0.xyz;
   // Native ImageAdjustments mix.
   r0.w = dot(r0.yzx, float3(0.300000012, 0.589999974, 0.109999999));
   r1.xyz = float3(0.400000006, 0.400000006, 0.400000006) * r0.xyz;
   r1.xyz = r0.www * float3(0.600000024, 0.600000024, 0.600000024) + r1.xyz;
   r1.xyz = r1.xyz * float3(1, 0.00658500008, 0.0199180003) + -r0.xyz;
   r0.xyz = saturate(r1.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r0.xyz);
   // Native 16-slice LUT interpolation.
   r1.yzw = float3(15, 0.05859375, 0.9375) * r0.xyz;
   r0.y = floor(r1.y);
   r0.x = r0.x * 15 + -r0.y;
   r1.x = r0.y * 0.0625 + r1.z;
   r1.xyzw = float4(0.001953125, 0.03125, 0.064453125, 0.03125) + r1.xwxw;
   r0.yzw = ColorGradingLUT.Sample(ColorGradingLUTSampler_s, r1.xy).xyz;
   r1.xyz = ColorGradingLUT.Sample(ColorGradingLUTSampler_s, r1.zw).xyz;
   r1.xyz = r1.xyz + -r0.yzw;
   r0.xyz = r0.xxx * r1.xyz + r0.yzw;
#endif
   // Native GammaOverlayColor and SDR gamma curve.
   r0.xyz = GammaOverlayColor.xyz + r0.xyz;
   r0.xyz = MELE_NativeGammaCurve(r0.xyz, GammaColorScaleAndInverse.xyz, GammaColorScaleAndInverse.w, true);

   return r0.xyz;
}

// Included here, not with the headers: MELE_CompositeDOF reads the _Globals fields and DOF textures declared above.
#include "Includes/Tonemap_MELE_Scene.hlsli"
#if TM_HAS_FILMIC
#include "Includes/Tonemap_MELE_FilmicExtended.hlsli"
#endif

void main(
    float4 v0 : TEXCOORD0,
    float2 v1 : TEXCOORD1,
    out float4 o0 : SV_Target0,
    out float o1 : SV_Target1)
{
   float4 r0, r1, r2, r3, r4;

   r0.xy = DynamicScale.xy * v0.zw;
   r1.xyz = SceneColorTexture.Sample(SceneColorTextureSampler_s, r0.xy).xyz;

#if TM_HAS_MOTIONBLUR
   // Native five-tap camera blur from 0x2754F750 (SFXMotionBlur in MotionBlurCommon.usf). VelocityBuffer.x is its
   // DynamicVelocity.x, which scales the camera vector.
   r0.z = VelocityBuffer.Sample(VelocityBufferSampler_s, r0.xy).x;
   r0.w = SceneDepthTexture.Sample(SceneDepthTextureSampler_s, r0.xy).x;
   r0.w = r0.w * MinZ_MaxZRatio.z + -MinZ_MaxZRatio.w;
   r0.w = max(1.00000001e-07, r0.w);
   r0.w = 1 / r0.w;
   r0.w = min(65504, r0.w);
   r1.w = cmp(r0.w < 14);
   r0.w = r1.w ? 65504 : r0.w;
   r2.xy = v0.xy * r0.ww;
   r2.yzw = PrevViewProjMatrix._m01_m11_m31 * r2.yyy;
   r2.xyz = PrevViewProjMatrix._m00_m10_m30 * r2.xxx + r2.yzw;
   r2.xyz = PrevViewProjMatrix._m02_m12_m32 * r0.www + r2.xyz;
   r2.xyz = PrevViewProjMatrix._m03_m13_m33 + r2.xyz;
   r2.xy = r2.xy / r2.zz;
   r2.xy = v0.xy + -r2.xy;
   r2.xy = StaticVelocityParameters.xy * r2.xy;
   r0.w = dot(r2.xy, r2.xy);
   r0.w = max(1, r0.w);
   r0.w = rsqrt(r0.w);
   r2.xy = r2.xy * r0.ww;
   r0.zw = r2.xy * r0.zz;
   r0.zw = DynamicVelocityParameters.xy * r0.zw;
   r2.xy = r0.zw * StepOffsetsOpaque[1] + r0.xy;
   r2.xy = max(RenderTargetClampParameter.xy, r2.xy);
   r2.xy = min(RenderTargetClampParameter.zw, r2.xy);
   r2.xyz = SceneColorTexture.Sample(SceneColorTextureSampler_s, r2.xy).xyz;
   r2.xyz = float3(0.200000003, 0.200000003, 0.200000003) * r2.xyz;
   r2.xyz = r1.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r2.xyz;
   r3.xy = r0.zw * StepOffsetsOpaque[2] + r0.xy;
   r3.xy = max(RenderTargetClampParameter.xy, r3.xy);
   r3.xy = min(RenderTargetClampParameter.zw, r3.xy);
   r3.xyz = SceneColorTexture.Sample(SceneColorTextureSampler_s, r3.xy).xyz;
   r2.xyz = r3.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r2.xyz;
   r3.xy = r0.zw * StepOffsetsOpaque[3] + r0.xy;
   r3.xy = max(RenderTargetClampParameter.xy, r3.xy);
   r3.xy = min(RenderTargetClampParameter.zw, r3.xy);
   r3.xyz = SceneColorTexture.Sample(SceneColorTextureSampler_s, r3.xy).xyz;
   r2.xyz = r3.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r2.xyz;
   r3.xy = r0.zw * StepOffsetsOpaque[4] + r0.xy;
   r3.xy = max(RenderTargetClampParameter.xy, r3.xy);
   r3.xy = min(RenderTargetClampParameter.zw, r3.xy);
   r3.xyz = SceneColorTexture.Sample(SceneColorTextureSampler_s, r3.xy).xyz;
   r2.xyz = r3.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r2.xyz;
   r0.zw = MotionBlurMaskScaleAndBias.xy * r0.zw;
   r0.z = dot(r0.zw, r0.zw);
   r0.z = sqrt(r0.z);
   r0.z = min(1, r0.z);
   r2.xyz = r2.xyz + -r1.xyz;
   r1.xyz = r0.zzz * r2.xyz + r1.xyz;
#endif
   r0.zw = cmp(float2(0, 0) < MinMaxBlurClamp.xy);
   r1.w = (int)r0.w | (int)r0.z;

   // Native near/far depth-of-field composite.
   if (r1.w != 0)
   {
      r1.xyz = MELE_CompositeDOF(r0.xy, r0.zw, r1.xyz);
   }

   // Scene-referred exposure before SDR and HDR tonemapping.
   r1.xyz = r1.xyz * LumaSettings.GameSettings.Exposure;

#if TM_HAS_FILMIC
   // Filmic path from 0x222186F8: bloom, exponential curve, then per-channel 4096x1 LUT. DELIBERATE DEVIATION: the
   // screen-blend weight reads the luma of the LINEAR scene, where the native CSO reads it after the curve (always 1
   // at the measured threshold). Bright pixels therefore lose their bloom; that keeps highlight detail and colour in
   // HDR, and the families were calibrated with it. Restoring the native weight was A/B'd in game and rejected; do not
   // change the weight or its input without an in-game A/B (see Shaders/Mass Effect Legendary Edition/AGENTS.md).
   r0.xyz = MELE_BloomScreenBlend(r0.xy, r1.xyz, r0.w);

   // Kept separate on purpose: the game evaluates L(F(C) + B), so C and B must not be summed before the
   // pre-curve.
   const float3 sceneLinear = r1.xyz;
   const float3 bloomLinear = r0.xyz * r0.www;

   // Native per-channel SDR curve: 1 - exp2(-1.7 * scene).
   r1.xyz = float3(-1.70000005, -1.70000005, -1.70000005) * r1.xyz;
   r1.xyz = exp2(r1.xyz);
   r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r0.xyz = r0.xyz * r0.www + r1.xyz;

   // Native 4096x1 R16_UNORM filmic LUT. Preserve its channel rotation.
   r0.xyz = float3(0.0616082214, 0.0616082214, 0.0616082214) * r0.xyz;
   r1.y = smpFilmicLUT.Sample(smpFilmicLUTSampler_s, r0.xx).x;
   r1.z = smpFilmicLUT.Sample(smpFilmicLUTSampler_s, r0.yy).x;
   r1.x = smpFilmicLUT.Sample(smpFilmicLUTSampler_s, r0.zz).x;
   r1.xyz = saturate(r1.xyz);
   // r1.xyz keeps the native post-filmic value, which reaches the 16-slice LUT untouched as this branch's SDR result.
#else
   // Non-filmic path from 0x2754F750: bloom, exponential curve, highlight desaturation, adjustments, then LUT.
   // DELIBERATE DEVIATION: the screen-blend weight reads the LINEAR RGB scene through r1.yzx, i.e. in GBR order (native
   // reads the curved BRG scene, where .yzx restores RGB), so bright pixels lose their bloom, with a hue-dependent
   // threshold. Keeps highlight detail and colour in HDR; the families were calibrated with it, and the native weight
   // and an RGB-order linear weight were both A/B'd in game and rejected. Do not change the weight, its input or its
   // channel order without an in-game A/B (see Shaders/Mass Effect Legendary Edition/AGENTS.md).
   r0.xyz = BlurredImageSeperateBloom.Sample(BlurredImageSeperateBloomSampler_s, r0.xy).xyz * LumaSettings.GameSettings.BloomIntensity;
   r0.xyz = BloomTintAndScreenBlendThreshold.zxy * r0.zxy;
   r0.w = dot(r1.yzx, float3(0.298999995, 0.587000012, 0.114));
   r0.xyzw = float4(4, 4, 4, -3) * r0.xyzw;
   r0.w = exp2(r0.w);
   r0.w = saturate(BloomTintAndScreenBlendThreshold.w * r0.w);

   // Captured before the curve below rewrites r1. Both are RGB: the blend above built the bloom in BRG and the .yzx
   // below rotates it back, while r1 still holds the post-exposure scene in RGB.
   const float3 sceneLinear = r1.xyz;
   const float3 bloomLinear = r0.yzx * r0.www;

   // Native per-channel SDR curve: 1 - exp2(-1.7 * scene).
   r1.xyz = float3(-1.70000005, -1.70000005, -1.70000005) * r1.zxy;
   r1.xyz = exp2(r1.xyz);
   r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r0.xyz = r0.xyz * r0.www + r1.xyz;

   // r0.xyz keeps the native per-channel value, which reaches the grade untouched as this branch's SDR result.
#endif

   // HDR working value. Family 01 grades the native curve continued on the scene; family 03 also continues the
   // second tone stage, the filmic LUT. Both reduce to the native grade input below their pivots.
   float3 workHDR = 0.0;
   bool workValid = false;
   if (LumaSettings.DisplayMode == 1)
   {
      float3 workRGB;
      bool sourceValid = MELE_TryExpExtendedInput(sceneLinear, bloomLinear, workRGB);
#if TM_HAS_FILMIC
      // workRGB, the continued scene plus bloom, is the filmic LUT's input z. The native filmic samples in r1 were
      // written BRG, so they are rotated to RGB here.
      const float3 z = workRGB;
      if (sourceValid)
      {
         sourceValid = MELE_TryEvaluateME2LEFilmicExtended(z, r1.yzx, workRGB);
      }
#endif
      // The bridge drives the real grade and 16-slice LUT, whose chain is transcribed BRG-in / RGB-out in both
      // branches, so the proxy is rotated into it. r is read from the frame's own cbuffer, never assumed to be 1.
      float q;
      float3 proxyRGB;
      if (sourceValid && MELE_TryBuildGradeProxy(workRGB, GammaColorScaleAndInverse.w * DefaultGamma, q, proxyRGB))
      {
         workValid = MELE_TryRestoreGradeRange(gamma_to_linear(MELE_ME12LE_GradeChain(proxyRGB.zxy), GCT_MIRROR), q, workHDR);
      }
   }

   // Use one native grade function for both the working value and SDR reference. Filmic feeds r1; non-filmic r0.
#if TM_HAS_FILMIC
   float3 sdrGamma = MELE_ME12LE_GradeChain(r1.xyz);
#else
   float3 sdrGamma = MELE_ME12LE_GradeChain(r0.xyz);
#endif
   float3 gradedHDR = MELE_NativeColorGradedHDR(sdrGamma, workHDR, workValid);

   // Shared tail: radial vignette, optional grain, and zero alpha. Defaults are ME2LE's power-200 curve and blue
   // white point; ME1LE entry points override both.
#define TM_VIGNETTE_TYPE 1
#ifndef TM_VIG_POW
#define TM_VIG_POW 200.0
#endif
#ifndef TM_VIG_FLOOR
#define TM_VIG_FLOOR kMELE_ME2LEVignetteFloor
#endif
#include "Includes/Tonemap_MELE_Output.hlsli"
}
