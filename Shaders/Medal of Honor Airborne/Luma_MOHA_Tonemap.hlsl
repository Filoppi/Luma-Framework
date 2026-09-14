// Medal of Honor: Airborne — Luma HDR tonemap replacement, shared by BOTH final colour passes: UE3's
// UberPostProcessBlend with DoF on (PS 0xB9548800, VS 0x3C98E35B) and FGammaCorrectionPixelShader with it off. That
// one draw does DoF composite + bloom + parametric grade + output gamma into the 8-bit canvas the UI then draws on,
// and its TWO saturate()s clip 19.4% of a bright frame — highlight detail the engine computes and drops. Strategy
// (analytic in-shader TM, no LUT; same shape as the Borderlands GOTY port): reconstruct the scene mix exactly as the
// game does -> untonemapped; run the game's own grade UNCHANGED on the saturate path -> ungraded_sdr, the look;
// UpgradeToneMap (RenoDX ShortFuse) adds back the highlight luminance that grade clipped, hue-matched -> recovered;
// DICE rolloff to the user's peak/paper white -> hdr; then lock hue/chroma to the UNCLAMPED grade so the dev's
// colour intent survives. Output is GAMMA space (POST_PROCESS_SPACE_TYPE 0, 1.0 = paper white) because the HUD
// blends src-alpha onto this same canvas right after and a linear buffer washes it out; UI_DRAW_TYPE 2 pre-scales by
// GamePaperWhite/UIPaperWhite. Two transcription rules hold throughout: every texture fetch in a dgVoodoo-translated
// shader is followed by an `and`/`or` pair against b3 (its D3D9 format emulation), and the replacements declare all
// 13 interpolators — VS->PS linkage is by REGISTER, so dropping either shifts colour or the whole signature.

// clang-format off
// ORDER IS LOAD-BEARING — do not sort. The game-local "Includes/Common.hlsl" MUST come first: it defines
// LUMA_GAME_CB_STRUCTS (via GameCBuffers.hlsl) BEFORE any shared header pulls Settings.hlsl, so
// LumaSettings.GameSettings resolves to the real grade struct rather than the empty dummy.
#include "Includes/Common.hlsl"             // game-local: defines LumaGameSettings (grade sliders) before the LumaSettings cbuffer
#include "../Includes/Color.hlsl"
#include "../Includes/ColorGradingLUT.hlsl" // RestoreHueAndChrominance, SimpleGamutClip
#include "../Includes/DICE.hlsl"            // DICETonemap / DefaultDICESettings
#include "../Includes/Reinhard.hlsl"        // ReinhardTonemap / DefaultReinhardSettings (NeutralSDR)
#include "../Includes/Tonemap.hlsl"         // UpgradeToneMap
// clang-format on

#include "Includes/GameBindings.hlsl" // b3/b4, the dgVoodoo masks, ApplyDgvMask, PowUE3

// HDR / vanilla. 1 = recover real highlights + DICE display map (default). 0 = vanilla clamped SDR reference.
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 1
#endif

// Run the display map in a BT.2020 working space (round-tripped back to BT.709). Gamut-correct handling of
// highly saturated highlights — NOT a display-gamut expansion.
#ifndef TONEMAP_IN_WIDER_GAMUT
#define TONEMAP_IN_WIDER_GAMUT 1
#endif

// UE3 UberPostProcess grade constants, at the register indices the disassembly reads them from.
#define DoFParams                 PsConstants[8]  // .x focus distance, .y 1/range, .z falloff exponent
#define DoFMaxBlur                PsConstants[9]  // .x max blur near, .y max blur far
#define SceneShadowsAndDesat      PsConstants[10] // .xyz shadows lift (subtracted), .w saturation weight
#define SceneInverseHighLights    PsConstants[11] // .xyz scene scale
#define SceneMidTones             PsConstants[12] // .xyz grade gamma
#define SceneLuminanceWeights     PsConstants[13] // .xyz desaturation luminance weights
#define GammaColorScaleAndInverse PsConstants[14] // .xyz output scale, .w output (inverse) gamma
#define GammaOverlayColor         PsConstants[15] // .xyz tint offset

