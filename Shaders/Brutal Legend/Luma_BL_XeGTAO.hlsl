// XeGTAO for Brutal Legend (adapted from Luma_DXHR_XeGTAO.hlsl). Reference: https://github.com/GameTechDev/XeGTAO
// Intel XeGTAO: Copyright (C) 2016-2021, Intel Corporation. SPDX-License-Identifier: MIT
// Self-contained: no game cbuffers, only the LumaGTAO constant buffer (b2) set by gtao.h.
// The input is the game's raw non-linear depth (R32_FLOAT, [0,1]). Linear view depth = DepthLinW / (depth + DepthLinZ).
// DepthLinZ/W come from the game's cb4 row 16 (b3, read on the GPU). TanHalfFovX/Y come from the GPU copy of cb4 row 17 (b4)
// when UseGpuFov > 0.5, else from the CPU values (CpuTanHalfFovX/Y).
// View space is right handed (camera looks down -Z), view xy = (uv * (-2 tanX, 2 tanY) + (tanX, -tanY)) * -viewZ.
// Normals are generated from depth (no normals buffer).
// Output: full res R8_UNORM visibility (1 = unoccluded), denoised, with pow(visibility, Intensity).

#ifndef XE_GTAO_QUALITY
#define XE_GTAO_QUALITY 2
#endif

cbuffer LumaGTAO : register(b2)
{
    uint2 ViewportSize;
    float2 ViewportPixelSize;
    float CpuTanHalfFovX;
    float CpuTanHalfFovY;
    float EffectRadius;  // view space units (meters)
    float Intensity;     // final visibility exponent, 1 = neutral
    float Darkness;      // linear occlusion strength, 1 = neutral
    float UseGpuFov;     // 1 = read TanHalfFovX/Y from GameFovRow
}

// Game PS constant buffer slot 4 (cb4), bound by gtao.h. Row 16 = depth linearization (z, w). FOV is not read from here: see GameFovRow
cbuffer GameCB4 : register(b3)
{
    float4 GameCB4Rows[17];
}

// GPU copy of cb4 row 17 (gtao.h CaptureFov), bound only when valid
cbuffer GameFov : register(b4)
{
    float4 GameFovRow;
}

#define DepthLinZ GameCB4Rows[16].z
#define DepthLinW GameCB4Rows[16].w
#define TanHalfFovX (UseGpuFov > 0.5 ? abs(GameFovRow.x) : CpuTanHalfFovX)
#define TanHalfFovY (UseGpuFov > 0.5 ? abs(GameFovRow.y) : CpuTanHalfFovY)

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

#define STEPS_PER_SLICE 3.0
#define SAMPLE_DISTRIBUTION_POWER 2.0
#define THIN_OCCLUDER_COMPENSATION 0.0
#define EFFECT_FALLOFF_RANGE 0.615
#define DEPTH_MIP_SAMPLING_OFFSET 3.3
#define DENOISE_BLUR_BETA 1.2

#define XE_GTAO_DEPTH_MIP_LEVELS 5.0
#define XE_GTAO_OCCLUSION_TERM_SCALE 1.5
#define XE_GTAO_PI 3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF 1.5707963267948966192313216916398

#define NDC_TO_VIEW_MUL float2(-2.0 * TanHalfFovX, 2.0 * TanHalfFovY)
#define NDC_TO_VIEW_ADD float2(TanHalfFovX, -TanHalfFovY)
#define NDC_TO_VIEW_MUL_X_PIXEL_SIZE (NDC_TO_VIEW_MUL * ViewportPixelSize)

float XeGTAO_LinearizeDepth(float depth)
{
    // Positive view depth for depth in [0, 1)
    return max(1e-6, DepthLinW / (depth + DepthLinZ));
}

float XeGTAO_ClampDepth(float depth)
{
    return clamp(depth, 0.0, 3.402823466e+38);
}

// weighted average depth filter
float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
    float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

    const float depthRangeScaleFactor = 0.75; // found empirically :)
    const float effectRadius = depthRangeScaleFactor * EffectRadius;
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
    return (weight0 * depth0 + weight1 * depth1 + weight2 * depth2 + weight3 * depth3) * rcp(max(weightSum, 1e-6));
}

groupshared float g_scratchDepths[8][8];

