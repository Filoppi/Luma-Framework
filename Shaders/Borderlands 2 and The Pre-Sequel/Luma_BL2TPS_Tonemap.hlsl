// clang-format off
// ORDER MATTERS — do NOT let clang-format sort these. The game-local Common.hlsl MUST come first: it defines
// LumaGameSettings (via GameCBuffers.hlsl) BEFORE the shared Settings.hlsl (reached through DICE.hlsl -> Common.hlsl)
// declares the LumaSettings cbuffer. Sorted after DICE.hlsl, GameSettings becomes the empty fallback struct and
// every LumaSettings.GameSettings.* reference fails to compile (invalid subscript).
#include "Includes/Common.hlsl" // game-local: LumaGameSettings (grade sliders) + shared Common (Color/Math/Settings) — keep FIRST
#include "../Includes/DICE.hlsl"
// clang-format on

// Borderlands 2 + The Pre-Sequel — uber post-process / tonemap SHARED IMPLEMENTATION (UE3, via dgVoodoo D3D9->11).
// Holds the grade body as RunTonemap(); the per-hash wrapper files (Tonemap_0x<HASH>.ps_5_0.hlsl, one per game ×
// dgVoodoo version) declare the full UE3 interpolator set + main() and forward to it. No hash in this filename ->
// not matched/replaced directly; it is #included by the wrappers.
//
// Vanilla body (DOF + screen-blend bloom + vignette + ImageAdjustments + 16-slice ColorGradingLUT) transcribed
// VERBATIM (register-level) from the readable DX9 BL2 tonemap (tonemap_0x54ED86A0.ps_3_0),
// constants remapped DX9 cN -> cb4[N+8].
//
// HDR = MELE's scheme (Shaders/Mass Effect Legendary Edition/Tonemap_ME_Analytic_Body.hlsl) with this game's curve:
// grade and LUT get the NATIVE scene, then the graded colour is divided by the compression the vanilla curve applied
// to the pixel's brightest channel (curve_scale), then DICE. One scalar per pixel keeps every channel ratio, so the
// game's own highlight whitening survives; SDR leaves curve_scale = 1 and is bit-for-bit vanilla. Not a
// compress->grade->expand wrap: the curve is an asymptotic Reinhard (see VanillaCurveLinear), there was nothing to
// protect, and the wrap cost up to 26 deg of hue where vanilla never clipped.
// The per-channel ImageAdjustments curve below MUST keep the original's swizzles (r0.zzxy / r3.z,w,xy) — a
// "cleaner" rewrite swaps channels and casts the whole image green.

// ---- Per-game texture/sampler slot map: Borderlands 2 vs The Pre-Sequel ------------------------------
// dgVoodoo maps DX9 sampler sN 1:1 onto DX11 tN. TPS inserts a LightShaftTexture at slot 1, shifting bloom/vignette/
// LUT/DOF down one (DX9: BL2 tonemap_0x54ED86A0 LUT@s3/DOF@s4; TPS tps_tonemap_0xF8997849 lightshaft@s1/LUT@s4/DOF@s5).
// The grade math is identical, so the body is shared: the TPS wrapper #defines these macros, BL2 leaves them at the
// identity below and stays byte-for-byte unchanged.
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

// The vanilla tone curve for ONE channel in LINEAR light; mirrors the per-channel ImageAdjustments block in
// RunTonemap, keep them in sync. Measured in-game (52 constant sets): an analytic Reinhard with its white point at
// scene 14, driven by the live exposure W - A = 0.22/W, Y = 1 + A/14 (so T(14) = 1), Z ~ 0.2512/W, K = 0 everywhere
// seen. c <= Z: (W*c)^(1/2.2), a pure gain once linearised; c > Z: Y*c/(c+A), so saturate() bites only at scene >= 14.
float VanillaCurveLinear(float c)
{
   const float A = ImageAdjustments2.x;
   const float Y = ImageAdjustments2.y;
   const float Z = ImageAdjustments2.z;
   const float W = ImageAdjustments2.w;
   const float K = ImageAdjustments3.x;
   float g = pow(max(c, 0.0) * W, 1.0 / 2.2);
   float t = Y * c / (c + A);
   float b = (c > Z) ? t : g;
   return pow(saturate(b + K * (t - b)), 2.2);
}