SamplerState SceneColorTextureSampler_s : register(s0);
SamplerState BlurredImageSampler_s : register(s1);
Texture2D<float4> SceneColorTexture : register(t0); // fp16 scene color; .w carries SCENE DEPTH, not alpha
Texture2D<float4> BlurredImage : register(t1);      // quarter-res DoF/bloom blur, stored pre-divided by 4 (scaled back x4 below)

// Luma HDR bloom pyramid mip 0 (Luma_Bloom_impl.hlsl, core DrawBloom): half-res LINEAR fp16, injected by main.cpp
// before this draw, slot must match kLumaBloomSlot. It REPLACES the game's bloom, so there is no double glow.
Texture2D<float4> LumaBloomTexture : register(t6);

// Added in scene-referred LINEAR light, before exposure, grade and the HDR display mapping, so the glow takes
// part in the tonemapping instead of being mapped separately. Half-res source, so one bilinear tap.
float3 ApplyLumaBloom(float3 untonemapped, float2 sceneUV)
{
   if (LumaSettings.GameSettings.LumaBloomEnable <= 0.5)
      return untonemapped;
   const float3 bloom = LumaBloomTexture.SampleLevel(SceneColorTextureSampler_s, sceneUV, 0.0).rgb;
   return untonemapped + max(0.0, bloom) * LumaSettings.GameSettings.BloomIntensity;
}

// Neutral SDR reference for the highlight-recovery delta.
float3 NeutralSDR(float3 color)
{
   ReinhardSettings settings = DefaultReinhardSettings();
   settings.by_luminance = true;
   return ReinhardTonemap(color, 100.f, 100.f, settings);
}

// The game's grade, verbatim from the disassembly. `clampSDR`: true = vanilla saturate() path, false = unclamped
// (max 0), keeping the highlights' real channel ratio instead of a per-channel hue shift. `outputScale` is normally
// GammaColorScaleAndInverse.xyz; the HDR path passes 1 and re-applies the real scale afterwards.
float3 GradeUE3(float3 scene, bool clampSDR, float3 outputScale)
{
   // Head: shadows -> scale -> midtones.
   float3 c = scene - SceneShadowsAndDesat.xyz;
   c = clampSDR ? saturate(c) : max(0.0, c); // mad_sat in the original
   c = c * SceneInverseHighLights.xyz;
   c = PowUE3(c, SceneMidTones.xyz);

   // Tail: desat + tint + output scale + gamma encode.
   float desat = dot(c, SceneLuminanceWeights.xyz);
   c = c * SceneShadowsAndDesat.www + GammaOverlayColor.xyz;
   c = c + desat;
   c = c * outputScale;
   c = clampSDR ? saturate(c) : max(0.0, c); // mul_sat in the original
   return PowUE3(c, GammaColorScaleAndInverse.www);
}

