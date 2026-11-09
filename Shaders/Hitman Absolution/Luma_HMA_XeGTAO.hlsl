// Copyright (C) 2016-2021, Intel Corporation
// SPDX-License-Identifier: MIT
// XeGTAO adapted from Luma's Fallout 4 / MGS4 implementations.
// Reference: https://github.com/GameTechDev/XeGTAO
// HMA: runs at the raw device depth resolution (R32_FLOAT, t0 of the game's SSAO draw), spatial noise only, two denoise passes.
// Writes visibility (1 = unoccluded) in rgb of the final R16G16B16A16_FLOAT target, alpha 0.

#ifndef XE_GTAO_QUALITY
#define XE_GTAO_QUALITY 2
#endif

// Game per-view constant buffer (PS b1 of the SSAO draw), bound to the CS at b1.
// Row 12: depth unpack (a = [2][2]-like scale, b = offset), rows 14 and 15: view rays at z=1 for uv (0,0) and uv (1,1).
cbuffer GameView : register(b1)
{
    float4 GameCB1[16];
}

cbuffer LumaGTAO : register(b2)
{
    uint2 ViewportSize;
    float2 ViewportPixelSize;
    float DepthScale; // Multiplier applied to the linear view depth (set to -1 if the view depth comes out negative in testing).
    float EffectRadius; // In view depth units (meters).
    float FinalValuePower; // "Intensity": exponent applied to the visibility.
    uint LinearDepth; // 1 when the SRV already contains positive linear view depth.
    float Thickness; // GT-VBAO only (main_pass_vbao_cs): constant thickness of the depth surfaces, in view depth units (meters).
}

static const float FarDepth = 1e10;

// Camera, derived from the game's constant buffer by "InitCamera()"
static float2 NDCToViewMul;
static float2 NDCToViewAdd;
static float CamDepthA; // viewZ = 1 / (CamDepthA * hardwareDepth - CamDepthB)
static float CamDepthB;

void InitCamera()
{
    // Rays at z = 1 for uv (0,0) and (1,1). The view position is (lerp(ray0, ray1, uv)) * viewZ, which is what
    // NDCToViewMul * uv + NDCToViewAdd gives, so the axis sign convention follows the game's own rays.
    NDCToViewMul = GameCB1[15].xy - GameCB1[14].xy;
    NDCToViewAdd = GameCB1[14].xy;
    CamDepthA = GameCB1[12].z;
    CamDepthB = GameCB1[12].y;
}

#if XE_GTAO_QUALITY == 0 // Low
    #define SLICE_COUNT 2.0
#elif XE_GTAO_QUALITY == 1 // Medium
    #define SLICE_COUNT 4.0
#elif XE_GTAO_QUALITY == 2 // High
    #define SLICE_COUNT 6.0
#elif XE_GTAO_QUALITY == 3 // Very High
    #define SLICE_COUNT 8.0
#elif XE_GTAO_QUALITY == 4 // Ultra
    #define SLICE_COUNT 10.0
#endif

#define EFFECT_RADIUS max(EffectRadius, 1e-4)
#define EFFECT_FALLOFF_RANGE 0.615
#define SAMPLE_DISTRIBUTION_POWER 2.0
#define FINAL_VALUE_POWER max(FinalValuePower, 0.01)
#define DEPTH_MIP_SAMPLING_OFFSET 4.5
#define STEPS_PER_SLICE 3.0
#define DENOISE_BLUR_BETA 1.2

#define XE_GTAO_OCCLUSION_TERM_SCALE 1.5
#define XE_GTAO_PI 3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF 1.5707963267948966192313216916398

#define VIEWPORT_PIXEL_SIZE ViewportPixelSize
#define NDC_TO_VIEW_MUL NDCToViewMul
#define NDC_TO_VIEW_ADD NDCToViewAdd
#define NDC_TO_VIEW_MUL_X_PIXEL_SIZE (NDC_TO_VIEW_MUL * VIEWPORT_PIXEL_SIZE)

float XeGTAO_ScreenSpaceToViewSpaceDepth(float depth)
{
    if (!LinearDepth)
    {
        depth = 1.0 / (CamDepthA * depth - CamDepthB);
    }
    depth *= DepthScale;
    return (isfinite(depth) && depth > 0.0) ? min(depth, FarDepth) : FarDepth;
}

