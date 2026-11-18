// ME1LE 0xAAE8755A and ME2LE 0xCC76075F stage-1 analytic scene grade, used by the galaxy map, some Mako scenes, and
// cutscenes. No LUT, vignette, motion blur, or film grain. Bindings: t0 scene, t1 DoF, t2/t3 near/far DoF, t4 bloom.
//   0xAAE8755A  ME1LE: no white point. The dumped CSO clamps to 1 after the native gamma curve and goes straight to the
//               metering dot product and o0, without the 0.832050323/3.25/pow-100 radial vignette constants of the
//               ME1LE LUT permutations.
//   0xCC76075F  ME2LE: adds a blue-tinted post-gamma white point (TM_ANALYTIC_WHITEPOINT).
// Preserve register-level swizzles for comparison with the live CSOs. This file produces linear gradedHDR and native
// gamma sdrGamma; the shared tail applies the display map.
// ME3LE analytic shader 0x225A8330 has a different cbuffer layout and no exponential curve.

#if _CC76075F
#define TM_ANALYTIC_WHITEPOINT float3(1.01036298, 1.00000572, 1.16309249)
#endif
#define TM_VIGNETTE_TYPE 0

// clang-format off
#include "Includes/Common.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Reinhard.hlsl" // ReinhardRange for HDRBridge's grade proxy, which family 02 never calls.
#include "Includes/Tonemap_MELE_HDRConfig.hlsli"   // HDR reconstruction constants.
#include "Includes/Tonemap_MELE_ExpExtended.hlsli" // Continued scene curve and its validated work input.
#include "Includes/Tonemap_MELE_HDRBridge.hlsli"   // Native-colour projection; needs Reinhard above.
#include "../Includes/CInfinityRolloff.hlsl" // Display-peak rolloff, for the shared output tail.
#include "../Includes/MacLeodBoynton.hlsl"   // Highlights Desaturation, for the shared output tail.
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

// Native highlight desaturation and ImageAdjustments mix, transcribed from the live CSOs. The native grade and its
// uncapped family-02 copy both open with it, unchanged; keep its register-level swizzles.
float3 MELE_Analytic_GradeHead(float3 c)
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
   return r0.xyz;
}

// Native analytic SDR grade transcribed from the live CSOs, evaluated once on the untouched per-channel value in every
// Display Mode (see "MELE_NativeColorGradedHDR" for HDR). Keep its register-level swizzles and optional ME2LE white
// point unchanged.
float3 MELE_Analytic_GradeChain(float3 c)
{
   float4 r0;
   r0.xyz = MELE_Analytic_GradeHead(c);
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
   r0.xyz = TM_ANALYTIC_WHITEPOINT * r0.xyz; // ME2LE blue-tinted white point; ME1LE defines none.
#endif
   r0.xyz = min(float3(1, 1, 1), r0.xyz);
   return r0.xyz;
}

