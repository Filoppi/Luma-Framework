// FFXV half resolution ambient occlusion pass (Scalable Ambient Obscurance, with its own temporal reprojection).
// The occlusion estimator is swapped with GTVBAO (ground truth visibility bitmask AO, ported from the FF7 Remake mod),
// and the game's temporal accumulation is replaced with a stricter one: the noise (FAST noise) changes every frame so the AO is accumulated over time in the history
// (the previous output of this pass, like the game did), with a depth aware reprojection that drops the history where the surroundings changed.
// This pass outputs the temporally accumulated AO in .x, and the view normal and the history length like the game's AO pass did (its two bilateral blur passes, which are left as they are, read the normal).

// The previous depth (t4), the velocity (t3) and the previous output (t5) are the game's own bindings of the original pass, t6 (FAST noise) and t7 (foliage mask) are bound by the mod.
// The game inputs (linear depth + mips, g-buffer normals, cbuffer) are reused as they are.

#include "Includes/Common.hlsl"

// The estimator is picked at runtime by the "Enable VBAO" setting (GTVBAO, or the game's SAO), the temporal accumulation by the "FFXV_AO_TEMPORAL" shader define.

// Temporal accumulation of the AO (reprojected with the game's velocity or the camera motion, with depth and moving occluder rejection)
#ifndef AO_TEMPORAL
#ifdef FFXV_AO_TEMPORAL
#define AO_TEMPORAL FFXV_AO_TEMPORAL
#else
#define AO_TEMPORAL 1
#endif
#endif

// Max history length, the effective sample count of the accumulation (the weight of the current AO is 1 / length)
#define TEMPORAL_MAX_FRAMES 8.0f // Max 15: the length is stored in 4 bits
// Min history length (in samples) of the pixels that are moving fast or have a changed neighbourhood
#define TEMPORAL_MIN_FRAMES 3.0f
// Depth rejection tolerance: relative to the view depth, plus this multiple of the local depth slope (measured at half resolution)
#define TEMPORAL_DEPTH_REL_TOLERANCE 0.01f
#define TEMPORAL_DEPTH_SLOPE_TOLERANCE 1.5f
// How much the pixel speed (in full res pixels per frame) shortens the history, like the FF7R one: 1/80 pixels per frame brings it to the min length
#define TEMPORAL_SPEED_SCALE 0.0125f
// Depth mismatch tolerated around the pixel before its history gets shortened (reprojection error): relative to the depth, plus a fraction of the AO reach in world units
#define TEMPORAL_FOOTPRINT_REL_TOLERANCE 0.04f
#define TEMPORAL_FOOTPRINT_RADIUS_TOLERANCE 0.1f

#define VBAO_STEP_COUNT 16
#define VBAO_SLICE_COUNT 1
// How far the rays reach, in AO pixels (half res, like VBAO_RAY_WIDTH_PIXELS of the FF7R one, which is 512 at its 3777 wide full res AO). 130 covers the same part of the screen at a 1920 wide render (960 wide AO). It doesn't depend on the game's AO radius.
#define VBAO_RAY_WIDTH_PIXELS 130.0f
// Min thickness of the depth buffer "surfaces" in world units (meters), like the FF7R one (20 cm). Thin: the objects occlude only a little behind their silhouette, which avoids the halos around them. It grows with the depth, see the "VBAO Thickness Scale" setting.
#define VBAO_THICKNESS_WORLD 0.2f
// The normal offset of the ray origin: absolute (world units) and relative to the view depth (the FF7R one is 0.0005 + 0.025 * the device depth, practically nothing)
#define VBAO_NORMAL_BIAS 0.0005f
#define VBAO_NORMAL_BIAS_REL 0.0f
// > 1 darkens (like the game's g_Contrast did for SAO)
#define VBAO_POWER 1.0f

