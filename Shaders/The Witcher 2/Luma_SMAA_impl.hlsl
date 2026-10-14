// SMAA implementation for The Witcher 2. Reference: https://github.com/iryoku/smaa
// ULTRA preset + color edge detection, run POST-final-grade on the graded gamma canvas: main.cpp uses the
// post-draw callback on the final grade (all four permutations, both dgVoodoo builds) to run the grade, then SMAA on its output, before the UI draws
// on the same canvas. The grade skips its built-in FXAA while SMAA is active (LumaData.CustomData2), so this
// is a strict replacement rather than double AA.
// The canvas is GAMMA (POST_PROCESS_SPACE_TYPE=0) and carries display-mapped HDR values, so >1 is possible;
// edge detection works in gamma, neighborhood blending in linear light (Luma_TW2_SMAALinearize) and re-encodes.
// No HDR/tonemap tail here, core Display Composition runs downstream.
// Predication uses the game's full-res r32_float depth, turned into an edge-ness signal by the Depth Extract
// CS; null-predication + scale 1.0 is the no-depth fallback.

#include "../Includes/Common.hlsl"

// (1/W, 1/H, W, H) at output resolution — filled by the mod (see main.cpp RunPostFinalGradeSMAA).
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = predication threshold scale: 2.0 when predication is active (normalized depth bound) -> frame-wide
   // threshold 0.10 on flats; 1.0 with a null predication texture (fallback) -> plain ULTRA threshold 0.05. yzw unused.
   float4 SmaaPredication;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAA_PREDICATION_SCALE SmaaPredication.x
// Calibrated against the signal the Depth Extract CS produces from TW2's depth (linear view-space
// metres, p50 ~7.3) — see Luma_TW2_DepthExtract.hlsl:
//  - flat threshold  = SCALE * SMAA_THRESHOLD            = 2.0 * 0.05      = 0.10 (rejects texture color-noise —
//    TW2 stone/foliage is one big color-noise field — so SMAA doesn't over-AA flat detail)
//  - silhouette thr  = SCALE * SMAA_THRESHOLD * (1-STR)  = 2.0 * 0.05 *0.5 = 0.05 (= plain ULTRA base; predication
//    only relaxes geometric edges back to normal sensitivity, never below). Keep this identity when retuning.
//  - PREDICATION_THRESHOLD is 0.5 and needs no per-scene tuning: the Depth Extract CS hands SMAA an EDGE-NESS
//    signal in [0,1], not a depth, so 0.5 is just its midpoint and is independent of scene scale, camera
//    height, FOV and resolution. Tune the CS tolerance instead. Tripping the gate is graceful either way:
//    it only restores the base 0.05 threshold, never lowers it.
#define SMAA_PREDICATION_STRENGTH  0.5
#define SMAA_PREDICATION_THRESHOLD 0.5
#define SMAAGather(tex, coord)     tex.Gather(LinearSampler, coord, 0)

// SMAAEdgeDetection
// tex0 = colorTexGamma (gamma-encoded graded canvas)
// tex1 = predicationTex (edge-ness in [0,1]; null fallback -> reads 0, scale 1.0 = plain ULTRA threshold)

// SMAANeighborhoodBlending
// tex0 = colorTex (linear copy), tex1 = blendTex. Re-encode to the canvas' gamma.
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color.rgb = linear_to_gamma(color.rgb, GCT_MIRROR);

#include "../Includes/SMAA_Passes.hlsl"
