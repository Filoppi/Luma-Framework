// Replaces the game's UE3.5 UberPostProcess final color pass (0xB030BAA6 with the FXAA luma output, 0xFE88487E without). The game
// grades and then saturate()s to SDR, dropping the highlight detail (sun, sky, emissives, specular, FX; ~5-6 stops) of its RGBA16F
// pre-tonemap scene. Analytic, no LUT:
//   1. Rebuild the game's scene + bloom mix exactly (untonemapped).
//   2. Run the exact vanilla clamped grade: the SDR reference (TONEMAP_TYPE 0) and the FXAA luma source.
//   3. HDR: the same analytic UE3 grade as an extended function (SDR clamps and floors removed, nothing else changed). It supplies
//      range, luminance and purity.
//   4. MacLeod-Boynton hue emulation: the hue of the RenoDX BL1 donor, the extended grade through a per-channel
//      ReinhardPiecewise(5, 1.5) in BT.2020 that bends the way the vanilla clip bent, in full, with its whitening at the user's
//      highlights desaturation.
//   5. The user's contrast and saturation, then a max-channel C-infinity rolloff to the user's peak, which keeps RGB ratios.
// SDR displays (and TONEMAP_TYPE 0) get the vanilla clamped grade.
//
// Output is stored gamma-encoded (POST_PROCESS_SPACE_TYPE 0, 1.0 = paper white) so the game's gamma SDR HUD blends on top as in
// vanilla (a linear buffer washes it out). With UI_DRAW_TYPE >= 2 the scene is also pre-scaled by GamePaperWhite / UIPaperWhite so
// the HUD lands at its own paper white. The Display Composition decodes, applies paper white, encodes scRGB and gamut maps at present.

// clang-format off
// Do not sort: the game-local "Includes/Common.hlsl" must come first (see there). Sorted below the shared "../Includes/*" block,
// Settings.hlsl's empty dummy wins -> "invalid subscript 'BloomIntensity'".
#include "Includes/Common.hlsl"             // LumaGameSettings (the grade sliders)
#include "../Includes/Color.hlsl"
#include "../Includes/CInfinityRolloff.hlsl" // Tonemap::CInfinityRollOff::MaxChannel (the display map)
#include "../Includes/Reinhard.hlsl" // Reinhard::ReinhardPiecewise (hue reference)
#include "../Includes/MacLeodBoynton.hlsl" // MacLeodBoynton::HueAndPurityEmulationBT2020
// clang-format on

// fxc flags the early return of "MacLeodBoynton::SolvePurity()" as an uninitialized result (X4000). It comes at code generation, so
// it stays off for the whole shader.
#pragma warning(disable : 4000)

// HDR / vanilla. 1 = extended UE3 grade + C-infinity display map (default). 0 = vanilla clamped SDR reference.
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// Anti-banding dither of one output quantizer step: the 8 bit code in SDR, the 10 bit BT.2020 PQ code in HDR.
// "gammaSpace": whether "color" is gamma encoded (else linear). "paperWhiteNits": the nits the display composition maps 1 to,
// which places the PQ code steps (HDR only, floored at 1).
void ApplyOutputDithering(inout float3 color, float2 uv, bool gammaSpace, bool hdr, float paperWhiteNits)
{
   if (hdr)
   {
      const float pqScale = max(paperWhiteNits, 1.0) / HDR10_MaxWhiteNits;
      const float3 linearColor = gammaSpace ? gamma_to_linear(color, GCT_MIRROR) : color;
      float3 pq = Linear_to_PQ(BT709_To_BT2020(linearColor * pqScale), GCT_MIRROR);
      ApplyDithering(pq, uv, true, 1.0, 10u, LumaSettings.FrameIndex, true);
      color = BT2020_To_BT709(PQ_to_Linear(pq, GCT_MIRROR)) / pqScale;
      if (gammaSpace)
      {
         color = linear_to_gamma(color, GCT_MIRROR);
      }
   }
   else
   {
      ApplyDithering(color, uv, gammaSpace, 1.0, 8u, LumaSettings.FrameIndex, true);
   }
}

