// Builds the upscaler inputs from the scene's alpha, which holds the encoded view depth: the device depth, the camera motion (the vc4
// view projection, turned into column vectors on the CPU with the PreViewTranslation change included) and, when enabled, FSR's masks.
// The device depth isn't reversed. As in ME1's fill, it comes from the alpha as the game's depth has no shader resource view.

// The marker the motion vector target is cleared to at the frame start (the largest float16): its pixels no patched draw wrote (sky,
// unpatched draws) get the camera motion. Test "!(x < MOTION_VECTOR_MARKER)" so a NaN one gets it too.
#define MOTION_VECTOR_MARKER 65504.0

// The scene's alpha holds Gearbox's EncodeFloatW of the view depth W (Common.usf, "GBX:Zoner"): -(W / 32)^2 up to 4096 units,
// (W / 8192)^2 past it, the sky clamped to 65503 (stored as fp16's 65472 or 65504). The inverse, in game units.
float DecodeFloatW(float encoded)
{
   return sqrt(abs(encoded)) * (encoded > 0.0 ? 8192.0 : 32.0);
}

cbuffer MotionVectorFill : register(b0)
{
   row_major float4x4 reprojection; // Current clip space to the previous frame's
   float2 jitter_ndc;               // This frame's projection jitter: in the depth, not in the motion vectors
   float2 depth_from_view;          // The projection's depth row: device depth = x + y / view depth
   float reactive_scale;
   float reactive_threshold;
   float reactive_enabled;
   float padding;
};

Texture2D<float4> scene : register(t0);
RWTexture2D<float2> motion_vectors : register(u0);
RWTexture2D<float> device_depth : register(u1);
Texture2D<float2> mask_input : register(t1); // x reactive, y transparency & composition
RWTexture2D<float> reactive : register(u2);
RWTexture2D<float> transparency : register(u3);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   uint2 size;
   motion_vectors.GetDimensions(size.x, size.y);
   if (any(id.xy >= size))
      return;
   if (reactive_enabled != 0.0)
   {
      // FSR's masks, from what the alpha blended draws wrote to the mask target ("MotionVectorPatch::PatchPixelShaderReactive", max
      // blended, x reactive and y transparency & composition). These draws have no motion vectors of their own, so FSR would keep the
      // history of what's behind them (ghosting). Reactive comes from all of them: scaled, then 0 under the threshold and 0.9 over it
      // (a low value shakes static glows), or capped at 0.9 without a threshold so FSR keeps a little history. Transparency &
      // composition comes from the non-additive ones (smoke, glass, water) as is, as AMD's sample passes the alpha.
      const float2 mask = mask_input.Load(int3(id.xy, 0));
      const float reactivity = mask.x * reactive_scale;
      reactive[id.xy] = reactive_threshold > 0.0 ? (reactivity < reactive_threshold ? 0.0 : 0.9) : min(reactivity, 0.9);
      transparency[id.xy] = mask.y;
   }
   // max() also drops a NaN; a cleared (0) alpha lands on the near plane
   const float depth = saturate(depth_from_view.x + depth_from_view.y / max(DecodeFloatW(scene.Load(int3(id.xy, 0)).a), 1e-4));
   device_depth[id.xy] = depth;
   if (motion_vectors[id.xy].x < MOTION_VECTOR_MARKER)
      return;
   // The camera motion: the device depth reprojected from the current to the previous frame's camera ("reprojection", column
   // vectors). Previous minus current, UV space, like the patched draws.
   const float4 current = float4((id.xy + 0.5) / size * float2(2.0, -2.0) + float2(-1.0, 1.0) - jitter_ndc, depth, 1.0);
   const float4 previous = mul(reprojection, current);
   motion_vectors[id.xy] = (previous.xy / previous.w - current.xy) * float2(0.5, -0.5);
}
