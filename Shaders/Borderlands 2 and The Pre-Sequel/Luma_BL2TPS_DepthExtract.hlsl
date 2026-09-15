// Borderlands 2 / The Pre-Sequel — SMAA predication signal, ported from Medal of Honor: Airborne (which took it
// from The Witcher 2). No depth buffer under dgVoodoo, but UE3 packs LINEAR view-space depth in the ALPHA of the fp16
// scene colour, captured in main.cpp at the tonemap draw. Rescaling that depth cannot work: SMAA's predication is a
// plain first difference between adjacent pixels, and on linear depth a plane's own per-pixel change grows as z^2, so
// a distant floor seen edge-on moves more than a near silhouette and no remap or threshold fixes that ratio.
//
// This measures deviation from the local tangent plane instead: a slope-adjusted second difference with a
// depth-proportional tolerance, the same math as XeGTAO_CalculateEdges (SVGF, Schied et al. 2017; Emil Persson 2009).
// Output is one-sided (LEFT and TOP only), because SMAA compares centre-vs-left and centre-vs-top; a symmetric mask
// would read 1 on both sides of a silhouette and difference to 0 exactly where predication must fire. Normalizing by
// centreZ makes it a unitless edge-ness in [0,1], so SMAA_PREDICATION_THRESHOLD is simply 0.5 whatever the scale, FOV
// or resolution - tune P.x, not it.
//
// DIFFERENCE FROM MoH: the sign. Geometry carries NEGATIVE view z here and sky/invalid is a >= 0 (the +65472 far
// sentinel, measured), so each tap goes through DepthFromAlpha, which also pins sky far away.

Texture2D<float4> scene : register(t0); // fp16 scene colour; .a = view-space Z (negative for geometry)
RWTexture2D<float> uav : register(u0);  // R16_FLOAT predication signal (0 = on the local plane, 1 = edge)

cbuffer PredCB : register(b0)
{
   float4 P; // P.x = relative tolerance: plane deviation counted as a full edge, as a fraction of view depth
}

// Sky / invalid (a >= 0, e.g. the +65472 sentinel) is pushed to the fp16 ceiling rather than to a finite scene
// distance, so a silhouette against the sky always clears the tolerance.
float DepthFromAlpha(float a)
{
   return (a < 0.0) ? -a : 65504.0;
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   const int3 p = int3(id.xy, 0);
   const float centerZ = DepthFromAlpha(scene.Load(p).a);
   // Out-of-bounds Loads return 0 (D3D11-defined); mirroring the centre there keeps the border flat instead of
   // reporting a false edge along the screen edges.
   const float leftZ = (id.x > 0) ? DepthFromAlpha(scene.Load(p - int3(1, 0, 0)).a) : centerZ;
   const float rightZ = DepthFromAlpha(scene.Load(p + int3(1, 0, 0)).a);
   const float topZ = (id.y > 0) ? DepthFromAlpha(scene.Load(p - int3(0, 1, 0)).a) : centerZ;
   const float bottomZ = DepthFromAlpha(scene.Load(p + int3(0, 1, 0)).a);

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
   // noise at 200 m). Both terms are required - the slope adjustment alone still degrades toward the vanishing
   // point, and a depth-proportional threshold alone cannot reject a grazing plane at all.
   const float tolerance = max(centerZ, 1e-3) * max(P.x, 1e-4);
   // Left and top only (see the header): this is the axis pairing SMAA's predication actually compares.
   uav[id.xy] = saturate(max(edgesLRTB.x, edgesLRTB.z) / tolerance);
}
