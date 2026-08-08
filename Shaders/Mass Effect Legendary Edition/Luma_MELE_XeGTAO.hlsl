// XeGTAO replacement for the trilogy-wide NVIDIA HBAO+ chain, adapted from the repository's canonical port.
// Source: https://github.com/GameTechDev/XeGTAO
//
// MELE-specific contracts shared by all three games:
// - Run at native AO half resolution and write visibility to blur u0, the game's final R8_UNORM AO target.
//   Apply shader 0x2E826C0F retains blend dst*src_color into the fp16 scene.
// - Inherit cb0 HBAO+ $Globals and cb2 CSOffsetConstants; layouts come from live disassembly of
//   0x80212FD6/0x06D92B08 and retain standard GFSDK offsets.
// - Depth input = the game's half-res r24_unorm_x8 depth copy (deinterleave 0x497830D8 t0), read with explicit
//   .Load: GatherRed on an r24_unorm_x8 view returns all-zeros on some drivers and silently kills the AO.
// - ViewNormalTex from horizon shader 0x80212FD6 stores view-space xy in R8G8_UNORM; reconstruct z locally.
// - With no TAA or motion vectors, freeze NoiseIndex at zero and rely on Very High quality plus two denoisers.
// - Divide UE3 view Z by DepthScale=50 to approximate the meter-scale range expected by XeGTAO.

// Native constant buffers inherited at the hooked dispatches; offsets come from live disassembly.

cbuffer _Globals : register(b0) // NVIDIA GFSDK_SSAO $Globals.
{
   float RadiusToScreen;        // Offset:   0
   float NegInvR2;              // Offset:   4; -1/R^2 in native view units.
   float NDotVBias;             // Offset:   8
   float2 InvFullResolution;    // Offset:  16; inverse AO resolution.
   float2 InvQuarterResolution; // Offset:  24
   int2 FullResOffset;          // Offset:  32
   int2 QuarterResOffset;       // Offset:  40
   float AOMultiplier;          // Offset:  48
   float PowExponent;           // Offset:  52; native blur darkness control.
   float4 ProjInfo;             // Offset:  64; view xy = (uv*xy + zw) * viewZ.
   float2 Float2Offset;         // Offset:  80
   float4 Jitter;               // Offset:  96
   int ArrayOffset;             // Offset: 112
   float4 JitterCS[8];          // Offset: 128
}

cbuffer CSOffsetConstants : register(b2)
{
   float4x4 ViewProjectionMatrixCS;  // Offset:   0
   float4 CameraPositionCS;          // Offset:  64
   float4 ScreenPositionScaleBiasCS; // Offset:  80
   float4 MinZ_MaxZRatioCS;          // Offset:  96; viewZ = 1 / (d * z - w), non-reverse Z.
   float4 DynamicScaleCS;            // Offset: 112
}

// Runtime parameters uploaded by main.cpp in b11.
cbuffer LumaGTAO : register(b11)
{
   float FinalValuePowerRT; // Darkness control calibrated to native AO.
   float DepthScaleRT;      // View-Z divisor from UE3 units to approximate meters.
   float RadiusOverrideRT;  // Positive values override EFFECT_RADIUS after depth scaling.
   float DebugViewRT;       // DEVELOPMENT: 0=off, 1=depth, 2=normals, 3=AO x8, 4=edges.
}

#include "Includes/Common.hlsl"

#if XE_GTAO_QUALITY == 0 // Low
#define SLICE_COUNT 4.0
#elif XE_GTAO_QUALITY == 1 // Medium
#define SLICE_COUNT 7.0
#elif XE_GTAO_QUALITY == 2 // High
#define SLICE_COUNT 10.0
#elif XE_GTAO_QUALITY == 3 // Very High
#define SLICE_COUNT 13.0
#elif XE_GTAO_QUALITY == 4 // Ultra
#define SLICE_COUNT 16.0
#endif

// Compile-time defaults; runtime b11 overrides the exposed controls.

#ifndef EFFECT_RADIUS
#define EFFECT_RADIUS 0.6 // Native ME1 radius: 30 UE3 units / DepthScale 50; runtime override wins.
#endif

#ifndef RADIUS_MULTIPLIER
#define RADIUS_MULTIPLIER 1.457 // Default 1.457
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.005 // Hard falloff matches native contact AO; Intel default 0.615 is softer.
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 1.5 // Default 2.0
#endif

