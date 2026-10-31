// Converts the game's TAA inputs into what the SR (DLSS/FSR) implementations expect.
// Runs inside the TAA dispatch, so the game's TAA_PARAMS (cb10) and its depth (t1) and motion vectors (t3) SRVs are still bound.

// taa CS cb10 (TAA_PARAMS): matReprojection rows take (uv, device depth, 1) to the previous frame's clip position
cbuffer TAA_PARAMS : register(b10)
{
   float4 matReprojection[4];
   uint2 screenSize;
   float2 texelSize;
}

Texture2D<float> Depth : register(t1);                 // D24S8, standard depth (far = 1)
Texture2D<float2> EncodedMotionVectors : register(t3); // G-buffer RT3: saturate((current - previous pixels + 127) / 255), (0, 0) = no object motion

RWTexture2D<float2> OutMotionVectors : register(u0); // previous - current, in pixels, y down
RWTexture2D<float> OutDepth : register(u1);          // FSR only (no 24 bit depth format): null for DLSS, which reads Depth itself

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
   if (any(id.xy >= screenSize))
      return;

   const float depth = Depth.Load(int3(id.xy, 0));
   const float2 encoded = EncodedMotionVectors.Load(int3(id.xy, 0));
   float2 motion; // current - previous, in pixels
   // Surfaces without object motion vectors (most of the static world) are reprojected with the camera, like the game's TAA does
   if (all(encoded == 0.0))
   {
      const float2 uv = (id.xy + 0.5) * texelSize;
      const float4 position = float4(uv, depth, 1.0);
      const float3 previous_clip = float3(dot(matReprojection[0], position), dot(matReprojection[1], position), dot(matReprojection[3], position));
      const float2 previous_uv = float2(previous_clip.x / previous_clip.z * 0.5 + 0.5, 0.5 - previous_clip.y / previous_clip.z * 0.5);
      motion = (uv - previous_uv) * screenSize;
   }
   else
   {
      motion = encoded * 255.0 - 127.0;
   }
   OutMotionVectors[id.xy] = -motion;
   OutDepth[id.xy] = depth;
}
