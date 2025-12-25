// XeGTAO addopted for Dishonored 2.
//
// Source: https://github.com/GameTechDev/XeGTAO

#include "../Includes/Common.hlsl"

cbuffer PerViewCB : register(b1)
{
    float4 cb_alwaystweak : packoffset(c0);
    float4 cb_viewrandom : packoffset(c1);
    float4x4 cb_viewprojectionmatrix : packoffset(c2);
    float4x4 cb_viewmatrix : packoffset(c6);
    float4 cb_subpixeloffset : packoffset(c10);
    float4x4 cb_projectionmatrix : packoffset(c11);
    float4x4 cb_previousviewprojectionmatrix : packoffset(c15);
    float4x4 cb_previousviewmatrix : packoffset(c19);
    float4x4 cb_previousprojectionmatrix : packoffset(c23);
    float4 cb_mousecursorposition : packoffset(c27);
    float4 cb_mousebuttonsdown : packoffset(c28);
    float4 cb_jittervectors : packoffset(c29);
    float4x4 cb_inverseviewprojectionmatrix : packoffset(c30);
    float4x4 cb_inverseviewmatrix : packoffset(c34);
    float4x4 cb_inverseprojectionmatrix : packoffset(c38);
    float4 cb_globalviewinfos : packoffset(c42);
    float3 cb_wscamforwarddir : packoffset(c43);
    uint cb_alwaysone : packoffset(c43.w);
    float3 cb_wscamupdir : packoffset(c44);
    uint cb_usecompressedhdrbuffers : packoffset(c44.w);
    float3 cb_wscampos : packoffset(c45);
    float cb_time : packoffset(c45.w);
    float3 cb_wscamleftdir : packoffset(c46);
    float cb_systime : packoffset(c46.w);
    float2 cb_jitterrelativetopreviousframe : packoffset(c47);
    float2 cb_worldtime : packoffset(c47.z);
    float2 cb_shadowmapatlasslicedimensions : packoffset(c48);
    float2 cb_resolutionscale : packoffset(c48.z);
    float2 cb_parallelshadowmapslicedimensions : packoffset(c49);
    float cb_framenumber : packoffset(c49.z);
    uint cb_alwayszero : packoffset(c49.w);
}

// TODO: Move this with all other settings.
#ifndef XE_GTAO_QUALITY
#define XE_GTAO_QUALITY 2
#endif

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
#define EFFECT_RADIUS 0.6 // Defualt 0.5
#endif

#ifndef RADIUS_MULTIPLIER
#define RADIUS_MULTIPLIER 1.457 // Default 1.457
#endif

#ifndef EFFECT_FALLOFF_RANGE
#define EFFECT_FALLOFF_RANGE 0.95 // Default 0.615
#endif

#ifndef SAMPLE_DISTRIBUTION_POWER
#define SAMPLE_DISTRIBUTION_POWER 1.5 // Default 2.0
#endif

#ifndef THIN_OCCLUDER_COMPENSATION
#define THIN_OCCLUDER_COMPENSATION 0.0 // Default 0.0
#endif

#ifndef FINAL_VALUE_POWER
#define FINAL_VALUE_POWER 4.3 // Default 2.2
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

//

#define VIEWPORT_PIXEL_SIZE LumaSettings.SwapchainInvSize

#define TAN_HALF_FOV_X rcp(cb_projectionmatrix._m00)
#define TAN_HALF_FOV_Y rcp(cb_projectionmatrix._m11)
#define NDC_TO_VIEW_MUL float2(TAN_HALF_FOV_X * 2.0, TAN_HALF_FOV_Y * -2.0)
#define NDC_TO_VIEW_ADD float2(-TAN_HALF_FOV_X, TAN_HALF_FOV_Y)
#define NDC_TO_VIEW_MUL_X_PIXEL_SIZE (NDC_TO_VIEW_MUL * VIEWPORT_PIXEL_SIZE)

#define XE_GTAO_DEPTH_MIP_LEVELS 5.0

