// Shared ME3 stage-1 body for four filmic/LUT permutations, selected by TM_HAS_MOTIONBLUR and TM_HAS_GRAIN.
//   0x36B90B12 = MB              0x49BD5A95 = MB + grain
//   0x00944C2E = (bare)          0x5AA0BD09 = grain
// Analytic shader 0x225A8330 remains standalone because its cbuffer and curve differ.
//
// Grade and filmic paths are transcribed from 0x00944C2E; motion blur comes from 0x36B90B12. Preserve their
// register-level structure for comparison with live CSOs.
// ME3 deltas vs the ME2 body:
// - The 4096x1 R16_UNORM filmic LUT is the tonemap; input scale 0.0616082214 covers scene-linear to about 16.2.
// - Channels remain straight RGB, bloom uses a 4x scale, and motion blur weights each tap by velocity.
// - The smoothstep vignette contains a blue-tinted white point; the slider affects only radial darkening.
// - All variants share one $Globals layout; grain appends c40/c41 and moves ScreenUVScaleBias from c40 to c42.

// clang-format off
#include "Includes/Common.hlsl"      // Defines game settings; keep first.
#include "../Includes/Color.hlsl"    // Transfer and color helpers.
#include "../Includes/DICE.hlsl"     // Display-peak tonemap.
#include "../Includes/Reinhard.hlsl" // Reversible max-channel compression.
// clang-format on

#ifndef TM_HAS_MOTIONBLUR
#define TM_HAS_MOTIONBLUR 1
#endif
#ifndef TM_HAS_GRAIN
#define TM_HAS_GRAIN 1
#endif

// Decompiler artifact kept so the verbatim transcription compiles unchanged.
#define cmp -

// Texture and sampler registers follow this fixed order:
//   [depth vel (MB)] scene dof near far bloom lut [noise (grain)] filmic
// Unlike ME2, motion-blur velocity occupies t2 and shifts later DoF, bloom, and LUT slots by one.
#if TM_HAS_MOTIONBLUR
#define R_DEPTH   t0
#define R_SCENE   t1
#define R_VEL     t2
#define R_DOF     t3
#define R_DOFNEAR t4
#define R_DOFFAR  t5
#define R_BLOOM   t6
#define R_LUT     t7
#define S_DEPTH   s0
#define S_SCENE   s1
#define S_VEL     s2
#define S_DOF     s3
#define S_DOFNEAR s4
#define S_DOFFAR  s5
#define S_BLOOM   s6
#define S_LUT     s7
#if TM_HAS_GRAIN
#define R_NOISE  t8
#define S_NOISE  s8
#define R_FILMIC t9
#define S_FILMIC s9
#else
#define R_FILMIC t8
#define S_FILMIC s8
#endif
#else
#define R_SCENE   t0
#define R_DOF     t1
#define R_DOFNEAR t2
#define R_DOFFAR  t3
#define R_BLOOM   t4
#define R_LUT     t5
#define S_SCENE   s0
#define S_DOF     s1
#define S_DOFNEAR s2
#define S_DOFFAR  s3
#define S_BLOOM   s4
#define S_LUT     s5
#if TM_HAS_GRAIN
#define R_NOISE  t6
#define S_NOISE  s6
#define R_FILMIC t7
#define S_FILMIC s7
#else
#define R_FILMIC t6
#define S_FILMIC s6
#endif
#endif

cbuffer _Globals : register(b0)
{
   float4 PackedParameters : packoffset(c0);
   float4 InputTextureSize : packoffset(c1);
   float4 MinMaxBlurClamp : packoffset(c2);
   float4 DOFKernelParams : packoffset(c3);
   float4 RenderTargetClampParameter : packoffset(c4);
   float4 MotionBlurMaskScaleAndBias : packoffset(c5);
   float4x4 ScreenToWorld : packoffset(c6);
   float4x4 PrevViewProjMatrix : packoffset(c10);
   float4 StaticVelocityParameters : packoffset(c14);
   float4 DynamicVelocityParameters : packoffset(c15);
   float StepOffsetsOpaque[5] : packoffset(c16);
   float StepWeightsOpaque[5] : packoffset(c21);
   float StepOffsetsTranslucent[5] : packoffset(c26);
   float StepWeightsTranslucent[5] : packoffset(c31);
   float4 BloomTintAndScreenBlendThreshold : packoffset(c36);
   float4 HalfResMaskRect : packoffset(c37);
   float4 GammaColorScaleAndInverse : packoffset(c38);
   float4 GammaOverlayColor : packoffset(c39);
#if TM_HAS_GRAIN
   float4 NoiseTextureOffset : packoffset(c40);
   float FilmGrain_Scale : packoffset(c41);
   float4 ScreenUVScaleBias : packoffset(c42);
#else
   float4 ScreenUVScaleBias : packoffset(c40);
#endif
}