float4 XeGTAO_CalculateEdges(float centerZ, float leftZ, float rightZ, float topZ, float bottomZ)
{
    float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;

    float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
    return saturate(1.25 - edgesLRTB * rcp(max(centerZ * 0.011, 1e-6)));
}

// 2 bits per edge mean 4 gradient values (0, 0.33, 0.66, 1)
float XeGTAO_PackEdges(float4 edgesLRTB)
{
    edgesLRTB = round(saturate(edgesLRTB) * 2.9);
    return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// Inputs are uv and linear view depth (positive), output is view space position (right handed)
float3 XeGTAO_ComputeViewspacePosition(float2 screenPos, float viewspaceDepth)
{
    float3 ret;
    ret.xy = (NDC_TO_VIEW_MUL * screenPos + NDC_TO_VIEW_ADD) * -viewspaceDepth;
    ret.z = -viewspaceDepth;
    return ret;
}

float3 XeGTAO_CalculateNormal(const float4 edgesLRTB, const float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos)
{
    float4 acceptedNormals = saturate(float4(edgesLRTB.x * edgesLRTB.z, edgesLRTB.z * edgesLRTB.y, edgesLRTB.y * edgesLRTB.w, edgesLRTB.w * edgesLRTB.x) + 0.01);

    pixLPos = normalize(pixLPos - pixCenterPos);
    pixRPos = normalize(pixRPos - pixCenterPos);
    pixTPos = normalize(pixTPos - pixCenterPos);
    pixBPos = normalize(pixBPos - pixCenterPos);

    float3 pixelNormal = acceptedNormals.x * cross(pixLPos, pixTPos) + acceptedNormals.y * cross(pixTPos, pixRPos) + acceptedNormals.z * cross(pixRPos, pixBPos) + acceptedNormals.w * cross(pixBPos, pixLPos);
    // View z is negated here (right handed, camera at origin looking down -Z), which mirrors the cross products
    // relative to the reference (z forward). Flip so the normal faces the camera (+Z in this space).
    pixelNormal = -normalize(pixelNormal);

    return pixelNormal;
}

float XeGTAO_FastSqrt(float x)
{
    return asfloat(0x1fbd1df5 + (asint(x) >> 1));
}

// input [-1, 1] and output [0, PI]
float XeGTAO_FastACos(float inX)
{
    const float PI = 3.141593;
    const float HALF_PI = 1.570796;
    float x = abs(inX);
    float res = -0.156583 * x + HALF_PI;
    res *= XeGTAO_FastSqrt(1.0 - x);
    return inX >= 0 ? res : PI - res;
}

void XeGTAO_MainPass(uint2 pixCoord, float2 localNoise, Texture2D sourceViewspaceDepth, SamplerState depthSampler, RWTexture2D<unorm float2> outWorkingAOTermAndEdges)
{
    float2 normalizedScreenPos = (pixCoord + 0.5) * ViewportPixelSize;

    float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * ViewportPixelSize));
    float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * ViewportPixelSize), int2(1, 1));

    // viewspace Z at the center
    float viewspaceZ = valuesUL.y;

    // viewspace Zs left top right bottom
    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;

    float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    const float edges = XeGTAO_PackEdges(edgesLRTB);

    // Normals generated from depth
    float3 CENTER = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    float3 LEFT = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1.0, 0.0) * ViewportPixelSize, pixLZ);
    float3 RIGHT = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(1.0, 0.0) * ViewportPixelSize, pixRZ);
    float3 TOP = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0.0, -1.0) * ViewportPixelSize, pixTZ);
    float3 BOTTOM = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0.0, 1.0) * ViewportPixelSize, pixBZ);
    float3 viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision
    viewspaceZ *= 0.99999;

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    const float3 viewVec = normalize(-pixCenterPos);

    // prevents normals that are facing away from the view vector
    viewspaceNormal = normalize(viewspaceNormal + max(0, -dot(viewspaceNormal, viewVec)) * viewVec);

    const float effectRadius = EffectRadius;
    const float sampleDistributionPower = SAMPLE_DISTRIBUTION_POWER;
    const float thinOccluderCompensation = THIN_OCCLUDER_COMPENSATION;
    const float falloffRange = EFFECT_FALLOFF_RANGE * effectRadius;
    const float falloffFrom = effectRadius * (1.0 - EFFECT_FALLOFF_RANGE);

    // fadeout precompute optimisation
    const float falloffMul = -1.0 / falloffRange;
    const float falloffAdd = falloffFrom / falloffRange + 1.0;

    float visibility = 0.0;

    {
        const float noiseSlice = localNoise.x;
        const float noiseSample = localNoise.y;

        const float pixelTooCloseThreshold = 1.3; // if the offset is under approx pixel size, push it out to the minimum distance

        // approx viewspace pixel size at pixCoord
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * abs(NDC_TO_VIEW_MUL_X_PIXEL_SIZE);

        float screenspaceRadius = effectRadius * rcp(max(pixelDirRBViewspaceSizeAtCenterZ.x, 1e-6));

        // fade out for small screen radii
        visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

        // min distance to start sampling from to avoid sampling from the center pixel
        const float minS = pixelTooCloseThreshold * rcp(max(screenspaceRadius, 1e-6));

        for (float slice = 0.0; slice < SLICE_COUNT; slice++) {
            float sliceK = (slice + noiseSlice) / SLICE_COUNT;
            float phi = sliceK * XE_GTAO_PI;
            float cosPhi = cos(phi);
            float sinPhi = sin(phi);
            float2 omega = float2(cosPhi, -sinPhi);

            // convert to screen units (pixels)
            omega *= screenspaceRadius;

            const float3 directionVec = float3(cosPhi, sinPhi, 0.0);
            const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
            const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));
            float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
            float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
            float projectedNormalVecLength = length(projectedNormalVec);
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(max(projectedNormalVecLength, 1e-6)));
            float n = signNorm * XeGTAO_FastACos(cosNorm);

            const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
            const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

            float horizonCos0 = lowHorizonCos0;
            float horizonCos1 = lowHorizonCos1;

            [unroll]
            for (float step = 0.0; step < STEPS_PER_SLICE; step++) {
                // R1 sequence
                const float stepBaseNoise = (slice + step * STEPS_PER_SLICE) * 0.6180339887498948482;
                float stepNoise = frac(noiseSample + stepBaseNoise);

                float s = (step + stepNoise) / STEPS_PER_SLICE;
                s = pow(s, sampleDistributionPower);
                s += minS;

                float2 sampleOffset = s * omega;
                float sampleOffsetLength = length(sampleOffset);
                const float mipLevel = clamp(log2(max(sampleOffsetLength, 1e-6)) - DEPTH_MIP_SAMPLING_OFFSET, 0.0, XE_GTAO_DEPTH_MIP_LEVELS);

                // Snap to pixel center
                sampleOffset = round(sampleOffset) * ViewportPixelSize;

                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float SZ0 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0);

                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;
                float SZ1 = sourceViewspaceDepth.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1);

                float3 sampleDelta0 = samplePos0 - pixCenterPos;
                float3 sampleDelta1 = samplePos1 - pixCenterPos;
                float sampleDist0 = max(length(sampleDelta0), 1e-6);
                float sampleDist1 = max(length(sampleDelta1), 1e-6);

                float3 sampleHorizonVec0 = sampleDelta0 * rcp(sampleDist0);
                float3 sampleHorizonVec1 = sampleDelta1 * rcp(sampleDist1);

                // discard samples out of radius with a falloff range
                float falloffBase0 = length(float3(sampleDelta0.x, sampleDelta0.y, sampleDelta0.z * (1.0 + thinOccluderCompensation)));
                float falloffBase1 = length(float3(sampleDelta1.x, sampleDelta1.y, sampleDelta1.z * (1.0 + thinOccluderCompensation)));
                float weight0 = saturate(falloffBase0 * falloffMul + falloffAdd);
                float weight1 = saturate(falloffBase1 * falloffMul + falloffAdd);

                float shc0 = dot(sampleHorizonVec0, viewVec);
                float shc1 = dot(sampleHorizonVec1, viewVec);

                shc0 = lerp(lowHorizonCos0, shc0, weight0);
                shc1 = lerp(lowHorizonCos1, shc1, weight1);

                horizonCos0 = max(horizonCos0, shc0);
                horizonCos1 = max(horizonCos1, shc1);
            }

            projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);

            float h0 = -XeGTAO_FastACos(horizonCos1);
            float h1 = XeGTAO_FastACos(horizonCos0);
            float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
            float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
            float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
            visibility += localVisibility;
        }
        visibility /= SLICE_COUNT;
        visibility = max(0.03, visibility); // disallow total occlusion
    }

    visibility = saturate(visibility / XE_GTAO_OCCLUSION_TERM_SCALE);
    outWorkingAOTermAndEdges[pixCoord] = float2(visibility, edges);
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

