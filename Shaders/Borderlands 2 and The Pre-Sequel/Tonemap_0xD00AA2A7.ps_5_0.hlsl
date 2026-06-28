// clang-format off
// ORDER MATTERS — do NOT let clang-format sort these. The game-local Common.hlsl MUST come first: it defines
// LumaGameSettings (via GameCBuffers.hlsl) BEFORE the shared Settings.hlsl (pulled in by Color.hlsl below)
// declares the LumaSettings cbuffer. If sorted after Color.hlsl, GameSettings becomes the empty fallback struct
// and every LumaSettings.GameSettings.* reference fails to compile (invalid subscript).
#include "Includes/Common.hlsl"             // game-local: LumaGameSettings (grade sliders) — keep FIRST
#include "../Includes/Color.hlsl"           // BT709_To_BT2020 / BT2020_To_BT709
#include "../Includes/ColorGradingLUT.hlsl" // SimpleGamutClip
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl" // Reinhard::ReinhardScalable (max-channel compressor)
// clang-format on

// Borderlands 2 uber post-process / tonemap (UE3, via dgVoodoo D3D9->11 -> ps_5_0).
// Vanilla body (DOF + screen-blend bloom + vignette + ImageAdjustments + 16-slice ColorGradingLUT) transcribed
// VERBATIM (register-level) from the readable DX9 BL2 tonemap (tonemap_0x54ED86A0.ps_3_0),
// constants remapped DX9 cN -> cb4[N+8] (verified vs the live 0xD00AA2A7 disasm).
//
// HDR output uses the "apply SDR LUT in HDR" method: max-channel-compress the raw scene into [0,1] BEFORE the
// grade+LUT (so neither the per-channel saturate nor the LUT clip -> highlight hue/chroma preserved), then expand
// back with the exact inverse (sdr / tm) and DICE-map to the display peak. SDR mode keeps tm=1 -> the grade runs on
// the raw scene exactly like vanilla. Compression MUST be max-channel: it scales all 3 channels by one factor (hue
// intact) AND pins the brightest channel <=1 (nothing escapes the LUT). by-luminance would leave a saturated
// channel >1 and the LUT would clip it; per-channel shifts hue outright.
// NOTE: the per-channel ImageAdjustments curve below MUST keep the original's exact swizzles (r0.zzxy / r3.z,w,xy) — a
// "cleaner" rewrite swaps channels and casts the whole image green.

// ---- Per-game texture/sampler slot map: Borderlands 2 vs The Pre-Sequel ------------------------------
// dgVoodoo maps each DX9 sampler sN 1:1 onto DX11 tN. BL2 and TPS run the same UE3 uber-post tonemap, but TPS
// inserts a LightShaftTexture at slot 1, shifting bloom/vignette/LUT/DOF DOWN one slot (verified against
// the two DX9 tonemaps: BL2 tonemap_0x54ED86A0 has LUT@s3/DOF@s4; TPS tps_tonemap_0xF8997849 has
// lightshaft@s1, LUT@s4, DOF@s5). The grade math is identical, so the body below is SHARED verbatim — only
// which register each named texture binds to changes, plus TPS's extra light-shaft composite term. The TPS
// file (Tonemap_0xFCFE623E.ps_5_0.hlsl) #defines these macros then #includes this file; with them undefined
// (BL2) every mapping is the identity = the original BL2 bindings, so BL2 is byte-for-byte unchanged.
#ifndef TM_T_BLOOM
#define TM_T_BLOOM     t1 // FilterColor1Texture (screen-blend bloom)
#define TM_T_VIGNETTE  t2 // VignetteTexture
#define TM_T_LUT       t3 // ColorGradingLUT (256x16, 16-slice)
#define TM_T_DOF       t4 // LowResPostProcessBuffer (half-res DOF)
#define TM_T_LUMABLOOM t5 // injected Luma HDR bloom (free slot on BL2)
#define TM_S_BLOOM     s1
#define TM_S_VIGNETTE  s2
#define TM_S_LUT       s3
#define TM_S_DOF       s4
#endif

