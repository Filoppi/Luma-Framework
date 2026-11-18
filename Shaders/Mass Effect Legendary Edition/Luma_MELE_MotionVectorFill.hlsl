// Camera motion for the motion vector pixels no patched draw wrote (sky, unpatched draws), which still hold the frame-start
// marker 65504, the largest float16: scene depth reprojected from the current to the previous frame's world camera (b1
// ViewProjectionMatrix and the PreViewTranslation change, transposed for column vectors on the CPU). Depth isn't reversed.
// When enabled, also FSR's masks from what the additive draws and the alpha blended ones without their own motion
// (particles, refused draws) wrote (Bayonetta's rules).

#include "Includes/MotionVectorFill.hlsl"

cbuffer MotionVectorFill : register(b0)
{
   row_major float4x4 reprojection; // Current clip space to the previous frame's
   float2 jitter_ndc;               // This frame's projection jitter: in the depth, not in the motion vectors
   float2 render_size;              // The scene's viewport: the top-left share of the targets under the engine's render scale
   float reactive_scale;
   float reactive_threshold;
   float reactive_enabled;
   float padding;
};

Texture2D<float> depth : register(t0);
Texture2D<float2> mask_input : register(t1); // x reactive, y transparency & composition (both max blended)
RWTexture2D<float2> motion_vectors : register(u0);
RWTexture2D<float> reactive : register(u1);
RWTexture2D<float> transparency : register(u2);
RWTexture2D<float> previous_additive : register(u3); // The additive draws' mask of the previous frame, replaced here

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   const uint2 size = uint2(render_size);
   if (any(id.xy >= size))
      return;
   if (reactive_enabled != 0.0)
   {
      // As in Bayonetta's motion vector fill: the additive draws write y = 0, so x > y is theirs (glows, sparks), scaled and
      // capped at 0.9 without a threshold, and kept one more frame (a light that went out is still lit in the upscaler's history);
      // the alpha blended ones (x = y = a: smoke, glass) 0.9 over the threshold only, as a continuous mask shakes whole sprites
      const float2 mask = mask_input.Load(int3(id.xy, 0));
      const float current_additive = (mask.x > mask.y ? mask.x : 0.0);
      const float additive = min(max(current_additive, previous_additive[id.xy]) * reactive_scale, 0.9);
      previous_additive[id.xy] = current_additive;
      const float alpha_blended = (mask.y < reactive_threshold ? 0.0 : 0.9);
      reactive[id.xy] = max(additive, alpha_blended);
      transparency[id.xy] = mask.y;
   }
   // The alpha blended draws blend their motion vectors by their alpha: over a marker pixel (sky behind smoke) a partial weight leaves
   // a value between the two, never a real motion (2 screens per frame), so it takes the camera motion too (NaN as well)
   if (abs(motion_vectors[id.xy].x) < 2.0)
      return;
   motion_vectors[id.xy] = CameraMotion(id.xy, size, reprojection, jitter_ndc, depth.Load(int3(id.xy, 0)));
}
