// clang-format off
// ORDER MATTERS — see Luma_TW2_Tonemap.hlsl (game-local Common.hlsl defines LumaGameSettings first).
#include "Includes/Common.hlsl" // game-local: LumaGameSettings — keep FIRST
#include "../Includes/Color.hlsl"
#include "../Includes/ColorGradingLUT.hlsl" // RestoreHueAndChrominance (vanilla clip hue/chroma emulation)
#include "../Includes/DICE.hlsl"
// clang-format on

// The Witcher 2 FINAL GRADE pass (dgVoodoo -> ps_5_0, hash 0xDE5CF9CD): FXAA + the in-game Gamma slider +
// highlight/shadow tint lerps + vignette, on the full-res fp16 canvas right before the UI draws.
// Vanilla body transcribed VERBATIM (register-level, constants cb4[N] = DX9 c(N-8)). This is also where the
// Luma HDR output block lives (expansion + DICE + UI paper-white pre-scale): the vanilla tint lerps weight by
// SATURATED luma and soft-clip everything above 1.0, so the HDR range dies here unless it is rebuilt after
// them. The tonemap replacements stay vanilla-only.
//
// Three permutations, and the only ones that exist: this file (FXAA + vignette), 0xCF3B72A9 with the game's
// Anti-aliasing off (no FXAA block, scene alpha instead of 0) and 0xBABBFFAD with the vignette stage dropped
// as well (no t2/s2 mask sample, no cb4[66..67]). The wrappers include this file and select with
// LUMA_TW2_NO_FXAA_PERM / LUMA_TW2_NO_VIGNETTE_PERM, so the grade tail lives in one place.
#ifndef LUMA_TW2_NO_FXAA_PERM
#define LUMA_TW2_NO_FXAA_PERM 0
#endif
#ifndef LUMA_TW2_NO_VIGNETTE_PERM
#define LUMA_TW2_NO_VIGNETTE_PERM 0
#endif

Texture2D<float4> t0 : register(t0); // scene canvas (full-res fp16, gamma-space, carries >1 Luma HDR range)
#if !LUMA_TW2_NO_VIGNETTE_PERM
Texture2D<float4> t2 : register(t2); // vignette mask
#endif

SamplerState s0_s : register(s0);
#if !LUMA_TW2_NO_VIGNETTE_PERM
SamplerState s2_s : register(s2);
#endif

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

// dgVoodoo texture-format fixup + guarded ops — transcribed verbatim. Deliberately duplicated per replacement
// rather than shared: each hash-replaced file stays self-contained for side-by-side comparison with its dump.
// Names match Luma_TW2_Tonemap.hlsl's copies exactly, so identical bodies never read as different helpers.
float4 DgVoodooTexFixup(float4 color, float4 mask_and, float4 mask_or)
{
   return asfloat((asuint(color) & asuint(mask_and)) | asuint(mask_or));
}
// 1e37 is the exact sentinel every dgVoodoo dump uses (l(9999999933815812510711506376257961984.0)); it must
// not be rounded up to 1e38, or "hiTarget * DgVoodooRcp(0)" below overflows to inf ten times sooner and the
// zero-weight lerp around it turns into 0 * inf = NaN (which the guard at the end paints black).
#define DGVOODOO_BIG 1e37
float DgVoodooRcp(float x)
{
   return (abs(x) > 0.0) ? (1.0 / x) : DGVOODOO_BIG;
}
float DgVoodooLog2(float x)
{
   float l = log2(abs(x));
   // dgVoodoo: log(0) = -inf -> -BIG (so exp2 later yields 0). It tests the -inf BIT PATTERN, not isinf(), so
   // a +inf input keeps propagating as vanilla does instead of being flipped to ~0 (a white pixel gone black).
   return (asuint(l) == 0xff800000u) ? -DGVOODOO_BIG : l;
}

// FXAA luma approximation the pass uses everywhere: R + 1.963211 * G
float FxaaLuma(float4 c)
{
   return c.y * 1.963211 + c.x;
}

float4 SampleScene(float2 uv)
{
   return DgVoodooTexFixup(t0.SampleLevel(s0_s, uv, 0.0), cb3[44], cb3[45]);
}

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
   float4 sampleM = SampleScene(v5.xy);
   float3 aaColor = sampleM.rgb; // the no-AA permutation stops here; the FXAA one may replace it below

