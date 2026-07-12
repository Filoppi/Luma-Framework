// Borderlands GOTY Enhanced — Luma HDR tonemap replacement (shared impl).
//
// Replaces the game's UE3.5 "UberPostProcess" final color pass (PS 0xB030BAA6 with FXAA luma output,
// 0xFE88487E without). The game runs a parametric grade then saturate()s to SDR, throwing away the real
// highlight detail the pre-tonemap RGBA16F scene color holds (sun/sky/emissive/specular/FX, ~5-6 stops).
//
// Strategy (analytic in-shader TM, no LUT):
//   1. Reconstruct the scene mix exactly as the game does (scene color + bloom).            -> untonemapped (real HDR)
//   2. Run the game's own grade (the SDR "artistic intent") UNCHANGED on the saturate path. -> ungraded_sdr (the look)
//   3. UpgradeToneMap (RenoDX ShortFuse, Tonemap.hlsl): add the highlight luminance the SDR  -> recovered
//      tonemap clipped back on top of the graded look, hue-matched.
//   4. DICE display rolloff to the user's peak/paper-white nits.                             -> hdr
//   5. Restore hue/chroma to the SDR reference so the dev's color intent is preserved.
//
// Output is stored in GAMMA space (POST_PROCESS_SPACE_TYPE==0, 1.0 = paper white) so the game's gamma-SDR
// HUD blends on top like vanilla (a linear buffer washes it out). With UI_DRAW_TYPE==2 the scene is also
// pre-scaled by GamePaperWhite/UIPaperWhite so the HUD lands at its own paper white. The core Display
// Composition decodes gamma + applies paper-white scaling + scRGB encode + gamut map at present.

// clang-format off
// ORDER IS LOAD-BEARING — do not sort. The game-local "Includes/Common.hlsl" MUST come first: it defines
// LUMA_GAME_CB_STRUCTS (via GameCBuffers.hlsl) BEFORE any shared header pulls Settings.hlsl, so
// LumaSettings.GameSettings resolves to the real grade struct rather than the empty dummy. When clang-format
// sorted it below the shared "../Includes/*" block, Settings.hlsl's dummy won -> "invalid subscript 'BloomIntensity'".
#include "Includes/Common.hlsl"             // game-local: defines LumaGameSettings (grade sliders) before LumaSettings cbuffer
#include "../Includes/Color.hlsl"
#include "../Includes/ColorGradingLUT.hlsl" // RestoreHueAndChrominance, SimpleGamutClip
#include "../Includes/DICE.hlsl"            // DICETonemap / DefaultDICESettings
#include "../Includes/Reinhard.hlsl"        // ReinhardTonemap / DefaultReinhardSettings (NeutralSDR)
#include "../Includes/Tonemap.hlsl"         // UpgradeToneMap
// clang-format on

// HDR / vanilla. 1 = recover real highlights + DICE display map (default). 0 = vanilla clamped SDR reference.
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// Run the display map in a BT.2020 working space (round-tripped back to BT.709). Gamut-correct handling of
// highly saturated highlights — NOT a display-gamut expansion.
#ifndef TONEMAP_IN_WIDER_GAMUT
#define TONEMAP_IN_WIDER_GAMUT 1
#endif

// Restore the SDR reference's hue/chroma onto the HDR result (keeps the dev's color grade intent).
#ifndef ENABLE_HUE_RESTORATION
#define ENABLE_HUE_RESTORATION 1
#endif

// HighlightDechroma is an optional user slider (see step 6 below); default 0 = off (only the mandatory DICE/gamut
// desaturation applies).

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

// Neutral SDR reference for the highlight-recovery delta.
float3 NeutralSDR(float3 color)
{
   ReinhardSettings settings = DefaultReinhardSettings();
   settings.by_luminance = true;
   return ReinhardTonemap(color, 100.f, 100.f, settings);
}