cbuffer SAOParams : register(b0)
{
  float4 g_ViewMatrix[4];
  float4 g_MotionMatrix[4];
  float4 g_ViewFarPlaneUVOffset;
  float4 g_ViewFarPlaneUVOffsetInverse;
  float4 g_ProjParams;
  float4 g_ProjParamsInverse;
  float g_Intensity;
  float g_Contrast;
  float g_Bias;
  float g_SampleOffset;
  float g_OcclusionMax;
  float g_FalloffRadius;
  float g_SpiralTurns;
  float g_ScreenMipRadiusFactor;
  float g_FarPlane;
  float g_FarPlaneInverse;
  float g_NearFalloffThreshold;
  float g_NearFalloffThresholdInverseSqr;
  float g_MaxScreenRadius;
  float g_RandomOffset;
  float2 g_UVOffset;
  float4 g_screenDims;
  float g_pixelVelocityThresholdInv;
  float g_occluderVelocityThresholdSq;
  float g_NearPlaneEpsilon;
  float g_ExpFactorFarPlaneZEpsilon;
  float g_OccluderEpsilon;
  int g_NumSamples;
}

SamplerState g_SamplePoint : register(s0);
SamplerState g_SampleLinear : register(s1);
SamplerState g_SamplePointMipLinearBorder : register(s2);
SamplerState g_SamplePointBorder : register(s3);
Texture2D<float4> g_DepthTex : register(t0); // Linear depth (view depth / far plane), 5 mips, 1 = sky
Texture2D<float4> g_NormalTex : register(t1);
Texture2D<float4> g_SpecularTex : register(t2);
Texture2D<float4> g_VelocityTex : register(t3); // Full res, xy: UV motion (x offset by +-4 when flagged), y == 1 means no velocity (static, use the camera motion)
Texture2D<float4> g_PrevDepthTex : register(t4); // Half res linear depth of the previous frame
Texture2D<float4> g_PrevAOTex : register(t5); // This pass' previous output (half res): .x = the accumulated AO, .w = the history length in the low 4 bits

// FAST noise (Gaussian filtered, uniform distribution, 4 independent values per texel): a 128x128 tile of 64 slices that is well distributed in space and time.
// Bound by the mod (t6 is free in the game's signature for this pass), see "shader_hashes_attach_fast_noise".
Texture2DArray<float4> FastNoise : register(t6);

// Foliage mask (AO resolution, 1 = foliage), built from the stencil of the G-buffer by Luma_FFXV_FoliageMask and bound by the mod, see "shader_hashes_attach_fast_noise".
// Unbound (mask disabled, or the stencil wasn't found) it reads 0: no foliage.
Texture2D<uint> FoliageMask : register(t7);

bool IsFoliage(float2 uv)
{
  const int2 targetSize = int2(g_ProjParams.zw * 0.5f); // The AO target is half of the render resolution
  return FoliageMask.Load(int3(clamp(int2(uv * float2(targetSize)), int2(0, 0), targetSize - 1), 0)) != 0u;
}

// 4 random values per pixel and frame: x = slice rotation, y and w = the step offset of the two sides, z = the arc offset
float4 VBAORandom(uint2 pixel, uint frameIndex)
{
  return FastNoise.Load(int4(int2(pixel & 127u), int(frameIndex & 63u), 0));
}

float3 ViewRay(float2 uv) // View space position of the pixel at depth 1 (the far plane)
{
  return float3(uv * g_ViewFarPlaneUVOffset.zw + g_ViewFarPlaneUVOffset.xy, g_FarPlane);
}

float3 ViewPosition(float2 uv, float depth)
{
  return ViewRay(uv) * depth;
}

