// SMAA (https://github.com/iryoku/smaa) in place of the game's compute FXAA resolve: ULTRA preset, color edge detection and depth
// predication. The input is a snapshot of the fp16 swapchain, gamma-encoded (POST_PROCESS_SPACE_TYPE 0, 1.0 = paper white), so
// highlights run past 1. Edge detection reads it as stored; blending filters it in linear light and re-encodes.

#include "../Includes/Common.hlsl"

// "SMAA_T2X" 1 is SMAA T2x (main.cpp's "BL SMAA T2x" passes). The scene jitters by a quarter pixel diagonally in two phases. The
// blending weights take the phase's area texture subsamples, the neighborhood blend stays in linear light with the velocity length in
// alpha, and the resolve blends it with the previous frame reprojected by the motion vectors. The output hook below runs at the resolve.
#ifndef SMAA_T2X
#define SMAA_T2X 0
#endif

// Written by main.cpp's FXAA resolve replacement. SmaaRtMetrics: (1/W, 1/H, W, H) at output resolution.
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = predication threshold scale: 2 with a valid same-size scene depth (raises the off-edge threshold against noise, lowered back
   // on geometry edges); 1 when it is missing or mismatched, which with the null predication texture is the plain ULTRA threshold
   // (0.05) rather than a frame-wide doubled 0.10 that under-detects edges. yzw unused.
   float4 SmaaPredication;
#if SMAA_T2X
   float4 SmaaSubsampleIndices; // The jitter phase's "SMAA.hlsl" @SUBSAMPLE_INDICES, 0 when the scene didn't jitter
#endif
}

#define SMAA_RT_METRICS SmaaRtMetrics
#if SMAA_T2X
#define SMAA_SUBSAMPLE_INDICES SmaaSubsampleIndices
#define SMAA_REPROJECTION      1
// The motion vector target ("mv_texture" in main.cpp) holds the UV delta from the current to the previous position. SMAA's velocity is
// the opposite.
#define SMAA_DECODE_VELOCITY(sample) (-(sample).rg)
#endif
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAA_PREDICATION_SCALE SmaaPredication.x
// Predication budget:
//  - flat threshold       = SCALE * SMAA_THRESHOLD                  = 2 * 0.05       = 0.10 (rejects texture colour noise)
//  - silhouette threshold = SCALE * SMAA_THRESHOLD * (1 - STRENGTH) = 2 * 0.05 * 0.5 = 0.05, the plain ULTRA base: predication only
//    relaxes geometric edges back to base sensitivity, never below.
// THRESHOLD is 0.5 because Luma_BL_DepthExtract.hlsl feeds a unitless edge-ness in [0,1], not a depth: half-way means "the extract
// called this a silhouette". Calibrate its tolerance, not this number.
#define SMAA_PREDICATION_STRENGTH  0.5
#define SMAA_PREDICATION_THRESHOLD 0.5
#define SMAAGather(tex, coord)     tex.Gather(LinearSampler, coord, 0)

// The predication signal above is a one-sided edge-ness; morphological edge suppression in place of the local contrast
// adaptation (both in SMAA_Passes.hlsl / SMAA.hlsl). main.cpp also drops the area texture's U-shape smoothing.
#define SMAA_PREDICATION_EDGENESS           1
#define SMAA_MORPHOLOGICAL_EDGE_SUPPRESSION 1

// SMAAEdgeDetection
// tex0 = colorTexGamma (stored scene snapshot)
// tex1 = predicationTex (plane-deviation edge mask)

// SMAANeighborhoodBlending
// tex0 = colorTex (the same snapshot, filtered in linear light), tex1 = blendTex. Re-encode to the canvas' gamma, alpha 0 as the tonemap
// wrote it, never T2x's velocity length.
#define SMAA_NEIGHBORHOOD_GAMMA_IN_LINEAR         1
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color = float4(linear_to_gamma(color.rgb, GCT_MIRROR), 0.0);

#include "Includes/SMAA_Passes.hlsl"
