// SMAA implementation
// See Demo for the reference https://github.com/iryoku/smaa

#include "Includes/Common.hlsl"

#define SMAA_RT_METRICS float4(LumaSettings.GameSettings.InvOutputRes, LumaSettings.GameSettings.OutputRes)
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       1
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
#include "../Includes/SMAA_Passes.hlsl"