#if !LUMA_TW2_NO_FXAA_PERM
   // CustomData2 == 1: Luma SMAA runs right after this pass, so skip the vanilla FXAA instead of double-AAing.
   // The neighbour taps sit INSIDE the branch on purpose: with SMAA on they would be 4 dead full-res fetches.
   [branch] if (LumaData.CustomData2 != 1)
   {
      const float2 rcpFrame = cb4[72].xy; // c64 vInvSurfaceSize

      // ---- vanilla FXAA (verbatim transcription) ----
      float4 sampleN = SampleScene(v5.xy + rcpFrame * float2(0.0, -1.0));
      float4 sampleW = SampleScene(v5.xy + rcpFrame * float2(-1.0, 0.0));
      float4 sampleE = SampleScene(v5.xy + rcpFrame * float2(1.0, 0.0));
      float4 sampleS = SampleScene(float2(v5.x, v5.y + rcpFrame.y));

      float lumaN = FxaaLuma(sampleN);
      float lumaW = FxaaLuma(sampleW);
      float lumaM = FxaaLuma(sampleM);
      float lumaE = FxaaLuma(sampleE);
      float lumaS = FxaaLuma(sampleS);

      float rangeMin = min(min(min(lumaW, lumaN), min(lumaE, lumaS)), lumaM);
      float rangeMax = max(max(max(lumaN, lumaW), max(lumaS, lumaE)), lumaM);
      float range = rangeMax - rangeMin;

      [branch] if (range >= max(0.0625, rangeMax * 0.125))
      {
         float3 sum5 = sampleN.rgb + sampleW.rgb + sampleM.rgb + sampleE.rgb + sampleS.rgb;

         float lumaSum4 = lumaN + lumaW + lumaE + lumaS;
         float blend = abs(lumaSum4 * 0.25 - lumaM) * DgVoodooRcp(range) - 0.5;
         float subpix = min(max(blend, 0.0) * 2.0, 2.0 / 3.0);

         float4 sampleNW = SampleScene(v5.xy - rcpFrame);
         float4 sampleNE = SampleScene(v5.xy + rcpFrame * float2(1.0, -1.0));
         float4 sampleSW = SampleScene(v5.xy + rcpFrame * float2(-1.0, 1.0));
         float4 sampleSE = SampleScene(v5.xy + rcpFrame);

         float3 sum9 = sum5 + sampleNW.rgb + sampleNE.rgb + sampleSW.rgb + sampleSE.rgb;

         float lumaNW = FxaaLuma(sampleNW);
         float lumaNE = FxaaLuma(sampleNE);
         float lumaSW = FxaaLuma(sampleSW);
         float lumaSE = FxaaLuma(sampleSE);

         // Edge orientation (vanilla operand order kept)
         float edgeHorz = abs(lumaN * -0.5 + lumaNW * 0.25 + lumaNE * 0.25) + abs(lumaW * 0.5 - lumaM + lumaE * 0.5) // |(W+E)*0.5 - M| written as vanilla mads
                          + abs(lumaS * -0.5 + lumaSW * 0.25 + lumaSE * 0.25);
         float edgeVert = abs(lumaNW * 0.25 + lumaW * -0.5 + lumaSW * 0.25) + abs(lumaN * 0.5 - lumaM + lumaS * 0.5) + abs(lumaNE * 0.25 + lumaE * -0.5 + lumaSE * 0.25);
         bool horzSpan = (edgeVert - edgeHorz) >= 0.0;

         float stepLength = horzSpan ? -rcpFrame.y : -rcpFrame.x;
         float luma1 = horzSpan ? lumaN : lumaW;
         float luma2 = horzSpan ? lumaS : lumaE;
         float grad1 = luma1 - lumaM;
         float grad2 = luma2 - lumaM;
         float lumaEnd1 = (lumaM + luma1) * 0.5;
         float lumaEnd2 = (lumaM + luma2) * 0.5;
         bool pair1 = (abs(grad1) - abs(grad2)) >= 0.0;
         float lumaLocalAvg = pair1 ? lumaEnd1 : lumaEnd2;
         float gradScaled = max(abs(grad1), abs(grad2));
         stepLength = pair1 ? stepLength : -stepLength;

         float halfStep = stepLength * 0.5;
         float2 posCenter;
         posCenter.x = v5.x + (horzSpan ? 0.0 : halfStep);
         posCenter.y = v5.y + (horzSpan ? halfStep : 0.0);

         float2 edgeStep = horzSpan ? float2(rcpFrame.x, 0.0) : float2(0.0, rcpFrame.y);
         float2 posN = posCenter - edgeStep;
         float2 posP = posCenter + edgeStep;
         float lumaEndN = lumaLocalAvg;
         float lumaEndP = lumaLocalAvg;
         float doneN = 0.0;
         float doneP = 0.0;

         [loop] for (int i = 8; i > 0; i--)
         {
            if (doneN == 0.0)
               lumaEndN = FxaaLuma(SampleScene(posN));
            if (doneP == 0.0)
               lumaEndP = FxaaLuma(SampleScene(posP));
            float hitN = (abs(lumaEndN - lumaLocalAvg) - gradScaled * 0.25 >= 0.0) ? 1.0 : 0.0;
            hitN += doneN;
            doneN = (-hitN >= 0.0) ? 0.0 : 1.0;
            float hitP = (abs(lumaEndP - lumaLocalAvg) - gradScaled * 0.25 >= 0.0) ? 1.0 : 0.0;
            hitP += doneP;
            doneP = (-hitP >= 0.0) ? 0.0 : 1.0;
            if (doneN * doneP != 0.0)
               break;
            if (-hitN >= 0.0)
               posN -= edgeStep;
            if (-hitP >= 0.0)
               posP += edgeStep;
         }

         float dstN = horzSpan ? (v5.x - posN.x) : (v5.y - posN.y);
         float dstP = horzSpan ? (posP.x - v5.x) : (posP.y - v5.y);
         float lumaEndNear = (dstN - dstP >= 0.0) ? lumaEndP : lumaEndN;
         float mBelow = (lumaM - lumaLocalAvg >= 0.0) ? 0.0 : 1.0;
         float endBelow = (lumaEndNear - lumaLocalAvg >= 0.0) ? -0.0 : -1.0;
         float goodSpan = mBelow + endBelow;
         float finalStep = (-abs(goodSpan) >= 0.0) ? 0.0 : stepLength;
         float spanLength = dstN + dstP;
         float pixelOffset = min(dstP, dstN) * -DgVoodooRcp(spanLength) + 0.5;
         pixelOffset *= finalStep;

         float2 posFinal;
         posFinal.x = v5.x + (horzSpan ? 0.0 : pixelOffset);
         posFinal.y = v5.y + (horzSpan ? pixelOffset : 0.0);
         float4 sampleEnd = SampleScene(posFinal);

         aaColor = subpix * sum9 * 0.111111 + sampleEnd.rgb;
         aaColor = -subpix * sampleEnd.rgb + aaColor; // = lerp(endColor, avg9, subpix), vanilla op order
      }
   }
