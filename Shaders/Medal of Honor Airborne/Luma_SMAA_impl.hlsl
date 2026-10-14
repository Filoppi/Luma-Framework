// SMAA implementation for Medal of Honor: Airborne (port of the shipped BL2/TPS and Witcher 2 setups — same
// dgVoodoo DX9->11 stack).
// Reference: https://github.com/iryoku/smaa
//
// ULTRA preset + color edge detection. The game ships with NO anti-aliasing of any kind: a structural sweep of the
// dumped pixel shaders found zero luma-coefficient edge detection, every render target reports sampleCount 1, and
// there is no in-game AA option (the community's only answer is an injector). So this is not a replacement for a
// native pass — nothing has to be suppressed, unlike TW2 where the grade's built-in FXAA must be skipped.
//
// Runs POST-final-grade on the graded gamma canvas: main.cpp drives it from the post-draw callback on whichever
// final pass ran this frame (UberPostProcessBlend 0xB9548800 with DoF on, FGammaCorrectionPixelShader 0x52B868E0
// with DoF off), i.e. after the grade and BEFORE the HUD draws onto the same canvas. The canvas is GAMMA
// (POST_PROCESS_SPACE_TYPE=0) and carries display-mapped HDR values (>1 possible) — edge detection works in gamma,
// neighborhood blending in linear light (Luma_MOHA_SMAALinearize) and re-encodes. The PS appends NO HDR/tonemap
// tail; the core Display Composition runs downstream.

#include "Includes/Common.hlsl"

// (1/W, 1/H, W, H) at output resolution — filled by the mod (see main.cpp RunPostFinalGradeSMAA).
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = predication threshold scale: 2.0 when predication is active, 1.0 with a null predication texture
   // (fallback) -> plain ULTRA threshold 0.05. yzw unused.
   float4 SmaaPredication;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAA_PREDICATION_SCALE SmaaPredication.x
// Predication is live: the depth-extract CS supplies the mask and the metrics CB advertises SCALE 2.0, so the two
// values below are load-bearing. SCALE falls back to 1.0 only when the mask is missing. The identities:
//  - flat threshold  = SCALE * SMAA_THRESHOLD           = 2.0 * 0.05       = 0.10 (rejects texture color-noise)
//  - silhouette thr  = SCALE * SMAA_THRESHOLD * (1-STR) = 2.0 * 0.05 * 0.5 = 0.05 (= plain ULTRA base; predication
//    only relaxes geometric edges back to normal sensitivity, never below).
//  - PREDICATION_THRESHOLD 0.5 needs no per-scene tuning because the CS hands SMAA an EDGE-NESS signal in [0,1]
//    (deviation from the local tangent plane), not a depth. The CS tolerance is the lever, not this value.
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
