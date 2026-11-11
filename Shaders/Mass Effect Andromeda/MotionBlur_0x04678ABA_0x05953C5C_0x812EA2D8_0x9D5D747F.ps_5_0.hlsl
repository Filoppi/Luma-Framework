// The game's motion blur gather (PS, full screen triangle VS 0x712F9EAE), decompiled, for its permutations with 20 px tiles. They
// share this body, and each hash selects its axes below:
//   MEA_MB_STEP_PER_TAP  1 = the tile velocity divided by the tap count (and an unsigned tile jitter, unshifted dither)
//   MEA_MB_KARIS         1 = taps weighted by 1 / (1 + max rgb), undone on the result
// Luma: the tile size (and the speed clamp, 2 tiles) comes from "cb0[0].y" as in the game's other permutations, not from a literal
// 20. When upscaling, the motion blur runs at the output size with the engine's tile for the output width (see
// "DrawOutputSizedMotionBlurPass").

#if _04678ABA || _9D5D747F
#define MEA_MB_STEP_PER_TAP 1
#else
#define MEA_MB_STEP_PER_TAP 0
#endif

#if _05953C5C || _9D5D747F
#define MEA_MB_KARIS 1
#else
#define MEA_MB_KARIS 0
#endif

Texture2D<float4> depthTexture : register(t0); // .x
Texture2D<float4> colorTexture : register(t1);
Texture2D<float4> motionTexture : register(t2);   // .xy, UV units
Texture2D<float4> tileTexture : register(t3);     // .xy max velocity (px), .z min speed, .w max speed
Texture2D<float4> velocityTexture : register(t4); // .x speed, .y depth, at the position >> cb0[1].w

cbuffer cb0 : register(b0)
{
   // [0] = (max taps u32, tile px u32, debug mode u32, motion scale), [1] = (taps per px, min speed, noise seed u32, velocity
   // shift u32), [2] = (W, H, tiles X, tiles Y), [3] = (.x width scale, .y position x scale)
   float4 cb0[4];
}

float3 DebugRamp(float x)
{
   float3 r = max((abs(x + float3(-1.0, -0.5, 0.0)) - 0.733333) * -1.363636, 0.0);
   return r * r * (3.0 - 2.0 * r);
}

float3 KarisWeight(float3 color)
{
#if MEA_MB_KARIS
   return color / (1.0 + max(color.r, max(color.g, color.b)));
#else
   return color;
#endif
}

float3 KarisUnweight(float3 color)
{
#if MEA_MB_KARIS
   return color / (1.0 - max(color.r, max(color.g, color.b)));
#else
   return color;
#endif
}