Texture2D<float4> t0 : register(t0); // SceneColorTexture (fp16 HDR scene)
#if TM_HAS_LIGHTSHAFT
Texture2D<float4> t_lightshaft : register(TM_T_LIGHTSHAFT); // LightShaftTexture (TPS god rays) — slot 1
#endif
Texture2D<float4> t1 : register(TM_T_BLOOM);     // FilterColor1Texture (bloom)        BL2 t1 / TPS t2
Texture2D<float4> t2 : register(TM_T_VIGNETTE);  // VignetteTexture                    BL2 t2 / TPS t3
Texture2D<float4> t3 : register(TM_T_LUT);       // ColorGradingLUT (256x16, 16-slice) BL2 t3 / TPS t4
Texture2D<float4> t4 : register(TM_T_DOF);       // LowResPostProcessBuffer (half-res DOF) BL2 t4 / TPS t5
Texture2D<float4> t5 : register(TM_T_LUMABLOOM); // Luma HDR pyramidal bloom, bound by the mod when LumaBloomEnable (BL2 t5 / TPS t8 — TPS t5 is the native DOF)
Texture2D<float4> t7 : register(t7);             // Half-res pre-blurred scene for the Luma Gaussian DoF, bound when it is active (free on both BL2/TPS)

SamplerState s0_s : register(s0);
SamplerState s1_s : register(TM_S_BLOOM);    // BL2 s1 / TPS s2
SamplerState s2_s : register(TM_S_VIGNETTE); // BL2 s2 / TPS s3
SamplerState s3_s : register(TM_S_LUT);      // BL2 s3 / TPS s4
SamplerState s4_s : register(TM_S_DOF);      // BL2 s4 / TPS s5

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

#define BloomTintAndScreenBlendThreshold cb4[16] // c8
#define ImageAdjustments2                cb4[17] // c9
#define ImageAdjustments3                cb4[18] // c10
#define HalfResMaskRect                  cb4[19] // c11
#define DOFKernelSize                    cb4[20] // c12
#define VignetteSettings                 cb4[21] // c13
#define VignetteColor                    cb4[22] // c14

