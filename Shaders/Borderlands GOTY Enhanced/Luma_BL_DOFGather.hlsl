// DLSS / FSR: the DOF/Bloom gather (0xF0D8D818), from the upscaled scene.
// Its DOF blur amount comes from the max depth of each 4x4 block, and the depth is jittered: a thin object near a block edge moves in
// and out of it every frame, and the block flips between sharp and blurred after the upscaler (a flickering halo around wires against
// the sky; any max over the block flips like that). Here the amount is the mean over the block of per pixel amounts accumulated over
// frames like the upscaler accumulates color: each pixel's amount (the gather's formula, same constants) is blended into its history,
// reprojected with the motion vectors and clamped to the current 3x3 range.

// The gather, as the game's: 4x4 scene texels per output texel (2x2 per thread, 2x2 threads through group memory), the mean of its
// 16 pixels' history amounts for the DOF blur amount (alpha), plus the bloom term; written one texel in from the border.
cbuffer GatherGlobals : register(b0)
{
   float4 gather_constants[4]; // [0] focus distance, 1 / range, exponent; [1] near / far max blur; [2].xy texel offset (ints); [3] bloom threshold, scale, range
};
cbuffer PSOffsetConstants : register(b2)
{
   float4 offset_constants[7]; // [6].zw: MinZ_MaxZRatio (device z to 1 / view z)
};

Texture2D<float4> scene : register(t0);
Texture2D<float4> depth : register(t1);
RWTexture2D<float4> gathered : register(u0);

groupshared float4 group_texels[64];

cbuffer DOFHistory : register(b5)
{
   float history_weight; // Weight of the current frame, 1 = no history
};

Texture2D<float2> motion_vectors : register(t2); // UV, previous minus current
Texture2D<float> previous_history : register(t3);
RWTexture2D<float> history : register(u1);
SamplerState linear_sampler : register(s0);

// The gather's blur amount for a device depth
float BlurAmount(float device_depth)
{
   const float view_depth = 1.0 / max(device_depth * offset_constants[6].z - offset_constants[6].w, 0.0);
   const float focus_offset = view_depth - gather_constants[0].x;
   const float max_blur = focus_offset < 0.0 ? gather_constants[1].x : gather_constants[1].y;
   return min(exp2(log2(max(saturate(abs(focus_offset) * gather_constants[0].y), 0.0001)) * gather_constants[0].z), max_blur);
}

// The group's 16x16 pixels and a 1 pixel border (edge clamped), each amount computed once
groupshared float group_amounts[18 * 18];

// A pixel's amount blended into its history; stored, and returned as stored (R16F)
float History(int2 pixel, int2 tile_origin, int2 size)
{
   const int2 tile_pixel = pixel - tile_origin;
   const float current = group_amounts[tile_pixel.y * 18 + tile_pixel.x];
   float result = current;
   const float2 uv = (float2(pixel) + 0.5) / size + motion_vectors.Load(int3(pixel, 0));
   if (history_weight < 1.0 && all(uv >= 0.0) && all(uv <= 1.0))
   {
      float lowest = current, highest = current;
      [unroll] for (int y = -1; y <= 1; y++)
      {
         [unroll] for (int x = -1; x <= 1; x++)
         {
            const float neighbor = group_amounts[(tile_pixel.y + y) * 18 + tile_pixel.x + x];
            lowest = min(lowest, neighbor);
            highest = max(highest, neighbor);
         }
      }
      const float previous = clamp(previous_history.SampleLevel(linear_sampler, uv, 0), lowest, highest);
      result = lerp(previous, current, history_weight);
   }
   history[pixel] = result;
   return f16tof32(f32tof16(result));
}

[numthreads(8, 8, 1)] void gather_cs(uint3 id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex) {
   const int2 pixel = int2(id.xy) * 2 + asint(gather_constants[2].xy);
   const float3 color = scene.Load(int3(pixel, 0)).rgb + scene.Load(int3(pixel + int2(1, 0), 0)).rgb + scene.Load(int3(pixel + int2(0, 1), 0)).rgb + scene.Load(int3(pixel + int2(1, 1), 0)).rgb;
   uint2 history_size;
   history.GetDimensions(history_size.x, history_size.y);
   const int2 size = int2(history_size);
   const int2 tile_origin = int2(group_id.xy) * 16 + asint(gather_constants[2].xy) - 1;
   for (uint i = group_index; i < 18 * 18; i += 8 * 8)
      group_amounts[i] = BlurAmount(depth.Load(int3(clamp(tile_origin + int2(i % 18, i / 18), 0, size - 1), 0)).x);
   GroupMemoryBarrierWithGroupSync();
   // Outside the scene: 0, as a load there
   float depth_term = 0.0;
   [unroll] for (int k = 0; k < 4; k++)
   {
      const int2 texel = pixel + int2(k & 1, k >> 1);
      if (all(texel >= 0) && all(texel < size))
         depth_term += History(texel, tile_origin, size);
   }
   group_texels[group_index] = float4(color, depth_term);
   GroupMemoryBarrierWithGroupSync();
   if (any(group_thread_id.xy & 1))
      return;

   const float4 right = group_texels[group_index + 1];
   const float4 down = group_texels[group_index + 8];
   const float4 diagonal = group_texels[group_index + 9];
   const float3 average = (right.rgb + down.rgb + diagonal.rgb + color) * 0.0625;
   const float blur = (depth_term + right.w + down.w + diagonal.w) * 0.0625;

   const float3 clamped = min(average, 65536.0);
   float bloom = saturate((dot(clamped, float3(0.3, 0.59, 0.11)) * gather_constants[3].y - gather_constants[3].x) / gather_constants[3].z);
   bloom = log2(bloom * 15.0 + 1.0) * 0.25;
   gathered[(id.xy >> 1) + 1] = float4(blur * average + clamped * bloom, blur);
}