#ifndef THIN_OCCLUDER_COMPENSATION
#define THIN_OCCLUDER_COMPENSATION 0.0 // Default 0.0; > 0 causes more mistakes than it fixes on big geometry
#endif

#ifndef FINAL_VALUE_POWER
#define FINAL_VALUE_POWER 1.0 // Fallback only; runtime FinalValuePowerRT applies.
#endif

#ifndef DEPTH_MIP_SAMPLING_OFFSET
#define DEPTH_MIP_SAMPLING_OFFSET 3.3 // Default 3.3
#endif

#ifndef SLICE_COUNT
#define SLICE_COUNT 3.0 // Default 3.0
#endif

#ifndef STEPS_PER_SLICE
#define STEPS_PER_SLICE 3.0 // Default 3.0
#endif

#ifndef DENOISE_BLUR_BETA
#define DENOISE_BLUR_BETA 1.2 // Default 1.2
#endif

// ViewNormalTex z sign; view-space normals face the camera.
#ifndef NORMAL_Z_SIGN
#define NORMAL_Z_SIGN (-1.0)
#endif

//

#define VIEWPORT_PIXEL_SIZE InvFullResolution

// GFSDK ProjInfo contains the live NDC-to-view multiply/add pair, including dialogue zoom.
#define NDC_TO_VIEW_MUL              ProjInfo.xy
#define NDC_TO_VIEW_ADD              ProjInfo.zw
#define NDC_TO_VIEW_MUL_X_PIXEL_SIZE (NDC_TO_VIEW_MUL * VIEWPORT_PIXEL_SIZE)

#define XE_GTAO_DEPTH_MIP_LEVELS     5.0
#define XE_GTAO_OCCLUSION_TERM_SCALE 1.5

#define XE_GTAO_PI                   3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF              1.5707963267948966192313216916398

// Convert non-reverse D24 through native MinZ_MaxZRatioCS, then scale UE3 units into XeGTAO's expected range.
// Without scaling, its mip and falloff assumptions cause broad over-occlusion.
float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
{
   float viewZ = 1.0 / max(1e-7, screenDepth * MinZ_MaxZRatioCS.z - MinZ_MaxZRatioCS.w);
   return max(0.0, viewZ) / max(1e-3, DepthScaleRT);
}

// Clamp the converted depth before mip generation.
float XeGTAO_ClampDepth(float depth)
{
   return clamp(depth, 0.0, 3.402823466e+38);
}

float XeGTAO_EffectRadius()
{
   return (RadiusOverrideRT > 0.0 ? RadiusOverrideRT : EFFECT_RADIUS) * RADIUS_MULTIPLIER;
}

// Edge-aware depth mip filter.
float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
   float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

   const float depthRangeScaleFactor = 0.75; // Empirical XeGTAO constant.
   const float effectRadius = depthRangeScaleFactor * XeGTAO_EffectRadius();
   const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
   const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

   // Precompute falloff coefficients.
   const float falloffMul = -1.0 / falloffRange;
   const float falloffAdd = falloffFrom / falloffRange + 1.0;

   float weight0 = saturate((maxDepth - depth0) * falloffMul + falloffAdd);
   float weight1 = saturate((maxDepth - depth1) * falloffMul + falloffAdd);
   float weight2 = saturate((maxDepth - depth2) * falloffMul + falloffAdd);
   float weight3 = saturate((maxDepth - depth3) * falloffMul + falloffAdd);

   float weightSum = weight0 + weight1 + weight2 + weight3;
   return (weight0 * depth0 + weight1 * depth1 + weight2 * depth2 + weight3 * depth3) * rcp(weightSum);
}

