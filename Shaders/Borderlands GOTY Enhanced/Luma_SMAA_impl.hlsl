// SMAA implementation for Borderlands GOTY Enhanced (replaces the game's compute FXAA resolve pass).
// Reference: https://github.com/iryoku/smaa
// ULTRA preset + color edge detection + depth predication (the game exposes a full-res depth buffer).
// Input color is the game's post-process buffer, stored in GAMMA space (POST_PROCESS_SPACE_TYPE 0, 1.0 = paper
// white) and fp16, so highlights run past 1. Edge detection reads it as stored; blending reads its linear decode
// (Luma_BL_SMAALinearize) and re-encodes.

#include "../Includes/Common.hlsl"

// (1/W, 1/H, W, H) at output resolution — filled by the mod (see main.cpp FXAA->SMAA branch).
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = predication threshold scale. 2.0 when valid same-size scene depth is bound (depth predication active:
   // raises the off-edge threshold to suppress noise, lowers it on real geometry edges). 1.0 when depth is
   // missing/mismatched -> with a null/zero predication texture this collapses to the plain ULTRA threshold
   // (0.05) instead of a frame-wide doubled 0.10 (which under-detects edges = worse AA). yzw unused.
   float4 SmaaPredication;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAA_PREDICATION_SCALE SmaaPredication.x
// Predication budget:
//  - flat threshold  = SCALE * SMAA_THRESHOLD           = 2.0 * 0.05       = 0.10 (rejects texture colour noise)
//  - silhouette thr  = SCALE * SMAA_THRESHOLD * (1-STR) = 2.0 * 0.05 * 0.5 = 0.05 (= plain ULTRA base; predication
//    only relaxes geometric edges back to base sensitivity, never below).
// THRESHOLD is 0.5 because Luma_BL_DepthExtract.hlsl feeds a unitless edge-ness in [0,1], not a depth: the
// half-way point simply means "the extract called this a silhouette". Calibrate its tolerance, not this number.
#define SMAA_PREDICATION_STRENGTH  0.5
#define SMAA_PREDICATION_THRESHOLD 0.5
#define SMAAGather(tex, coord)     tex.Gather(LinearSampler, coord, 0)

// SMAAEdgeDetection
// tex0 = colorTexGamma (stored scene snapshot)
// tex1 = predicationTex (plane-deviation edge mask)

// SMAANeighborhoodBlending
// tex0 = colorTex (linear copy), tex1 = blendTex. Re-encode to the canvas' gamma.
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color.rgb = linear_to_gamma(color.rgb, GCT_MIRROR);

#include "../Includes/SMAA_Passes.hlsl"
