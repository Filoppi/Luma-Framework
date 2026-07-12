// XeGTAO adapted for Borderlands GOTY Enhanced (UE3.5) — replaces the game's NVIDIA HBAO+ (GFSDK_SSAO).
// Source: https://github.com/GameTechDev/XeGTAO
//
// BL GOTY specifics:
// - The native AO pipeline is FULL-RES (3840x2160 @ 4K): deinterleave 0xFFE232A6 -> normals 0xB2B47225 ->
//   coarse horizon AO 0xF534EB09 -> bilateral blur 0x4E1BEE34 -> apply-multiply PS 0x44764BF6. We run at
//   full res and write the final visibility term into the game's own FINAL AO target (the blur CS u0, a
//   r16g16_float; apply blit reads .x). The apply blit (blend dst*src_color) is untouched by us.
// - We bind the game's OWN constant buffers: cb0 = HBAO+ $Globals (ProjInfo = NDC->view, live FOV) and
//   cb2 = CSOffsetConstants (MinZ_MaxZRatioCS = depth linearization).
// - Depth input = the game's full-res r24_g8 scene depth (deinterleave 0xFFE232A6 t0), viewed r24_unorm_x8.
//   Read with explicit .Load: GatherRed on an r24_unorm_x8 view returns all-zeros on this driver (silently
//   kills the AO) — do not "optimize" this back to Gather.
// - Normals input = the game's ViewNormalTex (coarse-AO pass 0xF534EB09 t0), r11g11b10_float: xyz packed
//   v*0.5+0.5 (full 3-channel view-space normal, no z reconstruct). View-space already.
// - BL GOTY has NO TAA and no motion vectors: NoiseIndex is FROZEN at 0 (never feed a frame index — the
//   pattern must be static or it boils), quality default is Very High and denoise runs twice; all
//   stability is spatial.
// - viewZ is in UE3 units (near plane ~10 units) — a huge range; DepthScale (default 50 -> ~meters)
//   rescales it into the range XeGTAO's Intel-tuned constants expect (R32F pyramid, so no fp16 precision
//   loss at huge Z). DepthScale is a tuning convenience, not a correctness requirement.

// --- Game constant buffers (bound by the game at the hooked dispatches) ---

cbuffer _Globals : register(b0) // NVIDIA GFSDK_SSAO $Globals
{
   float RadiusToScreen;        // Offset:   0
   float NegInvR2;              // Offset:   4  (GFSDK name; this build stores +1/R^2 (measured positive) -> R=sqrt(1/NegInvR2), view units = vanilla AO radius)
   float NDotVBias;             // Offset:   8
   float2 InvFullResolution;    // Offset:  16  (1 / the AO input resolution; native HBAO+ runs full-res here — we size scratch + dispatches from that same depth, so the UV math matches by construction)
   float2 InvQuarterResolution; // Offset:  24
   int2 FullResOffset;          // Offset:  32
   int2 QuarterResOffset;       // Offset:  40
   float AOMultiplier;          // Offset:  48
   float PowExponent;           // Offset:  52  (vanilla darkness dial, applied in the blur CS)
   float4 ProjInfo;             // Offset:  64  (viewPos.xy = (uv*ProjInfo.xy + ProjInfo.zw) * viewZ)
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
   float4 MinZ_MaxZRatioCS;          // Offset:  96  (viewZ = 1 / (d * .z - .w), non-reverse-Z)
   float4 DynamicScaleCS;            // Offset: 112
}

// --- Luma runtime knobs (set from main.cpp; live-tunable via DEV sliders, no recompile) ---
cbuffer LumaGTAO : register(b11)
{
   float FinalValuePowerRT; // primary darkness dial, calibrated to the vanilla AO histogram
   float DepthScaleRT;      // viewZ divisor (UE3 units -> ~meters); THE dial against broad over-occlusion
   float RadiusOverrideRT;  // > 0 overrides EFFECT_RADIUS (view units after DepthScale)
   float DebugViewRT;       // DEVELOPMENT: 0=off 1=depth gradient 2=normals 3=AO x8 4=edges
}

// clang-format off
#include "Includes/Common.hlsl" // game-local: order load-bearing (pulls GameCBuffers before shared includes) — do not let SortIncludes move it
// clang-format on

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

// User configurable
//

#ifndef EFFECT_RADIUS
#define EFFECT_RADIUS 0.64 // anchored to the game's own HBAO+ radius: probed NegInvR2=0.000463 -> R=sqrt(1/NegInvR2)=46.5 UE3-units; chosen so the effective search (EFFECT_RADIUS * RADIUS_MULTIPLIER * DepthScale) = 0.64*1.457*50 = 46.6uu matches native R. RadiusOverrideRT > 0 wins.
#endif