groupshared float g_scratchDepths[8][8];
void XeGTAO_PrefilterDepths16x16(uint2 dispatchThreadID, uint2 groupThreadID, Texture2D sourceNDCDepth, RWTexture2D<float> outDepth0, RWTexture2D<float> outDepth1, RWTexture2D<float> outDepth2, RWTexture2D<float> outDepth3, RWTexture2D<float> outDepth4)
{
   // MIP 0
   const uint2 baseCoord = dispatchThreadID;
   const uint2 pixCoord = baseCoord * 2;
   // GatherRed returns zero on some R24_UNORM_X8 views. Integer Loads preserve depth; D3D11 defines
   // out-of-bounds Loads as zero before conversion and clamping.
   float d00 = sourceNDCDepth.Load(int3(pixCoord + uint2(0, 0), 0)).x;
   float d10 = sourceNDCDepth.Load(int3(pixCoord + uint2(1, 0), 0)).x;
   float d01 = sourceNDCDepth.Load(int3(pixCoord + uint2(0, 1), 0)).x;
   float d11 = sourceNDCDepth.Load(int3(pixCoord + uint2(1, 1), 0)).x;
   float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(d00));
   float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(d10));
   float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(d01));
   float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(d11));
   outDepth0[pixCoord + uint2(0, 0)] = depth0;
   outDepth0[pixCoord + uint2(1, 0)] = depth1;
   outDepth0[pixCoord + uint2(0, 1)] = depth2;
   outDepth0[pixCoord + uint2(1, 1)] = depth3;

   // MIP 1
   float dm1 = XeGTAO_DepthMIPFilter(depth0, depth1, depth2, depth3);
   outDepth1[baseCoord] = dm1;
   g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

   GroupMemoryBarrierWithGroupSync();

   // MIP 2
   [branch] if (all((groupThreadID.xy % 2) == 0))
   {
      float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
      float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
      float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
      float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

      float dm2 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
      outDepth2[baseCoord / 2] = dm2;
      g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
   }

   GroupMemoryBarrierWithGroupSync();

   // MIP 3
   [branch] if (all((groupThreadID.xy % 4) == 0))
   {
      float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
      float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
      float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
      float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

      float dm3 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
      outDepth3[baseCoord / 4] = dm3;
      g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
   }

   GroupMemoryBarrierWithGroupSync();

   // MIP 4
   [branch] if (all((groupThreadID.xy % 8) == 0))
   {
      float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
      float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
      float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
      float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

      float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
      outDepth4[baseCoord / 8] = dm4;
      // g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm4;
   }
}

float4 XeGTAO_CalculateEdges(float centerZ, float leftZ, float rightZ, float topZ, float bottomZ)
{
   float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;

   float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
   float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
   float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
   edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
   return saturate(1.25 - edgesLRTB * rcp(centerZ * 0.011));
}

// Pack four edge gradients into two bits each: 0, 0.33, 0.66, or 1.
float XeGTAO_PackEdges(float4 edgesLRTB)
{
   edgesLRTB = round(saturate(edgesLRTB) * 2.9);
   return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// Reconstruct view-space position from screen UV and view-space depth.
float3 XeGTAO_ComputeViewspacePosition(float2 screenPos, float viewspaceDepth)
{
   float3 ret;
   ret.xy = (NDC_TO_VIEW_MUL * screenPos.xy + NDC_TO_VIEW_ADD) * viewspaceDepth;
   ret.z = viewspaceDepth;
   return ret;
}

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN, https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
float XeGTAO_FastSqrt(float x)
{
   return asfloat(0x1fbd1df5 + (asint(x) >> 1));
}

// Maps [-1,1] to [0,PI]. Source: https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
float XeGTAO_FastACos(float inX)
{
   // Shared Common defines PI as a macro, so use a literal locally.
   float x = abs(inX);
   float res = -0.156583 * x + 1.570796;
   res *= XeGTAO_FastSqrt(1.0 - x);
   return inX >= 0 ? res : 3.141593 - res;
}

void XeGTAO_MainPass(uint2 pixCoord, float2 localNoise, float3 viewspaceNormal, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<unorm float2> outWorkingAOTermAndEdges)
{
   float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

   // Gather is safe on the prefiltered R32F mip; the zero-read quirk applies only to native R24 depth.
   float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE));
   float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));

   // Center view-space Z.
   float viewspaceZ = valuesUL.y;

   // Neighbor view-space Z in left, top, right, bottom order.
   const float pixLZ = valuesUL.x;
   const float pixTZ = valuesUL.z;
   const float pixRZ = valuesBR.z;
   const float pixBZ = valuesBR.x;

   float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
   const float edges = XeGTAO_PackEdges(edgesLRTB);