void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0, // DOF radial/kernel coords (.zw)
    float4 v6 : TEXCOORD1, // scene UV (.xy), half-res DOF UV (.zw)
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   float4 o;
   float4 r0, r1, r2, r3;

   // --- DOF: vanilla half-res Gaussian composite, OR Luma separable HDR Gaussian ---
   // The game's depth/object DoF decision is baked into t4.a (the in-focus weight: 1 = sharp focused
   // subject, 0.25 = max-blurred background; devkit clone confirmed it tracks the focused subject's
   // silhouette). The Luma path KEEPS that decision — when the game turns DoF off, t4.a -> ~1 -> the blend
   // weight -> 0 -> no blur, so cutscenes/scripted focus stay game-driven — but replaces the half-res
   // Gaussian with a denser separable Gaussian of the prefiltered scene (t7), preserving HDR so bright
   // defocused areas glow. DOFKernelSize.z/.w + v5.zw drive only the vanilla fallback below.
   float2 sceneUV = v6.xy;
   float2 dofUV = clamp(v6.zw, HalfResMaskRect.xy, HalfResMaskRect.zw);
   float4 sceneTap = t0.SampleLevel(s0_s, sceneUV, 0);
   float centerInFocus = t4.SampleLevel(s4_s, dofUV, 0).a; // 1 = sharp .. 0.25 = max blur

   // Replicate the game's EXACT per-pixel DoF blend weight (radial DOF-kernel falloff, VERBATIM from the
   // vanilla branch below, + the t4.a in-focus weight), then blend the blurred scene by it. The game blends
   // only a fraction of its blurred buffer (sharpness >> t4.a alone), so matching this weight is what keeps
   // the effect at vanilla strength instead of fully replacing the background. Without it it over-blurs.
   float dofRadial = (v5.z * 2.0 - 1.0) * DOFKernelSize.z;
   float radialSharp = 1.0 - saturate(dofRadial * dofRadial);
   float blurWeight = 1.0 - saturate(radialSharp + centerInFocus); // 0 = sharp .. 1 = full vanilla blur

   float dofType = LumaSettings.GameSettings.DOFType; // 0 = vanilla game Gaussian, 2 = Luma separable Gaussian
   float3 hdr_color;
   if (dofType < 0.5)
   {
      // --- vanilla DOF composite (verbatim) ---
      r0.y = DOFKernelSize.w + v5.w;
      r0.x = v5.z;
      r0.xy = r0.xy * 2 + -1;
      r0.xy = r0.xy * DOFKernelSize.z;
      r0.x = saturate(dot(r0.x, r0.x) + 0);
      r0.x = -r0.x + 1;
      r0.yz = max(v6.xzww, HalfResMaskRect.xxyw).yz;
      r1.xy = min(HalfResMaskRect.zw, r0.yz);
      r1 = t4.Sample(s4_s, r1.xy);
      r0.x = saturate(r0.x + r1.w);
      r2 = float4(1, 1, 0, 0) * v6.xyxx;
      r2 = t0.SampleLevel(s0_s, r2.xy, 0);
      hdr_color = lerp(r1.xyz * 4, r2.rgb, r0.x);
   }
   else if (blurWeight <= 0.02)
   {
      hdr_color = sceneTap.rgb; // in-focus (both Luma modes) -> no blur; covers most of the screen (cheap)
   }
   else
   {
      // Luma separable Gaussian: t7 holds the pre-blurred half-res scene (dense H+V Gaussian on the prefilter),
      // so this is a single smooth tap — no sparse gather, no jitter -> grain-free. Blend at the game's DoF
      // strength (blurWeight). HDR is preserved, so bright defocused areas glow.
      hdr_color = lerp(sceneTap.rgb, t7.SampleLevel(s0_s, sceneUV, 0).rgb, blurWeight);
   }

   // --- bloom ---
   if (LumaSettings.GameSettings.LumaBloomEnable > 0.5)
   {
      // Luma HDR pyramidal bloom (t5, generated by the mod from the linear fp16 scene). Additive in linear so
      // bright HDR sources glow proportionally (the game's bloom is UNORM-clamped + quarter-res single-level).
      // v6.xy = the screen UV used for the scene sample above; s1_s is a linear sampler.
      float3 lumaBloom = t5.SampleLevel(s1_s, v6.xy, 0).rgb;
      hdr_color += lumaBloom * LumaSettings.GameSettings.BloomIntensity;
   }
   else
   {
      // Vanilla bloom (screen-blend gated by luminance, t1). BloomIntensity scales it (1 = vanilla).
      r0.w = dot(hdr_color, float3(0.300000012, 0.589999974, 0.109999999));
      r0.w = r0.w * -3;
      r0.w = exp2(r0.w);
      r0.w = saturate(r0.w * BloomTintAndScreenBlendThreshold.w);
      r1 = t1.Sample(s1_s, v5.zw);
      r1.xyz = r1.xyz * BloomTintAndScreenBlendThreshold.xyz;
      r1.xyz = r1.xyz * 4;
      hdr_color += r1.xyz * r0.w * LumaSettings.GameSettings.BloomIntensity;
   }

#if TM_HAS_LIGHTSHAFT
   // Light shafts / god rays (TPS only). Vanilla TPS composites a separate low-res light-shaft buffer here,
   // between bloom and the tonemap: an inverse-luminance gate (only adds into darker pixels), additive *4
   // color, and a per-pixel attenuation in .a that scales the existing scene down where shafts occlude.
   // Transcribed verbatim from the DX9 TPS tonemap (tps_tonemap_0xF8997849). BL2 has no such pass.
   {
      float lsGate = saturate(exp2(dot(hdr_color, float3(0.300000012, 0.589999974, 0.109999999)) * -3.0));
      float4 ls = t_lightshaft.Sample(s0_s, v5.zw);
      hdr_color = hdr_color * ls.w + (ls.xyz * 4.0) * lsGate;
   }
