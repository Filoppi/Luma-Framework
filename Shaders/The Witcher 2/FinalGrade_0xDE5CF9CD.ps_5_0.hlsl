// clang-format off
// ORDER MATTERS — see Luma_TW2_Tonemap.hlsl (game-local Common.hlsl defines LumaGameSettings first).
#include "Includes/Common.hlsl" // game-local: LumaGameSettings — keep FIRST
#include "../Includes/Color.hlsl"
#include "../Includes/DICE.hlsl"
#include "../Includes/Reinhard.hlsl"    // Reinhard::ReinhardPiecewise, the soft hue reference
#include "Includes/MacLeodBoynton.hlsl" // MacLeodBoynton::HueOnlyBT2020. Byte-identical copy of the BL GOTY production model: do not edit here, sync it from "Borderlands GOTY Enhanced/Includes"
#include "Includes/GameBindings.hlsl"   // b3/b4, the dgVoodoo masks, ApplyDgvMask, DgVoodooRcp, DgVoodooLog2
// clang-format on

// The Witcher 2 FINAL GRADE pass (dgVoodoo -> ps_5_0, hash 0xDE5CF9CD), the engine's
// CEnvFinalColorBalanceParameters: FXAA + shadow offset, midtone power and highlight gain + luma-keyed split
// toning + vignette, on the full-res fp16 canvas right before the UI draws.
// Vanilla body transcribed VERBATIM (register-level, constants cb4[N] = DX9 c(N-8)). This is also where the
// Luma HDR output block lives (highlight hue + DICE + UI paper-white pre-scale): the highlight split tone lerps
// toward a SATURATED luma target and soft-clips everything above 1.0, so the HDR range dies here unless it is
// rebuilt after it. The tonemap replacements stay vanilla-only.
//
// Four permutations, the engine's 2x2 matrix of FXAA in/out x vignette in/out: this file (FXAA + vignette),
// 0x058E2498 with the vignette stage dropped (no t2/s2 mask sample, no cb4[66..67]), 0xCF3B72A9 with the game's
// Anti-aliasing off (no FXAA block, scene alpha instead of 0) and 0xBABBFFAD with both dropped. The wrappers
// include this file and select with LUMA_TW2_NO_FXAA_PERM / LUMA_TW2_NO_VIGNETTE_PERM, so the grade tail lives
// in one place. (The four dark-mode permutations are unreachable in the Enhanced Edition, see NOTES.md.)
#ifndef LUMA_TW2_NO_FXAA_PERM
#define LUMA_TW2_NO_FXAA_PERM 0
#endif
#ifndef LUMA_TW2_NO_VIGNETTE_PERM
#define LUMA_TW2_NO_VIGNETTE_PERM 0
#endif

Texture2D<float4> t0 : register(t0); // scene canvas (full-res fp16, LINEAR light: the vMidtone power below encodes it; carries >1 Luma HDR range)
#if !LUMA_TW2_NO_VIGNETTE_PERM
Texture2D<float4> t2 : register(t2); // vignette mask
#endif

SamplerState s0_s : register(s0);
#if !LUMA_TW2_NO_VIGNETTE_PERM
SamplerState s2_s : register(s2);
#endif

// FXAA luma approximation the pass uses everywhere: R + 1.963211 * G
float FxaaLuma(float4 c)
{
   return c.y * 1.963211 + c.x;
}