#if DEVELOPMENT
   // Debug values pass unfiltered through denoise and native AO apply.
   if (DebugViewRT > 0.5 && DebugViewRT < 1.5) // Depth gradient validates live conversion and scale.
   {
      outWorkingAOTermAndEdges[pixCoord] = float2(saturate(frac(log2(max(viewspaceZ, 1e-6)))), 1.0);
      return;
   }
   if (DebugViewRT >= 3.5 && DebugViewRT < 4.5) // Edge confidence.
   {
      outWorkingAOTermAndEdges[pixCoord] = float2(dot(edgesLRTB, 0.25), 1.0);
      return;
   }
#endif

   // Move the center slightly toward the camera to suppress depth precision artifacts.
   viewspaceZ *= 0.99999; // Tuned for FP32 view-space depth.

   const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
   const float3 viewVec = normalize(-pixCenterPos);

   // Fold back-facing normals toward the view vector for extreme cases.
   viewspaceNormal = normalize(viewspaceNormal + max(0, -dot(viewspaceNormal, viewVec)) * viewVec);

#if DEVELOPMENT
   if (DebugViewRT >= 1.5 && DebugViewRT < 2.5) // Smooth view-facing shading indicates correct normal decode.
   {
      outWorkingAOTermAndEdges[pixCoord] = float2(saturate(abs(dot(viewspaceNormal, viewVec))), 1.0);
      return;
   }
#endif

   const float effectRadius = XeGTAO_EffectRadius();
   const float sampleDistributionPower = SAMPLE_DISTRIBUTION_POWER;
   const float thinOccluderCompensation = THIN_OCCLUDER_COMPENSATION;
   const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
   const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

   // Precompute falloff coefficients.
   const float falloffMul = -1.0 / falloffRange;
   const float falloffAdd = falloffFrom / falloffRange + 1.0;

   float visibility = 0.0;

   // Algorithm 1: https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
   {
      const float noiseSlice = localNoise.x;
      const float noiseSample = localNoise.y;

      // Sampling heuristics.
      const float pixelTooCloseThreshold = 1.3; // Push sub-pixel offsets to the minimum useful distance.

      // Approximate view-space pixel size at the center depth.
      const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDC_TO_VIEW_MUL_X_PIXEL_SIZE;

      float screenspaceRadius = effectRadius * rcp(abs(pixelDirRBViewspaceSizeAtCenterZ.x));

      // Fade for small screen-space radii.
      visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

      // Minimum useful distance from the center pixel.
      const float minS = pixelTooCloseThreshold * rcp(screenspaceRadius);

      //[unroll]
      for (float slice = 0.0; slice < SLICE_COUNT; slice++)
      {
         float sliceK = (slice + noiseSlice) / SLICE_COUNT;
         // Algorithm 1, lines 5-6.
         float phi = sliceK * XE_GTAO_PI;
         float cosPhi = cos(phi);
         float sinPhi = sin(phi);
         float2 omega = float2(cosPhi, -sinPhi); // Keep full precision for large radii.

         // Convert to screen-space pixels.
         omega *= screenspaceRadius;

         // Algorithm 1, line 8.
         const float3 directionVec = float3(cosPhi, sinPhi, 0.0);

         // Algorithm 1, line 9.
         const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

         // Algorithm 1, line 10.
         // axisVec is orthogonal to directionVec and viewVec, used to define projectedNormal
         const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

         // Algorithm 1, line 11.
         float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

         // Algorithm 1, line 13.
         float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));

         // Algorithm 1, line 14.
         float projectedNormalVecLength = length(projectedNormalVec);
         float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(projectedNormalVecLength));

         // Algorithm 1, line 15.
         float n = signNorm * XeGTAO_FastACos(cosNorm);

         // Use the normal-dependent lower horizon rather than the paper's fixed -1 target.
         const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
         const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

         // Algorithm 1, lines 17-18, with the side loop unrolled.
         float horizonCos0 = lowHorizonCos0; //-1;
         float horizonCos1 = lowHorizonCos1; //-1;

         [unroll] for (float step = 0.0; step < STEPS_PER_SLICE; step++)
         {
            // R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
            const float stepBaseNoise = (slice + step * STEPS_PER_SLICE) * 0.6180339887498948482; // Compile-time unrolled.
            float stepNoise = frac(noiseSample + stepBaseNoise);

            // Approximate Algorithm 1 line 20 with noise.
            float s = (step + stepNoise) / STEPS_PER_SLICE;

            // Apply sample distribution power.
            s = pow(s, sampleDistributionPower);

            // Avoid the center pixel.
            s += minS;

            // Approximate Algorithm 1 lines 21-22.
            float2 sampleOffset = s * omega;

            float sampleOffsetLength = length(sampleOffset);

            // Point XY sampling avoids interpolation between neighboring depths within a mip.
            const float mipLevel = clamp(log2(sampleOffsetLength) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, XE_GTAO_DEPTH_MIP_LEVELS);

            // Snap to pixel centers for correct direction and slope math; retain full precision at high resolution.
            sampleOffset = round(sampleOffset) * VIEWPORT_PIXEL_SIZE;

            float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
            float SZ0 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
            float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0);

            float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
            float SZ1 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
            float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1);

            float3 sampleDelta0 = samplePos0 - pixCenterPos; // Reduced precision causes visible errors.
            float3 sampleDelta1 = samplePos1 - pixCenterPos;
            float sampleDist0 = length(sampleDelta0);
            float sampleDist1 = length(sampleDelta1);

            // Approximate Algorithm 1 lines 23-24.
            float3 sampleHorizonVec0 = sampleDelta0 * rcp(sampleDist0);
            float3 sampleHorizonVec1 = sampleDelta1 * rcp(sampleDist1);

            // Bound sampling radius with a smooth falloff, following section 4.3. The thickness heuristic rejects
            // samples behind the center earlier.
            float falloffBase0 = length(float3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1.0 + thinOccluderCompensation)));
            float falloffBase1 = length(float3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1.0 + thinOccluderCompensation)));
            float weight0 = saturate(falloffBase0 * falloffMul + falloffAdd);
            float weight1 = saturate(falloffBase1 * falloffMul + falloffAdd);

            // Sample horizon cosines.
            float shc0 = dot(sampleHorizonVec0, viewVec);
            float shc1 = dot(sampleHorizonVec1, viewVec);

            // Blend rejected samples toward the lower horizon.
            shc0 = lerp(lowHorizonCos0, shc0, weight0); // Angular interpolation is more accurate but too expensive.
            shc1 = lerp(lowHorizonCos1, shc1, weight1);

            // THIN_OCCLUDER_COMPENSATION=0 disables the thickness adjustment.
            horizonCos0 = max(horizonCos0, shc0);
            horizonCos1 = max(horizonCos1, shc1);
         }