// The biggest view depth difference to the 4 neighbours (half res pixels, world units): a measure of the local slope, for the tolerance of the history depth test.
float CalculateDepthSlope(float2 uv, float centerDepth)
{
  const float2 pixelToUV = 2.0f / g_ProjParams.zw; // The AO target is half of the render resolution
  float centerZ = centerDepth * g_FarPlane;
  float4 neighbourDeltas = float4(
    g_DepthTex.SampleLevel(g_SamplePoint, uv - float2(pixelToUV.x, 0.0f), 0).x,
    g_DepthTex.SampleLevel(g_SamplePoint, uv + float2(pixelToUV.x, 0.0f), 0).x,
    g_DepthTex.SampleLevel(g_SamplePoint, uv - float2(0.0f, pixelToUV.y), 0).x,
    g_DepthTex.SampleLevel(g_SamplePoint, uv + float2(0.0f, pixelToUV.y), 0).x) * g_FarPlane - centerZ;
  return max(max(abs(neighbourDeltas.x), abs(neighbourDeltas.y)), max(abs(neighbourDeltas.z), abs(neighbourDeltas.w)));
}


// GTVBAO shared sample ray construction and interval insertion.
void AddOccludedInterval(inout uint occupiedBits, float2 horizonCosines, float halfSide, float baseHorizon, float arcJitter)
{
  const uint fullMask = 0xffffffffu;
  float2 horizon01 = saturate(baseHorizon + halfSide - halfSide * horizonCosines + arcJitter);
  uint2 bins = (uint2)floor(horizon01 * 32.0f);
  uint startMask = bins.x < 32u ? (fullMask << bins.x) : 0u;
  uint endMask = bins.y != 0u ? (fullMask >> (32u - bins.y)) : 0u;
  occupiedBits |= startMask & endMask;
}

