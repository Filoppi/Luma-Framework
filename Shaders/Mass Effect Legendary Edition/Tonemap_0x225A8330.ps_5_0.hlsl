// ME3LE analytic stage-1 permutation used by the galaxy map and some cutscenes. It has no LUT, motion blur, grain,
// or pre-grade tonemap curve: analytic Scene* operates directly on linear scene+bloom, followed by gamma, the
// ME3LE blue-tinted white point, and a clamp. Bindings: t0 scene, t1 DoF, t2/t3 near/far DoF, t4 bloom.
//
// Transcribed from live CSO 0x225A8330. Its only tone limits are the grade's own clamps, so family 05 recovers range
// by running the grade on a bounded proxy and restoring the scale afterwards, then moves only hue toward the exact
// native hard-clipped result.

// clang-format off
#include "Includes/Common.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl" // ReinhardRange, used by the grade proxy.
#include "Includes/Tonemap_MELE_HDRConfig.hlsli"   // HDR reconstruction constants.
#include "Includes/Tonemap_MELE_HDRBridge.hlsli"   // Max-channel grade proxy; needs Reinhard above.
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

// Native analytic SDR grade transcribed from the live CSO. main() runs it once on the untouched scene+bloom in every
// Display Mode and, in HDR, once more on the grade proxy; HDR takes its range from the proxy path and uses the exact
// hard-clipped native result only as the hue reference. Preserve its register-level operations.
float3 MELE_ME3LEAnalytic_GradeChain(float3 c)
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
   // Native SDR gamma curve and ME3LE white point.
   r0.xyz = MELE_NativeGammaCurve(r0.xyz, GammaColorScaleAndInverse.xyz, GammaColorScaleAndInverse.w, true);

   r0.xyz = float3(1.01036298, 1.00000572, 1.16309249) * r0.xyz; // Blue-tinted white point; no radial vignette.
   r0.xyz = min(float3(1, 1, 1), r0.xyz);
   return r0.xyz;
}

// Included here, not with the headers: MELE_CompositeDOF reads the _Globals fields and DOF textures declared above.
#include "Includes/Tonemap_MELE_Scene.hlsli"

#include "Includes/Tonemap_MELE_HueReference.hlsli"

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
   // No tonemap curve here, so the raw scene reaches the grade and the saturate its grade opens with is this
   // permutation's vanilla blowout. Family 05 answers that by preparing the grade input, so nothing here
   // measures the clip.
   float3 workHDR = 0.0;
   bool workValid = false;
   if (LumaSettings.DisplayMode == 1)
   {
      // Family 05. The grade chain is called unchanged, caps and all: this path earns its range by preparing
      // the INPUT, not by stripping the grade. The blue white point and the black floor stay inside it.
      float q;
      float3 proxy;
      if (MELE_TryBuildGradeProxy(untonemapped, GammaColorScaleAndInverse.w * DefaultGamma, q, proxy))
      {
         workValid = MELE_TryRestoreGradeRange(gamma_to_linear(MELE_ME3LEAnalytic_GradeChain(proxy), GCT_MIRROR), q, workHDR);
      }
   }

   float3 sdrGamma = MELE_ME3LEAnalytic_GradeChain(r0.xyz);

   // The exact native SDR result is the starting value and the only fallback, for the whole triple. The output tail
   // decodes sdrGamma again on purpose; see Tonemap_MELE_Output.hlsli.
   float3 gradedHDR = gamma_to_linear(sdrGamma, GCT_MIRROR);
   if (workValid)
   {
      // The native hard-clipped result is ONLY a hue reference, following RenoDX hard-clip hue emulation; the working
      // value keeps its own OKLab lightness and chroma magnitude. If the transfer breaks, MELE_HueReferenceOKLab
      // returns its target untouched, so the reconstruction survives.
      gradedHDR = MELE_HueReferenceOKLab(workHDR, gradedHDR, MELE_HARDCLIP_HUE_STRENGTH);
   }

   // ME3LE analytic tail: no vignette or grain; preserve native output luma in alpha.
#define TM_VIGNETTE_TYPE 0
#define TM_ALPHA_LUMA    1
#include "Includes/Tonemap_MELE_Output.hlsli"
}