float XeGTAO_ClampDepth(float depth)
{
    return clamp(depth, 1e-6, FarDepth);
}

float GetEffectRadius(float viewspaceZ)
{
    return EFFECT_RADIUS;
}

// weighted average depth filter
float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
    float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

    const float depthRangeScaleFactor = 0.75; // found empirically :)
    const float effectRadius = depthRangeScaleFactor * GetEffectRadius(maxDepth);
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
void XeGTAO_PrefilterDepths16x16(uint2 dispatchThreadID, uint2 groupThreadID, Texture2D sourceNDCDepth, SamplerState depthSampler, RWTexture2D<float> outDepth0, RWTexture2D<float> outDepth1, RWTexture2D<float> outDepth2, RWTexture2D<float> outDepth3, RWTexture2D<float> outDepth4)
{
    // MIP 0
    const uint2 baseCoord = dispatchThreadID;
    const uint2 pixCoord = baseCoord * 2;
    float4 depths4;
    depths4.w = sourceNDCDepth.Load(int3(min(pixCoord, ViewportSize - 1), 0)).x;
    depths4.z = sourceNDCDepth.Load(int3(min(pixCoord + uint2(1, 0), ViewportSize - 1), 0)).x;
    depths4.x = sourceNDCDepth.Load(int3(min(pixCoord + uint2(0, 1), ViewportSize - 1), 0)).x;
    depths4.y = sourceNDCDepth.Load(int3(min(pixCoord + uint2(1, 1), ViewportSize - 1), 0)).x;
    float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.w));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.z));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.x));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.y));
    if (all(pixCoord + uint2(0, 0) < ViewportSize))
        outDepth0[pixCoord + uint2(0, 0)] = depth0;
    if (all(pixCoord + uint2(1, 0) < ViewportSize))
        outDepth0[pixCoord + uint2(1, 0)] = depth1;
    if (all(pixCoord + uint2(0, 1) < ViewportSize))
        outDepth0[pixCoord + uint2(0, 1)] = depth2;
    if (all(pixCoord + uint2(1, 1) < ViewportSize))
        outDepth0[pixCoord + uint2(1, 1)] = depth3;

    // MIP 1
    float dm1 = XeGTAO_DepthMIPFilter(depth0, depth1, depth2, depth3);
    if (all(baseCoord < max(ViewportSize >> 1, 1)))
        outDepth1[baseCoord] = dm1;
    g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

    GroupMemoryBarrierWithGroupSync();

    // MIP 2
    [branch]
    if (all((groupThreadID.xy % 2) == 0)) {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
        float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

        float dm2 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        if (all(baseCoord / 2 < max(ViewportSize >> 2, 1)))
            outDepth2[baseCoord / 2] = dm2;
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    [branch]
    if (all((groupThreadID.xy % 4) == 0)) {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
        float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

        float dm3 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        if (all(baseCoord / 4 < max(ViewportSize >> 3, 1)))
            outDepth3[baseCoord / 4] = dm3;
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 4
    [branch]
    if (all((groupThreadID.xy % 8) == 0)) {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
        float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

        float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        if (all(baseCoord / 8 < max(ViewportSize >> 4, 1)))
            outDepth4[baseCoord / 8] = dm4;
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
    const float pi = 3.141593;
    const float half_pi = 1.570796;
    float x = abs(inX);
    float res = -0.156583 * x + half_pi;
    res *= XeGTAO_FastSqrt(1.0 - x);
    return inX >= 0 ? res : pi - res;
}

// Weight each depth triangle by its two edge confidences, preventing geometry
// across depth discontinuities from dominating the reconstructed normal.
float3 XeGTAO_CalculateNormal(float4 edges, float3 center, float3 left, float3 right, float3 top, float3 bottom)
{
    float4 weights = saturate(float4(edges.x * edges.z, edges.z * edges.y, edges.y * edges.w, edges.w * edges.x) + 0.01);
    left = normalize(left - center);
    right = normalize(right - center);
    top = normalize(top - center);
    bottom = normalize(bottom - center);
    float3 normal = weights.x * cross(left, top) + weights.y * cross(top, right)
                  + weights.z * cross(right, bottom) + weights.w * cross(bottom, left);
    return dot(normal, normal) > 1e-12 ? normalize(normal) : normalize(-center);
}

void XeGTAO_MainPass(uint2 pixCoord, float2 localNoise, float3 viewspaceNormal, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<unorm float2> outWorkingAOTermAndEdges)
{
    uint depthWidth, depthHeight, depthMipLevels;
    sourceViewspaceDepth.GetDimensions(0, depthWidth, depthHeight, depthMipLevels);
    float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

    // The depth chain is our own R32_FLOAT texture (not a game depth view), so Gather is safe here.
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

    if (viewspaceZ >= FarDepth)
    {
        outWorkingAOTermAndEdges[pixCoord] = float2(1.0 / XE_GTAO_OCCLUSION_TERM_SCALE, edges);
        return;
    }

    float3 center = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, center,
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1, 0) * VIEWPORT_PIXEL_SIZE, pixLZ),
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(1, 0) * VIEWPORT_PIXEL_SIZE, pixRZ),
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0, -1) * VIEWPORT_PIXEL_SIZE, pixTZ),
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0, 1) * VIEWPORT_PIXEL_SIZE, pixBZ));

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision
    viewspaceZ *= 0.99999;

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    const float3 viewVec = normalize(-pixCenterPos);

    // prevents normals that are facing away from the view vector
    viewspaceNormal = normalize(viewspaceNormal + max(0.0, -dot(viewspaceNormal, viewVec)) * viewVec);

    const float effectRadius = GetEffectRadius(viewspaceZ);
    const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
    const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

    // fadeout precompute optimisation
    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float visibility = 0.0;
    float unoccludedVisibility = 0.0;

    // see "Algorithm 1" in https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
    {
        const float noiseSlice = localNoise.x;
        const float noiseSample = localNoise.y;

        // quality settings / tweaks / hacks
        const float pixelTooCloseThreshold = 1.3; // if the offset is under approx pixel size (pixelTooCloseThreshold), push it out to the minimum distance

        // approx viewspace pixel size at pixCoord
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDC_TO_VIEW_MUL_X_PIXEL_SIZE;

        float screenspaceRadius = effectRadius * rcp(max(abs(pixelDirRBViewspaceSizeAtCenterZ.x), 1e-6));

        // this is the min distance to start sampling from to avoid sampling from the center pixel
        const float minS = pixelTooCloseThreshold * rcp(screenspaceRadius);

        [unroll]
        for (float slice = 0.0; slice < SLICE_COUNT; slice++) {
            float sliceK = (slice + noiseSlice) / SLICE_COUNT;
            // lines 5, 6 from the paper
            float phi = sliceK * XE_GTAO_PI;
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float2 omega = float2(cosPhi, -sinPhi); //lpfloat2 on omega causes issues with big radii

            // convert to screen units (pixels) for later use
            omega *= screenspaceRadius;

            // line 8 from the paper
            const float3 directionVec = float3(cosPhi, sinPhi, 0.0);

            // line 9 from the paper
            const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

            // line 10 from the paper
            float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

            // line 11 from the paper
            float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            // line 13 from the paper
            float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));

            // line 14 from the paper
            float projectedNormalVecLength = length(projectedNormalVec);
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(max(projectedNormalVecLength, 1e-6)));

            // line 15 from the paper
            float n = signNorm * XeGTAO_FastACos(cosNorm);

            // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
            const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
            const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

            // lines 17, 18 from the paper, manually unrolled the 'side' loop
            float horizonCos0 = lowHorizonCos0;
            float horizonCos1 = lowHorizonCos1;

            [unroll]
            for (float step = 0.0; step < STEPS_PER_SLICE; step++) {
                // R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
                const float stepBaseNoise = (slice + step * STEPS_PER_SLICE) * 0.6180339887498948482;
                float stepNoise = frac(noiseSample + stepBaseNoise);

                // approx line 20 from the paper, with added noise
                float s = (step + stepNoise) / STEPS_PER_SLICE;

                // additional distribution modifier
                s = pow(s, SAMPLE_DISTRIBUTION_POWER);

                // avoid sampling center pixel
                s += minS;

                // approx lines 21-22 from the paper, unrolled
                float2 sampleOffset = s * omega;

                float sampleOffsetLength = length(sampleOffset);

                // point sampling only: linear filtering would mix neighbouring depths on the same MIP level
                const float mipLevel = clamp(log2(sampleOffsetLength) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, (float(depthMipLevels) - 1.0));

                // Snap to pixel center
                sampleOffset = round(sampleOffset) * VIEWPORT_PIXEL_SIZE;

                // Keep the sample direction on its slice. Reject off screen samples below.
                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float SZ0 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0);

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float SZ1 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1);

                float3 sampleDelta0 = samplePos0 - pixCenterPos;
                float3 sampleDelta1 = samplePos1 - pixCenterPos;
                float sampleDist0 = length(sampleDelta0);
                float sampleDist1 = length(sampleDelta1);

                // approx lines 23, 24 from the paper, unrolled
                float3 sampleHorizonVec0 = sampleDelta0 * rcp(max(sampleDist0, 1e-6));
                float3 sampleHorizonVec1 = sampleDelta1 * rcp(max(sampleDist1, 1e-6));

                // any sample out of radius should be discarded - also use fallof range for smooth transitions
                float falloffBase0 = length(sampleDelta0);
                float falloffBase1 = length(sampleDelta1);
                // The sampler clamps, but depths fetched outside the screen must not affect either horizon.
                float weight0 = saturate(falloffBase0 * falloffMul + falloffAdd) * (all(sampleScreenPos0 >= 0.0) && all(sampleScreenPos0 < 1.0));
                float weight1 = saturate(falloffBase1 * falloffMul + falloffAdd) * (all(sampleScreenPos1 >= 0.0) && all(sampleScreenPos1 < 1.0));

                // sample horizon cos
                float shc0 = dot(sampleHorizonVec0, viewVec);
                float shc1 = dot(sampleHorizonVec1, viewVec);

                // discard unwanted samples
                shc0 = lerp(lowHorizonCos0, shc0, weight0);
                shc1 = lerp(lowHorizonCos1, shc1, weight1);

                horizonCos0 = max(horizonCos0, shc0);
                horizonCos1 = max(horizonCos1, shc1);
            }

            // XeGTAO's fudge against slight overdarkening on high slopes
            projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);

            // line ~27, unrolled
            float h0 = -XeGTAO_FastACos(horizonCos1);
            float h1 = XeGTAO_FastACos(horizonCos0);

            float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
            float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
            float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
            visibility += localVisibility;

            // Evaluate the same integral without occluders, used as the reference so an unobstructed surface stays fully visible.
            float unoccludedH0 = -XeGTAO_FastACos(lowHorizonCos1);
            float unoccludedH1 = XeGTAO_FastACos(lowHorizonCos0);
            float unoccludedIarc0 = (cosNorm + 2.0 * unoccludedH0 * sin(n) - cos(2.0 * unoccludedH0 - n)) / 4.0;
            float unoccludedIarc1 = (cosNorm + 2.0 * unoccludedH1 * sin(n) - cos(2.0 * unoccludedH1 - n)) / 4.0;
            unoccludedVisibility += projectedNormalVecLength * (unoccludedIarc0 + unoccludedIarc1);
        }
        visibility /= max(unoccludedVisibility, 1e-6);
        visibility = max(visibility, 0.0);
        visibility = max(0.03, visibility); // disallow total occlusion, in linear space. The intensity pow is applied in denoise_final_cs
    }

    visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
    outWorkingAOTermAndEdges[pixCoord] = float2(visibility, edges);
}