// --- Game bindings (must match the original shader exactly) ---
cbuffer _Globals : register(b0)
{
   float4 PackedParameters : packoffset(c0);
   float2 MinMaxBlurClamp : packoffset(c1);
   float4 SceneShadowsAndDesaturation : packoffset(c2);
   float4 SceneInverseHighLights : packoffset(c3);
   float4 SceneMidTones : packoffset(c4);
   float4 SceneScaledLuminanceWeights : packoffset(c5);
   float4 GammaColorScaleAndInverse : packoffset(c6);
   float4 GammaOverlayColor : packoffset(c7);
}

cbuffer PSOffsetConstants : register(b2)
{
   float4 ScreenPositionScaleBias : packoffset(c0);
   float4 MinZ_MaxZRatio : packoffset(c1);
   float4 DynamicScale : packoffset(c2);
}

SamplerState SceneColorTextureSampler_s : register(s0);
SamplerState BlurredImageSampler_s : register(s1);
Texture2D<float4> SceneColorTexture : register(t0);
Texture2D<float4> BlurredImage : register(t1);

// Grade steps 1-3 (shadows, highlights, midtones), verbatim from the decompiled shader. Returns the post-midtones color, where the
// original 0xB030BAA6 takes its FXAA luma. "clampSDR" true is the vanilla SDR path, verbatim (upper saturate()s, 1e-4 floors). False
// is the same grade as an extended HDR function: the upper saturate()s become max(0, ...) and the 1e-4 floors 0 (black reaches 0
// rather than the SDR floor's 0.0152 in gamma); every parameter, every other operation and the gamma exponent are unchanged, so
// highlights keep their channel ratio instead of the per-channel saturate() hue shift.
float3 GradeUE3_PostMidtones(float3 scene, bool clampSDR)
{
   float3 c = clampSDR ? saturate(-SceneShadowsAndDesaturation.xyz + scene)
                       : max(0.0, -SceneShadowsAndDesaturation.xyz + scene);
   c = SceneInverseHighLights.xyz * c;
   c = clampSDR ? max(9.99999975e-05, abs(c)) : max(0.0, abs(c));
   c = log2(c);
   c = SceneMidTones.xyz * c;
   return exp2(c);
}

// The grade's tail, verbatim from the back half of the original: desaturation, overlay, scale and gamma encode. "clampSDR" as in
// GradeUE3_PostMidtones.
float3 GradeUE3_FromPostMidtones(float3 c, bool clampSDR)
{
   float desat = dot(c, SceneScaledLuminanceWeights.xyz);
   c = c * SceneShadowsAndDesaturation.www + GammaOverlayColor.xyz;
   c = c + desat;
   c = clampSDR ? saturate(GammaColorScaleAndInverse.xyz * c) : max(0.0, GammaColorScaleAndInverse.xyz * c);
   c = clampSDR ? max(9.99999975e-05, c) : max(0.0, c);
   c = log2(c);
   c = GammaColorScaleAndInverse.www * c;
   return exp2(c); // gamma-encoded graded color
}

