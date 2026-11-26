// SMAA implementation
// See Demo for the reference https://github.com/iryoku/smaa

#include "Includes/Common.hlsl"

// Metrics come from a dedicated cbuffer at b1 (filled by DrawSMAA's cb_smaa_metrics), not from the game's LumaSettings, as this game's settings have no OutputRes.
cbuffer SMAAMetricsCB : register(b1)
{
   float4 SMAAMetricsValue; // (1/width, 1/height, width, height)
};

#define SMAA_RT_METRICS SMAAMetricsValue
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       0
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
#include "../Includes/SMAA_Passes.hlsl"