// The tonemap grade. v5 = TEXCOORD0 (DOF radial/kernel coords in .zw), v6 = TEXCOORD1 (scene UV .xy, half-res DOF
// UV .zw) — the only interpolators the body uses. Returns the final gamma-space color (o0.a is always 0).
float4 RunTonemap(float4 v5, float4 v6)
{
   float4 o;
   float4 r0, r1, r2, r3;

   // --- DOF composite (verbatim) ---
   // t4.a is the in-focus weight (1 = sharp subject, 0.25 = max-blurred background); summed with a radial falloff it
   // picks between the half-res blurred buffer (stored pre-divided by 4) and the sharp scene.
   float3 hdr_color;
   r0.y = DOFKernelSize.w + v5.w;
   r0.x = v5.z;
   r0.xy = r0.xy * 2 + -1;
   r0.xy = r0.xy * DOFKernelSize.z;
   r0.x = saturate(dot(r0.xy, r0.xy) + 0); // dp2: BOTH components (verified in all four dgVoodoo dumps)
   r0.x = -r0.x + 1;
   r0.yz = max(v6.xzww, HalfResMaskRect.xxyw).yz;
   r1.xy = min(HalfResMaskRect.zw, r0.yz);
   r1 = t4.Sample(s4_s, r1.xy);
   r0.x = saturate(r0.x + r1.w);
   r2 = float4(1, 1, 0, 0) * v6.xyxx;
   r2 = t0.SampleLevel(s0_s, r2.xy, 0);
   hdr_color = lerp(r1.xyz * 4, r2.rgb, r0.x);

   // --- bloom ---
   if (LumaSettings.GameSettings.LumaBloomEnable > 0.5)
   {
      // Luma pyramidal bloom (t5, built by the mod from the fp16 scene; the game's is UNORM-clamped). Composited like
      // the vanilla branch below - the artists' per-area tint and the x4 - so BloomIntensity 1 is vanilla strength
      // (it arrives pre-scaled by the pyramid-to-native energy ratio, main.cpp). The vanilla screen-blend gate
      // (saturate(exp2(-3*luma) * .w)) is deliberately skipped: an 8-bit approximation that cancels the glow of the
      // brightest sources, the one thing this bloom exists to fix.
      float3 lumaBloom = t5.SampleLevel(s1_s, v6.xy, 0).rgb;
      hdr_color += lumaBloom * (BloomTintAndScreenBlendThreshold.xyz * (4.0 * LumaSettings.GameSettings.BloomIntensity));
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
   // Light shafts / god rays (TPS only), verbatim from tps_tonemap_0xF8997849: an inverse-luminance gate (adds only
   // into darker pixels), additive x4 colour, and a per-pixel attenuation in .a where shafts occlude.
   {
      float lsGate = saturate(exp2(dot(hdr_color, float3(0.300000012, 0.589999974, 0.109999999)) * -3.0));
      float4 ls = t_lightshaft.Sample(s0_s, v5.zw);
      hdr_color = hdr_color * ls.w + (ls.xyz * 4.0) * lsGate;
   }
#endif

   // User Exposure (scene-referred, pre-grade; 1 = vanilla). Applies to both SDR and HDR — the grade below tracks it.
   hdr_color *= LumaSettings.GameSettings.Exposure;

   // The grade runs on the NATIVE scene like vanilla; HDR recovery is one scalar AFTER it (curve_scale), as in MELE.
   r0.xyz = hdr_color;

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

   // Compression the vanilla curve is about to apply to this pixel's brightest channel, relative to an anchor it
   // leaves alone. Taken post-vignette because that IS the curve's input, and UNCLIPPED, so a channel pinned by the
   // curve's own saturate comes back proportional to the real light. Deliberately ONE scalar: it keeps every channel
   // ratio, so the game's own highlight whitening survives (MELE's AGENTS.md forbids blending it per channel). Anchor
   // = 18% grey as in MELE, but clamped under the branch join, which slides below 0.18 once W passes 1.382 - a fixed
   // 0.18 would breathe the whole frame by up to 2.2% with adaptation.
   const float3 curve_input = r0.xyz; // pre-curve, post-vignette: the physical colour, unskewed
   float curve_scale = 1.0;
   if (LumaSettings.DisplayMode == 1)
   {
      // Under the join the curve is a pure gain, so the anchor's compression is exactly W unless a non-zero K (never
      // observed, read live anyway) mixes the toe in. Z tracks 1/W, so anchor*W <= 0.25 and saturate never reaches it.
      const float anchor = max(1e-4, min(0.18, ImageAdjustments2.z * 0.99));
      const float anchor_compression = (ImageAdjustments3.x == 0.0) ? ImageAdjustments2.w : (VanillaCurveLinear(anchor) / anchor);
      float mch = max3(curve_input);
      curve_scale = (mch > 1e-6 && anchor_compression > 1e-6) ? ((VanillaCurveLinear(mch) / mch) / anchor_compression) : 1.0;
   }

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
   // (Vanilla trilinear: the LUT is near-linear between grid points, so tetrahedral interpolation buys no visible gain.)
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

   // ====================== Luma HDR output (vanilla curve inverse) ======================
   // o.rgb is the graded look in gamma space, produced from the NATIVE scene. HDR divides it by curve_scale, the
   // exact inverse of what the vanilla curve compressed; the grade itself is untouched. SDR never computes it.
   float3 graded_sdr_gamma = o.rgb;
   float3 sdr_lin = gamma_to_linear(graded_sdr_gamma, GCT_MIRROR);

   const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
   const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

   float3 postProcessedColor;

   if (LumaSettings.DisplayMode == 1) // HDR
   {
      // min(): only ever expand. Just above the branch join the game's own ~1.4% step pushes curve_scale over 1 - a
      // vanilla artefact to keep. Below the join curve_scale is exactly 1 and HDR is bit-for-bit vanilla.
      float3 recovered = sdr_lin / min(1.0, max(curve_scale, 1e-6));

      // Display rolloff to the user's peak/paper-white nits. DICE by-luminance keeps hue; the *_CORRECT_CHANNELS_BEYOND_
      // PEAK_WHITE type also gamut-maps a single channel riding past peak. Feed linear BT.709 directly: DICE converts to
      // BT.2020 itself, and a manual 709<->2020 round-trip no longer cancels once the per-channel gamut map is in.
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
   else // SDR (still presented through the scRGB swapchain) — sdr_lin is the vanilla grade (curve_scale stayed 1)
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

   return float4(postProcessedColor, 0.0); // vanilla wrote o0.w = 0
}