// uv: of the pixel, viewPosition: of the pixel (before the normal offset), rayWidthPixels in AO target pixels, thickness in world units.
// skipFoliage: the samples flagged as foliage in the mask don't occlude (plants are not solid occluders, unless the pixel is foliage itself).
float CalculateVBAO(float2 uv, float3 centerPosition, float3 viewNormal, float4 random, float rayWidthPixels, float thickness, bool skipFoliage)
{
  const float inverseStepCount = 1.0f / float(VBAO_STEP_COUNT);
  const float inverseSliceCount = 1.0f / float(VBAO_SLICE_COUNT);
  const float2 targetSize = g_ProjParams.zw * 0.5f; // The AO target is half of the render resolution
  const float2 pixelToUV = 1.0f / targetSize;

  float3 normal = normalize(viewNormal);
  float centerZ = centerPosition.z;
  // View rays at z = 1, view space is left handed (x right, y up, z forward) as in the FF7R version
  float3 rayOrigin = float3(uv * g_ViewFarPlaneUVOffset.zw + g_ViewFarPlaneUVOffset.xy, g_FarPlane) * (1.0f / g_FarPlane);
  float3 rayDx = float3(g_ViewFarPlaneUVOffset.z * pixelToUV.x, 0.0f, 0.0f) * (1.0f / g_FarPlane);
  float3 rayDy = float3(0.0f, g_ViewFarPlaneUVOffset.w * pixelToUV.y, 0.0f) * (1.0f / g_FarPlane);
  float3 viewPosition = centerPosition + normal * (VBAO_NORMAL_BIAS + VBAO_NORMAL_BIAS_REL * centerZ);
  float3 viewDirection = normalize(-viewPosition);
  float accumulatedOcclusion = 0.0f;

  [loop]
  for (uint sliceIndex = 0u; sliceIndex < VBAO_SLICE_COUNT; ++sliceIndex)
  {
    float sliceAngle = (float(sliceIndex) + random.x) * inverseSliceCount * 3.14159265359f;
    float sinSlice, cosSlice;
    sincos(sliceAngle, sinSlice, cosSlice);
    float2 screenSliceDirection = float2(cosSlice, sinSlice);
    float3 rayStep = rayDx * cosSlice + rayDy * sinSlice;
    float3 slicePlaneNormal = normalize(cross(rayOrigin, -rayStep));
    float normalPlaneDot = dot(normal, slicePlaneNormal);
    float3 projectedNormal = normal - slicePlaneNormal * normalPlaneDot;
    float projectedNormalLengthSquared = max(1.0f - normalPlaneDot * normalPlaneDot, 0.0f);
    if (projectedNormalLengthSquared < 1.0e-8f)
      continue;

    float3 tangent = cross(slicePlaneNormal, normal);
    float inverseProjectedNormalLength = rsqrt(projectedNormalLengthSquared);
    float sinNormal = dot(tangent, viewDirection) * inverseProjectedNormalLength;
    float baseHorizon = 0.5f + 0.5f * sinNormal;
    float arcJitter = frac(random.z + float(sliceIndex) * 0.569840296f) / 32.0f;
    float negativeSideJitter = frac(random.y + float(sliceIndex) * 0.754877666f);
    float positiveSideJitter = frac(random.w + float(sliceIndex) * 0.569840296f);
    uint occupiedBits = 0u;

    [unroll]
    for (int side = -1; side <= 1; side += 2)
    {
      float sideValue = float(side);
      float2 screenDirection = screenSliceDirection * sideValue;
      float3 viewRayStep = rayStep * sideValue;
      float sideStepJitter = side < 0 ? negativeSideJitter : positiveSideJitter;
      [loop]
      for (uint stepIndex = 0u; stepIndex < VBAO_STEP_COUNT; ++stepIndex)
      {
        float u = (float(stepIndex) + sideStepJitter) * inverseStepCount;
        float sampleDistance = 1.0f + (rayWidthPixels - 1.0f) * (u * u);
        float2 sampleUV = uv + screenDirection * sampleDistance * pixelToUV;
        if (any(sampleUV < 0.0f) || any(sampleUV > 1.0f))
          break;
        if (skipFoliage && IsFoliage(sampleUV))
          continue;
        // The full res depth, no mips: like the FF7R one. The depth mips (min filtered) would grow the silhouettes of the thin objects (legs) and darken the ground around them.
        float sampleDepth = g_DepthTex.SampleLevel(g_SamplePoint, sampleUV, 0).x;
        float sampleZ = sampleDepth * g_FarPlane;
        float3 sampleRay = rayOrigin + viewRayStep * sampleDistance;
        float3 frontDelta = sampleRay * sampleZ - viewPosition;
        float3 backDelta = frontDelta + sampleRay * thickness;
        float2 horizons = float2(
          dot(frontDelta, viewDirection) * rsqrt(max(dot(frontDelta, frontDelta), 1.0e-8f)),
          dot(backDelta, viewDirection) * rsqrt(max(dot(backDelta, backDelta), 1.0e-8f)));
        horizons = side >= 0 ? horizons.xy : horizons.yx;
        AddOccludedInterval(occupiedBits, horizons, sideValue * 0.5f, baseHorizon, arcJitter);
        if (occupiedBits == 0xffffffffu)
          break;
      }
      if (occupiedBits == 0xffffffffu)
        break;
    }
    accumulatedOcclusion += float(countbits(occupiedBits)) / 32.0f;
  }
  return saturate(1.0f - accumulatedOcclusion * inverseSliceCount);
}


#if AO_TEMPORAL

// The game's velocity encoding: x is offset by +-4 when flagged (y is the flag, 1 = no velocity)
float2 DecodeVelocity(float2 raw, out bool hasVelocity)
{
  hasVelocity = raw.y != 1.0f;
  if (hasVelocity && abs(raw.x) >= 4.0f)
    raw.x -= 4.0f * sign(raw.x);
  return raw;
}

float PixelSpeed01(float2 velocity) // 0-1, 1 at 80 full res pixels per frame
{
  return saturate(length(g_screenDims.xy * velocity) * TEMPORAL_SPEED_SCALE);
}