// Shared HDR back half, used by both replaced passes: highlight recovery -> display rolloff -> hue lock -> user
// sliders -> engine fade -> paper white -> encode -> sanitize.
//
// The three references are all GAMMA-space SDR grades of the same `untonemapped`:
//   `sdr_vanilla`  the pass's grade exactly as the original computes it, fade included (the TONEMAP_TYPE 0 output)
//   `sdr_nofade`   the same grade with the engine fade removed (the look reference the recovery builds on)
//   `hue_ref`      the same grade run UNCLAMPED, fade removed (keeps the real highlight channel ratio)
//
// `fadeScale` / `overlay` carry the engine fade the references were stripped of, applied HERE, after the sliders
// (UberPostProcessBlend scales, GammaCorrection lerps toward a colour). fadeScale 1 / overlay.w 0 disables either.
float3 FinishMOHA(float3 untonemapped, float3 sdr_vanilla, float3 sdr_nofade, float3 hue_ref, float3 fadeScale, float4 overlay, float2 sceneUV)
{
#if TONEMAP_TYPE >= 1
   float3 ungraded_sdr = gamma_to_linear(sdr_nofade); // linear SDR reference (1.0 = white)

   // 3. Recover the highlight luminance the SDR tonemap clipped, on top of the graded look.
   float3 neutral_sdr = NeutralSDR(untonemapped);
   float3 recovered = UpgradeToneMap(untonemapped, neutral_sdr, ungraded_sdr);

   // 4. Display rolloff to the user's peak/paper-white nits (DICE, hue-preserving by luminance). Both are floored:
   // DICE divides by them, and a NaN in a unorm target reads back black - it looks exactly like "the 3D disappeared".
   const float paperWhite = max(LumaSettings.GamePaperWhiteNits, 1.0) / sRGB_WhiteLevelNits;
   const float peakWhite = max(LumaSettings.PeakWhiteNits, paperWhite * sRGB_WhiteLevelNits) / sRGB_WhiteLevelNits;
#if TONEMAP_IN_WIDER_GAMUT
   recovered = BT709_To_BT2020(recovered);
#endif
   // Luminance in PQ (hue-preserving), then CORRECT_CHANNELS_BEYOND_PEAK_WHITE desaturates any channel still over
   // peak toward white — panels clip per channel, so an uncorrected saturated highlight clips with a hue shift.
   DICESettings ds = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
   float3 hdr = DICETonemap(recovered * paperWhite, peakWhite, ds) / paperWhite;
#if TONEMAP_IN_WIDER_GAMUT
   hdr = BT2020_To_BT709(SimpleGamutClip(hdr, true));
#endif

   // 5. Lock hue EXACTLY to the un-blown reference (no hue rotation with brightness).
   // Reference = the grade run UNCLAMPED: keeps the real highlight channel ratio (a bright blue stays blue), unlike
   // vanilla whose saturate() shifts hue at the clip. Hue 1.0 exact, chrominance 0.0 (composition gamut-maps).
   hdr = RestoreHueAndChrominance(hdr, gamma_to_linear(hue_ref), 1.0, 0.0);

   // 6. Perceptual highlight dechroma: bright sources fade toward white as luminance approaches peak. Keeps
   // colored mid-highlights, whitens only the brightest.
   const float highlightDechroma = LumaSettings.GameSettings.HighlightDechroma;
   if (highlightDechroma > 0.0)
   {
      // Exponent in [1, 0.05], never 0: pow(x,0) is 1 everywhere, i.e. a full-frame greyscale rather than a
      // highlight dechroma.
      float dcExp = lerp(1.0, 0.05, highlightDechroma);
      float dcWeight = saturate(pow(saturate(GetLuminance(hdr) / peakWhite), dcExp));
      hdr = Saturation(hdr, 1.0 - dcWeight);
   }

   // User saturation (shared helper: a lerp against BT.709 luminance, NOT hue-preserving). 1.0 = vanilla.
   hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation);

   // User contrast: slope around 18% mid-gray (linear, 1.0 = paper white). 1.0 = vanilla. Excursions are
   // caught by the NaN/clamp tail; > peak highlights are acceptable for a creative slider.
   const float midGray = 0.18;
   hdr = (hdr - midGray) * LumaSettings.GameSettings.Contrast + midGray;

   // Re-apply the engine fade linearly, LAST, after the creative sliders: contrast pivots on mid-gray, so a fade
   // before it resolves to (0 - 0.18) * C + 0.18 at full fade and never reaches black. At rest both are no-ops.
   hdr *= fadeScale;
   hdr = lerp(hdr, overlay.xyz, overlay.w);

   float3 outColor = hdr; // linear, 1.0 = paper white
#else
   // Vanilla reference: linearize the clamped SDR grade.
   float3 outColor = gamma_to_linear(saturate(sdr_vanilla));
#endif

   // --- Common tail: UI paper-white pre-scale + post-process-space encode ---
