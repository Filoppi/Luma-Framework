// SMAA predication signal in place of the raw depth buffer, ported from Medal of Honor: Airborne (which took it from The Witcher 2).
//
// It measures deviation from the local tangent plane, not depth. Depth cannot work: SMAA's predication is a plain first difference
// between adjacent pixels, and on linear depth a plane's own per-pixel change grows as z^2, so a distant floor seen edge-on moves
// more between neighbours than a nearby silhouette does; no monotonic remap of z and no threshold fixes that ratio.
//
// The math is a slope-adjusted second difference with a depth-proportional tolerance, as "XeGTAO_CalculateEdges" in
// the shared "XeGTAO.hlsl" (SVGF, Schied et al. 2017; Emil Persson 2009). The output is one-sided, against the left and top neighbours
// only, because SMAA compares centre-vs-left on one axis and centre-vs-top on the other; a symmetric mask would read 1 on both sides
// of a silhouette and difference to 0 exactly where predication must fire. Normalized by centreZ it is a unitless edge-ness in
// [0,1], so SMAA_PREDICATION_THRESHOLD is 0.5 whatever the scale, FOV or resolution: tune P.x, not it.
//
// Unlike MoH, the input is hardware D24, not linear depth: each tap is linearized through the game's own MinZ_MaxZRatio, the
// expression XeGTAO uses. DepthScaleRT is not applied: the tolerance is a fraction of view depth, so a uniform divisor cancels out
// of the ratio and the calibrated value carries across games.

#include "Includes/DepthEdges.hlsl"

Texture2D<float4> depth : register(t0); // hardware d24 scene depth (r24_unorm_x8_uint view), non-reverse-Z
RWTexture2D<float> uav : register(u0);  // R16_FLOAT predication signal (0 = on the local plane, 1 = edge)

cbuffer PredCB : register(b0)
{
   float4 P; // P.x = relative tolerance: plane deviation counted as a full edge, as a fraction of view depth
}

// The cel-shading pass's PSOffsetConstants, captured with the depth (see main.cpp). Only MinZ_MaxZRatio is read.
cbuffer PSOffsetConstants : register(b2)
{
   float4 ScreenPositionScaleBias;
   float4 MinZ_MaxZRatio;
}

float ViewDepthAt(int3 p)
{
   const float d = depth.Load(p).r;
   return max(0.0, 1.0 / max(1e-7, d * MinZ_MaxZRatio.z - MinZ_MaxZRatio.w));
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   const int3 p = int3(id.xy, 0);
   const float centerZ = ViewDepthAt(p);
   const float leftZ = ViewDepthAt(p - int3(1, 0, 0));
   const float rightZ = ViewDepthAt(p + int3(1, 0, 0));
   const float topZ = ViewDepthAt(p - int3(0, 1, 0));
   const float bottomZ = ViewDepthAt(p + int3(0, 1, 0));

   // Depth-proportional tolerance: what matters scales with distance (a 10 cm step is a silhouette at 2 m and
   // noise at 200 m). Both terms are required - the slope adjustment alone degrades toward the vanishing point,
   // and a depth-proportional threshold alone cannot reject a grazing plane at all.
   uav[id.xy] = PlaneDeviationEdgenessMirrored(id.xy, centerZ, leftZ, rightZ, topZ, bottomZ, DepthExtractTolerance(centerZ, P.x));
}