#endif

   // User Exposure (scene-referred, pre-grade; 1 = vanilla). Applies to both SDR and HDR — the grade below tracks it.
   hdr_color *= LumaSettings.GameSettings.Exposure;

   // --- max-channel compress BEFORE grade+LUT (HDR only) ---
   // tm = per-pixel uniform scale (<=1) that maps the brightest channel into [0,1] via a midgray-pinned Reinhard.
   // One scale for all 3 channels => hue/chroma intact; brightest channel pinned <=1 => the saturate(line ~129) and
   // the 16-slice LUT never clip. SDR mode leaves tm=1 so the grade runs on the raw scene exactly like vanilla.
   float tm = 1.0;
   if (LumaSettings.DisplayMode == 1)
   {
      float mx = max(hdr_color.r, max(hdr_color.g, hdr_color.b));
      float mx_c = Reinhard::ReinhardScalable(mx, 1.0, 0.0, MidGray, MidGray);
      tm = (mx > 1e-6) ? (mx_c / mx) : 1.0;
   }
   r0.xyz = hdr_color * tm;

   // --- vignette (verbatim) ---
   float3 vignette_color = r0.rgb;
   r1.xyz = r0.xyz * VignetteColor.xyz;
   r2.xyz = r0.xyz * -VignetteColor.xyz + r0.xyz;
   r1.xyz = v6.y * r2.xyz + r1.xyz;
   r2.xyz = r0.xyz * r1.xyz;
   r1.xyz = r0.xyz * -r1.xyz + r0.xyz;
   r3.xy = v6.xy + v6.xy;
   r3 = t2.Sample(s2_s, r3.xy);
   r0.w = saturate(r3.x + VignetteSettings.y);
   r1.xyz = r0.w * r1.xyz + r2.xyz;
   r2.y = 0.00999999978;
   r0.w = r2.y + -VignetteSettings.x;
   r0.xyz = (r0.w >= 0) ? r0.xyz : r1.xyz;
   // User Vignette Intensity: lerp between the pre-vignette color and the vignetted result (1 = vanilla, 0 = none).
   r0.xyz = lerp(vignette_color, r0.xyz, LumaSettings.GameSettings.VignetteIntensity);

   // --- ImageAdjustments per-channel curve (verbatim; keep swizzles exactly) ---
   r1 = r0.zzxy + -ImageAdjustments2.z;
   r1 = saturate(r1 * 10000);
   r2.xyz = r0.xyz + ImageAdjustments2.x;
   r3.z = 1 / abs(r2.x);
   r3.w = 1 / abs(r2.y);
   r3.xy = 1 / abs(r2.z);
   r2 = r0.zzxy * r3;
   r0.xyz = r0.xyz * ImageAdjustments2.w;
   r3.x = log2(r0.x);
   r3.y = log2(r0.y);
   r3.z = log2(r0.z);
   r0.xyz = r3.xyz * 0.454545468;
   r3.z = exp2(r0.x);
   r3.w = exp2(r0.y);
   r3.xy = exp2(r0.z);
   r0 = r2.yyzw * ImageAdjustments2.y + -r3.yyzw;
   r0 = r1 * r0 + r3;
   r1 = r2 * ImageAdjustments2.y + -r0.yyzw;
   r0 = saturate(ImageAdjustments3.x * r1 + r0);

   // --- 16-slice ColorGradingLUT (verbatim trilinear; keep swizzles exactly) ---
   // (Tetrahedral interpolation was measured against this on the live LUT: maxΔ 0.5/255, 0% visibly different — the
   // LUT is near-linear between grid points so it gives no gain at +cost. Reverted to vanilla trilinear.)
   r1.xyw = (r0.xwzz * float4(14.9998999, 0.9375, 0.9375, 0.05859375)).xyw;
   r0.x = frac(r1.x);
   r0.x = -r0.x + r1.x;
   r1.x = r0.x * 0.0625 + r1.w;
   r0.x = r0.y * 15 + -r0.x;
   r1 = r1.xyxy + float4(0.001953125, 0.03125, 0.064453125, 0.03125);
   r2 = t3.Sample(s3_s, r1.zw);
   r1 = t3.Sample(s3_s, r1.xy);
   r0.yzw = (-r1.xxyz + r2.xxyz).yzw;
   o.xyz = r0.x * r0.yzw + r1.xyz;

   // ====================== Luma HDR output (max-channel inverse) ======================
   // o.rgb = the graded look the LUT produced. In HDR it is the grade of the COMPRESSED scene (hdr_color*tm);
   // dividing by tm is the exact inverse -> recovers real HDR range with true hue+chroma (two bright pixels that
   // both saturate the LUT are re-separated by /tm). In SDR tm was 1, so o.rgb is the vanilla grade of the raw scene.
   float3 graded_sdr_gamma = o.rgb;
   float3 sdr_lin = gamma_to_linear(graded_sdr_gamma, GCT_MIRROR);

   const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
   const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

   float3 postProcessedColor;

   if (LumaSettings.DisplayMode == 1) // HDR
   {
      float3 recovered = sdr_lin / max(tm, 1e-6); // exact inverse of the pre-grade compression -> HDR range (linear BT.709)

      // Display rolloff to the user's peak/paper-white nits. DICE by-luminance preserves hue; the
      // *_CORRECT_CHANNELS_BEYOND_PEAK_WHITE type additionally gamut-maps any single channel that rides past peak
      // (e.g. saturated blues) back under it (fleet default).
      // DICE takes/returns linear BT.709 and processes internally in BT.2020 (its defaults), so feed BT.709
      // directly: do NOT round-trip primaries by hand. (With type 1 the manual 709<->2020 cancelled because the
      // luminance compress is a pure scalar; the corrected type's per-channel gamut map breaks that cancellation.)
      DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
      float3 hdr = DICETonemap(recovered * paperWhite, peakWhite, settings) / paperWhite;

      // --- User HDR grade (HDR display path only; defaults are vanilla no-ops) ---
      // Highlight desaturation: bright sources fade toward white as luminance approaches peak (eye/sensor
      // saturation). exponent in [1,0.05] keeps mid-tones colored; only luminance->peak whitens.
      const float highlightDechroma = LumaSettings.GameSettings.HighlightDechroma;
      if (highlightDechroma > 0.0)
      {
         float dcExp = lerp(1.0, 0.05, highlightDechroma);
         float dcWeight = saturate(pow(saturate(GetLuminance(hdr) / peakWhite), dcExp));
         hdr = Saturation(hdr, 1.0 - dcWeight);
      }
      hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation); // user Saturation (Oklab; 1 = vanilla)
      // user Contrast: slope around 18% mid-gray (linear, 1.0 = paper white). Excursions caught by the NaN/clamp tail.
      const float midGray = 0.18;
      hdr = (hdr - midGray) * LumaSettings.GameSettings.Contrast + midGray;

      postProcessedColor = hdr;
   }
   else // SDR (still presented through the scRGB swapchain) — sdr_lin is the vanilla grade (tm was 1)
   {
      postProcessedColor = sdr_lin;
   }