float4 SampleScene(float2 uv)
{
   return ApplyDgvMask(t0.SampleLevel(s0_s, uv, 0.0), DgvMaskT0, DgvFillT0);
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

   // ---- vanilla grade tail (verbatim) = CEnvFinalColorBalanceParameters: shadow offset, midtone power (log2/exp2),
   // highlight gain, split toning, vignette ----
   float lumaAA = dot(aaColor, float3(0.299, 0.587, 0.114));
   float3 color = saturate(lumaAA * cb4[62].w) * -cb4[62].rgb + aaColor; // c54 vShadow: offset, ramped in from black by .w

#if TONEMAP_TYPE == 1
   // HDR: grade the colour at its clip point and give the brightness back after linearization (the HDR block below).
   // Vanilla fed this grade at most 1 (the glow blend clips first), and extrapolating its per-channel power and gain
   // past that turned a neutral 10 into Y ~24 with a blue cast in a graded area, and let the Gamma slider (it scales
   // vMidtone) move highlight brightness. Dividing by the max channel keeps every stage on its authored 0-1 domain; at
   // or below 1 the scale is exactly 1.0, so the result is bit-identical there.
   const float gradeScale = (LumaSettings.DisplayMode == 1) ? max(max3(color), 1.0) : 1.0;
   color /= gradeScale;
#endif

   float3 clamped = max(color, 0.0);
   color.r = DgVoodooLog2(clamped.r);
   color.g = DgVoodooLog2(clamped.g);
   color.b = DgVoodooLog2(clamped.b);
   color *= cb4[61].rgb; // c53 vMidtone: per-channel power exponent, the display encode (measured 1/2.2 at neutral)
   color = exp2(color);
   color *= cb4[60].rgb; // c52 vHighlight: gain

   float lum = dot(color, float3(0.299, 0.587, 0.114));

   // Shadow split tone (c60 vSplitToneShadows, c62 vSplitToneBalance.y, c63 vSplitToneRange.x): lerp toward
   // lum * cb4[68].rgb / cb4[70].y, weight sat((1.3 - lum) * cb4[71].x * 4) * cb4[68].w
   float3 shadowToneTarget = lum * cb4[68].rgb;
   float shadowToneWeight = saturate((1.3 - lum) * cb4[71].x * 4.0) * cb4[68].w;
   float3 shadowToneBranch = shadowToneWeight * (shadowToneTarget * DgVoodooRcp(cb4[70].y) - color) + color;

   // Highlight split tone (c61 vSplitToneHighlights, c62 vSplitToneBalance.z, c63 vSplitToneRange.y): lerp toward
   // SATURATED lum * cb4[69].rgb / cb4[70].z — the vanilla >1 soft clip lives here
   float3 highlightToneTarget = saturate(lum) * cb4[69].rgb;
   float highlightToneWeight = saturate((lum + 0.3) * cb4[71].y * 4.0) * cb4[69].w;
   float3 highlightToneBranch = highlightToneWeight * (highlightToneTarget * DgVoodooRcp(cb4[70].z) - color) + color;

   // Vanilla: final = lerp(shadowToneBranch, highlightToneBranch, sat(lum * cb4[70].x * 5)), c62 vSplitToneBalance.x
   float mixWeight = saturate(lum * cb4[70].x * 5.0);
   float3 graded = mixWeight * (highlightToneBranch - shadowToneBranch) + shadowToneBranch;

   // User Color Grading Intensity fades the split toning back toward the un-toned grade: "color" is already the
   // exact reference (offset, power and gain applied, no split tone), so nothing is recomputed. In the vanilla
   // tail on purpose, so it applies in SDR too. Side effect: the highlight split tone carries vanilla's
   // saturate(lum) soft clip, so below 1.0 highlights above white reach a little further in HDR.
   [branch] if (LumaSettings.GameSettings.ColorGradingIntensity != 1.0)
       graded = lerp(color, graded, LumaSettings.GameSettings.ColorGradingIntensity);

#if LUMA_TW2_NO_VIGNETTE_PERM
   // This permutation has no vignette stage at all (see FinalGradeNoVignette_0xBABBFFAD.ps_5_0.hlsl): the
   // engine compiles the mask sample and cb4[66..67] out, so the grade result IS the vanilla output and the
   // Vignette Intensity slider has nothing to scale here.
   float3 vanillaColor = graded;
#else
   // Vignette (c58 vVignetteWeights, c59 vVignetteColor)
   float4 vignette = ApplyDgvMask(t2.Sample(s2_s, v7.xy), DgvMaskT2, DgvFillT2);
   float vigWeight = saturate(dot(cb4[66], vignette));
   float3 vanillaColor = vigWeight * (cb4[67].rgb - graded) + graded;
   // User Vignette Intensity: lerp between the pre-vignette grade and the vignetted result (1 = vanilla,
   // bit-exact; 0 = no vignette). Sits in the vanilla tail on purpose, so it applies in SDR as well.
   vanillaColor = lerp(graded, vanillaColor, LumaSettings.GameSettings.VignetteIntensity);
#endif

#if TONEMAP_TYPE == 1
   // ---- Luma HDR output (BL2-shape tail; this pass runs once per frame, full-res, scene only) ----
   {
      // VANILLA_ENCODING_TYPE 1: gamma 2.2 buffers. gradeScale gives back the brightness the grade was normalized by.
      float3 lin = gamma_to_linear(vanillaColor, GCT_MIRROR) * gradeScale;

      float3 postProcessedColor;
      if (LumaSettings.DisplayMode == 1) // HDR
      {
         const float paperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
         const float peakWhite = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;

         // Highlight hue: the colour stage of the other hard-clip ports (Borderlands GOTY, Medal of Honor Airborne,
         // Mass Effect 2007 and 2010) and of RenoDX's BL1 template. Vanilla clipped per channel twice, in the glow blend
         // before this grade and in the 8-bit present blit after it, which bends a bright saturated source toward
         // yellow-white; Luma removes both clips. This stage bends the hue the same way, partway and without the
         // whitening: a per-channel ReinhardPiecewise(5, 1.5) of the colour itself in BT.2020 supplies a hue DIRECTION,
         // and MacLeod-Boynton rebuilds it on the colour's own purity and T = L + M, before the display map so DICE rolls
         // off the bent colour. Full hue strength and no purity transfer (the RenoDX ports' Hue Shift 100% and Blowout 0),
         // fixed rather than exposed. Measured in a graded area (2026-09-13): fire (6, 2, 0.4) goes from 8 to 23 degrees
         // at unchanged purity. The exact clip colour as reference instead (hue 60, half the purity) read as greenish
         // white at HDR brightness, and a fully clipped reference is achromatic, with no MacLeod-Boynton direction at all
         // (it turned such fire blue).
         // The gate sits at the reference's shoulder: below it ReinhardPiecewise returns its input exactly, and the
         // BT.2020 channels of a BT.709 colour never exceed its max, so the reference equals the colour, the stage is a
         // no-op up to float rounding, and those pixels skip the MacLeod-Boynton solve. One constant for both keeps them
         // from drifting apart.
         const float hueReferenceShoulder = 1.5;
         [branch] if (max3(lin) > hueReferenceShoulder)
         {
            const float3 target2020 = BT709_To_BT2020(lin);
            lin = BT2020_To_BT709(MacLeodBoynton::HueOnlyBT2020(target2020, Reinhard::ReinhardPiecewise(target2020, 5.0, hueReferenceShoulder)));
         }

         // User contrast BEFORE the display map so DICE contains whatever it pushes up: after the rolloff the
         // slider would escape the peak it just established, and nothing downstream re-contains it.
         // Multiplicative around mid-gray, the repo's form (RenoDX_Contrast); 0.18 is mid-gray here too,
         // display-referred with 1.0 = paper white (code 0.5). Gated so the shipped 1.0 stays bit-exact. The pow
         // is spelled out with a floored log2 so Contrast 0 on a black pixel is 0 * log2(1e-30) = 0 rather than
         // pow(0, 0) = NaN. Black stays black at every setting (0^C = 0), so the upstream fade-to-black no longer
         // lands on 0.18 * (1 - Contrast) as the old additive pivot did.
         [branch] if (LumaSettings.GameSettings.Contrast != 1.0)
         {
            const float midGray = 0.18;
            lin = exp2(LumaSettings.GameSettings.Contrast * log2(max(lin / midGray, 1e-30))) * midGray;
         }

         DICESettings settings = DefaultDICESettings(DICE_TYPE_BY_LUMINANCE_PQ_CORRECT_CHANNELS_BEYOND_PEAK_WHITE);
         // Highlight dechroma handed to DICE rather than run as our own pass afterwards. Core's is better placed: it ramps
         // on the MAX CHANNEL (by luminance a bright blue never triggers), exists only between ShoulderStart * PeakWhite
         // and peak (1/3 of peak for this type, so mid-tones cannot be touched), and runs INSIDE the containment in the
         // processing primaries. 0 = off for the OUTPUT but not the cost: DICE's guard carries no [branch], so fxc
         // flattens it for every pixel above the shoulder.
         settings.HighlightsDesaturation = LumaSettings.GameSettings.HighlightDechroma;
         float3 hdr = DICETonemap(lin * paperWhite, peakWhite, settings) / paperWhite;

         // User saturation LAST, after the display map: the repo's convention.
         hdr = Saturation(hdr, LumaSettings.GameSettings.Saturation);

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

      postProcessedColor = IsNaN_Strict(postProcessedColor) ? 0.0 : postProcessedColor; // explicit exponent/mantissa bit test
      postProcessedColor = max(0.0, postProcessedColor);
      postProcessedColor = linear_to_gamma(postProcessedColor, GCT_MIRROR);

      // Anti-banding dither, one step of the output quantizer: the 8-bit code in SDR, 10-bit BT.2020 PQ in HDR.
      if (LumaSettings.GameSettings.Dithering > 0.5)
      {
         if (LumaSettings.DisplayMode == 0)
            ApplyDithering(postProcessedColor, v5.xy, true, 1.0, 8u, LumaSettings.FrameIndex, true);
         else
         {
            const float pqScale = max(LumaSettings.UIPaperWhiteNits, 1.0) / HDR10_MaxWhiteNits;
            float3 pq = Linear_to_PQ(BT709_To_BT2020(gamma_to_linear(postProcessedColor, GCT_MIRROR) * pqScale), GCT_MIRROR);
            ApplyDithering(pq, v5.xy, true, 1.0, 10u, LumaSettings.FrameIndex, true);
            postProcessedColor = linear_to_gamma(BT2020_To_BT709(PQ_to_Linear(pq, GCT_MIRROR)) / pqScale, GCT_MIRROR);
         }
      }

      vanillaColor = postProcessedColor;
   }
#endif // TONEMAP_TYPE == 1

#if LUMA_TW2_NO_FXAA_PERM
   o0 = float4(vanillaColor, sampleM.a); // this permutation passes the scene alpha through
#else
   o0 = float4(vanillaColor, 0.0); // vanilla writes o0.w = 0
#endif
}
