// Camera motion for motion vector pixels no patched draw wrote (still the marker 65504 from the frame start, the largest float16:
// sky, unpatched draws): the scene depth reprojected from the current to the previous frame's world camera (b1 ViewProjectionMatrix,
// turned into column vectors on the CPU). Depth isn't reversed. When enabled, also FSR's masks (see
// "Includes/MotionVectorFill.hlsl").

#include "Includes/MotionVectorFill.hlsl"

cbuffer MotionVectorFill : register(b0)
{
   row_major float4x4 reprojection; // Current clip space to the previous frame's
   float2 jitter_ndc;               // This frame's projection jitter: in the depth, not in the motion vectors
   float reactive_scale;
   float reactive_threshold;
   float reactive_enabled;
   float3 padding;
};

Texture2D<float> depth : register(t0);
RWTexture2D<float2> motion_vectors : register(u0);
Texture2D<float2> mask_input : register(t1); // x reactive, y transparency & composition
RWTexture2D<float> reactive : register(u1);
RWTexture2D<float> transparency : register(u2);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   uint2 size;
   motion_vectors.GetDimensions(size.x, size.y);
   if (any(id.xy >= size))
      return;
   if (reactive_enabled != 0.0)
   {
      WriteFsrMasks(reactive, transparency, id.xy, mask_input.Load(int3(id.xy, 0)), reactive_scale, reactive_threshold);
   }
   if (motion_vectors[id.xy].x < MOTION_VECTOR_MARKER)
      return;
   WriteCameraMotion(motion_vectors, id.xy, size, reprojection, jitter_ndc, depth.Load(int3(id.xy, 0)));
}
