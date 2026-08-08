// Shared stage-1 body for ME1/ME2 analytic scene permutations used by the galaxy map, some Mako scenes, and
// cutscenes. These permutations have no LUT, motion blur, or film grain. Bindings: t0 scene, t1 DoF, t2/t3
// near/far DoF, t4 bloom.
//
// ME1 0xAAE8755A and ME2 0xCC76075F share the decompiled scene preparation and grade. Thin entry points select
// vignette parameters and ME2's post-gamma white point. Preserve register-level swizzles for comparison with the
// live CSOs. This body produces linear graded_hdr and native gamma sdr_gamma; the shared tail applies DICE.
// ME3 analytic shader 0x225A8330 has a different cbuffer layout and no exponential curve.

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
   float4 BloomTintAndScreenBlendThreshold : packoffset(c4);
   float4 SceneShadowsAndDesaturation : packoffset(c5);
   float4 SceneInverseHighLights : packoffset(c6);
   float4 SceneMidTones : packoffset(c7);
   float4 SceneScaledLuminanceWeights : packoffset(c8);
   float4 GammaColorScaleAndInverse : packoffset(c9);
   float4 GammaOverlayColor : packoffset(c10);
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

// Native analytic SDR grade transcribed from the live CSOs, evaluated exactly once on the untouched per-channel
// value in every Display Mode: SDR is its output and nothing else, HDR only scales it. Keep its register-level
// swizzles and optional ME2 white point unchanged.
float3 MELE_Analytic_GradeChain(float3 c)
{
   float4 r0, r1, r2;
   r0.xyz = c;
   // Native highlight desaturation.
   r1.xyz = float3(0.98082906, 0.980000436, 0.993047416) * r0.xyz;
   r0.w = dot(r0.xyz, float3(0.333000004, 0.333000004, 0.333000004));
   r0.w = cmp(1.10000002 < r0.w);
   r1.w = r0.w ? 1.000000 : 0;
   r2.x = dot(r1.xyz, float3(0.300000012, 0.589999974, 0.109999999));
   r2.xyz = -r0.xyz * float3(0.98082906, 0.980000436, 0.993047416) + r2.xxx;
   r1.xyz = r2.xyz * float3(0.5, 0.5, 0.5) + r1.xyz;
   r0.w = r0.w ? 0 : 1;
   r0.xyz = r0.www * r0.xyz;
   r0.xyz = r1.www * r1.xyz + r0.xyz;
   // Native ImageAdjustments mix.
   r0.w = dot(r0.xyz, float3(0.300000012, 0.589999974, 0.109999999));
   r1.xyz = float3(0.400000006, 0.400000006, 0.400000006) * r0.xyz;
   r1.xyz = r0.www * float3(0.600000024, 0.600000024, 0.600000024) + r1.xyz;
   r1.xyz = r1.xyz * float3(0.00658500008, 0.0199180003, 1) + -r0.xyz;
   r0.xyz = r1.xyz * float3(0.200000003, 0.200000003, 0.200000003) + r0.xyz;
   // Native analytic Scene grade.
   r0.xyz = saturate(-SceneShadowsAndDesaturation.xyz + r0.xyz);
   r0.xyz = SceneInverseHighLights.xyz * r0.xyz;
   r0.xyz = log2(r0.xyz);
   r0.xyz = SceneMidTones.xyz * r0.xyz;
   r0.xyz = exp2(r0.xyz);
   r0.w = dot(r0.xyz, SceneScaledLuminanceWeights.xyz);
   r0.xyz = r0.xyz * SceneShadowsAndDesaturation.www + GammaOverlayColor.xyz;
   r0.xyz = r0.xyz + r0.www;
   // Native SDR gamma curve.
   r0.xyz = MELE_NativeGammaCurve(r0.xyz, GammaColorScaleAndInverse.xyz, GammaColorScaleAndInverse.w, false);

#ifdef TM_ANALYTIC_WHITEPOINT
   r0.xyz = TM_ANALYTIC_WHITEPOINT * r0.xyz; // ME2 blue-tinted white point; ME1 defines none.
#endif
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

   // Native near/far depth-of-field composite.
   if (r1.w != 0)
   {
      r1.xyz = MELE_CompositeDOF(r0.xy, r0.zw, r1.xyz);
   }

   // Scene-referred exposure before tonemapping.
   r1.xyz = r1.xyz * LumaSettings.GameSettings.Exposure;

   // Native screen-blend using Luma's rebound fp16 bloom; preserve unclamped linear scene+bloom for HDR.
   r0.xyz = MELE_BloomScreenBlend(r0.xy, r1.xyz, r0.w);

   float3 untonemapped = r0.xyz * r0.www + r1.xyz;

   // Native per-channel SDR curve: 1 - exp2(-1.7 * scene).
   r1.xyz = float3(-1.70000005, -1.70000005, -1.70000005) * r1.xyz;
   r1.xyz = exp2(r1.xyz);
   r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r0.xyz = r0.xyz * r0.www + r1.xyz;
   // The native per-channel value reaches the analytic grade untouched, so the vanilla white blowout survives into
   // HDR: HDR only measures the reversible max-channel Reinhard ratio that expands the grade output below. Its
   // 0.18 -> 0.1911 anchor matches the native exponential curve at mid-gray.
   float mele_scale = 1.0;
   if (LumaSettings.DisplayMode == 1)
   {
      float mele_mch = max(max3(untonemapped), 1e-6);
      // 1-exp2(-1.7*0.18) = 0.1911.
      mele_scale = Reinhard::ReinhardScalable(mele_mch, 1.0, 0.0, 0.18, 0.1911) / mele_mch;
   }

   // Apply the same native grade function to the working value and, below, to the SDR reference.
   float3 sdr_gamma = MELE_Analytic_GradeChain(r0.xyz);

   // Undo compression only where scale < 1, preserving the native diffuse/shadow grade while expanding HDR
   // highlights. SDR leaves mele_scale at 1.
   float3 graded_hdr = gamma_to_linear(sdr_gamma, GCT_MIRROR) / min(1.0, mele_scale);

   // Entry point supplies vignette macros. Analytic ME1/ME2 permutations have no grain and write zero alpha.
#include "Includes/Tonemap_MELE_Output.hlsli"
}