// GT-VBAO (visibility bitmask, Therrien et al.): same preamble, edges, normal, slices, steps, sample pattern, noise and radius as XeGTAO_MainPass.
// Each slice is a 32 sector bitmask over the hemisphere of the normal (bin 0 and 32 are the hemisphere edges).
// A sample occludes the arc between its front surface and the same surface pushed back by Thickness along the view ray.
void VBAO_AddInterval(inout uint occupiedBits, float u0, float u1)
{
    const uint fullMask = 0xffffffffu;
    const uint2 bins = (uint2)floor(float2(min(u0, u1), max(u0, u1)) * 32.0);
    uint startMask = bins.x < 32u ? (fullMask << bins.x) : 0u;
    uint endMask = bins.y != 0u ? (fullMask >> (32u - bins.y)) : 0u;
    occupiedBits |= startMask & endMask;
}

void XeGTAO_MainPassVBAO(uint2 pixCoord, float2 localNoise, float3 viewspaceNormal, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<unorm float2> outWorkingAOTermAndEdges)
{
    uint depthWidth, depthHeight, depthMipLevels;
    sourceViewspaceDepth.GetDimensions(0, depthWidth, depthHeight, depthMipLevels);
    float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

    float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE));
    float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));

    float viewspaceZ = valuesUL.y;

    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;

    float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    const float edges = XeGTAO_PackEdges(edgesLRTB);

    if (viewspaceZ >= FarDepth)
    {
        outWorkingAOTermAndEdges[pixCoord] = float2(1.0 / XE_GTAO_OCCLUSION_TERM_SCALE, edges);
        return;
    }

    float3 center = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, center,
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1, 0) * VIEWPORT_PIXEL_SIZE, pixLZ),
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(1, 0) * VIEWPORT_PIXEL_SIZE, pixRZ),
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0, -1) * VIEWPORT_PIXEL_SIZE, pixTZ),
        XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0, 1) * VIEWPORT_PIXEL_SIZE, pixBZ));

    viewspaceZ *= 0.99999;

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    const float3 viewVec = normalize(-pixCenterPos);

    viewspaceNormal = normalize(viewspaceNormal + max(0.0, -dot(viewspaceNormal, viewVec)) * viewVec);

    const float effectRadius = GetEffectRadius(viewspaceZ);
    const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
    const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float visibilitySum = 0.0;
    float weightSum = 0.0;
    {
        const float noiseSlice = localNoise.x;
        const float noiseSample = localNoise.y;

        const float pixelTooCloseThreshold = 1.3;

        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDC_TO_VIEW_MUL_X_PIXEL_SIZE;

        float screenspaceRadius = effectRadius * rcp(max(abs(pixelDirRBViewspaceSizeAtCenterZ.x), 1e-6));

        const float minS = pixelTooCloseThreshold * rcp(screenspaceRadius);

        [unroll]
        for (float slice = 0.0; slice < SLICE_COUNT; slice++) {
            float sliceK = (slice + noiseSlice) / SLICE_COUNT;
            float phi = sliceK * XE_GTAO_PI;
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float2 omega = float2(cosPhi, -sinPhi);

            omega *= screenspaceRadius;

            const float3 directionVec = float3(cosPhi, sinPhi, 0.0);

            const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);

            // In-slice axis that separates the two sides of the slice (+: the direction of directionVec)
            const float3 sideAxis = orthoDirectionVec * rsqrt(max(dot(orthoDirectionVec, orthoDirectionVec), 1e-12));

            float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

            float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));

            float projectedNormalVecLength = length(projectedNormalVec);
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(max(projectedNormalVecLength, 1e-6)));

            // sin of the normal angle in the slice (positive toward sideAxis), the bitmask coordinate of the normal is 0.5 - 0.5 * sin
            float sinNorm = signNorm * sqrt(max(1.0 - cosNorm * cosNorm, 0.0));
            const float baseCoord = 0.5 - 0.5 * sinNorm;

            // Sub bin jitter of the arcs, to dither the 32 sectors
            const float arcJitter = frac(noiseSample + slice * 0.5698402909980532) / 32.0;

            uint occupiedBits = 0u;

            [unroll]
            for (float step = 0.0; step < STEPS_PER_SLICE; step++) {
                const float stepBaseNoise = (slice + step * STEPS_PER_SLICE) * 0.6180339887498948482;
                float stepNoise = frac(noiseSample + stepBaseNoise);

                float s = (step + stepNoise) / STEPS_PER_SLICE;

                s = pow(s, SAMPLE_DISTRIBUTION_POWER);

                s += minS;

                float2 sampleOffset = s * omega;

                float sampleOffsetLength = length(sampleOffset);

                const float mipLevel = clamp(log2(sampleOffsetLength) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, (float(depthMipLevels) - 1.0));

                sampleOffset = round(sampleOffset) * VIEWPORT_PIXEL_SIZE;

                [unroll]
                for (int side = 0; side < 2; side++) {
                    const float2 sampleScreenPos = side == 0 ? normalizedScreenPos + sampleOffset : normalizedScreenPos - sampleOffset;
                    // Off screen samples don't occlude
                    if (any(sampleScreenPos < 0.0) || any(sampleScreenPos >= 1.0))
                        continue;
                    const float sampleDepth = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos, mipLevel).x;
                    if (!(sampleDepth < FarDepth))
                        continue;
                    const float3 samplePos = XeGTAO_ComputeViewspacePosition(sampleScreenPos, sampleDepth);
                    const float3 sampleDelta = samplePos - pixCenterPos;
                    const float sampleDist = length(sampleDelta);
                    // Hard cut at the effect radius (a bitmask has no per sample weight)
                    if (sampleDist >= effectRadius)
                        continue;
                    const float3 frontDir = sampleDelta * rcp(max(sampleDist, 1e-6));
                    // The same surface pushed back by the thickness along the view ray (ray at z = 1, so Thickness is in depth units)
                    const float3 viewRay = XeGTAO_ComputeViewspacePosition(sampleScreenPos, 1.0);
                    const float3 backDir = normalize(sampleDelta + viewRay * Thickness);

                    const float cosFront = dot(frontDir, viewVec);
                    const float cosBack = dot(backDir, viewVec);
                    const float sideSign = dot(frontDir, sideAxis) < 0.0 ? -1.0 : 1.0;

                    const float u0 = saturate(baseCoord + sideSign * (1.0 - cosFront) * 0.5 + arcJitter);
                    const float u1 = saturate(baseCoord + sideSign * (1.0 - cosBack) * 0.5 + arcJitter);
                    VBAO_AddInterval(occupiedBits, u0, u1);
                }
            }

            // Cosine weighted by the projected normal length, as in XeGTAO
            const float sliceVisibility = 1.0 - float(countbits(occupiedBits)) / 32.0;
            visibilitySum += projectedNormalVecLength * sliceVisibility;
            weightSum += projectedNormalVecLength;
        }

        float visibility = weightSum > 1e-6 ? visibilitySum / weightSum : 1.0;
        visibility = max(0.03, visibility); // disallow total occlusion, in linear space. The intensity pow is applied in denoise_final_cs
        visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
        outWorkingAOTermAndEdges[pixCoord] = float2(visibility, edges);
    }
}

