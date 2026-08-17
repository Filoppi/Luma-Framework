// The Witcher 2 — build the SMAA predication signal from the game's depth.
//
// Input is the game's full-res r32_float depth, captured in main.cpp at the TINT tonemap draw (t1) or the AO
// pack pass. LINEAR VIEW-SPACE metres, not device Z (min 1.77, p50 7.3, max 686).
//
// Not a rescale, on purpose. SMAA predicates on a plain first difference between adjacent pixels, and on
// LINEAR depth that cannot separate a silhouette from a surface seen edge-on: a plane's own per-pixel change
// grows as z², so at 50 m a floor legitimately moves ~0.6 m per pixel while a 1 m silhouette is only ~1.5x
// above it. No monotonic remap of z and no threshold fixes that ratio.
// Instead this measures deviation from the local tangent plane — a slope-adjusted second difference with a
// depth-proportional tolerance, the same math as XeGTAO_CalculateEdges in Luma_TW2_XeGTAO.hlsl.
//
// Output is edge-ness in [0,1] against the LEFT and TOP neighbours only: SMAA compares centre-vs-left on one
// axis and centre-vs-top on the other, so a one-sided measure jumps 0 -> 1 exactly ACROSS a silhouette, while
// a symmetric mask would read 1 on both sides and difference to 0 where predication must fire. Single-channel
// because the predication Gather reads only R. With this signal SMAA_PREDICATION_THRESHOLD is simply 0.5.

Texture2D<float> depth : register(t0); // game r32_float depth (LINEAR view-space metres)
RWTexture2D<float> uav : register(u0); // R16_FLOAT predication signal (0 = on the local plane, 1 = edge)

cbuffer PredCB : register(b0)
{
   float4 P; // P.x = relative tolerance: plane deviation counted as a full edge, as a fraction of view depth
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   const int3 p = int3(id.xy, 0);
   const float centerZ = depth.Load(p);
   // Out-of-bounds Loads return 0 (D3D11-defined); mirroring the centre there keeps the border flat instead of
   // reporting a false edge along the screen edges.
   const float leftZ = (id.x > 0) ? depth.Load(p - int3(1, 0, 0)) : centerZ;
   const float rightZ = depth.Load(p + int3(1, 0, 0));
   const float topZ = (id.y > 0) ? depth.Load(p - int3(0, 1, 0)) : centerZ;
   const float bottomZ = depth.Load(p + int3(0, 1, 0));

   // Deviation from the local plane: compare each one-sided delta against the slope implied by the opposite
   // neighbour, and keep whichever is smaller. On a plane (any orientation) the two agree and this cancels to
   // ~0; at a depth discontinuity neither cancels.
   float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;
   const float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
   const float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
   const float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
   edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));

   // Depth-proportional tolerance: the deviation that matters scales with distance (a 10 cm step is a
   // silhouette at 2 m and noise at 200 m). Both terms are required — the slope adjustment alone still
   // degrades toward the vanishing point, and a depth-proportional threshold alone cannot reject a grazing
   // plane at all.
   const float tolerance = max(centerZ, 1e-3) * max(P.x, 1e-4);
   // Left and top only (see the header): this is the axis pairing SMAA's predication actually compares.
   uav[id.xy] = saturate(max(edgesLRTB.x, edgesLRTB.z) / tolerance);
}