float4 main(float4 position : SV_Position) : SV_Target
{
#define MEA_MB_COLOR_TAP(pos) colorTexture.Load(int3(int2(pos), 0)).rgb

   const uint mode = asuint(cb0[0].z);
   const uint maxTaps = asuint(cb0[0].x);

   // A per pixel tile jitter, in tiles
   float2 tileJitter = 0.0;
   if (mode == 0)
   {
      uint2 hash = uint2(position.xy) + asuint(cb0[1].z);
      hash = hash.x * uint2(0x0f4559d5, 0x2e48eddb) + hash.y;
      hash *= 1025;
      hash ^= hash >> 6;
      hash *= 9;
      hash &= 127;
      tileJitter = (float2(hash) * 0.007874 - 0.5) * 0.75;
#if !MEA_MB_STEP_PER_TAP
      tileJitter = -tileJitter;
#endif
   }
   const float2 scaledPosition = float2(position.x * cb0[3].y, position.y);
   const float tileSize = float(asuint(cb0[0].y));
   const float maxSpeed = tileSize * 2.0;
   float2 tileCoord = scaledPosition / tileSize + tileJitter;
   tileCoord = min(max(tileCoord, 0.0), cb0[2].zw - 1.0);
   const float4 tile = tileTexture.Load(int3(int2(tileCoord), 0));

   if (mode == 1)
   {
      return float4(DebugRamp(saturate(tile.z * 100.0)), 1.0);
   }

   float3 center = colorTexture.Load(int3(int2(position.xy), 0)).rgb;
   if (tile.w <= 1.0)
   {
      const float3 tint = (mode == 3 ? float3(0.0, 0.239294, 1.0) : 1.0);
      return float4(center * tint, 1.0);
   }

   const uint taps = min(max(uint(tile.w * cb0[1].x) << 1, 2u), maxTaps);
   if (mode == 2)
   {
      return float4(DebugRamp(float(taps) / float(maxTaps)), 1.0);
   }
   center = KarisWeight(center);

#if MEA_MB_STEP_PER_TAP
   const uint2 parity = uint2(position.xy) & 1;
   const float2 tileVelocity = tile.xy / float(taps);
#else
   const uint2 parity = uint2(position.xy + float2(1.0, 0.0)) & 1;
   const float2 tileVelocity = tile.xy;
#endif
   const float dither = (float(parity.x) * 0.5 - 0.25) * (float(parity.y) * 2.0 - 1.0);
   const float tapWeight = 1.0 / float(taps + 1);
   const float2 tapStep = tileVelocity * tapWeight * 2.0;
   const float2 tapStart = tileVelocity * ((dither * tapWeight + tapWeight) * 2.0 - 1.0) + position.xy;
   const uint halfTaps = taps >> 1;
   const float4 bounds = float4(cb0[3].x, 1.0, cb0[3].x, 1.0) * cb0[2].xyxy;

   float3 sum = 0.0;
   float weightSum = 0.0;
   float inBoundsCount = 0.0;

   // Uniform tile velocity: a plain box gather
   if (tile.z < cb0[1].y * 0.0025)
   {
      for (uint i = 0; i < halfTaps; i++)
      {
         const float4 pair = float4(float(int(halfTaps - i) - 1).xx, float(halfTaps + i).xx) * tapStep.xyxy + tapStart.xyxy;
         const float3 color0 = KarisWeight(MEA_MB_COLOR_TAP(pair.xy));
         const float3 color1 = KarisWeight(MEA_MB_COLOR_TAP(pair.zw));
         const bool4 inside = (pair >= 0.0) && (pair < bounds);
         const float2 inBounds = float2(inside.x && inside.y, inside.z && inside.w);
         inBoundsCount += inBounds.x + inBounds.y;
         weightSum += inBounds.x + inBounds.y;
         sum += color0 * inBounds.x + color1 * inBounds.y;
      }
      const float normalization = 1.0 / max(inBoundsCount, 0.00001);
      const float3 blurred = KarisUnweight(center * (1.0 - weightSum * normalization) + sum * normalization);
      const float3 tint = (mode == 3 ? float3(0.239294, 1.0, 0.239294) : 1.0);
      return float4(blurred * tint, 1.0);
   }

   // Depth and speed aware gather
   const int2 centerPixel = int2(scaledPosition);
   const float centerDepth = depthTexture.Load(int3(centerPixel, 0)).x;
   const float2 centerMotion = motionTexture.Load(int3(centerPixel, 0)).xy * cb0[0].w * cb0[2].xy * float2(0.25, -0.25);
   const float centerSpeed = min(maxSpeed, length(centerMotion));
   const float distanceScale = 2.0 * tile.w * tapWeight;
   const float centerIndex = float(halfTaps) - 0.5;
   const uint velocityShift = asuint(cb0[1].w);
   for (uint i = 0; i < halfTaps; i++)
   {
      const float index0 = float(int(halfTaps - i) - 1);
      const float index1 = float(halfTaps + i);
      const float4 pair = float4(index0.xx, index1.xx) * tapStep.xyxy + tapStart.xyxy;
      const float3 color0 = KarisWeight(MEA_MB_COLOR_TAP(pair.xy));
      const float3 color1 = KarisWeight(MEA_MB_COLOR_TAP(pair.zw));
      const int4 velocityPixels = int4(int(pair.x * cb0[3].y), int(pair.y), int(pair.z * cb0[3].y), int(pair.w));
      const float2 velocity0 = velocityTexture.Load(int3(velocityPixels.xy >> velocityShift, 0)).xy;
      const float2 velocity1 = velocityTexture.Load(int3(velocityPixels.zw >> velocityShift, 0)).xy;

      const float2 tapDistance = max(abs(float2(index0, index1) - centerIndex) * distanceScale - 1.0, 0.0);
      const float2 depthOrder0 = saturate(float2(velocity0.y - centerDepth, centerDepth - velocity0.y) + 0.5);
      const float2 depthOrder1 = saturate(float2(velocity1.y - centerDepth, centerDepth - velocity1.y) + 0.5);
      const float weight0 = dot(depthOrder0, saturate(float2(centerSpeed, velocity0.x) - tapDistance.x));
      const float weight1 = dot(depthOrder1, saturate(float2(centerSpeed, velocity1.x) - tapDistance.y));
      // Mirrored weights: the tap pair takes the weight of the closer and faster tap
      const bool closer1 = velocity1.y < velocity0.y;
      const bool faster1 = velocity0.x < velocity1.x;
      const float mirrored0 = (closer1 && faster1) ? weight1 : weight0;
      const float mirrored1 = (closer1 || faster1) ? weight1 : weight0;

      const bool4 inside = (pair >= 0.0) && (pair < bounds);
      const float2 inBounds = float2(inside.x && inside.y, inside.z && inside.w);
      inBoundsCount += inBounds.x + inBounds.y;
      weightSum += mirrored0 * inBounds.x + mirrored1 * inBounds.y;
      sum += color0 * (inBounds.x * mirrored0) + color1 * (inBounds.y * mirrored1);
   }
   const float normalization = 1.0 / max(inBoundsCount, 0.00001);
   const float3 blurred = KarisUnweight(center * (1.0 - weightSum * normalization) + sum * normalization);
   const float3 tint = (mode == 3 ? float3(1.0, 0.239294, 0.0) : 1.0);
   return float4(blurred * tint, 1.0);
}