#if 1 // XeGTAO training-set slope adjustment: 0.05 gives PSNR 21.34 versus 21.45 when disabled.
         projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);
#endif

         // Approximate Algorithm 1 line 27.
         float h0 = -XeGTAO_FastACos(horizonCos1);
         float h1 = XeGTAO_FastACos(horizonCos0);
         float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
         float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
         float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
         visibility += localVisibility;
      }
      visibility /= SLICE_COUNT;
      visibility = pow(visibility, max(0.05, FinalValuePowerRT)); // Runtime match to the native AO histogram.
      visibility = max(0.03, visibility);                         // A visible surface cannot be fully occluded.
   }

   visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
   outWorkingAOTermAndEdges[pixCoord] = float2(visibility, edges);
}

void XeGTAO_DecodeGatherPartial(float4 packedValue, out float outDecoded[4])
{
   for (int i = 0; i < 4; i++)
   {
      outDecoded[i] = packedValue[i];
   }
}

float4 XeGTAO_UnpackEdges(float _packedVal)
{
   uint packedVal = uint(_packedVal * 255.5);
   float4 edgesLRTB;
   edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0; // Retain the mask for explicit packed-input safety.
   edgesLRTB.y = float((packedVal >> 4) & 0x03) / 3.0;
   edgesLRTB.z = float((packedVal >> 2) & 0x03) / 3.0;
   edgesLRTB.w = float((packedVal >> 0) & 0x03) / 3.0;

   return saturate(edgesLRTB);
}

void XeGTAO_AddSample(float ssaoValue, float edgeValue, inout float sum, inout float sumWeight)
{
   float weight = edgeValue;

   sum += weight * ssaoValue;
   sumWeight += weight;
}