// Grade steps 1-3 (shadows -> highlights -> midtones), lifted verbatim from the decompiled shader. Returns the
// post-midtones color: the exact point original 0xB030BAA6 takes its FXAA luma from.
// `clampSDR`: true = vanilla saturate() path; false = unclamped (max 0), keeping highlights' real channel ratio
// (hue) instead of the per-channel saturate hue shift.
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

// Tail of the grade: desat + overlay + scale + gamma encode (verbatim from the back half of the original).
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

float3 GradeUE3(float3 scene, bool clampSDR)
{
   return GradeUE3_FromPostMidtones(GradeUE3_PostMidtones(scene, clampSDR), clampSDR);
}

// Core tonemap. `v0`/`v1` are the game's interpolators (TEXCOORD0/1). Returns scene-referred linear color
// (1.0 = paper white) in `outColor`, and the FXAA luma the game's edge CS expects in `outLuma`.
void RunBLTonemap(float4 v0, float2 v1, out float3 outColor, out float outLuma)
{
   // 1. Scene mix (scene color attenuated by inverse-blur weight, plus bloom) — the real pre-tonemap HDR.
   float3 sceneColor = SceneColorTexture.Sample(SceneColorTextureSampler_s, DynamicScale.xy * v0.zw).xyz;
   float4 blurred = BlurredImage.Sample(BlurredImageSampler_s, v1.xy);
   float3 untonemapped = sceneColor * saturate(1.0 - blurred.w) + blurred.xyz * LumaSettings.GameSettings.BloomIntensity;

   // Scene exposure (multiplier), scene-referred / pre-grade; the SDR reference below derives from the same
   // `untonemapped`, so the grade tracks the exposure change.
   untonemapped *= LumaSettings.GameSettings.Exposure;

   // 2. The game's own grade (artistic intent), gamma-encoded SDR. Computed once via the split halves so the
   // post-midtones intermediate can feed the FXAA luma below.
   float3 postMidtones = GradeUE3_PostMidtones(untonemapped, true);
   float3 ungraded_sdr_gamma = GradeUE3_FromPostMidtones(postMidtones, true);

   // FXAA luma (game's edge CS reads SV_Target1): BT.709 luma of the post-midtones color (see GradeUE3_PostMidtones).
   outLuma = 0.25 * log2(dot(postMidtones, float3(0.212670997, 0.715160012, 0.0721689984)) * 15.0 + 1.0);

#if TONEMAP_TYPE >= 1
   float3 ungraded_sdr = gamma_to_linear(ungraded_sdr_gamma); // linear SDR reference (1.0 = white)

   // 3. Recover the highlight luminance the SDR tonemap clipped, on top of the graded look.
   float3 neutral_sdr = NeutralSDR(untonemapped);
   float3 recovered = UpgradeToneMap(untonemapped, neutral_sdr, ungraded_sdr);

   // 4. Display rolloff to the user's peak/paper-white nits (DICE, hue-preserving by luminance).
   const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
   const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
#if TONEMAP_IN_WIDER_GAMUT
   recovered = BT709_To_BT2020(recovered);
#endif
   // Tonemap luminance in PQ (hue-preserving: rgb scaled by the luminance ratio), then CORRECT_CHANNELS_BEYOND_
   // PEAK_WHITE pulls any channel that still exceeds peak back into range by desaturating it toward white. Modern
   // HDR panels clip each rgb channel at peak individually, so an uncorrected saturated highlight (e.g. a bright
   // blue) would hard-clip with a hue shift; the correction trades a little highlight saturation for a clean,
   // in-range rolloff. DesaturationVsDarkeningRatio 1.0
   // (default) = contain by desaturating, not darkening (darkening flattens detail).
   DICESettings ds = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
   float3 hdr = DICETonemap(recovered * paperWhite, peakWhite, ds) / paperWhite;
#if TONEMAP_IN_WIDER_GAMUT
   hdr = BT2020_To_BT709(SimpleGamutClip(hdr, true));
#endif

   // 5. Lock hue EXACTLY to the un-blown reference (objectively correct: no hue rotation with brightness).
#if ENABLE_HUE_RESTORATION
   // Reference = the grade run UNCLAMPED: keeps the real highlight channel ratio (a bright blue stays blue),
   // unlike the vanilla SDR whose per-channel saturate() shifts the hue at the clip. Hue strength 1.0 (exact),
   // chrominance 0.0 — we keep the by-luminance DICE chroma and let gamut mapping (GAMUT_MAPPING_TYPE in the
   // composition) roll chroma to the displayable maximum at each luminance. No hand-tuned desaturation.
   float3 hueRef = gamma_to_linear(GradeUE3(untonemapped, false));
   hdr = RestoreHueAndChrominance(hdr, hueRef, 1.0, 0.0);
#endif

   // 6. Perceptual highlight dechroma: bright sources fade toward white as luminance approaches peak (eye/sensor
   // saturation). Keeps colored mid-highlights, whitens only the brightest (so warm-tinted white lamps read as
   // neutral white at peak).
   const float highlightDechroma = LumaSettings.GameSettings.HighlightDechroma;
   if (highlightDechroma > 0.0)
   {
      // Map the slider to an exponent in [1, 0.05]: at the 1.0 max the exponent stays > 0 so mid-tones keep
      // their color (dcWeight < 1) and only luminance->peak fades to white. An exponent of exactly 0 would make
      // pow(x,0)=1 everywhere -> Saturation(hdr,0) -> full-frame greyscale, which is not what the slider means.
      float dcExp = lerp(1.0, 0.05, highlightDechroma);
      float dcWeight = saturate(pow(saturate(GetLuminance(hdr) / peakWhite), dcExp));
      hdr = Saturation(hdr, 1.0 - dcWeight);
   }

   // User saturation (Oklab, shared helper). 1.0 = vanilla.
   hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation);

   // User contrast: slope around 18% mid-gray (linear, 1.0 = paper white). 1.0 = vanilla. Excursions are
   // caught by the NaN/clamp tail; > peak highlights are acceptable for a creative slider.
   const float midGray = 0.18;
   hdr = (hdr - midGray) * LumaSettings.GameSettings.Contrast + midGray;

   outColor = hdr; // linear, 1.0 = paper white
