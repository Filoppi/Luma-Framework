// ME3 analytic stage-1 permutation used by the galaxy map and some cutscenes. It has no LUT, motion blur, grain,
// or pre-grade tonemap curve: analytic Scene* operates directly on linear scene+bloom, followed by gamma, the
// ME3 blue-tinted white point, and a clamp. Bindings: t0 scene, t1 DoF, t2/t3 near/far DoF, t4 bloom.
//
// Transcribed from live CSO 0x225A8330. With only an SDR clamp, the reversible max-channel wrap uses an identity
// 0.18 -> 0.18 anchor before restoring linear HDR highlights.

// clang-format off
#include "Includes/Common.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl" // ReinhardScalable (max-channel compress)
// clang-format on

#define cmp -

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
   float4 SceneShadowsAndDesaturation : packoffset(c38);
   float4 SceneInverseHighLights : packoffset(c39);
   float4 SceneMidTones : packoffset(c40);
   float4 SceneScaledLuminanceWeights : packoffset(c41);
   float4 GammaColorScaleAndInverse : packoffset(c42);
   float4 GammaOverlayColor : packoffset(c43);
}

SamplerState SceneColorTextureSampler_s : register(s0);
SamplerState DOFTextureSampler_s : register(s1);
SamplerState DOFBlurredNearSampler_s : register(s2);
SamplerState DOFBlurredFarSampler_s : register(s3);
SamplerState BlurredImageSeperateBloomSampler_s : register(s4);
Texture2D<float4> SceneColorTexture : register(t0);
Texture2D<float4> DOFTexture : register(t1);
Texture2D<float4> DOFBlurredNear : register(t2);
Texture2D<float4> DOFBlurredFar : register(t3);
Texture2D<float4> BlurredImageSeperateBloom : register(t4);

// Native analytic SDR grade transcribed from the live CSO, evaluated exactly once on the untouched per-channel
// value in every Display Mode: SDR is its output and nothing else, HDR only scales it. Preserve its
// register-level operations.
float3 MELE_ME3Analytic_GradeChain(float3 c)
{
   float4 r0;
   r0.xyz = c;
   // Native analytic Scene grade directly on linear input.
   r0.xyz = saturate(-SceneShadowsAndDesaturation.xyz + r0.xyz);
   r0.xyz = SceneInverseHighLights.xyz * r0.xyz;
   r0.xyz = log2(r0.xyz);
   r0.xyz = SceneMidTones.xyz * r0.xyz;
   r0.xyz = exp2(r0.xyz);
   r0.w = dot(r0.xyz, SceneScaledLuminanceWeights.xyz);
   r0.xyz = r0.xyz * SceneShadowsAndDesaturation.www + r0.www;
   r0.xyz = GammaOverlayColor.xyz + r0.xyz;
   // Native SDR gamma curve and ME3 white point.
   r0.xyz = MELE_NativeGammaCurve(r0.xyz, GammaColorScaleAndInverse.xyz, GammaColorScaleAndInverse.w, true);

   r0.xyz = float3(1.01036298, 1.00000572, 1.16309249) * r0.xyz; // Blue-tinted white point; no radial vignette.
   r0.xyz = min(float3(1, 1, 1), r0.xyz);
   return r0.xyz;
}

// Included here, not with the headers: MELE_CompositeDOF reads the _Globals fields and DOF textures declared above.
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
   r0.zw = cmp(float2(0, 0) < MinMaxBlurClamp.xy);
   r1.w = (int)r0.w | (int)r0.z;

   // Trilogy-wide native near/far depth-of-field composite.
   if (r1.w != 0)
   {
      r1.xyz = MELE_CompositeDOF(r0.xy, r0.zw, r1.xyz);
   }

   // Scene-referred exposure before tonemapping.
   r1.xyz = r1.xyz * LumaSettings.GameSettings.Exposure;

   // Native bloom screen blend using Luma's rebound fp16 bloom.
   r0.xyz = MELE_BloomScreenBlend(r0.xy, r1.xyz, r0.w);

   float3 untonemapped = r0.xyz * r0.www + r1.xyz;
   r0.xyz = untonemapped;
   // No tonemap curve here, so the raw scene reaches the grade and the grade chain's own clamp is this
   // permutation's vanilla blowout. HDR only measures the reversible max-channel ratio, with an identity mid-gray
   // anchor because native SDR has no curve to match.
   float mele_scale = 1.0;
   if (LumaSettings.DisplayMode == 1)
   {
      float mele_mch = max(max3(untonemapped), 1e-6);
      mele_scale = Reinhard::ReinhardScalable(mele_mch, 1.0, 0.0, 0.18, 0.18) / mele_mch;
   }

   float3 sdr_gamma = MELE_ME3Analytic_GradeChain(r0.xyz);

   // Undo compression only where scale < 1, preserving native diffuse/shadow grading and restoring HDR
   // highlights. SDR leaves mele_scale at 1.
   float3 graded_hdr = gamma_to_linear(sdr_gamma, GCT_MIRROR) / min(1.0, mele_scale);

   // ME3 analytic tail: no vignette or grain; preserve native output luma in alpha.
#define TM_VIGNETTE_TYPE 0
#define TM_ALPHA_LUMA    1
#include "Includes/Tonemap_MELE_Output.hlsli"
}
