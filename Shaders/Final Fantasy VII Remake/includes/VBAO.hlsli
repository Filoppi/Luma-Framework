#ifndef VBAO_HLSLI
#define VBAO_HLSLI

#define VBAO_STEP_COUNT 16
#define VBAO_SLICE_COUNT 1
#define VBAO_RAY_WIDTH_PIXELS 512.0f
#define VBAO_THICKNESS 20.0f
#define VBAO_NORMAL_BIAS 0.0005f

// Match the view-space XY used by the game's original SSAO. The temporal
// shader's cb1[24..27] has no verified clip-to-view projection mapping.
float3 VBAOViewRayZ1(float2 localPixel)
{
  float2 rasterNdc = localPixel * (cb1[122].zw * 2.0f) - 1.0f;
  float2 viewNdc = rasterNdc * float2(1.0f, -1.0f);
  float2 aoProjectionScale = cb0[18].z * float2(1.0f, cb0[19].z);
  return float3(viewNdc * aoProjectionScale, 1.0f);
}

float VBAODeviceDepthToViewDepth(float deviceDepth)
{
  return deviceDepth * cb1[57].x + cb1[57].y
    + rcp(deviceDepth * cb1[57].z - cb1[57].w);
}

// VBAO shared sample ray construction and interval insertion.
void AddOccludedInterval(inout uint occupiedBits, float2 horizonCosines, float halfSide, float baseHorizon, float arcJitter)
{
  const uint fullMask = 0xffffffffu;
  float2 horizon01 = saturate(baseHorizon + halfSide - halfSide * horizonCosines + arcJitter);
  uint2 bins = (uint2)floor(horizon01 * 32.0f);
  uint startMask = bins.x < 32u ? (fullMask << bins.x) : 0u;
  uint endMask = bins.y != 0u ? (fullMask >> (32u - bins.y)) : 0u;
  occupiedBits |= startMask & endMask;
}

float CalculateVBAO(float2 localPixel, float deviceDepth, float centerZ, float3 viewNormal, float4 random)
{
  const float inverseStepCount = 1.0f / float(VBAO_STEP_COUNT);
  const float inverseSliceCount = 1.0f / float(VBAO_SLICE_COUNT);
  float3 normal = normalize(viewNormal);
  float3 rayOrigin = VBAOViewRayZ1(localPixel);
  float3 rayDx = VBAOViewRayZ1(localPixel + float2(1.0f, 0.0f)) - rayOrigin;
  float3 rayDy = VBAOViewRayZ1(localPixel + float2(0.0f, 1.0f)) - rayOrigin;
  float3 viewPosition = rayOrigin * centerZ + normal * (VBAO_NORMAL_BIAS + 0.025f * deviceDepth);
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
        float sampleDistance = 1.0f + (VBAO_RAY_WIDTH_PIXELS - 1.0f) * (u * u);
        float2 samplePixel = localPixel + screenDirection * sampleDistance;
        if (any(samplePixel < 0.0f) || any(samplePixel >= cb1[122].xy))
          break;
        int2 sampleTexturePixel = int2(floor(samplePixel)) + int2(cb1[121].xy);
        float sampleZ = VBAODeviceDepthToViewDepth(t2.Load(int3(sampleTexturePixel, 0)).x);
        float3 sampleRay = rayOrigin + viewRayStep * sampleDistance;
        float3 frontDelta = sampleRay * sampleZ - viewPosition;
        float3 backDelta = frontDelta + sampleRay * VBAO_THICKNESS;
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


#endif // VBAO_HLSLI