#endif // !LUMA_TW2_NO_FXAA_PERM

   // ---- vanilla grade tail (verbatim): desat, gamma slider (log2/exp2 pow), tints, vignette ----
   float lumaAA = dot(aaColor, float3(0.299, 0.587, 0.114));
   float3 color = saturate(lumaAA * cb4[62].w) * -cb4[62].rgb + aaColor;

   float3 clamped = max(color, 0.0);
   color.r = DgVoodooLog2(clamped.r);
   color.g = DgVoodooLog2(clamped.g);
   color.b = DgVoodooLog2(clamped.b);
   color *= cb4[61].rgb; // in-game Gamma slider exponent
   color = exp2(color);
   color *= cb4[60].rgb; // scale

   float lum = dot(color, float3(0.299, 0.587, 0.114));

   // Highlight tint branch: lerp toward lum * cb4[68].rgb / cb4[70].y, weight sat((1.3 - lum) * cb4[71].x * 4) * cb4[68].w
   float3 hiTarget = lum * cb4[68].rgb;
   float hiWeight = saturate((1.3 - lum) * cb4[71].x * 4.0) * cb4[68].w;
   float3 hiBranch = hiWeight * (hiTarget * DgVoodooRcp(cb4[70].y) - color) + color;

   // Shadow tint branch: lerp toward SATURATED lum * cb4[69].rgb / cb4[70].z — the vanilla >1 soft-clip lives here
   float3 loTarget = saturate(lum) * cb4[69].rgb;
   float loWeight = saturate((lum + 0.3) * cb4[71].y * 4.0) * cb4[69].w;
   float3 loBranch = loWeight * (loTarget * DgVoodooRcp(cb4[70].z) - color) + color;

   // Vanilla: final = lerp(hiBranch, loBranch, sat(lum * cb4[70].x * 5))
   float mixWeight = saturate(lum * cb4[70].x * 5.0);
   float3 graded = mixWeight * (loBranch - hiBranch) + hiBranch;

   // User Color Grading Intensity fades the two tint lerps back toward the untinted grade (the game's
   // yellow-sepia cast); "color" is already the exact untinted reference, so nothing is recomputed. In the
   // vanilla tail on purpose, so it applies in SDR too. Side effect: the shadow-tint branch carries vanilla's
   // saturate(lum) soft-clip, so below 1.0 highlights above white reach a little further in HDR.
   [branch] if (LumaSettings.GameSettings.ColorGradingIntensity != 1.0)
       graded = lerp(color, graded, LumaSettings.GameSettings.ColorGradingIntensity);