#if TM_HAS_MOTIONBLUR
Texture2D<float4> SceneDepthTexture : register(R_DEPTH);
Texture2D<float4> VelocityBuffer : register(R_VEL);
#endif
Texture2D<float4> SceneColorTexture : register(R_SCENE);
Texture2D<float4> DOFTexture : register(R_DOF);
Texture2D<float4> DOFBlurredNear : register(R_DOFNEAR);
Texture2D<float4> DOFBlurredFar : register(R_DOFFAR);
Texture2D<float4> BlurredImageSeperateBloom : register(R_BLOOM);
Texture2D<float4> ColorGradingLUT : register(R_LUT);
#if TM_HAS_GRAIN
Texture2D<float4> NoiseTexture : register(R_NOISE);
#endif
Texture2D<float4> smpFilmicLUT : register(R_FILMIC);

#if TM_HAS_MOTIONBLUR
SamplerState SceneDepthTextureSampler_s : register(S_DEPTH);
SamplerState VelocityBufferSampler_s : register(S_VEL);
#endif
SamplerState SceneColorTextureSampler_s : register(S_SCENE);
SamplerState DOFTextureSampler_s : register(S_DOF);
SamplerState DOFBlurredNearSampler_s : register(S_DOFNEAR);
SamplerState DOFBlurredFarSampler_s : register(S_DOFFAR);
SamplerState BlurredImageSeperateBloomSampler_s : register(S_BLOOM);
SamplerState ColorGradingLUTSampler_s : register(S_LUT);
#if TM_HAS_GRAIN
SamplerState NoiseTextureSampler_s : register(S_NOISE);
#endif
SamplerState smpFilmicLUTSampler_s : register(S_FILMIC);

// Native ME3 SDR grade transcribed from the live CSO, evaluated exactly once on the untouched per-channel value
// in every Display Mode: SDR is its output and nothing else, HDR only scales it. Preserve register-level
// operations; the filmic 1D LUT stays inline in main().
float3 MELE_ME3_GradeChain(float3 c)
{
   float4 r0, r1;
   r0.xyz = c;
   // Native 16-slice LUT: slice from B, strip x from R.
   r0.w = 15 * r0.z;
   r1.w = floor(r0.w);
   r0.z = r0.z * 15 + -r1.w;
   r1.x = r1.w * 0.0625 + (0.05859375 * r0.x);
   r1.y = 0.9375 * r0.y;
   r1.xyzw = float4(0.001953125, 0.03125, 0.064453125, 0.03125) + r1.xyxy;
   float3 lut_a = ColorGradingLUT.Sample(ColorGradingLUTSampler_s, r1.xy).xyz;
   float3 lut_b = ColorGradingLUT.Sample(ColorGradingLUTSampler_s, r1.zw).xyz;
   r0.xyz = r0.zzz * (lut_b - lut_a) + lut_a;
   // Native GammaOverlayColor and SDR gamma curve.
   r0.xyz = GammaOverlayColor.xyz + r0.xyz;
   r0.xyz = MELE_NativeGammaCurve(r0.xyz, GammaColorScaleAndInverse.xyz, GammaColorScaleAndInverse.w, true);

   return r0.xyz;
}