#ifndef RADIUS_MULTIPLIER
#define RADIUS_MULTIPLIER 1.457 // Default 1.457
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.005 // punchy hard-edge falloff: full sample weight up to the radius edge = crisp contact AO matching BL's punchy native HBAO+ (vs the soft 0.615/0.95 gradual fade). Pairs with power ~1.0
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 1.5 // Default 2.0
#endif

#ifndef THIN_OCCLUDER_COMPENSATION
#define THIN_OCCLUDER_COMPENSATION 0.0 // Default 0.0; > 0 causes more mistakes than it fixes on big geometry
#endif

#ifndef FINAL_VALUE_POWER
#define FINAL_VALUE_POWER 1.0 // Default 2.2; shadow default for FinalValuePowerRT (the CB value is what actually applies)
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

// BL packs the full xyz view-space normal (v*0.5+0.5) — decoded directly, no reconstruction. NORMAL_Z_SIGN
// flips only the decoded z if the view-space handedness needs it (verify via DebugViewRT=2: smooth
// per-surface shading = correct; flip to -1.0 if the shading looks inverted).
#ifndef NORMAL_Z_SIGN
#define NORMAL_Z_SIGN (1.0)
#endif

//

#define VIEWPORT_PIXEL_SIZE InvFullResolution

// GFSDK ProjInfo is exactly the NDC->view mul/add pair (live FOV, dialogue zoom included).
#define NDC_TO_VIEW_MUL              ProjInfo.xy
#define NDC_TO_VIEW_ADD              ProjInfo.zw
#define NDC_TO_VIEW_MUL_X_PIXEL_SIZE (NDC_TO_VIEW_MUL * VIEWPORT_PIXEL_SIZE)

#define XE_GTAO_DEPTH_MIP_LEVELS     5.0
#define XE_GTAO_OCCLUSION_TERM_SCALE 1.5

#define XE_GTAO_PI                   3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF              1.5707963267948966192313216916398

// Hardware d24 (non-reverse-Z) -> view Z via the game's own MinZ_MaxZRatioCS, then rescaled by
// DepthScaleRT so the tuned XeGTAO constants (radius/falloff, ~meter scale) apply. UE3 near plane ~10
// units and far -> infinity; without the rescale the huge Z range causes broad over-occlusion
// (mips/falloff assumptions break).
float XeGTAO_ScreenSpaceToViewSpaceDepth(const float screenDepth)
{
   float viewZ = 1.0 / max(1e-7, screenDepth * MinZ_MaxZRatioCS.z - MinZ_MaxZRatioCS.w);
   return max(0.0, viewZ) / max(1e-3, DepthScaleRT);
}

// This is also a good place to do non-linear depth conversion for cases where one wants the 'radius' (effectively the threshold between near-field and far-field GI),
// is required to be non-linear (i.e. very large outdoors environments).
float XeGTAO_ClampDepth(float depth)
{
   return clamp(depth, 0.0, 3.402823466e+38);
}

float XeGTAO_EffectRadius()
{
   return (RadiusOverrideRT > 0.0 ? RadiusOverrideRT : EFFECT_RADIUS) * RADIUS_MULTIPLIER;
}

// weighted average depth filter
float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
   float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

   const float depthRangeScaleFactor = 0.75; // found empirically :)
   const float effectRadius = depthRangeScaleFactor * XeGTAO_EffectRadius();
   const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
   const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

   // fadeout precompute optimisation
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
   // Explicit integer Loads of the 2x2 instead of GatherRed — GatherRed on the r24_unorm_x8 depth
   // view returns 0 on some drivers (flat depth -> visibility 1 -> AO silently gone). Loads read real
   // values. Out-of-bounds Loads return 0 (D3D11-defined), clamped depth.
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