#if UI_DRAW_TYPE >= 2
   // Pre-scale so the gamma-SDR HUD on this same canvas lands at UIPaperWhite after composition rescales by it.
   // Guarded: an unset GamePaperWhiteNits would black the scene and leave the HUD, i.e. "the 3D disappeared".
   if (LumaSettings.GamePaperWhiteNits > 0.0)
      outColor *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif
   outColor = max(0.0, outColor); // negatives would turn into NaN in linear_to_gamma below
#if POST_PROCESS_SPACE_TYPE == 0
   // Store gamma so the game's gamma-space HUD blends like vanilla; composition decodes + applies paper white.
   outColor = linear_to_gamma(outColor);
   // Anti-banding dither in the stored gamma space (the core composition does not dither). Animated triangular
   // noise; sub-perceptual at bit depth 9. HDR path only.
#if TONEMAP_TYPE >= 1
   if (LumaSettings.GameSettings.Dithering > 0.5)
      ApplyDithering(outColor, sceneUV, true, 1.0, DITHERING_BIT_DEPTH, LumaSettings.FrameIndex, true);
#endif
#endif
   // Sanitize LAST: recovery, hue restore, encode and dither can each emit NaN, and a NaN in a unorm target reads
   // back black. Also covers cb4[15] (-nan(ind) on a load fade), which vanilla's saturate flushes to 0.
   outColor = IsNaN_Strict(outColor) ? 0.0 : outColor; // bit test, not "x != x": that form gets optimized away
   outColor = max(0.0, outColor);

#if DEVELOPMENT
   // Bring-up bisect (DEV only, all off = normal): DevSetting01 solid magenta (replacement is bound), DevSetting02
   // the reconstructed scene mix (t0/t1/UVs/masks read right), DevSetting03 the vanilla clamped grade.
   if (LumaSettings.DevSetting01 > 0.5)
      return float3(1.0, 0.0, 1.0);
   if (LumaSettings.DevSetting02 > 0.5)
      return linear_to_gamma(saturate(untonemapped));
   if (LumaSettings.DevSetting03 > 0.5)
      return saturate(sdr_vanilla);
#endif

   return outColor;
}

// Core tonemap. `sceneUV` is TEXCOORD1 (t0), `blurUV` is TEXCOORD0 (t1) — note the original samples t0 with v6
// and t1 with v5. Returns the value to store in the canvas (gamma space when POST_PROCESS_SPACE_TYPE==0).
float3 RunMOHATonemap(float2 blurUV, float2 sceneUV)
{
   // 1. Scene mix, exactly as vanilla: depth-driven DoF weight, bloom at x4, normalized by the weight sum.
   float4 scene = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, sceneUV), DgvMaskT0, DgvFillT0);

   const float depth = scene.w; // UE3 packs scene depth in the fp16 alpha
   const float signedDistance = depth - DoFParams.x;
   const float normalizedDistance = saturate(abs(signedDistance) * DoFParams.y);
   const float maxBlur = (signedDistance >= 0.0) ? DoFMaxBlur.y : DoFMaxBlur.x;
   const float blurAmount = min(PowUE3(normalizedDistance.xxx, DoFParams.zzz).x, maxBlur);
   const float sceneWeight = saturate(1.0 - blurAmount);

   float4 blurred = ApplyDgvMask(BlurredImage.Sample(BlurredImageSampler_s, blurUV), DgvMaskT1, DgvFillT1);
   // Stored pre-divided by 4, hence the x4; vanilla's unorm view also capped it at 4.0, a cap the fp16 upgrade lifted.
   // NEVER scaled by BloomIntensity: DoF and bloom are SUMMED, and in the sniper scope this term IS the frame.
   const float3 bloom = blurred.xyz * 4.0;
   const float weightSum = blurred.w * 4.0 + sceneWeight;

   float3 untonemapped = scene.xyz * sceneWeight + bloom;
   untonemapped *= (abs(weightSum) > 0.0) ? rcp(weightSum) : FLT_MAX; // rcp guard, as the original does

   // AFTER the normalisation: the weight sum belongs to the DoF composite, and dividing the glow by it would
   // make the bloom fade in and out with the defocus amount.
   untonemapped = ApplyLumaBloom(untonemapped, sceneUV);

   // Scene exposure (multiplier), scene-referred / pre-grade; the SDR reference below derives from the same
   // `untonemapped`, so the grade tracks the exposure change.
   untonemapped *= LumaSettings.GameSettings.Exposure;

   // The grade's output scale doubles as the engine FADE (level start, cutscenes). It stays OUT of the HDR references
   // and is re-applied as a gain at the end: UpgradeToneMap's delta is ADDITIVE, so folding it in blows up the ratio.
   const float3 outputScale = GammaColorScaleAndInverse.xyz;

   // 2. The game's own grade (artistic intent), gamma-encoded SDR. Vanilla-exact, fade included.
   float3 ungraded_sdr_gamma = GradeUE3(untonemapped, true, outputScale);

   // References carry no fade (outputScale = 1); it is re-applied inside FinishMOHA as a plain gain.
   return FinishMOHA(untonemapped, ungraded_sdr_gamma, GradeUE3(untonemapped, true, 1.0), GradeUE3(untonemapped, false, 1.0),
                     outputScale, float4(0.0, 0.0, 0.0, 0.0), sceneUV);
}