#if LUMA_TW2_NO_VIGNETTE_PERM
   // This permutation has no vignette stage at all (see FinalGradeNoVignette_0xBABBFFAD.ps_5_0.hlsl): the
   // engine compiles the mask sample and cb4[66..67] out, so the grade result IS the vanilla output and the
   // Vignette Intensity slider has nothing to scale here.
   float3 vanillaColor = graded;
#else
   // Vignette
   float4 vignette = DgVoodooTexFixup(t2.Sample(s2_s, v7.xy), cb3[48], cb3[49]);
   float vigWeight = saturate(dot(cb4[66], vignette));
   float3 vanillaColor = vigWeight * (cb4[67].rgb - graded) + graded;
   // User Vignette Intensity: lerp between the pre-vignette grade and the vignetted result (1 = vanilla,
   // bit-exact; 0 = no vignette). Sits in the vanilla tail on purpose, so it applies in SDR as well.
   vanillaColor = lerp(graded, vanillaColor, LumaSettings.GameSettings.VignetteIntensity);
#endif

#if TONEMAP_TYPE == 1
   // ---- Luma HDR output (BL2-shape tail; this pass runs once per frame, full-res, scene only) ----
   {
      float3 lin = gamma_to_linear(vanillaColor, GCT_MIRROR); // VANILLA_ENCODING_TYPE 1: gamma 2.2 buffers

      float3 postProcessedColor;
      if (LumaSettings.DisplayMode == 1) // HDR
      {
         const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
         const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

         // Vanilla highlight emulation (ALWAYS ON — part of the game's look, not a user knob). The vanilla SDR
         // ceiling was a per-channel clamp at exactly 1.0, and it lived in the ROP, not in any shader: dgVoodoo's
         // present blit (PS 0x2749AFD8) is a bare "sample_l -> o0" with zero math, and it wrote into an 8-bit
         // r8g8b8a8 intermediate, so the output merger did the clamping. Luma upgrades that very intermediate to
         // fp16 (texture_upgrade_formats in main.cpp), which is exactly why the clamp — and with it the vanilla
         // look — disappears and has to be put back here, in the last pass before that blit. On a bright
         // saturated source one channel reaches 1.0 first, which both skews the hue toward white (fire ->
         // yellow-white) AND desaturates it; the shadow-tint branch above adds a partial soft clip of its own.
         //
         // REFERENCE = saturate(lin), the vanilla per-channel clip itself: this pass transcribes the whole
         // vanilla grade, so the real vanilla SDR color is already here and needs no stand-in. A synthetic
         // ReinhardPiecewise reference is only the fallback for passes with no vanilla SDR in hand, and it is a
         // poor one here — a ceiling-5 reference recovers ~4% of the needed hue swing on a moderate highlight
         // (G/R 0.347 where vanilla clips to 0.400), leaving fire red.
         //
         // Clip in BT.709: the artifact happened in the game's 8-bit sRGB buffer, so that is the faithful domain
         // and both gamut matrices disappear with it. Clipping before or after the transfer function is
         // equivalent, since a per-channel clamp at 1.0 commutes with any monotonic curve fixing 1 -> 1.
         // The gate is exact, not conservative: below 1.0 saturate is the identity, so hueRef == lin and the
         // restore provably does nothing, sparing two Oklab round trips on the ~96% of pixels that sit below.
         [branch] if (max(lin.r, max(lin.g, lin.b)) > 1.0)
         {
            float3 hueRef = saturate(lin);
            // Shipped 0.8 hue / 0.4 whitening, both from GameSettings. 0.8 is the catalog value for a clipped
            // SDR reference; 0.4 reproduces part of the clip's own chroma loss without paying for path-to-white
            // twice, since DICE desaturates again at the display peak and Highlights Desaturation adds more on
            // request. A non-zero whitening is only defensible because the reference here IS the vanilla clip.
            // Only the CHROMA argument can whiten: the helper transfers hue, then restores chrominance exactly.
            //
            // HAZARD: hue strength must stay BELOW 1.0. Once every channel clips, hueRef is (1,1,1) whose
            // chrominance is 1.7e-4 rather than 0, so the helper misses its safe-division fallback and its
            // renormalization goes near-singular — the hue runs away while chroma stays put. On (6,5,4) linear:
            // +0.87 deg at 0.80, +2.02 at 0.90, +4.49 at 0.95, +36.9 at 0.99, +143.9 at 1.00, where a white-hot
            // pixel comes out CYAN. 0.80 keeps 61-79% of the vanilla hue swing; 0.90 is the hard ceiling.
            lin = RestoreHueAndChrominance(lin, hueRef, saturate(LumaSettings.GameSettings.HighlightsHueStrength), saturate(LumaSettings.GameSettings.HighlightsHueChroma));
         }

         DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
         float3 hdr = DICETonemap(lin * paperWhite, peakWhite, settings) / paperWhite;

         const float highlightDechroma = LumaSettings.GameSettings.HighlightDechroma;
         if (highlightDechroma > 0.0)
         {
            float dcExp = lerp(1.0, 0.05, highlightDechroma);
            float dcWeight = saturate(pow(saturate(GetLuminance(hdr) / peakWhite), dcExp));
            hdr = Saturation(hdr, 1.0 - dcWeight);
         }
         hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation);

         // User contrast: slope around 18% mid-gray (linear, 1.0 = paper white), after DICE and saturation.
         // Gated so the shipped 1.0 stays bit-exact rather than paying a subtract/multiply/add round trip.
         // KNOWN, ACCEPTED: the pivot means black does not stay black below 1.0 — a fully faded frame lands on
         // 0.18 * (1 - Contrast), so cutscene fade-to-blacks read dark grey. The fade is applied upstream in
         // the tonemap pass, so it cannot be reordered after the pivot.
         [branch] if (LumaSettings.GameSettings.Contrast != 1.0)
         {
            const float midGray = 0.18;
            hdr = (hdr - midGray) * LumaSettings.GameSettings.Contrast + midGray;
         }

         postProcessedColor = hdr;
      }
      else // SDR (still presented through the scRGB swapchain)
      {
         postProcessedColor = lin;
      }