// packing/unpacking for edges; 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1) for smoother transitions!
float XeGTAO_PackEdges(float4 edgesLRTB)
{
   edgesLRTB = round(saturate(edgesLRTB) * 2.9);
   return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// Inputs are screen XY and viewspace depth, output is viewspace position
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

// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
float XeGTAO_FastACos(float inX)
{
   // NOTE: no local "PI" here — the game-local Common.hlsl include chain defines PI as a macro.
   float x = abs(inX);
   float res = -0.156583 * x + 1.570796;
   res *= XeGTAO_FastSqrt(1.0 - x);
   return inX >= 0 ? res : 3.141593 - res;
}

void XeGTAO_MainPass(uint2 pixCoord, float2 localNoise, float3 viewspaceNormal, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<unorm float2> outWorkingAOTermAndEdges)
{
   float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

   // Center + cross depths from the prefiltered (already linearized + scaled) mip0. This texture is our
   // own R32F, Gather is safe here (the r24 quirk is only on the game's depth view).
   float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE));
   float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));

   // viewspace Z at the center
   float viewspaceZ = valuesUL.y;

   // viewspace Zs left top right bottom
   const float pixLZ = valuesUL.x;
   const float pixTZ = valuesUL.z;
   const float pixRZ = valuesBR.z;
   const float pixBZ = valuesBR.x;

   float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
   const float edges = XeGTAO_PackEdges(edgesLRTB);

#if DEVELOPMENT
   // Debug views (visible on screen through the game's own AO apply blit; the final denoise passes raw
   // values through when DebugViewRT > 0).
   if (DebugViewRT > 0.5 && DebugViewRT < 1.5) // 1 = depth gradient (proves live depth + linearization/scale)
   {
      outWorkingAOTermAndEdges[pixCoord] = float2(saturate(frac(log2(max(viewspaceZ, 1e-6)))), 1.0);
      return;
   }
   if (DebugViewRT >= 3.5 && DebugViewRT < 4.5) // 4 = edges
   {
      outWorkingAOTermAndEdges[pixCoord] = float2(dot(edgesLRTB, 0.25), 1.0);
      return;
   }
#endif

   // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
   viewspaceZ *= 0.99999; // this is good for FP32 depth buffer

   const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
   const float3 viewVec = normalize(-pixCenterPos);

   // prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases, but in Vanilla it seems rare so it's disabled by default
   viewspaceNormal = normalize(viewspaceNormal + max(0, -dot(viewspaceNormal, viewVec)) * viewVec);