// 0-1: how much the area around the pixel (what the AO rays look at) is static: 12 taps (3 rings of 4) are reprojected with the camera motion and their depth is compared with the previous frame one.
// Moving objects and disocclusions near the pixel don't match, whether they have a velocity or not, and that is what leaves a trailing AO behind moving objects.
float FootprintStability(float2 v1, float rayWidthPixels, float worldReach)
{
  const float2 pixelToUV = 2.0f / g_ProjParams.zw;
  float worstMismatch = 0.0f;
  [unroll]
  for (uint i = 0u; i < 12u; ++i)
  {
    float sinAngle, cosAngle;
    sincos((float(i & 3u) + 0.37f * float(i >> 2u)) * 1.57079633f, sinAngle, cosAngle);
    float2 tapUV = v1 + float2(cosAngle, sinAngle) * (rayWidthPixels * ((i >> 2u) == 0u ? 0.2f : ((i >> 2u) == 1u ? 0.5f : 0.85f))) * pixelToUV;
    if (any(tapUV <= 0.0f) || any(tapUV >= 1.0f))
      continue;
    float tapDepth = g_DepthTex.SampleLevel(g_SamplePoint, tapUV, 1).x; // Mip 1 is the half res depth, like the previous depth texture
    if (tapDepth >= 1.0f)
      continue;
    float tapZ = tapDepth * g_FarPlane;
    float3 current = float3((tapUV * g_ViewFarPlaneUVOffset.zw + g_ViewFarPlaneUVOffset.xy) * tapDepth, -tapZ);
    float3 prevPosition = g_MotionMatrix[0].xyz * current.x + g_MotionMatrix[1].xyz * current.y + g_MotionMatrix[2].xyz * current.z + g_MotionMatrix[3].xyz;
    float expectedDepth = g_FarPlaneInverse * -prevPosition.z;
    float2 prevTapUV = (prevPosition.xy / max(expectedDepth, 1.0e-6f) - g_ViewFarPlaneUVOffset.xy) * g_ViewFarPlaneUVOffsetInverse.zw;
    if (any(prevTapUV <= 0.0f) || any(prevTapUV >= 1.0f))
      continue;
    // The closest of the 4 texels around the position: the depth buffer is jittered, so on the silhouettes the previous depth is shifted by up to a pixel
    float4 previousDepths = g_PrevDepthTex.GatherRed(g_SamplePoint, prevTapUV);
    float4 errors = abs(previousDepths - expectedDepth) * g_FarPlane;
    float error = min(min(errors.x, errors.y), min(errors.z, errors.w));
    float tolerance = TEMPORAL_FOOTPRINT_REL_TOLERANCE * tapZ + TEMPORAL_FOOTPRINT_RADIUS_TOLERANCE * worldReach;
    worstMismatch += saturate(error / tolerance - 1.0f) * (1.0f / 12.0f);
  }
  // Average of the taps (x2): a single tap on a thin silhouette (foliage) should not drop the history, but a moving object covering the area does
  return 1.0f - saturate(worstMismatch * 2.0f);
}