#define XE_GTAO_PI 3.1415926535897932384626433832795
#define XE_GTAO_PI_HALF 1.5707963267948966192313216916398

float XeGTAO_ScreenSpaceToViewSpaceDepth(float screenDepth)
{
    return -cb_inverseprojectionmatrix._m23 / (cb_inverseprojectionmatrix._m32 * screenDepth + cb_inverseprojectionmatrix._m33);
}

// This is also a good place to do non-linear depth conversion for cases where one wants the 'radius' (effectively the threshold between near-field and far-field GI), 
// is required to be non-linear (i.e. very large outdoors environments).
float XeGTAO_ClampDepth(float depth)
{
    return clamp(depth, 0.0, 3.402823466e+38);
}

// weighted average depth filter
float XeGTAO_DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
    float maxDepth = max(max(depth0, depth1), max(depth2, depth3));

    const float depthRangeScaleFactor = 0.75; // found empirically :)
    const float effectRadius = depthRangeScaleFactor * EFFECT_RADIUS * RADIUS_MULTIPLIER;
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
    float4 depths4 = sourceNDCDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));
    float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.w));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.z));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.x));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.y));
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
    [branch]
    if (all((groupThreadID.xy % 2) == 0))
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
    [branch]
    if (all(( groupThreadID.xy % 4) == 0))
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
    [branch]
    if (all((groupThreadID.xy % 8) == 0))
    {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
        float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

        float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth4[baseCoord / 8] = dm4;
        //g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm4;
    }
}

float4 XeGTAO_CalculateEdges(float centerZ, float leftZ, float rightZ, float topZ, float bottomZ)
{
    float4 edgesLRTB = float4(leftZ, rightZ, topZ, bottomZ) - centerZ;

    float slopeLR = (edgesLRTB.y - edgesLRTB.x) * 0.5;
    float slopeTB = (edgesLRTB.w - edgesLRTB.z) * 0.5;
    float4 edgesLRTBSlopeAdjusted = edgesLRTB + float4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    edgesLRTB = min(abs(edgesLRTB), abs(edgesLRTBSlopeAdjusted));
    return saturate(1.25 - edgesLRTB / (centerZ * 0.011));
}

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 XeGTAO_ComputeViewspacePosition(float2 screenPos, float viewspaceDepth)
{
    float3 ret;
    ret.xy = (NDC_TO_VIEW_MUL * screenPos.xy + NDC_TO_VIEW_ADD) * viewspaceDepth;
    ret.z = viewspaceDepth;
    return ret;
}

float3 XeGTAO_CalculateNormal(const float4 edgesLRTB, const float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos)
{
    // Get this pixel's viewspace normal
    float4 acceptedNormals = saturate(float4(edgesLRTB.x * edgesLRTB.z, edgesLRTB.z * edgesLRTB.y, edgesLRTB.y * edgesLRTB.w, edgesLRTB.w * edgesLRTB.x) + 0.01);

    pixLPos = normalize(pixLPos - pixCenterPos);
    pixRPos = normalize(pixRPos - pixCenterPos);
    pixTPos = normalize(pixTPos - pixCenterPos);
    pixBPos = normalize(pixBPos - pixCenterPos);

    float3 pixelNormal = acceptedNormals.x * cross(pixLPos, pixTPos) + acceptedNormals.y * cross(pixTPos, pixRPos) + acceptedNormals.z * cross(pixRPos, pixBPos) + acceptedNormals.w * cross(pixBPos, pixLPos);
    pixelNormal = normalize(pixelNormal);

    return pixelNormal;
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
    return inX >= 0.0 ? res : pi - res;
}

