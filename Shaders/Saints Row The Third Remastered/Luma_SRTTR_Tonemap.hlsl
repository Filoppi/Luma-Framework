// clang-format off
// The game-local Common.hlsl must come first: it defines LumaGameSettings before the shared Settings.hlsl
// declares the LumaSettings cbuffer.
#include "Includes/Common.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"
// clang-format on

// Saints Row: The Third Remastered - hdr_filter tonemap CS, shared body. Included by the per-hash wrappers
// (Tonemap_0x941A9154_0x835784B0 = LUT perms 52/53, TonemapNoLUT_0xAB466B4A_0xFEDD50B7 = perms 48/49), which set
// SRTTR_TM_HAS_LUT.
//
// The vanilla math is transcribed from the perm 52 listing: exposure, vignette, bloom, tint, BiasScale, then a
// per-channel filmic toe/shoulder in natural-log space whose output is gamma 2.2 code (compose writes it to the
// swapchain through a UNORM view), then a 4x4 Bayer dither, saturation and the 32^3 LUT indexed by that code.
//
// HDR follows Mass Effect Legendary Edition family 01 (Shaders/Mass Effect Legendary Edition/Includes/
// Tonemap_MELE_HDRBridge.hlsli) as ported to Borderlands 2 (Luma_BL2TPS_Tonemap.hlsl): the native SDR grade is the
// complete colour reference, a continuation of the native curve passes through a reversible bounded copy of the
// real LUT to give HDR luminance, only that luminance is projected onto the native graded RGB ratios, and DICE (by
// channel, in PQ) is the only display rolloff. No path takes RGB ratios from the working value.
//
// The continuation is the tangent of the LINEAR-LIGHT curve G(x) = F(x)^2.2 over the curve's own input x. The pivot
// is the inflection of G, scene 0.40
// (post-exposure, pre-BiasScale; G 0.25 of white): G is convex below it, so a lower pivot (e.g. mid-gray 0.18) puts
// the tangent under vanilla, and G is steepest there, so a higher one gives dimmer highlights.
// Derived for the default_district.xtbl curve (Toe 0.1, Shoulder 0.9, Steep 0.36, BiasScale 3).

#define SRTTR_HDR_PIVOT                  0.40
#define SRTTR_HDR_BRIDGE_SHOULDER        0.75
#define SRTTR_HDR_PROXY_EPS              1e-4
#define SRTTR_NATIVE_COLOR_MIN_LUMINANCE 1e-6

cbuffer CB_CUSTOM : register(b10)
{
   float4 cb10[13]; // HDR_PARAMS, 208 bytes
}

#define ScreenSize     asuint(cb10[0].xy)
#define ScreenSizeInv  cb10[0].zw
#define KeyValue       cb10[2].w
#define Toe            cb10[5].x
#define Shoulder       cb10[5].y
#define Steep          cb10[5].z
#define BiasScale      cb10[5].w
#define BloomAmount    cb10[6].x
#define BloomBoost     cb10[6].z
#define VignetteAmount cb10[7].w
#define TintColor      cb10[8]

Texture2D<float4> SourceHdrImage : register(t0);
StructuredBuffer<float> LuminanceOutput : register(t2); // [0] = adapted log2 luminance
Texture2D<float4> BloomImage : register(t3);
#if SRTTR_TM_HAS_LUT
Texture3D<float4> ColorGradingLUT : register(t4);
#endif
RWTexture2D<float4> TonemappedOutput : register(u0);
SamplerState Sampler_Linear_CC : register(s2);

// Saturation (TintColor.w) and LUT, on curve output code values. ColorGradingIntensity 0 leaves the curve output
// ungraded; the HDR working value goes through here too, so it stays consistent with the reference.
float3 SRTTR_Grade(float3 code)
{
   const float luma = dot(float3(0.3, 0.59, 0.11), code);
   float3 graded = TintColor.w * (code - luma) + luma;
#if SRTTR_TM_HAS_LUT
   graded = ColorGradingLUT.SampleLevel(Sampler_Linear_CC, graded, 0).rgb;
#endif
   return lerp(code, graded, LumaSettings.GameSettings.ColorGradingIntensity);
}

