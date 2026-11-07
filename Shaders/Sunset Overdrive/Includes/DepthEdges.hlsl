#ifndef SRC_DEPTH_EDGES_HLSL
#define SRC_DEPTH_EDGES_HLSL

// SMAA predication signal from depth: deviation from the local tangent plane, a slope-adjusted second difference with a
// depth-proportional tolerance (the math of XeGTAO_CalculateEdges; SVGF, Schied et al. 2017; Emil Persson 2009).
// A plain depth difference cannot work: a plane's own per-pixel change grows with distance.
//
// Each one-sided delta is compared against the slope implied by the opposite neighbour and the smaller is kept: on a plane
// (any orientation) the two agree and cancel to ~0, at a depth discontinuity neither does. Taking the min also means a
// garbage opposite tap (off the texture) can only shrink the result, never invent an edge.
//
// Returns edge-ness in [0,1] against the LEFT and TOP neighbours only: SMAA compares centre-vs-left and centre-vs-top, so a
// one-sided measure jumps 0 -> 1 exactly across a silhouette (a symmetric one would difference to 0 there), and
// SMAA_PREDICATION_THRESHOLD is 0.5. "tolerance" is the plane deviation counted as a full edge, in the depth's units
// (normally a fraction of the centre's view depth).
float PlaneDeviationEdgeness(float center, float left, float right, float top, float bottom, float tolerance)
{
   float4 edgesLRTB = float4(left, right, top, bottom) - center;
   const float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
   const float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
   edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB)));
   return saturate(max(edgesLRTB.x, edgesLRTB.z) / tolerance);
}

// PlaneDeviationEdgeness for a compute-shader texel whose five depth values are already loaded. Out-of-bounds Loads return
// 0 (D3D11-defined), so the left/top neighbour is replaced by the centre on the first column/row: a flat border instead of a
// false edge rim. Right/bottom stay as loaded (a garbage opposite tap can only shrink the result); a caller that wants them
// mirrored too passes the centre itself.
float PlaneDeviationEdgenessMirrored(uint2 pixel, float center, float left, float right, float top, float bottom, float tolerance)
{
   return PlaneDeviationEdgeness(center, (pixel.x > 0) ? left : center, right, (pixel.y > 0) ? top : center, bottom, tolerance);
}

// The depth-proportional tolerance of the DepthExtract passes: "relativeTolerance" (fraction of the centre's depth) with a
// floor on both factors so sky / zero depth keeps a nonzero tolerance.
float DepthExtractTolerance(float center, float relativeTolerance)
{
   return max(center, 1e-3) * max(relativeTolerance, 1e-4);
}

// A value proportional to linear view depth from a device depth d with a standard (1 - d ~ near / z) or reversed
// (d ~ near / z) projection: the relative tolerance cancels the unknown near plane, so no camera constants are needed.
// The sky (0 after the flip) clamps at 2^-24.
float RelativeViewDepth(float deviceDepth, bool reversedZ)
{
   return rcp(max(reversedZ ? deviceDepth : 1.0 - deviceDepth, 1.0 / 16777216.0));
}

// One predication texel straight from a device depth texture. "relativeTolerance" is the plane deviation counted as a
// full edge, as a fraction of view depth (TW2's validated 0.02).
float DepthPredicationEdgeness(Texture2D<float> depth, uint2 pixel, bool reversedZ, float relativeTolerance = 0.02)
{
   const int3 p = int3(pixel, 0);
   const float centerZ = RelativeViewDepth(depth.Load(p), reversedZ);
   const float leftZ = RelativeViewDepth(depth.Load(p - int3(1, 0, 0)), reversedZ);
   const float rightZ = RelativeViewDepth(depth.Load(p + int3(1, 0, 0)), reversedZ);
   const float topZ = RelativeViewDepth(depth.Load(p - int3(0, 1, 0)), reversedZ);
   const float bottomZ = RelativeViewDepth(depth.Load(p + int3(0, 1, 0)), reversedZ);
   return PlaneDeviationEdgenessMirrored(pixel, centerZ, leftZ, rightZ, topZ, bottomZ, centerZ * relativeTolerance);
}

#endif // SRC_DEPTH_EDGES_HLSL