#if DEVELOPMENT
   if (DebugViewRT >= 1.5 && DebugViewRT < 2.5) // 2 = normals view-facing term (smooth per-surface shading = correct decode)
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

   // fadeout precompute optimisation
   const float falloffMul = -1.0 / falloffRange;
   const float falloffAdd = falloffFrom / falloffRange + 1.0;

   float visibility = 0.0;

   // see "Algorithm 1" in https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
   {
      const float noiseSlice = localNoise.x;
      const float noiseSample = localNoise.y;

      // quality settings / tweaks / hacks
      const float pixelTooCloseThreshold = 1.3; // if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance

      // approx viewspace pixel size at pixCoord; approximation of NDCToViewspace( normalizedScreenPos.xy + consts.ViewportPixelSize.xy, pixCenterPos.z ).xy - pixCenterPos.xy;
      const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDC_TO_VIEW_MUL_X_PIXEL_SIZE;

      float screenspaceRadius = effectRadius * rcp(abs(pixelDirRBViewspaceSizeAtCenterZ.x));

      // fade out for small screen radii
      visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

      // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
      const float minS = pixelTooCloseThreshold * rcp(screenspaceRadius);

      //[unroll]
      for (float slice = 0.0; slice < SLICE_COUNT; slice++)
      {
         float sliceK = (slice + noiseSlice) / SLICE_COUNT;
         // lines 5, 6 from the paper
         float phi = sliceK * XE_GTAO_PI;
         float cosPhi = cos(phi);
         float sinPhi = sin(phi);
         float2 omega = float2(cosPhi, -sinPhi); // lpfloat2 on omega causes issues with big radii

         // convert to screen units (pixels) for later use
         omega *= screenspaceRadius;

         // line 8 from the paper
         const float3 directionVec = float3(cosPhi, sinPhi, 0.0);

         // line 9 from the paper
         const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

         // line 10 from the paper
         // axisVec is orthogonal to directionVec and viewVec, used to define projectedNormal
         const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

         // line 11 from the paper
         float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

         // line 13 from the paper
         float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));

         // line 14 from the paper
         float projectedNormalVecLength = length(projectedNormalVec);
         float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(projectedNormalVecLength));

         // line 15 from the paper
         float n = signNorm * XeGTAO_FastACos(cosNorm);

         // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
         const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
         const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

         // lines 17, 18 from the paper, manually unrolled the 'side' loop
         float horizonCos0 = lowHorizonCos0; //-1;
         float horizonCos1 = lowHorizonCos1; //-1;

         [unroll] for (float step = 0.0; step < STEPS_PER_SLICE; step++)
         {
            // R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
            const float stepBaseNoise = (slice + step * STEPS_PER_SLICE) * 0.6180339887498948482; // <- this should unroll
            float stepNoise = frac(noiseSample + stepBaseNoise);

            // approx line 20 from the paper, with added noise
            float s = (step + stepNoise) / STEPS_PER_SLICE; // + (lpfloat2)1e-6f);

            // additional distribution modifier
            s = pow(s, sampleDistributionPower);

            // avoid sampling center pixel
            s += minS;

            // approx lines 21-22 from the paper, unrolled
            float2 sampleOffset = s * omega;

            float sampleOffsetLength = length(sampleOffset);

            // note: when sampling, using point_point_point or point_point_linear sampler works, but linear_linear_linear will cause unwanted interpolation between neighbouring depth values on the same MIP level!
            const float mipLevel = clamp(log2(sampleOffsetLength) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, XE_GTAO_DEPTH_MIP_LEVELS);

            // Snap to pixel center (more correct direction math, avoids artifacts due to sampling pos not matching depth texel center - messes up slope - but adds other
            // artifacts due to them being pushed off the slice). Also use full precision for high res cases.
            sampleOffset = round(sampleOffset) * VIEWPORT_PIXEL_SIZE;

            float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
            float SZ0 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
            float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0);

            float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
            float SZ1 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
            float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1);

            float3 sampleDelta0 = samplePos0 - pixCenterPos; // using lpfloat for sampleDelta causes precision issues
            float3 sampleDelta1 = samplePos1 - pixCenterPos; // using lpfloat for sampleDelta causes precision issues
            float sampleDist0 = length(sampleDelta0);
            float sampleDist1 = length(sampleDelta1);

            // approx lines 23, 24 from the paper, unrolled
            float3 sampleHorizonVec0 = sampleDelta0 * rcp(sampleDist0);
            float3 sampleHorizonVec1 = sampleDelta1 * rcp(sampleDist1);

            // any sample out of radius should be discarded - also use fallof range for smooth transitions; this is a modified idea from "4.3 Implementation details, Bounding the sampling area"
            // this is our own thickness heuristic that relies on sooner discarding samples behind the center
            float falloffBase0 = length(float3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1.0 + thinOccluderCompensation)));
            float falloffBase1 = length(float3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1.0 + thinOccluderCompensation)));
            float weight0 = saturate(falloffBase0 * falloffMul + falloffAdd);
            float weight1 = saturate(falloffBase1 * falloffMul + falloffAdd);

            // sample horizon cos
            float shc0 = dot(sampleHorizonVec0, viewVec);
            float shc1 = dot(sampleHorizonVec1, viewVec);

            // discard unwanted samples
            shc0 = lerp(lowHorizonCos0, shc0, weight0); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos0), acos(shc0), weight0 ));
            shc1 = lerp(lowHorizonCos1, shc1, weight1); // this would be more correct but too expensive: cos(lerp( acos(lowHorizonCos1), acos(shc1), weight1 ));

            // thickness heuristic disabled (THIN_OCCLUDER_COMPENSATION == 0)
            horizonCos0 = max(horizonCos0, shc0);
            horizonCos1 = max(horizonCos1, shc1);
         }

#if 1 // I can't figure out the slight overdarkening on high slopes, so I'm adding this fudge - in the training set, 0.05 is close (PSNR 21.34) to disabled (PSNR 21.45)
         projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);
#endif

         // line ~27, unrolled
         float h0 = -XeGTAO_FastACos(horizonCos1);
         float h1 = XeGTAO_FastACos(horizonCos0);
         float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
         float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
         float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
         visibility += localVisibility;
      }
      visibility /= SLICE_COUNT;
      visibility = pow(visibility, max(0.05, FinalValuePowerRT)); // runtime dial (calibrated to the vanilla AO histogram)
      visibility = max(0.03, visibility);                         // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)
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
   edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0; // there's really no need for mask (as it's an 8 bit input) but I'll leave it in so it doesn't cause any trouble in the future
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
                    RWTexture2D<float2> outputTexture // the game's final AO target is r16g16_float (apply reads .x)
#else
                    RWTexture2D<unorm float2> outputTexture