void XeGTAO_DecodeGatherPartial(float4 packedValue, out float outDecoded[4])
{
    for (int i = 0; i < 4; i++) {
        outDecoded[i] = packedValue[i];
    }
}

void XeGTAO_AddSample(float ssaoValue, float edgeValue, inout float sum, inout float sumWeight)
{
    float weight = edgeValue;
    sum += weight * ssaoValue;
    sumWeight += weight;
}

// Denoises 2 horizontal pixels. outValue[side] = float2(ao, edges)
void XeGTAO_Denoise(uint2 pixCoordBase, Texture2D sourceAOTermAndEdges, SamplerState texSampler, float blurAmount, out float2 outValue[2])
{
    const float diagWeight = 0.85 * 0.5;

    float aoTerm[2];
    float4 edgesC_LRTB[2];
    float weightTL[2];
    float weightTR[2];
    float weightBL[2];
    float weightBR[2];

    const float2 gatherCenter = float2(pixCoordBase.x, pixCoordBase.y) * ViewportPixelSize;
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
        float4 edgesL_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.x : edgesQ0.y);
        float4 edgesT_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.z : edgesQ1.w);
        float4 edgesR_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ1.x : edgesQ1.y);
        float4 edgesB_LRTB = XeGTAO_UnpackEdges(side == 0 ? edgesQ2.w : edgesQ2.z);

        edgesC_LRTB[side] = XeGTAO_UnpackEdges(side == 0 ? edgesQ0.y : edgesQ1.x);

        // enforce edge symmetry
        edgesC_LRTB[side] *= float4(edgesL_LRTB.y, edgesR_LRTB.x, edgesT_LRTB.w, edgesB_LRTB.z);

        // small amount of AO leaking from neighbours with 3 or 4 edges
        const float leak_threshold = 2.5;
        const float leak_strength = 0.5;
        float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.0)) * rcp(4.0 - leak_threshold)) * leak_strength;
        edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);

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
        outValue[side] = float2(aoTerm[side], side == 0 ? edgesQ0.y : edgesQ1.x);
    }
}