// This expects float RT!
void XeGTAO_MainPass(uint2 pixCoord, float2 localNoise, Texture2D sourceViewspaceDepth, SamplerState depthSampler, out float4 outWorkingAOTermAndPackedDepth)
{
    float2 normalizedScreenPos = (pixCoord + 0.5) * VIEWPORT_PIXEL_SIZE;

    float4 valuesUL = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE));
    float4 valuesBR = sourceViewspaceDepth.GatherRed(depthSampler, float2(pixCoord * VIEWPORT_PIXEL_SIZE), int2(1, 1));

    // viewspace Z at the center
    float viewspaceZ = valuesUL.y; //sourceViewspaceDepth.SampleLevel( depthSampler, normalizedScreenPos, 0 ).x;

    // The original SSAO shader depth packing.
    float2 r0;
    float r1;
    r1.x = viewspaceZ;
    r0.x = 1.00390625 * r1.x;
    r0.x = trunc(r0.x);
    r0.xy = float2(256,0.00392156886) * r0.xx;
    r0.x = r1.x * 257 + -r0.x;
    outWorkingAOTermAndPackedDepth.z = r0.y;
    outWorkingAOTermAndPackedDepth.w = 0.00392156886 * r0.x;

    // viewspace Zs left top right bottom
    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;

    float4 edgesLRTB = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);

    // Generating screen space normals in-place is faster than generating normals in a separate pass but requires
    // use of 32bit depth buffer (16bit works but visibly degrades quality) which in turn slows everything down. So to
    // reduce complexity and allow for screen space normal reuse by other effects, we've pulled it out into a separate
    // pass.
    // However, we leave this code in, in case anyone has a use-case where it fits better.
    float3 CENTER = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    float3 LEFT = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1.0, 0.0) * VIEWPORT_PIXEL_SIZE, pixLZ);
    float3 RIGHT = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(1.0, 0.0) * VIEWPORT_PIXEL_SIZE, pixRZ);
    float3 TOP = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0.0, -1.0) * VIEWPORT_PIXEL_SIZE, pixTZ);
    float3 BOTTOM = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(0.0, 1.0) * VIEWPORT_PIXEL_SIZE, pixBZ);
    float3 viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
    viewspaceZ *= 0.99999; // this is good for FP32 depth buffer

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ);
    const float3 viewVec = normalize(-pixCenterPos);

    // prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases, but in Vanilla it seems rare so it's disabled by default
    viewspaceNormal = normalize(viewspaceNormal + max(0, -dot(viewspaceNormal, viewVec)) * viewVec);

    const float effectRadius = EFFECT_RADIUS * RADIUS_MULTIPLIER;
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

        float screenspaceRadius = effectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;

        // fade out for small screen radii 
        visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

        // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
        const float minS = pixelTooCloseThreshold / screenspaceRadius;

        [unroll]
        for (float slice = 0.0; slice < SLICE_COUNT; slice++)
        {
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
            //axisVec is orthogonal to directionVec and viewVec, used to define projectedNormal
            const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

            // alternative line 9 from the paper
            // float3 orthoDirectionVec = cross( viewVec, axisVec );

            // line 11 from the paper
            float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);

            // line 13 from the paper
            float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));

            // line 14 from the paper
            float projectedNormalVecLength = length(projectedNormalVec);
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) / projectedNormalVecLength);

            // line 15 from the paper
            float n = signNorm * XeGTAO_FastACos(cosNorm);

            // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
            const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
            const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

            // lines 17, 18 from the paper, manually unrolled the 'side' loop
            float horizonCos0 = lowHorizonCos0; //-1;
            float horizonCos1 = lowHorizonCos1; //-1;

            [unroll]
            for (float step = 0.0; step < STEPS_PER_SLICE; step++)
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
                float3 sampleHorizonVec0 = sampleDelta0 / sampleDist0;
                float3 sampleHorizonVec1 = sampleDelta1 / sampleDist1;

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

                // thickness heuristic - see "4.3 Implementation details, Height-field assumption considerations"
#if 0   // (disabled, not used) this should match the paper
                float newhorizonCos0 = max(horizonCos0, shc0);
                float newhorizonCos1 = max(horizonCos1, shc1);
                horizonCos0 = horizonCos0 > shc0 ? lerp(newhorizonCos0, shc0, thinOccluderCompensation) : newhorizonCos0;
                horizonCos1 = horizonCos1 > shc1 ? lerp(newhorizonCos1, shc1, thinOccluderCompensation) : newhorizonCos1;
