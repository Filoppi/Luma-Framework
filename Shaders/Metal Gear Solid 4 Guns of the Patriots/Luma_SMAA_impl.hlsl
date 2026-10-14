// SMAA implementation
// See Demo for the reference https://github.com/iryoku/smaa
//
// This replaces the game's FXAA pass (0xFAB5AE7C, when active), which ran on the gamma space post process
// buffer, so both the edge detection and the neighborhood blending run in gamma space too (which is what
// SMAA's color edge detection expects anyway), and no color space conversion pass is needed.

#include "../Includes/Common.hlsl"

#define SMAA_RT_METRICS float4(LumaSettings.RenderInvSize, LumaSettings.RenderSize)
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION       0 // We have no depth buffer hooked up in this game
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)

// SMAAEdgeDetection
// (predication is disabled, so no "tex1" here)

// SMAANeighborhoodBlending
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color.a = 1.0; // The original FXAA pass also always wrote out an opaque alpha

#include "../Includes/SMAA_Passes.hlsl"