// Included here, not with the headers: MELE_CompositeDOF reads the _Globals fields and DOF textures declared above.
#include "Includes/Tonemap_MELE_Filmic.hlsli"
#include "Includes/Tonemap_MELE_Scene.hlsli"

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
   // Native camera blur from 0x36B90B12. VelocityBuffer.x is a SoftEdge mask; each tap uses weight 0.2*velocity
   // and the result is normalized by their sum.
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
   r1.w = VelocityBuffer.Sample(VelocityBufferSampler_s, r2.xy).x;
   r2.z = 0.200000003 * r1.w;
   r2.xyw = SceneColorTexture.Sample(SceneColorTextureSampler_s, r2.xy).xyz;
   r2.xyz = r2.xyw * r2.zzz;
   r2.xyz = r1.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r2.xyz;
   r1.w = r1.w * 0.200000003 + 0.200000003;
   r3.xy = r0.zw * StepOffsetsOpaque[2] + r0.xy;
   r3.xy = max(RenderTargetClampParameter.xy, r3.xy);
   r3.xy = min(RenderTargetClampParameter.zw, r3.xy);
   r2.w = VelocityBuffer.Sample(VelocityBufferSampler_s, r3.xy).x;
   r3.z = 0.200000003 * r2.w;
   r3.xyw = SceneColorTexture.Sample(SceneColorTextureSampler_s, r3.xy).xyz;
   r2.xyz = r3.xyw * r3.zzz + r2.xyz;
   r1.w = r2.w * 0.200000003 + r1.w;
   r3.xy = r0.zw * StepOffsetsOpaque[3] + r0.xy;
   r3.xy = max(RenderTargetClampParameter.xy, r3.xy);
   r3.xy = min(RenderTargetClampParameter.zw, r3.xy);
   r2.w = VelocityBuffer.Sample(VelocityBufferSampler_s, r3.xy).x;
   r3.z = 0.200000003 * r2.w;
   r3.xyw = SceneColorTexture.Sample(SceneColorTextureSampler_s, r3.xy).xyz;
   r2.xyz = r3.xyw * r3.zzz + r2.xyz;
   r1.w = r2.w * 0.200000003 + r1.w;
   r3.xy = r0.zw * StepOffsetsOpaque[4] + r0.xy;
   r3.xy = max(RenderTargetClampParameter.xy, r3.xy);
   r3.xy = min(RenderTargetClampParameter.zw, r3.xy);
   r2.w = VelocityBuffer.Sample(VelocityBufferSampler_s, r3.xy).x;
   r3.z = 0.200000003 * r2.w;
   r3.xyw = SceneColorTexture.Sample(SceneColorTextureSampler_s, r3.xy).xyz;
   r2.xyz = r3.xyw * r3.zzz + r2.xyz;
   r1.w = r2.w * 0.200000003 + r1.w;
   r2.xyz = r2.xyz / r1.www;
   r0.zw = MotionBlurMaskScaleAndBias.xy * r0.zw;
   r0.z = dot(r0.zw, r0.zw);
   r0.z = sqrt(r0.z);
   r0.z = min(1, r0.z);
   r2.xyz = r2.xyz + -r1.xyz;
   r1.xyz = r0.zzz * r2.xyz + r1.xyz;
#endif
   r0.zw = cmp(float2(0, 0) < MinMaxBlurClamp.xy);
   r1.w = (int)r0.w | (int)r0.z;

   // Trilogy-wide native composite of fp16 near/far DoF buffers.
   if (r1.w != 0)
   {
      r1.xyz = MELE_CompositeDOF(r0.xy, r0.zw, r1.xyz);
   }

   // Scene-referred exposure before SDR and HDR tonemapping.
   r1.xyz = r1.xyz * LumaSettings.GameSettings.Exposure;
   // Native bloom screen blend from 0x00944C2E.
   r0.xyz = MELE_BloomScreenBlend(r0.xy, r1.xyz, r0.w);

   // Linear HDR scene plus bloom.
   float3 untonemapped = r0.xyz * r0.www + r1.xyz;
   r0.xyz = untonemapped;

   // Native per-channel 4096x1 R16_UNORM filmic tonemap; input scale covers scene-linear to about 16.2.
   r0.xyz = float3(0.0616082214, 0.0616082214, 0.0616082214) * r0.xyz;
   r0.x = smpFilmicLUT.Sample(smpFilmicLUTSampler_s, r0.xx).x;
   r0.y = smpFilmicLUT.Sample(smpFilmicLUTSampler_s, r0.yy).x;
   r0.z = smpFilmicLUT.Sample(smpFilmicLUTSampler_s, r0.zz).x;
   // The native per-channel filmic value reaches the grade untouched, so the vanilla white blowout survives into
   // HDR: only the hue-preserving expansion scalar comes from the wrap.
   float mele_expand = 1.0; // Post-grade uncompression; 1 in the native range.
   if (LumaSettings.DisplayMode == 1)
   {
      mele_expand = MELE_FilmicMaxChannelExpand(untonemapped);
   }

   // Use one native grade function for both the working value and SDR reference.
   float3 sdr_gamma = MELE_ME3_GradeChain(r0.xyz);

   // Scalar uncompression preserves native mids/shadows and restores extrapolated HDR highlights. SDR leaves
   // mele_expand at 1.
   float3 graded_hdr = gamma_to_linear(sdr_gamma, GCT_MIRROR) * mele_expand;

   // ME3 tail: smoothstep vignette, optional grain, and native output luma in alpha.
#define TM_VIGNETTE_TYPE 3
#define TM_ALPHA_LUMA    1
#include "Includes/Tonemap_MELE_Output.hlsli"
}
