#ifndef SRC_MOTION_VECTOR_FILL_HLSL
#define SRC_MOTION_VECTOR_FILL_HLSL

// The motion vector fill for motion vectors that come from patched draws ("motion_vector_patch.h"). The caller rebuilds the device
// depth, then these complete the upscaler's inputs.

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

void WriteCameraMotion(RWTexture2D<float2> motion_vectors, uint2 pixel, uint2 size, float4x4 reprojection, float2 jitter_ndc, float depth)
{
   motion_vectors[pixel] = CameraMotion(pixel, size, reprojection, jitter_ndc, depth);
}

// FSR's masks from what the alpha blended draws wrote themselves ("MotionVectorPatch::PatchPixelShaderReactive", max blended;
// "mask": x reactive, y transparency & composition). They draw without motion vectors of their own, so FSR would keep the history
// of what's behind them (ghosting). The reactive one from all of them: scaled, then 0 under the threshold and 0.9 over it (a low one
// shakes static glows), or without a threshold capped at 0.9 so FSR still keeps a little history. The transparency & composition one
// from the non-additive ones (smoke, glass, water), as is (AMD's sample passes the alpha).
void WriteFsrMasks(RWTexture2D<float> reactive, RWTexture2D<float> transparency, uint2 pixel, float2 mask, float reactive_scale, float reactive_threshold)
{
   const float reactivity = mask.x * reactive_scale;
   reactive[pixel] = reactive_threshold > 0.0 ? (reactivity < reactive_threshold ? 0.0 : 0.9) : min(reactivity, 0.9);
   transparency[pixel] = mask.y;
}

#endif // SRC_MOTION_VECTOR_FILL_HLSL