// Family 02: "MELE_Analytic_GradeChain" with exactly three upper caps lifted, so the working branch can carry values
// above the native ceiling. The grade is a formula, so it is extended as one, with no LUT and no max-channel proxy.
// The saturate opening the Scene grade becomes max(0, .), since log2 follows it. The gamma curve loses its cap and
// gains no floor, because these permutations feed mul_sat straight into log. The closing min(1) is dropped.
// Everything else is kept: the shared head, every Scene* field and its order, GammaOverlayColor, both halves of
// GammaColorScaleAndInverse, and the ME2LE encoded white point.
//
// Lifting the caps makes overflow reachable, so the guards live here and not in the native chain. Each runs before
// the operation it protects, since a later max() would swallow a NaN. One failing channel declines the whole triple,
// as switching channels independently would move hue. Values derived from artist dials, whose sign is free, get the
// finite check, not the non-negative one.
//
// Only the two cbuffer values no later guard observes are checked up front: SceneMidTones, because an infinite
// exponent on a positive base collapses to an exact 0 that passes every later guard, and GammaColorScaleAndInverse.w,
// which must be positive because the tail divides by it, with its reciprocal checked too because a tiny invGamma
// overflows it. The other Scene* and Gamma* fields reach a guarded value through arithmetic that turns a non-finite
// into NaN or inf; their unused .w never takes part. A zero output scale is a fade and is not refused. Bad parameters
// are never repaired, false only declines the reconstruction.
bool MELE_TryAnalyticGradeChainHDR(float3 c, out float3 workHDR)
{
   const float invGamma = GammaColorScaleAndInverse.w;
   const float gammaExponent = 1.0 / invGamma;
   // Raw cbuffer reads take the bit tests, not MELE_IsFinite, whose comparisons fxc deletes on them (see
   // Includes/Common.hlsl). The exponent is computed, so its comparison survives; past the bit tests it is positive
   // exactly when invGamma is.
   if (IsAnyNaN_Strict(SceneMidTones.xyz) || any(IsInfinite_Strict(SceneMidTones.xyz)) || IsNaN_Strict(invGamma) || IsInfinite_Strict(invGamma) || !MELE_IS_FINITE_POSITIVE(gammaExponent))
   {
      workHDR = float3(0.0, 0.0, 0.0);
      return false;
   }

   float4 r0;
   r0.xyz = MELE_Analytic_GradeHead(c);

   // Native analytic Scene grade, with only the upper half of the opening saturate removed. The shift itself is
   // checked, not its max, which would hide a NaN; that also covers the signed head above and c, whose non-finite
   // values propagate into it.
   const float3 shifted = -SceneShadowsAndDesaturation.xyz + r0.xyz;
   // A negative shift is ordinary artist data. A non-finite SceneInverseHighLights turns the product into NaN or
   // inf, which the non-negative check on the log base rejects.
   r0.xyz = SceneInverseHighLights.xyz * max(float3(0, 0, 0), shifted);
   if (!MELE_IsFinite(shifted) || !MELE_IsFiniteNonNegative(r0.xyz))
   {
      workHDR = r0.xyz;
      return false;
   }
   // log2(0) is -inf in SM4/5, and exp2(m * -inf) is 0 for any m > 0: the vanilla result for a black channel, which
   // must survive. A zero base with a non-positive exponent would be NaN or +inf instead, so it is refused before
   // the logarithm rather than invented into 0^0 = 1.
   if (any(r0.xyz == 0.0 && SceneMidTones.xyz <= 0.0))
   {
      workHDR = r0.xyz;
      return false;
   }
   r0.xyz = exp2(SceneMidTones.xyz * log2(r0.xyz));
   // exp2 never returns a negative, so this rejects only NaN and +inf; an underflow to zero is legitimate.
   // The weights are signed, so their dot only needs to be finite; a non-finite weight cannot cancel to finite.
   r0.w = dot(r0.xyz, SceneScaledLuminanceWeights.xyz);
   if (!MELE_IsFiniteNonNegative(r0.xyz) || !MELE_IsFinite(r0.w))
   {
      workHDR = r0.xyz;
      return false;
   }
   // Same scale and exponent as the native tail, without its cap. Any non-finite desaturation, overlay or scale
   // field surfaces in gammaInput.
   const float3 gammaInput = GammaColorScaleAndInverse.xyz * (r0.xyz * SceneShadowsAndDesaturation.www + GammaOverlayColor.xyz + r0.www);
   // GCT_MIRROR is the odd extension, so a negative encoded value is signed data and not a bad pow base; finiteness
   // is the contract for it, non-negativity is checked after the decode.
   float3 encoded = linear_to_gamma(gammaInput, GCT_MIRROR, gammaExponent);
#ifdef TM_ANALYTIC_WHITEPOINT
   encoded = TM_ANALYTIC_WHITEPOINT * encoded; // Still encoded, still the same tint; it may now exceed 1.
#endif
   workHDR = gamma_to_linear(encoded, GCT_MIRROR);
   return MELE_IsFinite(gammaInput) && MELE_IsFinite(encoded) && MELE_IsFiniteNonNegative(workHDR);
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

   // Screen blend using Luma's rebound fp16 bloom. The weight deliberately reads the linear scene, see
   // "MELE_BloomScreenBlend".
   r0.xyz = MELE_BloomScreenBlend(r0.xy, r1.xyz, r0.w);

   // Captured before the curve below overwrites r1. Straight RGB, with no BRG rotation to undo.
   const float3 sceneLinear = r1.xyz;
   const float3 bloomLinear = r0.xyz * r0.www;

   // Native per-channel SDR curve: 1 - exp2(-1.7 * scene).
   r1.xyz = float3(-1.70000005, -1.70000005, -1.70000005) * r1.xyz;
   r1.xyz = exp2(r1.xyz);
   r1.xyz = float3(1, 1, 1) + -r1.xyz;
   r0.xyz = r0.xyz * r0.www + r1.xyz;
   // The native per-channel value reaches the analytic grade untouched as this body's SDR output. Family 02 builds a
   // second, uncapped working value from the scene instead.
   float3 workHDR = 0.0;
   bool workValid = false;
   if (LumaSettings.DisplayMode == 1)
   {
      // Below the pivot with an inert grade this reduces to the native result.
      float3 workNative;
      const bool sourceValid = MELE_TryExpExtendedInput(sceneLinear, bloomLinear, workNative);
      workValid = sourceValid && MELE_TryAnalyticGradeChainHDR(workNative, workHDR);
   }

   // The uncapped twin above supplies only relative luminance.
   float3 sdrGamma = MELE_Analytic_GradeChain(r0.xyz);
   float3 gradedHDR = MELE_NativeColorGradedHDR(sdrGamma, workHDR, workValid);

   // Analytic ME1LE/ME2LE permutations have no vignette or grain and write zero alpha.
#include "Includes/Tonemap_MELE_Output.hlsli"
}