// Reprojects the accumulated AO of the previous frame (this pass' previous output). v1: the pass' UV.
// Like the FF7R one, the history is reprojected with the velocity (dynamic pixels) or the camera motion (static pixels), and it's discarded or shortened by:
// leaving the screen, depth mismatch, no previous history, pixel speed and moving objects within the AO radius.
// Returns the history AO and its length in samples (0 = no valid history), and the max length the history of this pixel should have: shortened by the pixel speed, and by moving objects around it.
float TemporalReproject(float2 v1, float depth, float depthSlope, float rayWidthPixels, float worldReach, out float prevLength, out float maxLength)
{
  prevLength = 0.0f;
  maxLength = TEMPORAL_MAX_FRAMES;
  float viewZ = depth * g_FarPlane;

  bool hasVelocity;
  float2 velocity = DecodeVelocity(g_VelocityTex.SampleLevel(g_SamplePoint, v1, 0).xy, hasVelocity);

  float2 prevUV;
  float expectedDepth; // Normalized, in the previous frame
  if (hasVelocity)
  {
    prevUV = v1 + velocity;
    expectedDepth = depth;
  }
  else
  {
    // Camera motion
    float3 current = float3((v1 * g_ViewFarPlaneUVOffset.zw + g_ViewFarPlaneUVOffset.xy) * depth, -viewZ);
    float3 prevPosition = g_MotionMatrix[0].xyz * current.x + g_MotionMatrix[1].xyz * current.y + g_MotionMatrix[2].xyz * current.z + g_MotionMatrix[3].xyz;
    expectedDepth = g_FarPlaneInverse * -prevPosition.z;
    if (expectedDepth <= 0.0f)
      return 0.0f;
    prevUV = (prevPosition.xy / expectedDepth - g_ViewFarPlaneUVOffset.xy) * g_ViewFarPlaneUVOffsetInverse.zw;
  }

  if (any(prevUV <= 0.0f) || any(prevUV >= 1.0f))
    return 0.0f;

  // Depth aware bilinear fetch of the history (a plain bilinear one bleeds the AO of the object across its silhouette, and drags it along when it moves).
  // The 4 history texels are weighted by how well their depth matches the expected one.
  float depthTolerance = TEMPORAL_DEPTH_REL_TOLERANCE * expectedDepth * g_FarPlane + TEMPORAL_DEPTH_SLOPE_TOLERANCE * depthSlope;
  const float2 targetSize = g_ProjParams.zw * 0.5f;
  float2 prevTexel = prevUV * targetSize - 0.5f;
  int2 baseTexel = (int2)floor(prevTexel);
  float2 texelFraction = prevTexel - float2(baseTexel);
  float historyAO = 0.0f;
  float historyWeight = 0.0f;
  [unroll]
  for (uint i = 0u; i < 4u; ++i)
  {
    int2 offset = int2(i & 1u, i >> 1u);
    int2 texel = clamp(baseTexel + offset, int2(0, 0), int2(targetSize) - 1);
    float2 bilinear = lerp(1.0f - texelFraction, texelFraction, float2(offset));
    float texelDepth = g_PrevDepthTex.Load(int3(texel, 0)).x;
    float depthError = abs(texelDepth - expectedDepth) * g_FarPlane;
    float weight = bilinear.x * bilinear.y * saturate(2.0f - 2.0f * depthError / max(depthTolerance, 1.0e-6f));
    float4 texelData = g_PrevAOTex.Load(int3(texel, 0));
    float2 texelHistory = float2(texelData.x, float(uint(texelData.w * 255.0f + 0.5f) & 15u)); // Accumulated AO, history length
    weight *= texelHistory.y > 0.5f ? 1.0f : 0.0f;
    historyAO += texelHistory.x * weight;
    prevLength += texelHistory.y * weight;
    historyWeight += weight;
  }
  if (historyWeight < 0.1f)
  {
    prevLength = 0.0f;
    return 0.0f;
  }
  historyAO /= historyWeight;
  prevLength /= historyWeight;

  maxLength = lerp(TEMPORAL_MAX_FRAMES, TEMPORAL_MIN_FRAMES, hasVelocity ? PixelSpeed01(velocity) : 0.0f);
  // The AO footprint (the neighbourhood the rays look at) changed: a moving object nearby, whether it has a velocity or not. This is what leaves a trailing AO behind moving objects.
  maxLength = min(maxLength, lerp(TEMPORAL_MIN_FRAMES, TEMPORAL_MAX_FRAMES, FootprintStability(v1, rayWidthPixels, worldReach)));

  return historyAO;
}