void XeGTAO_Denoise(uint2 pixCoordBase, Texture2D sourceAOTermAndEdges, SamplerState texSampler,
#if XE_GTAO_FINAL_APPLY
                    RWTexture2D<unorm float> outputTexture // Native single-channel R8_UNORM AO target.
#else
                    RWTexture2D<unorm float2> outputTexture
#endif
)
{
#if DEVELOPMENT
   // Pass debug values through without denoising.
   if (DebugViewRT > 0.5)
   {
      for (int dside = 0; dside < 2; dside++)
      {
         const uint2 dpix = uint2(pixCoordBase.x + dside, pixCoordBase.y);
         float v = sourceAOTermAndEdges.Load(int3(dpix, 0)).x;
#if XE_GTAO_FINAL_APPLY
         if (DebugViewRT >= 2.5 && DebugViewRT < 3.5) // AO x8 exposes broad over-occlusion.
            v = saturate(1.0 - (1.0 - v * XE_GTAO_OCCLUSION_TERM_SCALE) * 8.0);
         outputTexture[dpix] = v;
#else
         outputTexture[dpix] = float2(v, sourceAOTermAndEdges.Load(int3(dpix, 0)).y);
#endif
      }
      return;
   }
#endif

#if XE_GTAO_FINAL_APPLY
   const float blurAmount = DENOISE_BLUR_BETA;
#else
   const float blurAmount = DENOISE_BLUR_BETA / 5.0;
#endif

   const float diagWeight = 0.85 * 0.5;

   float aoTerm[2]; // pixCoordBase and its right neighbor.
   float4 edgesC_LRTB[2];
   float weightTL[2];
   float weightTR[2];
   float weightBL[2];
   float weightBR[2];

   // Gather edge and visibility quads.
   const float2 gatherCenter = float2(pixCoordBase.x, pixCoordBase.y) * VIEWPORT_PIXEL_SIZE;
   float4 edgesQ0 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(0, 0));
   float4 edgesQ1 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(2, 0));
   float4 edgesQ2 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(1, 2));

   float visQ0[4];
   XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(0, 0)), visQ0);
   float visQ1[4];
   XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(2, 0)), visQ1);
   float visQ2[4];
   XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(0, 2)), visQ2);
   float visQ3[4];
   XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(2, 2)), visQ3);

   for (int side = 0; side < 2; side++)
   {
      const int2 pixCoord = int2(pixCoordBase.x + side, pixCoordBase.y);

      float4 edgesL_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.x : edgesQ0.y);
      float4 edgesT_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.z : edgesQ1.w);
      float4 edgesR_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ1.x : edgesQ1.y);
      float4 edgesB_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ2.w : edgesQ2.z);

      edgesC_LRTB[side] = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.y : edgesQ1.x);

      // Edge detection is not guaranteed symmetric across neighboring pixels. Enforce symmetry for a sharper blur.
      edgesC_LRTB[side] *= float4(edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z);

#if 1 // Permit slight neighbor leakage across three or four edges to reduce aliasing.
      const float leak_threshold = 2.5;
      const float leak_strength = 0.5;
      float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.0)) * rcp(4.0 - leak_threshold)) * leak_strength;
      edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);
#endif

      // Diagonal weights shared by both denoise passes.
      weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
      weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
      weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
      weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

      // Accumulate the 3x3 neighborhood.
      float ssaoValue = side == 0 ? visQ0[1] : visQ1[0];
      float ssaoValueL = side == 0 ? visQ0[0] : visQ0[1];
      float ssaoValueT = side == 0 ? visQ0[2] : visQ1[3];
      float ssaoValueR = side == 0 ? visQ1[0] : visQ1[1];
      float ssaoValueB = side == 0 ? visQ2[2] : visQ3[3];
      float ssaoValueTL = side == 0 ? visQ0[3] : visQ0[2];
      float ssaoValueBR = side == 0 ? visQ3[3] : visQ3[2];
      float ssaoValueTR = side == 0 ? visQ1[3] : visQ1[2];
      float ssaoValueBL = side == 0 ? visQ2[3] : visQ2[2];

      float sumWeight = blurAmount;
      float sum = ssaoValue * sumWeight;

      XeGTAO_AddSample(ssaoValueL, edgesC_LRTB[side].x, sum, sumWeight);
      XeGTAO_AddSample(ssaoValueR, edgesC_LRTB[side].y, sum, sumWeight);
      XeGTAO_AddSample(ssaoValueT, edgesC_LRTB[side].z, sum, sumWeight);
      XeGTAO_AddSample(ssaoValueB, edgesC_LRTB[side].w, sum, sumWeight);

      XeGTAO_AddSample(ssaoValueTL, weightTL[side], sum, sumWeight);
      XeGTAO_AddSample(ssaoValueTR, weightTR[side], sum, sumWeight);
      XeGTAO_AddSample(ssaoValueBL, weightBL[side], sum, sumWeight);
      XeGTAO_AddSample(ssaoValueBR, weightBR[side], sum, sumWeight);

      aoTerm[side] = sum / sumWeight;

