// SMAA predication signal from the game's R24 scene depth (non-reversed, sky = 1), taken from the ambient pass.
// In a measured frame a raw R24 depth threshold only caught nearby silhouettes, hence the plane deviation measure.
// Method and the SMAA_PREDICATION_THRESHOLD 0.5 contract: see "PlaneDeviationEdgeness" and "RelativeViewDepth" in
// Includes/DepthEdges.hlsl.

#include "Includes/DepthEdges.hlsl"

Texture2D<float> depth : register(t0);
RWTexture2D<float> edgeness : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   edgeness[id.xy] = DepthPredicationEdgeness(depth, id.xy, false);
}