#elif 0 // (disabled, not used) this is slightly different from the paper but cheaper and provides very similar results
                horizonCos0 = lerp(max(horizonCos0, shc0), shc0, thinOccluderCompensation);
                horizonCos1 = lerp(max(horizonCos1, shc1), shc1, thinOccluderCompensation);
#else   // this is a version where thicknessHeuristic is completely disabled
                horizonCos0 = max(horizonCos0, shc0);
                horizonCos1 = max(horizonCos1, shc1);
#endif

            }

#if 1       // I can't figure out the slight overdarkening on high slopes, so I'm adding this fudge - in the training set, 0.05 is close (PSNR 21.34) to disabled (PSNR 21.45)
            projectedNormalVecLength = lerp(projectedNormalVecLength, 1.0, 0.05);
#endif

            // line ~27, unrolled
            float h0 = -XeGTAO_FastACos(horizonCos1);
            float h1 = XeGTAO_FastACos(horizonCos0);
#if 0       // we can skip clamping for a tiny little bit more performance
            h0 = n + clamp(h0 - n, -XE_GTAO_PI_HALF, XE_GTAO_PI_HALF);
            h1 = n + clamp(h1 - n, -XE_GTAO_PI_HALF, XE_GTAO_PI_HALF);
#endif
            float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
            float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
            float localVisibility = projectedNormalVecLength * (iarc0 + iarc1);
            visibility += localVisibility;
        }

        visibility /= SLICE_COUNT;
        visibility = pow(visibility, FINAL_VALUE_POWER);
        visibility = max(0.03, visibility); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)
    }

    outWorkingAOTermAndPackedDepth.x = visibility;

    // The original SSAO shader is setting `y` always to 1.
    outWorkingAOTermAndPackedDepth.y = 1.0;
}

// Implementation
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

SamplerState smp : register(s0);
Texture2D tex : register(t0);
RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);

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
    for (uint curLevel = XE_HILBERT_WIDTH / 2U; curLevel > 0U; curLevel /= 2U)
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

// without TAA, temporalIndex is always 0
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
    float2 noise;

    // Hilbert curve driving R2 (see https://www.shadertoy.com/view/3tB3z3)
    #ifdef XE_GTAO_HILBERT_LUT_AVAILABLE // load from lookup texture...
    uint index = g_srcHilbertLUT.Load(uint3(pixCoord % 64, 0)).x;
    #else // ...or generate in-place?
    uint index = HilbertIndex(pixCoord.x, pixCoord.y);
    #endif

    index += 288 * (temporalIndex % 64); // why 288? tried out a few and that's the best so far (with XE_HILBERT_LEVEL 6U) - but there's probably better :)

    // R2 sequence - see http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/
    return float2(frac(0.5 + index * float2(0.75487766624669276005, 0.5698402909980532659114)));
}

[numthreads(8, 8, 1)] // <- hard coded to 8x8; each thread computes 2x2 blocks so processing 16x16 block: Dispatch needs to be called with (width + 16-1) / 16, (height + 16-1) / 16
void prefilter_depths16x16_cs(uint2 dtid : SV_DispatchThreadID, uint2 gtid : SV_GroupThreadID)
{
    // tex0 = depth
    // smp = g_samplerPointClamp
    XeGTAO_PrefilterDepths16x16(dtid, gtid, tex, smp, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4);
}

void main_pass_ps(float4 pos : SV_Position, out float4 outWorkingAOTermAndPackedDepth : SV_Target)
{
    // tex0 = g_srcWorkingDepth
    // smp = g_samplerPointClamp
    XeGTAO_MainPass(pos.xy, SpatioTemporalNoise(pos.xy, cb_framenumber), tex, smp, outWorkingAOTermAndPackedDepth);
}