#endif // AO_TEMPORAL

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float2 uv = v1 + g_UVOffset;
  float depth = g_DepthTex.SampleLevel(g_SamplePoint, uv, 0).x;
  float depthSlope = CalculateDepthSlope(uv, depth);
  if (depth == 1.0f) // Sky
  {
    o0 = float4(1.0f, 1.0f, 1.0f, 1.0f); // Like the game
    return;
  }

  float viewZ = depth * g_FarPlane;

  // View space position and normal (z forward)
  float3 position = ViewPosition(uv, depth);
  float3 normal;
  if (g_SpecularTex.SampleLevel(g_SamplePoint, uv, 0).y != 0.0f)
  {
    // Reconstruct the normal from the closest depth neighbours
    float2 pixelSize = g_ProjParamsInverse.zw;
    float depthLeft = g_DepthTex.SampleLevel(g_SamplePoint, uv - float2(pixelSize.x, 0.0f), 0).x;
    float depthUp = g_DepthTex.SampleLevel(g_SamplePoint, uv - float2(0.0f, pixelSize.y), 0).x;
    float depthRight = g_DepthTex.SampleLevel(g_SamplePoint, uv + float2(pixelSize.x, 0.0f), 0).x;
    float depthDown = g_DepthTex.SampleLevel(g_SamplePoint, uv + float2(0.0f, pixelSize.y), 0).x;
    bool useLeft = abs(depthLeft - depth) < abs(depthRight - depth);
    bool useUp = abs(depthUp - depth) < abs(depthDown - depth);
    float horizontalDepth = useLeft ? depthLeft : depthRight;
    float horizontalSign = useLeft ? -1.0f : 1.0f;
    float verticalDepth = useUp ? depthUp : depthDown;
    float verticalSign = useUp ? -1.0f : 1.0f;
    float3 horizontalPosition = ViewPosition(uv + float2(horizontalSign * pixelSize.x, 0.0f), horizontalDepth);
    float3 verticalPosition = ViewPosition(uv + float2(0.0f, verticalSign * pixelSize.y), verticalDepth);
    normal = normalize(cross(horizontalSign * (horizontalPosition - position), verticalSign * (verticalPosition - position)));
  }
  else
  {
    // Octahedral encoding, in world space, rotated by the view matrix
    float3 encoded = g_NormalTex.SampleLevel(g_SampleLinear, uv, 0).yxz * float3(15.9375f, 1.992674f, 0.124542125f);
    float2 oct;
    oct.x = floor(encoded.x) * 0.000488400517f + encoded.y - 1.0f;
    oct.y = frac(encoded.x) * 2.00048852f + encoded.z - 1.0f;
    float octZ = 1.0f - abs(oct.x) - abs(oct.y);
    if (octZ < 0.0f)
      oct = float2(oct.x >= 0.0f ? 1.0f : -1.0f, oct.y >= 0.0f ? 1.0f : -1.0f) * (1.0f - abs(oct.yx));
    float3 n = normalize(float3(oct, octZ));
    float3 viewNormal = g_ViewMatrix[0].xyz * n.x + g_ViewMatrix[1].xyz * n.y + g_ViewMatrix[2].xyz * n.z;
    normal = float3(viewNormal.xy, -viewNormal.z);
  }

  // Screen space and world space radius
  float screenRadius = min(g_ProjParams.x * g_FalloffRadius / viewZ, g_MaxScreenRadius);
  float worldRadius = viewZ * screenRadius * g_ProjParamsInverse.x * 0.5f;
  // The ray reach in AO pixels, and its size in world units at this depth (view space width of a half res pixel = g_ViewFarPlaneUVOffset.z * depth * 2 / full res width)
  const float rayWidthPixels = VBAO_RAY_WIDTH_PIXELS;
  float worldReach = rayWidthPixels * g_ViewFarPlaneUVOffset.z * depth * (2.0f / g_ProjParams.z);

  float4 random = VBAORandom((uint2)v0.xy, LumaSettings.FrameIndex);

  // The history of the previous frame (reprojected) and the max length it can have
  float prevLength = 0.0f;
  float maxLength = TEMPORAL_MAX_FRAMES;
  float history = 0.0f;
#if AO_TEMPORAL
  history = TemporalReproject(v1, depth, depthSlope, rayWidthPixels, worldReach, prevLength, maxLength);