// ====================== HDR luminance reconstruction ======================

// Every guarded value is a light quantity with no meaning below zero; two ordered comparisons reject NaN,
// infinities and negatives (see Luma_BL2TPS_Tonemap.hlsl for the measured cost against the bit tests).
bool SRTTR_IsFiniteNonNegative(float x)
{
   return x >= 0.0 && x <= FLT_MAX;
}
bool SRTTR_IsFiniteNonNegative(float3 v)
{
   return all(v >= 0.0) && all(v <= FLT_MAX);
}

// The native curve F and its slope dF/dln(x) at one natural-log input, analytic per segment. Only ever evaluated
// at the pivot, which is built from cbuffer constants, so the continuation cannot depend on the pixel.
float SRTTR_CurveAndSlope(float lx, float toeMatch, float shoulderMatch, out float slope)
{
   if (lx < toeMatch)
   {
      const float scale = 2.0 * (1.0 - Toe);
      const float rate = 2.0 * Steep / (1.0 - Toe);
      const float f = scale / (1.0 + exp(-rate * (lx - toeMatch)));
      slope = rate * f * (1.0 - f / scale);
      return f;
   }
   if (lx < shoulderMatch)
   {
      slope = Steep;
      return Steep * (lx + 0.733) + 0.18;
   }
   const float scale = 2.0 * (1.0 - Shoulder);
   const float rate = 2.0 * Steep / (1.0 - Shoulder);
   const float headroom = scale / (1.0 + exp(rate * (lx - shoulderMatch)));
   slope = rate * headroom * (1.0 - headroom / scale);
   return 1.0 - headroom;
}

// The tangent of G at the pivot: x = pivot, y = G(pivot), z = dG/dx. Zero when the continuation is unusable.
float3 SRTTR_BuildContinuation(float toeMatch, float shoulderMatch)
{
   const float pivot = SRTTR_HDR_PIVOT * BiasScale;
   float codeSlope;
   const float pivotCode = SRTTR_CurveAndSlope(log(pivot + 0.001), toeMatch, shoulderMatch, codeSlope);
   const float pivotLinear = gamma_to_linear1(pivotCode, GCT_NONE);
   // dG/dx = gamma * F^(gamma - 1) * dF/dln(x) / (x + 0.001)
   const float linearSlope = DefaultGamma * pow(pivotCode, DefaultGamma - 1.0) * codeSlope / (pivot + 0.001);
   if (!(pivot > 0.0 && pivot <= FLT_MAX) || !(pivotCode > 0.0 && pivotCode <= FLT_MAX) || !(linearSlope > 0.0 && linearSlope <= FLT_MAX))
   {
      return 0.0;
   }
   return float3(pivot, pivotLinear, linearSlope);
}

