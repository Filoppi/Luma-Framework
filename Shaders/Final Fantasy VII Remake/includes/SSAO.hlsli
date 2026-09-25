#ifndef SSAO_COMMON_HLSLI
#define SSAO_COMMON_HLSLI

// Original game SSAO plus the optional VBAO replacement.

// SSAO algorithm selection is defined by the patch's shader define:
// 0 = original game SSAO, 1 = VBAO.
#ifndef SSAO_ALGORITHM
#define SSAO_ALGORITHM 0
#endif

#if SSAO_ALGORITHM < 0 || SSAO_ALGORITHM > 1
#error SSAO_ALGORITHM must be 0 (original) or 1 (VBAO)
#endif

float DeviceDepthToViewDepth(float deviceDepth)
{
  float linearPart = deviceDepth * cb1[57].x + cb1[57].y;
  float denominator = deviceDepth * cb1[57].z - cb1[57].w;
  return linearPart + rcp(denominator);
}

// Fast atan-like approximation used by the game's original horizon integration.
float ApproximateHorizonAngle(float cosElevation)
{
  float x2 = cosElevation * cosElevation;
  float x4 = x2 * x2;
  return (-1.00048f - 0.57032f * x4) * cosElevation;
}

float IntegrateHorizonSide(float alongSlice, float normalZ, float negativeHorizon, float positiveHorizon)
{
  float projectedLength = max(length(float2(alongSlice, normalZ)), 1.0e-4f);
  float cosElevation = clamp(-normalZ / projectedLength, -1.0f, 1.0f);
  float sinElevation = sqrt(max(1.0f - cosElevation * cosElevation, 0.0f)) * ((alongSlice < 0.0f) ? -1.0f : 1.0f);
  float gamma = (ApproximateHorizonAngle(cosElevation) + 1.57080f) * ((alongSlice < 0.0f) ? -1.0f : 1.0f);
  float upper = min(positiveHorizon - gamma, 1.57080f);
  float lower = max(-negativeHorizon - gamma, -1.57080f);
  float integral = sinElevation * (lower + upper + 2.0f * gamma)
    - 0.5f * (cos(gamma + 2.0f * upper) + cos(gamma + 2.0f * lower));
  return integral * projectedLength;
}

float2 ClampAndReflectNdcOffset(float2 offset, out bool touchedScreenEdge)
{
  float2 signs = float2(offset.x < 0.0f ? -1.0f : 1.0f, offset.y < 0.0f ? -1.0f : 1.0f);
  float2 reflected = offset - 2.0f * signs * max(abs(offset) - 1.0f, 0.0f);
  float2 clamped = clamp(reflected, -1.0f, 1.0f);
  touchedScreenEdge = any(clamped != offset);
  return clamped;
}

// Mode 0: readable reconstruction of the original game's four-direction,
// four-radius horizon AO. The game temporal resolve still runs after this value.
float CalculateOriginalGameSSAO(float2 blueNoise, float centerZ, float2 viewNdc, float2 rasterNdc, float3 viewNormal)
{
  float rotation = blueNoise.y * 1.57080f;
  float sinRotation, cosRotation;
  sincos(rotation, sinRotation, cosRotation);

  float projectedRadiusPixels = cb1[122].x * 0.125f;
  float pixelFootprint = cb0[3].z * rcp(cb0[20].x) * 1.41421354f;
  float projectedRadius = min(500.0f * cb0[18].w / max(centerZ, 1.0e-6f), 0.75f);
  float2 centerViewXY = centerZ * float2(viewNdc.x, viewNdc.y * cb0[19].z);

  float horizons[4] = { 3.14159265f, 3.14159265f, 3.14159265f, 3.14159265f };
  float2 directions[4] = {
    float2(cosRotation, sinRotation), float2(sinRotation, -cosRotation),
    float2(-cosRotation, -sinRotation), float2(-sinRotation, cosRotation)
  };

  [unroll]
  for (uint radiusIndex = 0u; radiusIndex < 4u; ++radiusIndex)
  {
    float radial = (float(radiusIndex) + blueNoise.x) * 0.25f;
    float stepRadius = max(pixelFootprint, radial * radial * projectedRadius);
    float mip = clamp(log2(max(projectedRadiusPixels * stepRadius, 1.0e-8f)), 0.0f, 4.0f);

    [unroll]
    for (uint directionIndex = 0u; directionIndex < 4u; ++directionIndex)
    {
      float2 ray = directions[directionIndex] * stepRadius;
      float2 sampleNdc = rasterNdc + float2(ray.x, 0.0f);
      sampleNdc.y = ray.y * cb0[19].w - rasterNdc.y;

      bool hitScreenEdge;
      float2 hzbNdc = ClampAndReflectNdcOffset(sampleNdc, hitScreenEdge);
      float2 hzbUv = hzbNdc * cb0[20].xy + cb0[20].zw;
      float sampleDeviceZ = t5.SampleLevel(s0_s, hzbUv, mip).x;
      float sampleZ = DeviceDepthToViewDepth(sampleDeviceZ);
      float3 delta = float3(
        sampleZ * sampleNdc.x - centerViewXY.x,
        sampleZ * sampleNdc.y * cb0[19].z - centerViewXY.y,
        sampleZ - centerZ);
      delta.xy *= cb0[18].z;

      float distanceToSample = max(length(delta), 1.0e-4f);
      float cosElevation = clamp(-delta.z / distanceToSample, -1.0f, 1.0f);
      float candidateHorizon = ApproximateHorizonAngle(cosElevation) - 1.57079f;
      float distanceFade = saturate((distanceToSample - 500.0f) * -0.01f);
      float frontFade = saturate((250.0f + delta.z) * 0.004f);
      float fadedHorizon = 3.14159265f + candidateHorizon * distanceFade;
      float updatedHorizon = lerp(horizons[directionIndex], min(fadedHorizon, horizons[directionIndex]), frontFade);
      if (!hitScreenEdge)
        horizons[directionIndex] = updatedHorizon;
    }
  }

  float along = cosRotation * viewNormal.x + sinRotation * viewNormal.y;
  float across = cosRotation * viewNormal.y - sinRotation * viewNormal.x;
  float integratedVisibility = IntegrateHorizonSide(along, viewNormal.z, horizons[2], horizons[0])
    + IntegrateHorizonSide(across, viewNormal.z, horizons[1], horizons[3]);
  return max(0.0f, 0.25f * integratedVisibility - 0.5f * viewNormal.z);
}

#if SSAO_ALGORITHM == 1
#include "VBAO.hlsli"
#endif

#endif // SSAO_COMMON_HLSLI
