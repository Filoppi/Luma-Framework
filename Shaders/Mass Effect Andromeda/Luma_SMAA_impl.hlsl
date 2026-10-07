// SMAA implementation for Mass Effect: Andromeda (replaces the game's FXAA pass).
// Reference: https://github.com/iryoku/smaa

#include "../Includes/Common.hlsl"

// (1/W, 1/H, W, H) at output resolution — filled by the mod's SMAA block in main.cpp.
cbuffer SmaaMetricsCB : register(b1)
{
   float4 SmaaRtMetrics;
}

#define SMAA_RT_METRICS SmaaRtMetrics
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       0
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)

// SMAAEdgeDetection
// tex0 = colorTexGamma (predication disabled, so tex1 is unused)

#include "../Includes/SMAA_Passes.hlsl"
