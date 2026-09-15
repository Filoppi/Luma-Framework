// Borderlands GOTY Enhanced — SMAA predication signal, ported from Medal of Honor: Airborne (which took it from
// The Witcher 2). Replaces feeding the raw depth buffer straight into SMAA's predication.
//
// This measures deviation from the local tangent plane, NOT depth. Depth itself cannot work: SMAA's predication is
// a plain first difference between adjacent pixels, and on linear depth a plane's own per-pixel change grows as
// z^2, so a distant floor seen edge-on moves more between neighbours than a nearby silhouette does - no monotonic
// remap of z and no threshold fixes that ratio.
//
// The math is a slope-adjusted second difference with a depth-proportional tolerance, the same as
// XeGTAO_CalculateEdges in Luma_BL_XeGTAO.hlsl (SVGF, Schied et al. 2017; Emil Persson 2009). Output is one-sided,
// against the LEFT and TOP neighbours only, because SMAA compares centre-vs-left on one axis and centre-vs-top on
// the other; a symmetric mask would read 1 on both sides of a silhouette and difference to 0 exactly where
// predication must fire. Normalizing by centreZ makes it a unitless edge-ness in [0,1], so
// SMAA_PREDICATION_THRESHOLD is simply 0.5 whatever the scale, FOV or resolution - tune P.x, not it.
//
// DIFFERENCE FROM MoH: this game hands us hardware d24, not a linear depth, so each tap is linearized first
// through the game's own MinZ_MaxZRatio - the identical expression XeGTAO uses. DepthScaleRT is deliberately
// NOT applied: the tolerance is a fraction of view depth, so a uniform divisor cancels out of the ratio and the
// calibrated value carries across games unchanged.

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
   // Out-of-bounds Loads return 0 (D3D11-defined); mirroring the centre there keeps the border flat instead of
   // reporting a false edge along the screen edges.
   const float leftZ = (id.x > 0) ? ViewDepthAt(p - int3(1, 0, 0)) : centerZ;
   const float rightZ = ViewDepthAt(p + int3(1, 0, 0));
   const float topZ = (id.y > 0) ? ViewDepthAt(p - int3(0, 1, 0)) : centerZ;
   const float bottomZ = ViewDepthAt(p + int3(0, 1, 0));

   // Deviation from the local plane: compare each one-sided delta against the slope implied by the opposite
   // neighbour and keep the smaller. On a plane (any orientation) the two agree and this cancels to ~0; at a depth
   // discontinuity neither cancels. Taking the min also means a garbage opposite tap (the last row/column, where
   // the Load runs off the texture) can only shrink the result, never invent an edge.
   float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;
   const float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
   const float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
   const float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
   edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));

   // Depth-proportional tolerance: what matters scales with distance (a 10 cm step is a silhouette at 2 m and
   // noise at 200 m). Both terms are required - the slope adjustment alone degrades toward the vanishing point,
   // and a depth-proportional threshold alone cannot reject a grazing plane at all.
   const float tolerance = max(centerZ, 1e-3) * max(P.x, 1e-4);
   // Left and top only (see the header): this is the axis pairing SMAA's predication actually compares.
   uav[id.xy] = saturate(max(edgesLRTB.x, edgesLRTB.z) / tolerance);
}
