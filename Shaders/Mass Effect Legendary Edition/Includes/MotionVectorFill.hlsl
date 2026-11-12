#ifndef SRC_MOTION_VECTOR_FILL_HLSL
#define SRC_MOTION_VECTOR_FILL_HLSL

// The camera motion half of the motion vector fill, for motion vectors that come from patched draws ("motion_vector_patch.h").

// The marker the motion vector target is cleared to at the frame start (the largest float16): its pixels no patched draw wrote (sky,
// unpatched draws) get the camera motion. Test "!(x < MOTION_VECTOR_MARKER)" so a NaN one gets it too.
#define MOTION_VECTOR_MARKER 65504.0

// The camera motion of a pixel: its device depth reprojected from the current to the previous frame's camera ("reprojection":
// current clip space to the previous frame's, column vectors). "jitter_ndc": this frame's projection jitter, in the depth, not in
// the motion vectors. Previous minus current, UV space, like the patched draws.
float2 CameraMotion(uint2 pixel, uint2 size, float4x4 reprojection, float2 jitter_ndc, float depth)
{
   const float4 current = float4((pixel + 0.5) / size * float2(2.0, -2.0) + float2(-1.0, 1.0) - jitter_ndc, depth, 1.0);
   const float4 previous = mul(reprojection, current);
   return (previous.xy / previous.w - current.xy) * float2(0.5, -0.5);
}

#endif // SRC_MOTION_VECTOR_FILL_HLSL