// The working value: the native linear-light response below the pivot, its tangent above, per channel, then
// graded through a bounded copy of the real LUT. Only the luminance leaves this function.
// On false targetLuminance must not be consumed.
bool SRTTR_TryBuildWorkingLuminance(float3 curveInput, float3 nativeCode, float3 continuation, out float targetLuminance)
{
   targetLuminance = 0.0;

   if (continuation.x <= 0.0 || !SRTTR_IsFiniteNonNegative(curveInput) || !SRTTR_IsFiniteNonNegative(nativeCode))
   {
      return false;
   }

   const float pivot = continuation.x;
   const float pivotLinear = continuation.y;
   const float linearSlope = continuation.z;
   const float3 nativeLinear = gamma_to_linear(nativeCode, GCT_NONE);
   const float3 workLinear = curveInput > pivot ? pivotLinear + linearSlope * (curveInput - pivot) : nativeLinear;
   if (!all(workLinear <= FLT_MAX))
   {
      return false;
   }

   // Max-channel proxy: one scalar, so the limiter cannot move an RGB ratio. q is applied and removed in the same
   // linear domain (the gamma encode only enters the LUT's domain), so no domain adapter is needed.
   const float m = max3(workLinear);
   float q = 1.0;
   if (m > SRTTR_HDR_BRIDGE_SHOULDER)
   {
      // Identity below the shoulder, C1, asymptotic to 1
      q = Reinhard::ReinhardRange(m.xxx, SRTTR_HDR_BRIDGE_SHOULDER).x / m;
   }
   const float3 proxyLinear = workLinear * q;
   if (q <= 0.0 || q > 1.0 || !all(proxyLinear <= 1.0 + SRTTR_HDR_PROXY_EPS))
   {
      return false;
   }

   // Never invert by re-reading the LUT output: the LUT moved the colour, so only dividing q back is exact.
   const float3 gradedLinear = max(0.0, gamma_to_linear(SRTTR_Grade(saturate(linear_to_gamma(proxyLinear, GCT_NONE))), GCT_MIRROR));
   const float3 restored = gradedLinear / q;
   if (!all(restored <= FLT_MAX))
   {
      return false;
   }
   targetLuminance = GetLuminance(restored, CS_BT709);
   return true;
}

// RGB ratios from the exact native grade result, luminance from the working value. Declines by returning the
// reference itself for the whole triple.
float3 SRTTR_NativeColorAtLuminance(float3 nativeReferenceLinear, float targetLuminance)
{
   if (!SRTTR_IsFiniteNonNegative(nativeReferenceLinear) || !SRTTR_IsFiniteNonNegative(targetLuminance))
   {
      return nativeReferenceLinear;
   }
   if (targetLuminance == 0.0)
   {
      return 0.0;
   }
   const float referenceLuminance = GetLuminance(nativeReferenceLinear, CS_BT709);
   if (!(referenceLuminance >= SRTTR_NATIVE_COLOR_MIN_LUMINANCE && referenceLuminance <= FLT_MAX))
   {
      return nativeReferenceLinear;
   }
   const float3 result = nativeReferenceLinear * (targetLuminance / referenceLuminance);
   if (!all(result <= FLT_MAX))
   {
      return nativeReferenceLinear;
   }
   return result;
}

groupshared float3 gContinuation;

