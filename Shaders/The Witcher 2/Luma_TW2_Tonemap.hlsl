// clang-format off
// ORDER MATTERS — do NOT let clang-format sort these. The game-local Common.hlsl MUST come first: it defines
// LumaGameSettings (via GameCBuffers.hlsl) BEFORE the shared Settings.hlsl (pulled in by Color.hlsl below)
// declares the LumaSettings cbuffer. If sorted after Color.hlsl, GameSettings becomes the empty fallback struct
// and every LumaSettings.GameSettings.* reference fails to compile (invalid subscript).
#include "Includes/Common.hlsl" // game-local: LumaGameSettings (grade sliders) — keep FIRST
#include "../Includes/Color.hlsl"
#include "Includes/GameBindings.hlsl" // b3/b4, the dgVoodoo masks, ApplyDgvMask, DgVoodooRcp
// clang-format on

// The Witcher 2 EE — tonemap ("exposure") pass SHARED IMPLEMENTATION (REDengine, DX9 via dgVoodoo D3D9->11).
// Holds the whole pass including main(); the per-hash wrapper files (Tonemap_0x<HASH>.ps_5_0.hlsl, one per
// permutation × dgVoodoo build) only set TM_BRIGHT_PASS and #include it. No hash in this filename -> not
// matched/replaced directly.
//
// Vanilla body transcribed VERBATIM (register-level) from the dgVoodoo-translated CSOs (0x91348C0F exposure,
// 0x00E31BF9 bloom bright-pass; DX9 origins 0xC5ADBC35/0xF01A691E), constants remapped DX9 cN -> cb4[N+8].
// The game's "tone map" (CEnvToneMappingParameters) is only an adaptive exposure multiply — NO curve, NO clamp.
// The adaptation is histogram auto-levels: black and white are luminance percentiles of the frame, and the
// exposure maps [black, white] to [0, 1] as one per-pixel ratio (hue kept), capped by m_maxMultiplier, so
// everything above the histogram white overshoots 1.0 unclamped.
// The whole post chain is fp16 and LINEAR up to the final grade, whose per-channel vMidtone power is the display
// encode (measured 1/2.2 in the neutral environment); the UI then blends src-alpha onto that gamma result.
// This pass stays bit-exact vanilla apart from the user Exposure; the Luma HDR output block runs at the end
// of the FINAL GRADE replacement instead, and fp16 keeps this pass's small overshoot alive for it.
//
// The SAME shader runs two roles per frame: a MAIN grade (RT at least the swapchain size with its aspect ratio, feeds
// post/UI/present) and an AUX draw (smaller RT, DoF/flare source). Both are vanilla here; main.cpp detects the role from
// the bound RT size, for the main-post-processing flag and to scope the exposure readback to one draw per frame.

// ---- Permutation map (set by the wrappers) -----------------------------------------------------------
// TM_BRIGHT_PASS 0 -> DX9 0xC5ADBC35: exposure + post-scale only, alpha passthrough, adaptation at t1/s1.
// TM_BRIGHT_PASS 1 -> DX9 0xF01A691E, the bloom bright-pass (CEnvBloomParameters): + threshold ramp, saturation
//                     and colour; writes alpha 1, adaptation at t2/s2, renders the half-res bloom/shaft source.
// Future static perms (DX9 0xA7D76FB1/0xEC6F063B, DX11 hashes unknown) read the exposure from
// PSC_LumRanges constants instead of the adaptation texture — add TM_STATIC here when they are dumped.
#if TM_BRIGHT_PASS
#define TM_T_ADAPT    t2
#define TM_S_ADAPT    s2
#define TM_ADAPT_MASK DgvMaskT2
#define TM_ADAPT_FILL DgvFillT2
#else
#define TM_T_ADAPT    t1
#define TM_S_ADAPT    s1
#define TM_ADAPT_MASK DgvMaskT1
#define TM_ADAPT_FILL DgvFillT1
#endif

Texture2D<float4> t0 : register(t0);              // scene (fp16, linear light, unclamped)
Texture2D<float4> t_adapt : register(TM_T_ADAPT); // 1x1 fp16 adaptation (.x black level, .z gain)

SamplerState s0_s : register(s0);
SamplerState s_adapt_s : register(TM_S_ADAPT);

#define LumWeights        cb4[58] // c50 PSC_LumWeights — luminance dot (dp4: folds scene alpha in)
#define LumRanges2        cb4[59] // c51 PSC_LumRanges2 — .x exposure cap (m_maxMultiplier), .y post-scale
#define BrightPassWeights cb4[60] // c52 vWeights — bright-pass luminance dot (dp4 folds alpha)
#define BrightPassParams  cb4[61] // c53 vParams — .x saturation, .y threshold, .z threshold range
#define BrightPassColor   cb4[62] // c54 vColor — bright-pass colour

// Full dgVoodoo interpolator set; v5 = TEXCOORD0 (scene UV in .xy) is the only one the pass uses.
// Output: the exposed linear color (alpha: scene passthrough on the exposure perm, 1 on the bright-pass perm).
void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0,
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   // --- vanilla body (verbatim transcription) ---
   float4 adaptation = t_adapt.SampleLevel(s_adapt_s, float2(0.0, 0.0), 0.0);
   adaptation = ApplyDgvMask(adaptation, TM_ADAPT_MASK, TM_ADAPT_FILL);
   float4 scene = t0.Sample(s0_s, v5.xy);
   scene = ApplyDgvMask(scene, DgvMaskT0, DgvFillT0);

#if TONEMAP_TYPE == 1
   // User exposure (1 = vanilla): a real scene multiplier before the adaptive exposure reads it, so the
   // bright-pass threshold keeps tracking the same relative levels as vanilla.
   scene.rgb *= LumaSettings.GameSettings.Exposure;
#endif

   // exposure = min(gain * max(lum - black, 0) / max(lum, 1e-6), cap): luminance levels [black, white] -> [0, 1].
   // The adaptation texel holds (black, white, gain = 1 / max(white - black, 0.01)).
   float lum = dot(LumWeights, scene);
   float inv_lum = DgVoodooRcp(max(1e-6, lum));
   float exposure = min(adaptation.z * max(lum - adaptation.x, 0.0) * inv_lum, LumRanges2.x);

   float3 scaled = scene.rgb * exposure * LumRanges2.y;

#if TM_BRIGHT_PASS
   // Bloom bright-pass: threshold ramp on the exposed luminance, saturation around it, colour.
   float brightRamp = saturate((dot(BrightPassWeights, float4(scaled, scene.a)) - BrightPassParams.y) * DgVoodooRcp(BrightPassParams.z));
   float lum_scaled = dot(BrightPassWeights.xyz, scaled);
   float3 saturated = (scaled - lum_scaled) * BrightPassParams.x + lum_scaled;
   float3 vanillaColor = max(saturated, 0.0) * brightRamp * BrightPassColor.rgb;
   const float vanillaAlpha = 1.0;
#else
   float3 vanillaColor = scaled;
   const float vanillaAlpha = scene.a;
#endif

   o0 = float4(vanillaColor, vanillaAlpha);
}