#endif

  float ao;
  if (LumaSettings.GameSettings.EnableVBAO != 0)
  {
    // The thickness grows linearly with the depth (the paper: "increase t linearly over the distance"): a fixed fraction of the size of the ray reach in world units, which is ~0.21 * the depth here (the radius of the game's SAO is ~0.1 * the depth, up to ~100 m)
    float thickness = max(VBAO_THICKNESS_WORLD, worldReach * LumaSettings.GameSettings.VBAOThicknessScale);
    bool useFoliageMask = LumaSettings.GameSettings.EnableVBAOFoliageMask != 0;
    bool centerIsFoliage = useFoliageMask && IsFoliage(uv);
    float vbao = CalculateVBAO(uv, position, normal, random, rayWidthPixels, thickness, useFoliageMask && !centerIsFoliage);
    ao = pow(vbao, VBAO_POWER);
    // The foliage darkens itself too much (the leaves occlude each other at every scale): blend it toward unoccluded
    if (centerIsFoliage)
      ao = lerp(ao, 1.0f, LumaSettings.GameSettings.VBAOFoliageBlend);
  }
  else
  {
    // Per pixel noise (4096 values), rotated over time
    int2 pixelCoords = (int2)(uv * g_ProjParams.zw);
    float noise = float(uint((pixelCoords.x * 3) ^ (pixelCoords.x * pixelCoords.y + pixelCoords.y)) & 4095u);
    float spin = noise * 5.0f + float(LumaSettings.FrameIndex % 64u) * 2.399963f;
    float falloffScale = (g_OcclusionMax - 1.0f) * worldRadius * worldRadius * g_NearFalloffThresholdInverseSqr;
    float inverseSampleCount = 1.0f / float(g_NumSamples);
    float occlusionSum = 0.0f;
    for (int i = 0; i < g_NumSamples; i++)
    {
      float t = (float(i) + g_SampleOffset) * inverseSampleCount;
      float radiusUV = screenRadius * t;
      float angle = t * g_SpiralTurns + spin;
      float sinAngle, cosAngle;
      sincos(angle, sinAngle, cosAngle);
      float mip = log2(radiusUV * g_ScreenMipRadiusFactor);
      float2 sampleUV = uv + radiusUV * float2(cosAngle, sinAngle);
      float sampleDepth = g_DepthTex.SampleLevel(g_SamplePointMipLinearBorder, sampleUV, mip).x;
      float3 delta = (ViewPosition(sampleUV, sampleDepth) - position) / worldRadius;
      float distanceSquared = dot(delta, delta);
      float maxOcclusion = max(g_OcclusionMax - distanceSquared * falloffScale, 1.0f);
      float occlusion = max((dot(delta, normal) - g_Bias * depth) / (distanceSquared + 0.01f), 0.0f);
      occlusionSum += min(maxOcclusion, occlusion);
    }
    ao = pow(max(1.0f - inverseSampleCount * g_Intensity * occlusionSum, 0.0f), g_Contrast);
  }

  // Accumulate the AO over time: the weight of the current AO is 1 / the length of the history (including this frame), like an average of the last frames
  float newLength = clamp(prevLength + 1.0f, 1.0f, max(maxLength, 1.0f));
  uint historyLength = uint(clamp(round(newLength), 1.0f, 15.0f));
  float resolvedAO = prevLength > 0.0f ? lerp(history, ao, 1.0f / newLength) : ao;
  // The target is 8 bit and the accumulation steps are small: dither it so the rounding doesn't stall the history (and doesn't bias it)
  resolvedAO += (frac(52.9829189f * frac(dot(v0.xy, float2(0.06711056f, 0.00583715f)) + 0.618034f * float(LumaSettings.FrameIndex))) - 0.5f) / 255.0f;

  // The same output format as the game's AO pass, as its bilateral blur passes read it: .x = AO, .yz = the xy of the view normal (z is reconstructed), .w = flags (128: normal z < 0) + here the history length in the low 4 bits
  o0 = float4(resolvedAO, normal.xy * 0.5f + 0.5f, float((normal.z >= 0.0f ? 0u : 128u) + historyLength) / 255.0f);
}
