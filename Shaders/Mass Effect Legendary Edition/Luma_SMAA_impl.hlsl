// SMAA Ultra replacement for the trilogy-wide MiniEngine FXAA chain.
// Reference: https://github.com/iryoku/smaa
// Color-edge detection reads the gamma-encoded post snapshot; neighborhood blending reads its linear decode
// (Luma_MELE_SMAALinearize) and re-encodes, so stage 2 still decodes a gamma result.

#include "../Includes/Common.hlsl"

// Filled by the FXAA replacement hook in main.cpp.
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
   // x = off-edge threshold scale: 2 with valid same-sized depth, 1 for plain Ultra with null depth.
   // y = depth-delta threshold; z = edge strength; w unused. Runtime values account for MELE's hyperbolic depth.
   float4 SmaaPredication;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION           1
#define SMAA_PREDICATION_SCALE     SmaaPredication.x
#define SMAA_PREDICATION_THRESHOLD SmaaPredication.y
#define SMAA_PREDICATION_STRENGTH  SmaaPredication.z
#define SMAAGather(tex, coord)     tex.Gather(LinearSampler, coord, 0)

// SMAAEdgeDetection
// tex0 = gamma post color; tex1 = scene depth for predication.

// SMAANeighborhoodBlending
// tex0 = linear post color; tex1 = blend weights. Re-encode to the buffer's gamma.
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color.rgb = linear_to_gamma(color.rgb, GCT_MIRROR);

#include "../Includes/SMAA_Passes.hlsl"