#if UI_DRAW_TYPE >= 2
      // Pre-scale so the gamma-SDR HUD drawn on top lands at UIPaperWhite after composition.
      postProcessedColor *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif

      postProcessedColor = (postProcessedColor == postProcessedColor) ? postProcessedColor : 0.0; // NaN -> 0
      postProcessedColor = max(0.0, postProcessedColor);
      postProcessedColor = linear_to_gamma(postProcessedColor, GCT_MIRROR);

      // Animated triangular dither in the stored gamma space, HDR and SDR alike (MELE precedent): the core
      // composition never dithers, and the 8-bit SDR output bands harder than the HDR one. Deliberately NOT
      // vanilla — vanilla had no dither — hence the runtime toggle.
      if (LumaSettings.GameSettings.Dithering > 0.5)
         ApplyDithering(postProcessedColor, v5.xy, true, 1.0, DITHERING_BIT_DEPTH, LumaSettings.FrameIndex, true);

      vanillaColor = postProcessedColor;
   }
#endif // TONEMAP_TYPE == 1

#if LUMA_TW2_NO_FXAA_PERM
   o0 = float4(vanillaColor, sampleM.a); // this permutation passes the scene alpha through
#else
   o0 = float4(vanillaColor, 0.0); // vanilla writes o0.w = 0
#endif
}