[numthreads(8, 8, 1)] void main(uint3 vThreadID : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
   const float toeMatch = (0.82 - Toe) / Steep - 0.733;
   const float shoulderMatch = (Shoulder - 0.18) / Steep - 0.733;

   // The HDR continuation only depends on cbuffer values, so one thread builds it for the whole group
   if (groupIndex == 0)
   {
      gContinuation = SRTTR_HDR_SCENE ? SRTTR_BuildContinuation(toeMatch, shoulderMatch) : 0.0;
   }
   GroupMemoryBarrierWithGroupSync();

   if (any(vThreadID.xy >= ScreenSize))
   {
      return;
   }

   const float2 uv = float2(vThreadID.xy) * ScreenSizeInv;

   // --- exposure, vignette, bloom, tint (vanilla) ---
   float3 bloomSum = BloomImage.Load(int3(vThreadID.xy >> 1, 0)).rgb;
   bloomSum += BloomImage.SampleLevel(Sampler_Linear_CC, uv, 1.0).rgb;
   bloomSum += BloomImage.SampleLevel(Sampler_Linear_CC, uv, 2.0).rgb;
   bloomSum += BloomImage.SampleLevel(Sampler_Linear_CC, uv, 3.0).rgb;
   bloomSum += BloomImage.SampleLevel(Sampler_Linear_CC, uv, 4.0).rgb;

   // max(0): the scene was r11g11b10_float, which cannot store negatives; upgraded to fp16 it can, and the vanilla
   // curve's abs() would fold them into positive colour.
   float3 color = max(0.0, SourceHdrImage.Load(int3(vThreadID.xy, 0)).rgb);
   color = color * KeyValue / (exp2(LuminanceOutput[0]) + 6.103515625e-05);
   const float2 centered = uv * 2.0 - 1.0;
   color *= 1.0 / (dot(centered, centered) * VignetteAmount * LumaSettings.GameSettings.VignetteIntensity + 1.0);
   color = BloomAmount * (bloomSum * 0.2 - color) + color;
   color = bloomSum * 0.2 * BloomBoost + color;
   color *= TintColor.rgb;
   color *= LumaSettings.GameSettings.Exposure;
   const float3 curveInput = color * BiasScale;

   // --- per-channel filmic toe / shoulder in ln space (vanilla) ---
   const float3 lx = log(abs(curveInput) + 0.001);
   const float3 toe = 2.0 * (1.0 - Toe) / (exp((-2.0 * Steep / (1.0 - Toe)) * (lx - toeMatch)) + 1.0);
   const float3 straight = Steep * (lx + 0.733) + 0.18;
   const float3 shoulder = 1.0 - 2.0 * (1.0 - Shoulder) / (exp((2.0 * Steep / (1.0 - Shoulder)) * (lx - shoulderMatch)) + 1.0);
   const float3 curveCode = lx < toeMatch ? toe : (lx < shoulderMatch ? straight : shoulder);

   float3 outputCode;
   if (SRTTR_HDR_SCENE)
   {
      // The reference skips the Bayer multiply: the compose replacement no longer divides it back out in HDR.
      const float3 sdrLinear = gamma_to_linear(SRTTR_Grade(curveCode), GCT_MIRROR);
      float3 recovered = sdrLinear;
      float workLuminance;
      // At or below the pivot the working value is the native response, whose graded luminance is the reference's own
      if (any(curveInput > gContinuation.x) && SRTTR_TryBuildWorkingLuminance(curveInput, curveCode, gContinuation, workLuminance))
      {
         recovered = SRTTR_NativeColorAtLuminance(sdrLinear, workLuminance);
      }

      // Contrast (a power around mid gray) and saturation go before DICE
      float3 hdr = recovered;
      const float contrast = LumaSettings.GameSettings.Contrast;
      [branch] if (contrast != 1.0)
      {
         hdr *= pow(max(GetLuminance(hdr, CS_BT709), FLT_MIN) / MidGray, contrast - 1.0);
      }
      const float saturation = LumaSettings.GameSettings.Saturation;
      [branch] if (saturation != 1.0)
      {
         hdr = Saturation(hdr, saturation, CS_BT709);
      }
      const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
      const float peakWhite = max(LumaSettings.PeakWhiteNits, LumaSettings.GamePaperWhiteNits) / sRGB_WhiteLevelNits;
      hdr = DICETonemap(hdr * paperWhite, peakWhite, DefaultDICESettings(DICE_TYPE_BY_CHANNEL_PQ)) / paperWhite;

      hdr *= SRTTR_UIPaperWhitePreScale();
      hdr = IsNaN_Strict(hdr) ? 0.0 : max(0.0, hdr);
      outputCode = linear_to_gamma(hdr, GCT_NONE);
   }
   else
   {
      outputCode = SRTTR_Grade(SRTTR_BAYER_IN_SCENE ? curveCode * (SRTTR_Bayer(vThreadID.xy) * kBayerScale + 1.0) : curveCode);
#if UI_DRAW_TYPE >= 2
      [branch] if (LumaSettings.GamePaperWhiteNits != LumaSettings.UIPaperWhiteNits)
      {
         outputCode = linear_to_gamma(gamma_to_linear(outputCode, GCT_MIRROR) * SRTTR_UIPaperWhitePreScale(), GCT_MIRROR);
      }
#endif
   }

   TonemappedOutput[vThreadID.xy] = float4(outputCode, 1.0);
}
