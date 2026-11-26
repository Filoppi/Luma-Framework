// SMAA implementation (replaces the game's FXAA, see "Replace FXAA with SMAA" in main.cpp)
// See Demo for the reference https://github.com/iryoku/smaa

#include "Includes/Common.hlsl"

// Metrics come from a dedicated cbuffer at b1 (filled by DrawSMAA's cb_smaa_metrics), not from the game's cbuffers.
cbuffer SMAAMetricsCB : register(b1)
{
   float4 SMAAMetricsValue; // (1/width, 1/height, width, height)
};

#define SMAA_RT_METRICS SMAAMetricsValue
#define SMAA_THRESHOLD 0.05
#define SMAA_MAX_SEARCH_STEPS 112
#define SMAA_MAX_SEARCH_STEPS_DIAG 20
#define SMAA_CORNER_ROUNDING 25
#define SMAA_PREDICATION 1
#define SMAA_PREDICATION_THRESHOLD 0.05
#define SMAA_PREDICATION_SCALE 1.0
#define SMAA_PREDICATION_STRENGTH 0.5

// The predication input is hardware depth d = z/w = 1 - 0.25/viewZ (1.0 = sky): convert each gathered component to log2 view depth,
// so that the predication deltas are relative depth changes. A null predication SRV gathers 0, which gives no depth edges.
float4 SMAAGatherToLog2ViewDepth(float4 d)
{
   return min(log2(0.25 / max(1.0 - d, 1e-6)), 20.0);
}
#define SMAAGather(tex, coord) SMAAGatherToLog2ViewDepth(tex.Gather(LinearSampler, coord, 0))

// The blend happens in linear (HDR, negatives kept): encode it back to the game's gamma 2.2 mirror space, alpha is passed through.
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color.rgb = linear_to_gamma(color.rgb, GCT_MIRROR);

#include "../Includes/SMAA_Passes.hlsl"