// Implementation
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SamplerState smp : register(s0); // point clamp

Texture2D tex0 : register(t0); // prefilter: raw device depth; main pass: depth mip pyramid; denoise: AO + edges

// Prefilter outputs (linear view depth mip pyramid)
RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);
// Main pass: visibility + edges
RWTexture2D<unorm float2> ao_term_and_edges : register(u5);
// First denoise: visibility + edges
RWTexture2D<unorm float2> denoised_ao_term_and_edges : register(u6);
// Final denoise: visibility (R8_UNORM)
RWTexture2D<unorm float> final_ao : register(u7);

#define XE_GTAO_NUMTHREADS_X 8
#define XE_GTAO_NUMTHREADS_Y 8

// From https://www.shadertoy.com/view/3tB3z3 - except we're using R2 here
#define XE_HILBERT_LEVEL 6U
#define XE_HILBERT_WIDTH (1U << XE_HILBERT_LEVEL)
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

// Spatial noise only (no temporal index)
float2 SpatioTemporalNoise(uint2 pixCoord)
{
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    // R2 sequence
    return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(8, 8, 1)] // each thread computes 2x2 pixels, so 16x16 per group: Dispatch with ((width + 15) / 16, (height + 15) / 16, 1)
void prefilter_depths16x16_cs(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
    // tex0 = raw device depth (Load, clamped to the viewport)
    const uint2 baseCoord = dtid;
    const uint2 pixCoord = baseCoord * 2;

    float depths4w = tex0.Load(int3(min(pixCoord, ViewportSize - 1), 0)).x;
    float depths4z = tex0.Load(int3(min(pixCoord + uint2(1, 0), ViewportSize - 1), 0)).x;
    float depths4x = tex0.Load(int3(min(pixCoord + uint2(0, 1), ViewportSize - 1), 0)).x;
    float depths4y = tex0.Load(int3(min(pixCoord + uint2(1, 1), ViewportSize - 1), 0)).x;
    float depth0 = XeGTAO_ClampDepth(XeGTAO_LinearizeDepth(depths4w));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_LinearizeDepth(depths4z));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_LinearizeDepth(depths4x));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_LinearizeDepth(depths4y));
    if (all(pixCoord + uint2(0, 0) < ViewportSize)) out_working_depth_mip0[pixCoord + uint2(0, 0)] = depth0;
    if (all(pixCoord + uint2(1, 0) < ViewportSize)) out_working_depth_mip0[pixCoord + uint2(1, 0)] = depth1;
    if (all(pixCoord + uint2(0, 1) < ViewportSize)) out_working_depth_mip0[pixCoord + uint2(0, 1)] = depth2;
    if (all(pixCoord + uint2(1, 1) < ViewportSize)) out_working_depth_mip0[pixCoord + uint2(1, 1)] = depth3;

    // MIP 1
    float dm1 = XeGTAO_DepthMIPFilter(depth0, depth1, depth2, depth3);
    out_working_depth_mip1[baseCoord] = dm1; // out of range writes are discarded by D3D11
    g_scratchDepths[gtid.x][gtid.y] = dm1;

    GroupMemoryBarrierWithGroupSync();

    // MIP 2
    [branch]
    if (all((gtid.xy % 2) == 0)) {
        float inTL = g_scratchDepths[gtid.x + 0][gtid.y + 0];
        float inTR = g_scratchDepths[gtid.x + 1][gtid.y + 0];
        float inBL = g_scratchDepths[gtid.x + 0][gtid.y + 1];
        float inBR = g_scratchDepths[gtid.x + 1][gtid.y + 1];

        float dm2 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        out_working_depth_mip2[baseCoord / 2] = dm2;
        g_scratchDepths[gtid.x][gtid.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    [branch]
    if (all((gtid.xy % 4) == 0)) {
        float inTL = g_scratchDepths[gtid.x + 0][gtid.y + 0];
        float inTR = g_scratchDepths[gtid.x + 2][gtid.y + 0];
        float inBL = g_scratchDepths[gtid.x + 0][gtid.y + 2];
        float inBR = g_scratchDepths[gtid.x + 2][gtid.y + 2];

        float dm3 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        out_working_depth_mip3[baseCoord / 4] = dm3;
        g_scratchDepths[gtid.x][gtid.y] = dm3;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 4
    [branch]
    if (all((gtid.xy % 8) == 0)) {
        float inTL = g_scratchDepths[gtid.x + 0][gtid.y + 0];
        float inTR = g_scratchDepths[gtid.x + 4][gtid.y + 0];
        float inBL = g_scratchDepths[gtid.x + 0][gtid.y + 4];
        float inBR = g_scratchDepths[gtid.x + 4][gtid.y + 4];

        float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        out_working_depth_mip4[baseCoord / 8] = dm4;
    }
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    // tex0 = depth mip pyramid (linear view depth), smp = point clamp
    XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid), tex0, smp, ao_term_and_edges);
}