void XeGTAO_DecodeGatherPartial(float4 packedValue, out float outDecoded[4])
{
    for (int i = 0; i < 4; i++) {
        outDecoded[i] = packedValue[i];
    }
}

float4 XeGTAO_UnpackEdges(float _packedVal)
{
    uint packedVal = uint(_packedVal * 255.5);
    float4 edgesLRTB;
    edgesLRTB.x = float((packedVal >> 6) & 0x03) / 3.0;
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

// Fills outValue[side] for the two pixels at pixCoordBase (x and x + 1).
// Not final: (visibility, packed edges) for the next denoise pass. Final: (visibility, 0) with the output scale applied.
void XeGTAO_Denoise(uint2 pixCoordBase, Texture2D sourceAOTermAndEdges, SamplerState texSampler, bool finalApply, out float2 outValue[2])
{
    const float blurAmount = finalApply ? DENOISE_BLUR_BETA : DENOISE_BLUR_BETA / 5.0;

    const float diagWeight = 0.85 * 0.5;

    float aoTerm[2];
    float4 edgesC_LRTB[2];
    float weightTL[2];
    float weightTR[2];
    float weightBL[2];
    float weightBR[2];

    outValue[0] = float2(0.0, 0.0);
    outValue[1] = float2(0.0, 0.0);

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

    for (int side = 0; side < 2; side++) {
        const int2 pixCoord = int2(pixCoordBase.x + side, pixCoordBase.y);
        if (any(pixCoord >= int2(ViewportSize))) continue;

        float4 edgesL_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.x : edgesQ0.y);
        float4 edgesT_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.z : edgesQ1.w);
        float4 edgesR_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ1.x : edgesQ1.y);
        float4 edgesB_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ2.w : edgesQ2.z);

        edgesC_LRTB[side] = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.y : edgesQ1.x);

        // Enforce edge symmetry between neighbouring pixels
        edgesC_LRTB[side] *= float4(edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z);

        // this allows some small amount of AO leaking from neighbours if there are 3 or 4 edges; this reduces both spatial and temporal aliasing
        const float leak_threshold = 2.5;
        const float leak_strength = 0.5;
        float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.0)) * rcp(4.0 - leak_threshold)) * leak_strength;
        edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);

        // for diagonals
        weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
        weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
        weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
        weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

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

        if (finalApply)
            outValue[side] = float2(saturate(aoTerm[side] * XE_GTAO_OCCLUSION_TERM_SCALE), 0.0);
        else
            outValue[side] = float2(aoTerm[side], side == 0 ? edgesQ0.y : edgesQ1.x);
    }
}

// Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SamplerState smp : register(s0);

Texture2D tex0 : register(t0);

// Each pass writes to different outputs, give them unique slots
RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);
RWTexture2D<unorm float2> ao_term_and_edges : register(u5);
RWTexture2D<unorm float2> denoised_ao : register(u6);
RWTexture2D<float4> final_ao : register(u7); // R16G16B16A16_FLOAT, rgb = visibility

#define XE_GTAO_NUMTHREADS_X 8
#define XE_GTAO_NUMTHREADS_Y 8

// From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
#define XE_HILBERT_LEVEL 6U
#define XE_HILBERT_WIDTH (1U << XE_HILBERT_LEVEL)
#define XE_HILBERT_AREA (XE_HILBERT_WIDTH * XE_HILBERT_WIDTH)
uint HilbertIndex(uint posX, uint posY)
{
    uint index = 0U;
    [unroll]
    for (uint curLevel = XE_HILBERT_WIDTH / 2U; curLevel > 0U; curLevel /= 2U) {
        uint regionX = (posX & curLevel) > 0U;
        uint regionY = (posY & curLevel) > 0U;
        index += curLevel * curLevel * ((3U * regionX) ^ regionY);
        if (regionY == 0U) {
            if (regionX == 1U) {
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

// without TAA, temporalIndex is always 0
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    index += 288 * (temporalIndex % 64);

    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(8, 8, 1)] // each thread computes 2x2 blocks so processing 16x16 block: Dispatch with ((width + 15) / 16, (height + 15) / 16, 1)
void prefilter_depths16x16_cs(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
    InitCamera();
    XeGTAO_PrefilterDepths16x16(dtid, gtid, tex0, smp, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    if (any(dtid >= ViewportSize)) return;
    InitCamera();
    // Spatial noise only (no TAA): frozen noise to prevent frame-to-frame flicker.
    XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid % 64, 0), float3(0, 0, -1), tex0, smp, ao_term_and_edges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main_pass_vbao_cs(uint2 dtid : SV_DispatchThreadID)
{
    if (any(dtid >= ViewportSize)) return;
    InitCamera();
    // Same spatial noise as main_pass_cs
    XeGTAO_MainPassVBAO(dtid, SpatioTemporalNoise(dtid % 64, 0), float3(0, 0, -1), tex0, smp, ao_term_and_edges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    // 2 horizontal pixels per thread
    const uint2 pix_coord_base = dtid * uint2(2, 1);
    float2 values[2];
    XeGTAO_Denoise(pix_coord_base, tex0, smp, false, values);
    for (int side = 0; side < 2; side++)
    {
        const uint2 pixCoord = uint2(pix_coord_base.x + side, pix_coord_base.y);
        if (all(pixCoord < ViewportSize))
            denoised_ao[pixCoord] = values[side];
    }
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_final_cs(uint2 dtid : SV_DispatchThreadID)
{
    const uint2 pix_coord_base = dtid * uint2(2, 1);
    float2 values[2];
    XeGTAO_Denoise(pix_coord_base, tex0, smp, true, values);
    for (int side = 0; side < 2; side++)
    {
        const uint2 pixCoord = uint2(pix_coord_base.x + side, pix_coord_base.y);
        if (all(pixCoord < ViewportSize))
        {
            // The consumer squares the AO (lit *= ao^2), so the exponent is halved here: effective exponent = FinalValuePower (Intensity).
            // values.x is already unpacked visibility (XeGTAO_Denoise with finalApply applies XE_GTAO_OCCLUSION_TERM_SCALE).
            const float visibility = saturate(pow(saturate(values[side].x), FINAL_VALUE_POWER * 0.5));
            final_ao[pixCoord] = float4(visibility, visibility, visibility, 0.0);
        }
    }
}