// The DoF-off final pass: UE3 FGammaCorrectionPixelShader (PS 0x52B868E0, VS 0xA2F269CA). With "bAllowDepthOfField =
// False" this makes the final LDR write instead, reading only the fp16 scene (t0) with its own register map.
#define GcColorScale   PsConstants[12] // .xyz ColorScale
#define GcOverlayColor PsConstants[13] // .xyz OverlayColor, .w its blend weight (this pass's fade)
#define GcInverseGamma PsConstants[14] // .x inverse display gamma (1/DisplayGamma, 0.4545 at the ini default 2.2)

// The grade with the overlay held OUT (it is the fade; FinishMOHA re-applies it after the display map).
float3 GradeGC(float3 scene, bool clampSDR)
{
   float3 c = scene * GcColorScale.xyz;
   c = clampSDR ? saturate(c) : max(0.0, c); // mad_sat in the original
   return PowUE3(c, GcInverseGamma.xxx);
}

// Vanilla-exact: saturate(lerp(scene * ColorScale, Overlay.rgb, Overlay.a)) then the inverse-gamma pow.
float3 GradeGCVanilla(float3 scene)
{
   float3 c = lerp(scene * GcColorScale.xyz, GcOverlayColor.xyz, GcOverlayColor.w);
   return PowUE3(saturate(c), GcInverseGamma.xxx);
}

// `sceneUV` is TEXCOORD0 (v5) — unlike UberPostProcessBlend, which reads the scene from TEXCOORD1.
float3 RunMOHAGammaCorrection(float2 sceneUV)
{
   float4 scene = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, sceneUV), DgvMaskT0, DgvFillT0);

   // The engine skips UberPostProcessBlend entirely in this configuration, so it has NO bloom of its own — the
   // Luma pyramid is the only glow this path ever gets, which makes it an addition here rather than a swap.
   float3 untonemapped = ApplyLumaBloom(scene.xyz, sceneUV) * LumaSettings.GameSettings.Exposure;

   // The original ends in a branch on PsConstants[8].x selecting a colour-grading LUT blend. That LUT is never bound,
   // so dgVoodoo folded its six sample stages to the constant (0,0,0,1). Only the direct path is reproduced.
   float3 ungraded_sdr_gamma = GradeGCVanilla(untonemapped);
   return FinishMOHA(untonemapped, ungraded_sdr_gamma, GradeGC(untonemapped, true), GradeGC(untonemapped, false),
                     1.0, GcOverlayColor, sceneUV);
}