// "v0" / "v1" are the game's interpolators (TEXCOORD0/1). Returns the color in post-process space (1.0 = paper white) in "outColor",
// and the FXAA luma the game's edge CS expects in "outLuma".
void RunBLTonemap(float4 v0, float2 v1, out float3 outColor, out float outLuma)
{
   // 1. The scene mix, the real pre-tonemap HDR: the scene attenuated by the blur's inverse weight, plus bloom.
   float3 sceneColor = SceneColorTexture.Sample(SceneColorTextureSampler_s, DynamicScale.xy * v0.zw).xyz;
   float4 blurred = BlurredImage.Sample(BlurredImageSampler_s, v1.xy);
   float3 untonemapped = sceneColor * saturate(1.0 - blurred.w) + blurred.xyz * LumaSettings.GameSettings.BloomIntensity;

   // Scene exposure (multiplier), scene-referred / pre-grade; the SDR reference below derives from the same
   // `untonemapped`, so the grade tracks the exposure change.
   untonemapped *= LumaSettings.GameSettings.Exposure;

   // 2. The vanilla grade's post-midtones intermediate: the FXAA luma source on both paths, and the first half
   // of the SDR reference below.
   float3 postMidtones = GradeUE3_PostMidtones(untonemapped, true);

   // FXAA luma, read by the game's edge CS at SV_Target1: the original's log2 curve of the BT.709-weighted sum of the post-midtones
   // color, a graded intermediate before the gamma exponent (see GradeUE3_PostMidtones).
   outLuma = 0.25 * log2(dot(postMidtones, float3(0.212670997, 0.715160012, 0.0721689984)) * 15.0 + 1.0);

#if TONEMAP_TYPE >= 1
   [branch] if (LumaSettings.DisplayMode == 1)
   {
      // 3. The extended HDR grade (clampSDR false), computed once: the hue reference and the display map both start from it.
      outColor = gamma_to_linear(GradeUE3_FromPostMidtones(GradeUE3_PostMidtones(untonemapped, false), false));

      // 4. The RenoDX BL1 hue (common.hlsli, ApplyCustomGrading). Skipped below the
      // shoulder, where the reference is the color itself. The clip color itself is no reference: a fully clipped one is
      // achromatic, with no MacLeod-Boynton direction at all.
      const float3 color2020 = BT709_To_BT2020(outColor);
      [branch] if (max3(color2020) > 1.5)
      {
         outColor = BT2020_To_BT709(MacLeodBoynton::HueAndPurityEmulationBT2020(color2020, Reinhard::ReinhardPiecewise(color2020, 5.0, 1.5),
                                                                                1.0, LumaSettings.GameSettings.HighlightDechroma));
      }
      // 5. Contrast (a power of relative luminance around mid gray) and a scale of the RGB distance from relative luminance: the rolloff
      // doesn't take them
      const float contrast = LumaSettings.GameSettings.Contrast;
      [branch] if (contrast != 1.0)
      {
         outColor *= pow(max(GetLuminance(outColor, CS_BT709), FLT_MIN) / MidGray, contrast - 1.0);
      }
      const float saturationScale = LumaSettings.GameSettings.Saturation;
      [branch] if (saturationScale != 1.0)
      {
         outColor = Saturation(outColor, saturationScale, CS_BT709);
      }
      // The rolloff (linear BT.709, 1.0 = paper white). The peak can't go below paper white (it needs to be above the 0.18 anchor).
      const float paperWhite = max(LumaSettings.GamePaperWhiteNits, 1.0);
      outColor = Tonemap::CInfinityRollOff::MaxChannel(outColor, max(LumaSettings.PeakWhiteNits, paperWhite) / paperWhite, 0.18, 1.0);
   }
   else
#endif
   {
      // Vanilla reference (and SDR displays): the exact clamped SDR grade (gamma-encoded), linearized.
      outColor = gamma_to_linear(saturate(GradeUE3_FromPostMidtones(postMidtones, true)));
   }

   // --- Common tail: the UI paper white pre-scale and the post-process space encode ---
   outColor *= GetUIPaperWhitePreScale(LumaSettings.GamePaperWhiteNits, LumaSettings.UIPaperWhiteNits);
   // The extended grade and the MacLeod-Boynton hue emulation can emit NaN or negatives (the scene carries small negative/WCG values):
   // none may reach the swapchain.
   outColor = IsNaN_Strict(outColor) ? 0.0 : max(0.0, outColor);
#if POST_PROCESS_SPACE_TYPE == 0
   // Anti-banding dither, one step of the output quantizer: in HDR the 10-bit BT.2020 PQ code, from linear before the gamma encode
   // below; in SDR the 8-bit code, after it.
#if TONEMAP_TYPE >= 1
   const bool dither = LumaSettings.GameSettings.Dithering > 0.5;
   if (dither && LumaSettings.DisplayMode != 0)
   {
      ApplyOutputDithering(outColor, v1.xy, false, true, LumaSettings.UIPaperWhiteNits);
   }
#endif
   // Store gamma so the game's gamma-space HUD blends like vanilla; composition decodes + applies paper white. Mirrored: the PQ dither
   // can step a black channel below 0.
   outColor = linear_to_gamma(outColor, GCT_MIRROR);
#if TONEMAP_TYPE >= 1
   if (dither && LumaSettings.DisplayMode == 0)
   {
      ApplyOutputDithering(outColor, v1.xy, true, false, 1.0);
   }
#endif
#endif
}