#else
   // Vanilla reference: linearize the clamped SDR grade.
   outColor = gamma_to_linear(saturate(ungraded_sdr_gamma));
#endif

   // --- Common tail: UI paper-white pre-scale + post-process-space encode ---
#if UI_DRAW_TYPE >= 2
   // Pre-scale so the gamma-SDR HUD (drawn on top) lands at UIPaperWhite after composition rescales by it.
   outColor *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif
   // Sanitize: the scene carries small negative/WCG values; the recovery + hue restore + gamma encode can
   // emit NaN or negatives (linear_to_gamma of a negative is NaN). Clamp so no garbage reaches the swapchain.
   outColor = (outColor == outColor) ? outColor : 0.0; // NaN -> 0 (NaN != NaN)
   outColor = max(0.0, outColor);
#if POST_PROCESS_SPACE_TYPE == 0
   // Store gamma so the game's gamma-space HUD blends like vanilla; composition decodes + applies paper white.
   outColor = linear_to_gamma(outColor);
   // Anti-banding dither in the stored gamma space (the core composition does not dither). Animated triangular
   // noise; sub-perceptual at bit depth 9 so the later SMAA/RCAS passes don't visibly amplify it. HDR path only.
#if TONEMAP_TYPE >= 1
   if (LumaSettings.GameSettings.Dithering > 0.5)
      ApplyDithering(outColor, v1.xy, true, 1.0, DITHERING_BIT_DEPTH, LumaSettings.FrameIndex, true);
#endif
#endif
}