#if XE_GTAO_FINAL_APPLY
      // Native R8_UNORM stores visibility only; the game applies it to the scene.
      outputTexture[pixCoord] = saturate(aoTerm[side] * XE_GTAO_OCCLUSION_TERM_SCALE);
#else
      outputTexture[pixCoord] = float2(aoTerm[side], side == 0 ? edgesQ0.y : edgesQ1.x);
#endif
   }
}

// Shader entry points and bindings.
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SamplerState smp : register(s0); // Luma binds point-clamp because native AO does not reliably set s0.

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);

RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);
RWTexture2D<unorm float2> ao_term_and_edges : register(u0);

#if XE_GTAO_FINAL_APPLY
RWTexture2D<unorm float> final_output : register(u0); // Native final R8_UNORM AO.
#else
RWTexture2D<unorm float2> final_output : register(u0);
#endif

#define XE_GTAO_NUMTHREADS_X 8
#define XE_GTAO_NUMTHREADS_Y 8

// Hilbert mapping from https://www.shadertoy.com/view/3tB3z3, paired with an R2 sequence.
#define XE_HILBERT_LEVEL 6U
#define XE_HILBERT_WIDTH (1U << XE_HILBERT_LEVEL)
#define XE_HILBERT_AREA  (XE_HILBERT_WIDTH * XE_HILBERT_WIDTH)
uint HilbertIndex(uint posX, uint posY)
{
   uint index = 0U;
   [unroll] for (uint curLevel = XE_HILBERT_WIDTH / 2U; curLevel > 0U; curLevel /= 2U)
   {
      uint regionX = (posX & curLevel) > 0U;
      uint regionY = (posY & curLevel) > 0U;
      index += curLevel * curLevel * ((3U * regionX) ^ regionY);
      if (regionY == 0U)
      {
         if (regionX == 1U)
         {
            posX = XE_HILBERT_WIDTH - 1U - posX;
            posY = XE_HILBERT_WIDTH - 1U - posY;
         }
         uint temp = posX;
         posX = posY;
         posY = temp;
      }
   }
   return index;
}

// MELE has no TAA; callers keep temporalIndex at zero to prevent boiling.
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
   float2 noise;
   uint index = HilbertIndex(pixCoord.x, pixCoord.y);
   index += 288 * (temporalIndex % 64); // Empirical stride for XE_HILBERT_LEVEL=6.
   // R2 sequence: http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
   return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(8, 8, 1)] // Each thread handles 2x2 pixels; dispatch in 16x16 blocks.
    void prefilter_depths16x16_cs(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID) {
       // tex0 = native half-resolution R24_UNORM_X8 depth captured at deinterleave.
       XeGTAO_PrefilterDepths16x16(dtid, gtid, tex0, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4);
    }

        [numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)] void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
   // tex0 = R32F view-space-depth pyramid; tex1 = native packed R8G8 normals; smp = point-clamp.

   // Decode packed view-space xy and reconstruct unit z. Camera-facing normals use negative z.
   float2 nxy = tex1.Load(int3(dtid, 0)).xy * 2.0 - 1.0;
   float3 viewspaceNormal;
   viewspaceNormal.xy = nxy;
   viewspaceNormal.z = NORMAL_Z_SIGN * sqrt(saturate(1.0 - dot(nxy, nxy)));
   viewspaceNormal = normalize(viewspaceNormal);

   XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid, 0), viewspaceNormal, tex0, smp, ao_term_and_edges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)] void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID) {
   // tex0 = packed AO and edges; smp = point-clamp.
   const uint2 pix_coord_base = dtid * uint2(2, 1); // Two horizontal pixels per thread.
   XeGTAO_Denoise(pix_coord_base, tex0, smp, final_output);
}
