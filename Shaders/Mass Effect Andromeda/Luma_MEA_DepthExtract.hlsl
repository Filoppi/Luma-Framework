// SMAA predication signal. Source: the game's linear view depth (r32_float, metres, ~20000 on the sky), written by PS
// 0xDE1C9EB9 from the D24 reverse-Z depth (near / device Z) and read by the AO, lighting and motion blur passes. A plain
// difference fails (a plane's change grows with distance), so this is tangent-plane deviation (XeGTAO_CalculateEdges), [0,1].

#include "Includes/DepthEdges.hlsl"

Texture2D<float> depth : register(t0); // view depth in metres
RWTexture2D<float> uav : register(u0); // R16_FLOAT predication signal (0 = on the local plane, 1 = edge)

cbuffer PredCB : register(b0)
{
   float4 P; // P.x = relative tolerance: plane deviation counted as a full edge, as a fraction of view depth
}

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   uint w, h;
   depth.GetDimensions(w, h);
   if (id.x >= w || id.y >= h)
      return;
   const int3 p = int3(id.xy, 0);
   const float centerZ = depth.Load(p);
   // Right/bottom mirror the centre too (left/top are mirrored by "PlaneDeviationEdgenessMirrored").
   const float leftZ = depth.Load(p - int3(1, 0, 0));
   const float rightZ = (id.x + 1 < w) ? depth.Load(p + int3(1, 0, 0)) : centerZ;
   const float topZ = depth.Load(p - int3(0, 1, 0));
   const float bottomZ = (id.y + 1 < h) ? depth.Load(p + int3(0, 1, 0)) : centerZ;

   // Depth-proportional tolerance, as what matters scales with distance. Both terms are required: the slope adjustment
   // alone degrades toward the vanishing point, and a depth-proportional threshold alone can't reject a grazing plane.
   uav[id.xy] = PlaneDeviationEdgenessMirrored(id.xy, centerZ, leftZ, rightZ, topZ, bottomZ, DepthExtractTolerance(centerZ, P.x));
}