// First denoise pass: 2 horizontal pixels per thread, blur beta / 5
[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    const uint2 pix_coord_base = dtid * uint2(2, 1);
    float2 values[2];
    XeGTAO_Denoise(pix_coord_base, tex0, smp, DENOISE_BLUR_BETA / 5.0, values);
    denoised_ao_term_and_edges[pix_coord_base + uint2(0, 0)] = values[0];
    denoised_ao_term_and_edges[pix_coord_base + uint2(1, 0)] = values[1];
}

// Applies the exponent (Intensity) and then the linear darkness (Darkness) to a raw occlusion-term visibility
float XeGTAO_ApplyDarkness(float occlusion_term)
{
    float v = pow(saturate(occlusion_term * XE_GTAO_OCCLUSION_TERM_SCALE), max(Intensity, 0.01));
    return saturate(1.0 - (1.0 - v) * max(Darkness, 0.0));
}

// Final denoise pass: blur beta, writes the final visibility with the exponent and darkness applied
[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_final_cs(uint2 dtid : SV_DispatchThreadID)
{
    const uint2 pix_coord_base = dtid * uint2(2, 1);
    float2 values[2];
    XeGTAO_Denoise(pix_coord_base, tex0, smp, DENOISE_BLUR_BETA, values);
    final_ao[pix_coord_base + uint2(0, 0)] = XeGTAO_ApplyDarkness(values[0].x);
    final_ao[pix_coord_base + uint2(1, 0)] = XeGTAO_ApplyDarkness(values[1].x);
}
