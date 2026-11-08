// An AO history for XeGTAO and GT-VBAO ("temporal_pass_cs"), run between the main pass and the denoise when the noise changes per frame
// under DLSS/FSR. Intel's XeGTAO leaves the temporal part to the TAA, but DLSS/FSR keep much of the AO's noise in the lit scene as detail.
// The previous frame's AO is reprojected with the upscaler's motion vectors. Pixels that still hold the fill's "MOTION_VECTOR_MARKER"
// (the game's AO can run before its motion vector fill) use the camera's motion instead. Each bilinear tap is rejected if its depth isn't
// the one the previous frame should see there, and the result is clamped to this frame's 3x3 range, because motion vectors miss the AO
// that a moving occluder casts on a still surface.
//
// Include this after "XeGTAO.hlsl" (or "GTVBAO.hlsl"). The game defines "XeGTAO_ScreenSpaceToViewSpaceDepth" for the depth it binds at
// t3 (the prefilter's tex0), and "XE_GTAO_TEMPORAL_DEVICE_DEPTH" when that depth isn't the device depth (z/w) of "ReprojectionRT".
// The depth and motion vectors are loaded at "XE_GTAO_DEPTH_LOAD_COORD", like the prefilter's depth.

#ifndef LUMA_XE_GTAO_TEMPORAL_HLSL
#define LUMA_XE_GTAO_TEMPORAL_HLSL

#include "MotionVectorFill.hlsl"

#ifndef XE_GTAO_TEMPORAL_DEVICE_DEPTH
#define XE_GTAO_TEMPORAL_DEVICE_DEPTH(depth) (depth)
#endif

cbuffer LumaGTAOTemporal : register(b10)
{
   row_major float4x4 ReprojectionRT; // Current clip space to the previous frame's (column vectors)
   float2 JitterNDCRT;                // This frame's projection jitter, in the depth and not in the motion vectors
   float HistoryWeightRT;             // 0 restarts the history
   float TemporalPaddingRT;
}

static const float kXeGTAO_HistoryDepthTolerance = 0.03; // Relative view Z mismatch rejecting a history tap

Texture2D<float2> history : register(t1);           // The previous frame's (AO, view Z)
Texture2D<float2> motion_vectors : register(t2);    // Previous minus current, UV of the scene viewport, unjittered
Texture2D<float> scene_device_depth : register(t3); // tex0 of the prefilter
RWTexture2D<float2> history_output : register(u1);

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)] void temporal_pass_cs(uint2 dtid : SV_DispatchThreadID) {
   // tex0 = the main pass's AO term (.x) and packed edges (.y); ao_term_and_edges = the same for the denoise
   const uint2 size = uint2(round(1.0 / VIEWPORT_PIXEL_SIZE));
   if (any(dtid >= size))
      return;
   const float2 current = tex0.Load(int3(dtid, 0)).xy;
   const float source_depth = scene_device_depth.Load(int3(XE_GTAO_DEPTH_LOAD_COORD(dtid), 0));
   const float device_depth = XE_GTAO_TEMPORAL_DEVICE_DEPTH(source_depth);
   const float view_z = XeGTAO_ScreenSpaceToViewSpaceDepth(source_depth);

   // Fed (NDC, depth, 1), the clip space w of the reprojection is the previous view Z over the current one
   const float4 current_clip = float4((dtid + 0.5) * VIEWPORT_PIXEL_SIZE * float2(2.0, -2.0) + float2(-1.0, 1.0) - JitterNDCRT, device_depth, 1.0);
   const float4 previous_clip = mul(ReprojectionRT, current_clip);
   const float expected_z = view_z * previous_clip.w;
   float2 motion = motion_vectors.Load(int3(XE_GTAO_DEPTH_LOAD_COORD(dtid), 0));
   if (!(motion.x < MOTION_VECTOR_MARKER))
   {
      motion = CameraMotion(dtid, size, ReprojectionRT, JitterNDCRT, device_depth);
   }

   const float2 previous_pixel = (dtid + 0.5) + motion * size - 0.5;
   const int2 base = int2(floor(previous_pixel));
   const float2 f = previous_pixel - base;
   float history_ao = 0.0;
   float weight_sum = 0.0;
   [unroll] for (uint i = 0; i < 4; i++)
   {
      const int2 tap = base + int2(i & 1, i >> 1);
      const float2 bilinear = float2(((i & 1) ? f.x : (1.0 - f.x)), ((i >> 1) ? f.y : (1.0 - f.y)));
      const float2 previous = history.Load(int3(tap, 0));
      const bool inside = all(tap >= 0) && all(tap < int2(size));
      const float weight = ((inside && abs(previous.y - expected_z) <= kXeGTAO_HistoryDepthTolerance * expected_z) ? (bilinear.x * bilinear.y) : 0.0);
      history_ao += previous.x * weight;
      weight_sum += weight;
   }

   float ao = current.x;
   if (weight_sum > 0.25)
   {
      float neighborhood_min = current.x, neighborhood_max = current.x;
      [unroll] for (int y = -1; y <= 1; y++)
      {
         [unroll] for (int x = -1; x <= 1; x++)
         {
            if (x == 0 && y == 0)
               continue;
            const float neighbor = tex0.Load(int3(clamp(int2(dtid) + int2(x, y), 0, int2(size) - 1), 0)).x;
            neighborhood_min = min(neighborhood_min, neighbor);
            neighborhood_max = max(neighborhood_max, neighbor);
         }
      }
      ao = lerp(current.x, clamp(history_ao / weight_sum, neighborhood_min, neighborhood_max), HistoryWeightRT);
   }
   ao_term_and_edges[dtid] = float2(ao, current.y);
   history_output[dtid] = float2(ao, view_z);
}

#endif // LUMA_XE_GTAO_TEMPORAL_HLSL