#if UI_DRAW_TYPE >= 2
   // Pre-scale so the gamma-SDR HUD drawn on top (not pre-scaled) lands at UIPaperWhite after the composition
   // rescales the buffer by it; the scene then lands at GamePaperWhite. Gives the HUD its own paper white.
   postProcessedColor *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif

   // Sanitize (inverse divide + DICE + gamma encode can emit NaN/negatives -> garbage on the swapchain).
   postProcessedColor = (postProcessedColor == postProcessedColor) ? postProcessedColor : 0.0; // NaN -> 0
   postProcessedColor = max(0.0, postProcessedColor);

   postProcessedColor = linear_to_gamma(postProcessedColor, GCT_MIRROR);

   // Sub-perceptual animated triangular dither (9-bit, gamma space) vs gradient banding from the HDR expansion +
   // 10-bit PQ encode. HDR only, runtime toggle (GameSettings.Dithering), FrameIndex animates it. Runs before
   // SMAA but ~1/511 noise is below SMAA's 0.05 edge threshold -> no spawned edges / RCAS amplification.
   if (LumaSettings.DisplayMode == 1 && LumaSettings.GameSettings.Dithering > 0.5)
      ApplyDithering(postProcessedColor, v6.xy, true, 1.0, DITHERING_BIT_DEPTH, LumaSettings.FrameIndex, true);

   o0.rgb = postProcessedColor;
   o0.a = 0; // vanilla wrote o.w = 0
}
