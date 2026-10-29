// SMAA predication signal from the scene depth (D32S8, reversed-Z with near at 1 and the sky at 0), taken from the camera velocity
// pass.
// Depth: the projection gives d = near / z minus a far term of ~1e-5, so 1 / d is about proportional to linear view depth.
// Method and the SMAA_PREDICATION_THRESHOLD 0.5 contract: see "PlaneDeviationEdgeness" and "RelativeViewDepth" in
// Includes/DepthEdges.hlsl.

#include "Includes/DepthEdges.hlsl"

Texture2D<float> depth : register(t0);
RWTexture2D<float> edgeness : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   edgeness[id.xy] = DepthPredicationEdgeness(depth, id.xy, true);
}
