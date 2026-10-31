// SMAA for Sunset Overdrive (reference: https://github.com/iryoku/smaa), in place of the game's SMAA blend PS_SmaaApplyBlend
// 0xD8DB5270. It runs the ULTRA preset with color edge detection on the tonemap's output, which the blend reads at t5 (a copy of it
// with SMAA 1x), and writes the blend's target.
// The input is the tonemap's sRGB encoded output with display-mapped HDR values, so >1 is possible. Edge detection works on it as is,
// and neighborhood blending filters the same texture after a gamma 2.2 decode (the display composition's) and re-encodes.
// The engine's render resolution is independent of the window, so the metrics come from main.cpp (LumaData.CustomData1/2 = size).
// Predication uses plane-deviation edge-ness in [0,1] of the scene depth (Luma_SO_SMAAPredication); main.cpp passes a null texture
// and scale 1 (plain ULTRA) without it.

// "SMAA_T2X" 1 is SMAA T2x in place of the game's (main.cpp's "SO SMAA T2x" passes). The scene jitters by a quarter pixel diagonally
// in two phases. The blending weights take the phase's area texture subsamples (LumaData.CustomData4, 0 when the frame didn't jitter),
// the neighborhood blend stays gamma 2.2 decoded with the velocity length in alpha, and the resolve blends it with the previous frame
// reprojected by the motion vectors. The output hook below runs at the resolve.
#ifndef SMAA_T2X
#define SMAA_T2X 0
#endif

#include "Includes/Common.hlsl"

#if SMAA_T2X
#define SMAA_SUBSAMPLE_INDICES float4(LumaData.CustomData4.xxx, 0.0)
#define SMAA_REPROJECTION      1
// The motion vectors are render pixels from the current to the previous position. SMAA's velocity is the opposite, in UV.
#define SMAA_DECODE_VELOCITY(sample) (-(sample).rg * SMAA_RT_METRICS.xy)
#endif

#define SMAA_RT_METRICS float4(1.0 / float2(LumaData.CustomData1, LumaData.CustomData2), float2(LumaData.CustomData1, LumaData.CustomData2))
#define SMAA_PRESET_ULTRA
#define SMAA_PREDICATION 1
// TW2's validated tuning: flagged silhouettes get plain ULTRA (2 x (1 - 0.5) x 0.05), the rest twice that.
#define SMAA_PREDICATION_SCALE     LumaData.CustomData3 // 2, or 1 (plain ULTRA) without depth
#define SMAA_PREDICATION_THRESHOLD 0.5                  // Midpoint of the [0,1] edge-ness signal
#define SMAA_PREDICATION_STRENGTH  0.5

// Edge detection: tex0 = colorTexGamma, tex1 = predication edge-ness (may be null)
// Neighborhood blending: tex0 = colorTex (the same input, filtered in linear light), tex1 = blendTex. Re-encoded with the same 2.2 power.
#define SMAA_NEIGHBORHOOD_GAMMA_IN_LINEAR 1
// Alpha as the tonemap writes it (1), never T2x's velocity length
#define SMAA_NEIGHBORHOOD_OUTPUT(color, position) color = float4(linear_to_gamma(color.rgb, GCT_MIRROR), 1.0);
// The predication signal above is a one-sided edge-ness; morphological edge suppression in place of the local contrast
// adaptation (both in Includes/SMAA_Passes.hlsl / Includes/SMAA.hlsl). main.cpp also drops the area texture's U-shape smoothing.
#define SMAA_PREDICATION_EDGENESS           1
#define SMAA_MORPHOLOGICAL_EDGE_SUPPRESSION 1
#include "Includes/SMAA_Passes.hlsl"