#endif
)
{
#if DEVELOPMENT
   // Debug views: pass the raw working value through unblurred so the on-screen viz is exact.
   if (DebugViewRT > 0.5)
   {
      for (int dside = 0; dside < 2; dside++)
      {
         const uint2 dpix = uint2(pixCoordBase.x + dside, pixCoordBase.y);
         float v = sourceAOTermAndEdges.Load(int3(dpix, 0)).x;
#if XE_GTAO_FINAL_APPLY
         if (DebugViewRT >= 2.5 && DebugViewRT < 3.5) // 3 = AO x8 amplification (spot broad over-occlusion)
            v = saturate(1.0 - (1.0 - v * XE_GTAO_OCCLUSION_TERM_SCALE) * 8.0);
         outputTexture[dpix] = float2(v, 0.0);
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

   float aoTerm[2]; // pixel pixCoordBase and pixel pixCoordBase + int2( 1, 0 )
   float4 edgesC_LRTB[2];
   float weightTL[2];
   float weightTR[2];
   float weightBL[2];
   float weightBR[2];

   // gather edge and visibility quads, used later
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

      // Edges aren't perfectly symmetrical: edge detection algorithm does not guarantee that a left edge on the right pixel will match the right edge on the left pixel (although
      // they will match in majority of cases). This line further enforces the symmetricity, creating a slightly sharper blur. Works real nice with TAA.
      edgesC_LRTB[side] *= float4(edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z);

#if 1 // this allows some small amount of AO leaking from neighbours if there are 3 or 4 edges; this reduces both spatial and temporal aliasing
      const float leak_threshold = 2.5;
      const float leak_strength = 0.5;
      float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.0)) * rcp(4.0 - leak_threshold)) * leak_strength;
      edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);
#endif

      // for diagonals; used by first and second pass
      weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
      weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
      weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
      weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

      // first pass
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
      // The game's final AO buffer is r16g16_float; the apply blit reads .x. Write AO to .x (.y unused).
      outputTexture[pixCoord] = float2(saturate(aoTerm[side] * XE_GTAO_OCCLUSION_TERM_SCALE), 0.0);
#else
      outputTexture[pixCoord] = float2(aoTerm[side], side == 0 ? edgesQ0.y : edgesQ1.x);
#endif
   }
}

// Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SamplerState smp : register(s0); // point-clamp, bound by Luma (the game's AO CS uses raw Loads, s0 is not reliably set)

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);

RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);
RWTexture2D<unorm float2> ao_term_and_edges : register(u0);

#if XE_GTAO_FINAL_APPLY
RWTexture2D<float2> final_output : register(u0); // the game's r16g16_float final AO (apply blit reads .x)
#else
RWTexture2D<unorm float2> final_output : register(u0);
#endif

#define XE_GTAO_NUMTHREADS_X 8
#define XE_GTAO_NUMTHREADS_Y 8

// From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
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

// BL GOTY has no TAA: temporalIndex is ALWAYS 0 (frozen pattern — static noise instead of boiling).
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
   float2 noise;
   uint index = HilbertIndex(pixCoord.x, pixCoord.y);
   index += 288 * (temporalIndex % 64); // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)
   // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
   return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(8, 8, 1)] // <- hard coded to 8x8; each thread computes 2x2 blocks so processing 16x16 block: Dispatch needs to be called with (width + 16-1) / 16, (height + 16-1) / 16
    void prefilter_depths16x16_cs(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID) {
       // tex0 = the game's full-res scene depth (r24_g8, viewed r24_unorm_x8), captured at the deinterleave dispatch
       XeGTAO_PrefilterDepths16x16(dtid, gtid, tex0, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4);
    }

        [numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)] void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
   // tex0 = prefiltered viewspace depth MIP pyramid (our R32F)
   // tex1 = the game's ViewNormalTex (r11g11b10_float, captured at the coarse-AO dispatch)
   // smp = point-clamp

   // Decode the game's packed view-space normals: r11g11b10_float, xyz in [0,1] -> [-1,1]. Full 3-channel,
   // so no z reconstruction; NORMAL_Z_SIGN flips z only if the handedness needs
   // it (verify via DebugViewRT=2; flip to -1.0 if shading looks inverted).
   float3 n = tex1.Load(int3(dtid, 0)).xyz * 2.0 - 1.0;
   n.z *= NORMAL_Z_SIGN;
   float3 viewspaceNormal = normalize(n);

   XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid, 0), viewspaceNormal, tex0, smp, ao_term_and_edges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)] void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID) {
   // tex0 = g_srcWorkingAOTerm and g_srcWorkingEdges, packed
   // smp = point-clamp
   const uint2 pix_coord_base = dtid * uint2(2, 1); // we're computing 2 horizontal pixels at a time (performance optimization)
   XeGTAO_Denoise(pix_coord_base, tex0, smp, final_output);
}
