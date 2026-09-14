#ifndef __XE_GTAO_HLSLI__
#define __XE_GTAO_HLSLI__

#pragma warning(disable : 3579)

// XeGTAO implementation (soomewhat stolen from BioShock Infinite mod)
// Source: https://github.com/GameTechDev/XeGTAO

#include "./Includes/Common.hlsl"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////
// Defines //
/////////////

#if XEGTAO_SLICECOUNT == 0
    #define SLICE_COUNT 3.0
#elif XEGTAO_SLICECOUNT == 1
    #define SLICE_COUNT 6.0
#elif XEGTAO_SLICECOUNT == 2
    #define SLICE_COUNT 8.0
#elif XEGTAO_SLICECOUNT == 3
    #define SLICE_COUNT 12.0
#elif XEGTAO_SLICECOUNT == 4
    #define SLICE_COUNT 16.0
#elif XEGTAO_SLICECOUNT == 5
    #define SLICE_COUNT 24.0
#elif XEGTAO_SLICECOUNT == 6
    #define SLICE_COUNT 32.0
#else
    #define SLICE_COUNT 6.0
#endif

#if XEGTAO_STEPSPERSLICE == 0
    #define STEPS_PER_SLICE 3.0
#elif XEGTAO_STEPSPERSLICE == 1
    #define STEPS_PER_SLICE 4.0
#else
    #define STEPS_PER_SLICE 3.0
#endif
// #define FINAL_VALUE_POWER 0.9 // Default 2.2

#if XEGTAO_HALFRES == 1 
    #define DEPTH_MIP_SAMPLING_MINIMUM_LEVEL 0
    #define DEPTH_MIP_SAMPLING_NORMALSSMOOTH 1 //best: 3 if full, 0 if half
#else
    #define DEPTH_MIP_SAMPLING_MINIMUM_LEVEL 0
    #define DEPTH_MIP_SAMPLING_NORMALSSMOOTH 0
#endif
#define DEPTH_MIP_SAMPLING_OFFSET 3.3 // Default 3.3

#define EFFECT_RADIUS 0.078 // Default 0.5 // TODO: why the helly is this so low?
#define RADIUS_MULTIPLIER 1.516 // Default 1.457
#define EFFECT_FALLOFF_RANGE 0.01 // Default 0.615
#define EFFECT_RADIUS_DISTANCE_SCALE 0.008 //0.008
#define SAMPLE_DISTRIBUTION_POWER 2 // Default 2.0
#define DEPTH_LINEAR_MAX 200
#define NORMAL_SMOOTH_SCALE 0.0167

#define THIN_OCCLUDER_COMPENSATION 0 // Default 0.0 
#define DENOISE_BLUR_BETA 0.00001 // Default 1.2
#define LUMINANCE_DODGE 0.03
#define MINIMUM_AO_OUTPUT 0.03 // 0.03

#define XE_GTAO_PI 3.1415926535897932384626433832795
#define XE_GTAO_PI_OVER_360 0.00872664625997
#define XE_GTAO_PI_HALF 1.5707963267948966192313216916398

#define XE_GTAO_DEPTH_MIP_LEVELS 5.0
// #define XE_GTAO_OCCLUSION_TERM_SCALE 1.0

#if XEGTAO_THREADS_NORMALSGEN == 0
    #define XEGTAO_THREADS_NORMALSGEN_X 8
    #define XEGTAO_THREADS_NORMALSGEN_Y 8
#elif XEGTAO_THREADS_NORMALSGEN == 1
    #define XEGTAO_THREADS_NORMALSGEN_X 16
    #define XEGTAO_THREADS_NORMALSGEN_Y 16
#endif

#if XEGTAO_THREADS_NORMALSSMOOTH == 0
    #define XEGTAO_THREADS_NORMALSSMOOTH_X 8
    #define XEGTAO_THREADS_NORMALSSMOOTH_Y 8
#elif XEGTAO_THREADS_NORMALSSMOOTH == 1
    #define XEGTAO_THREADS_NORMALSSMOOTH_X 16
    #define XEGTAO_THREADS_NORMALSSMOOTH_Y 16
#endif

#if XEGTAO_THREADS_AO == 0
    #define XEGTAO_THREADS_AO_X 8
    #define XEGTAO_THREADS_AO_Y 8
#elif XEGTAO_THREADS_AO == 1
    #define XEGTAO_THREADS_AO_X 16
    #define XEGTAO_THREADS_AO_Y 16
#endif

#if XEGTAO_THREADS_DENOISE == 0
    #define XEGTAO_THREADS_DENOISE_X 8
    #define XEGTAO_THREADS_DENOISE_Y 8
#elif XEGTAO_THREADS_DENOISE == 1
    #define XEGTAO_THREADS_DENOISE_X 16
    #define XEGTAO_THREADS_DENOISE_Y 16
#endif

struct GTAOConstants
{
    float2 ViewportPixelSize;
    float2 RenderPixelSize;
    float2 ViewportSize;
    float2 NDCToViewMul;
    float2 NDCToViewAdd;
    float2 NDCToViewMul_x_PixelSize;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////
// Bindings //
//////////////

// Luma cb is bound too

// Scene cb
cbuffer Scene : register(b1)
{
  float4 g_irradiance_r_transforms[4] : packoffset(c0);
  float4 g_irradiance_g_transforms[4] : packoffset(c4);
  float4 g_irradiance_b_transforms[4] : packoffset(c8);
  float4 g_light_env_stage_diffuse : packoffset(c12);
  float4 g_light_env_stage_specular : packoffset(c13);
  float4 g_light_env_chara_diffuse : packoffset(c14);
  float4 g_light_env_chara_ambient : packoffset(c15);
  float4 g_light_env_chara_specular : packoffset(c16);
  float4 g_light_env_reflect_diffuse : packoffset(c17);
  float4 g_light_env_reflect_ambient : packoffset(c18);
  float4 g_light_env_reflect_specular : packoffset(c19);
  float4 g_light_env_proj_diffuse : packoffset(c20);
  float4 g_light_env_proj_specular : packoffset(c21);
  float4 g_light_env_proj_position : packoffset(c22);
  float4 g_light_stage_dir : packoffset(c23);
  float4 g_light_stage_diff : packoffset(c24);
  float4 g_light_stage_spec : packoffset(c25);
  float4 g_light_chara_dir : packoffset(c26);
  float4 g_light_chara_spec : packoffset(c27);
  float4 g_light_chara_luce : packoffset(c28);
  float4 g_light_chara_back : packoffset(c29);
  float4 g_light_face_diff : packoffset(c30);
  float4 g_chara_color0 : packoffset(c31);
  float4 g_chara_color1 : packoffset(c32);
    // if w == 1 full tint? (Systematic Love)
  float4 g_chara_f_dir : packoffset(c33);
  float4 g_chara_f_ambient : packoffset(c34);
  float4 g_chara_f_diffuse : packoffset(c35);
  float4 g_chara_tc_param : packoffset(c36);
  float4 g_fog_depth_color : packoffset(c37);
  float4 g_fog_height_params : packoffset(c38);
  float4 g_fog_height_color : packoffset(c39);
  float4 g_fog_bump_params : packoffset(c40);
  float4 g_fog_state_params : packoffset(c41); 
    // 0.9, -1.1, 26.2, 0.03663
    // linear fog: amount?, negative start offset, far distance, rcp(far distance - negative start offset)
  float4 g_normal_tangent_transforms[3] : packoffset(c42);
  float4 g_esm_param : packoffset(c45);
  float4 g_self_shadow_receivers[6] : packoffset(c46);
  float4 g_shadow_ambient : packoffset(c52);
  float4 g_shadow_ambient1 : packoffset(c53);
  float4 g_framebuffer_size : packoffset(c54);
  float4 g_light_reflect_dir : packoffset(c55);
  float4 g_clip_plane : packoffset(c56); // (0, -1, 0, 0)
  float4 g_npr_cloth_spec_color : packoffset(c57);
  float4 g_view[3] : packoffset(c58);
  float4 g_view_inverse[3] : packoffset(c61);
  float4 g_projection_view[4] : packoffset(c64); 
  float4 g_view_position : packoffset(c68);
  float4 g_light_projection[4] : packoffset(c69);
  float4 g_light_projection_depth[4] : packoffset(c73);
  float4 g_foward_z_projection_row2 : packoffset(c77);
}

SamplerState s0 : register(s0); //point 
SamplerState s1 : register(s1); //linear
Texture2D<float4> t0 : register(t0); 
Texture2D<float4> t1 : register(t1); 
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4);
Texture2D<float>  finalpass_origdepth : register(t2);

RWTexture2D<float> out_working_depth_32   : register(u0);
RWTexture2D<float> out_working_depth_mip0 : register(u1);
RWTexture2D<float> out_working_depth_mip1 : register(u2);
RWTexture2D<float> out_working_depth_mip2 : register(u3);
RWTexture2D<float> out_working_depth_mip3 : register(u4);
RWTexture2D<float> out_working_depth_mip4 : register(u5);
RWTexture2D<unorm float2> ao_term_and_edges : register(u0); // Main Pass
RWTexture2D<unorm float4> normals : register(u0); // Normal Generate Pass
RWTexture2D<float3> normals_11_11_10f : register(u0); // Normal Generate Pass
RWTexture2D<unorm float2> final_output : register(u0); // Final Denoise Pass

#define XEGTAO_UNORM_NORMALS 3
#if XEGTAO_UNORM_NORMALS == 0
float3 NormalsNormalize(float3 x) { return x; }
float3 NormalsDenormalize(float3 x) { return x; }
#elif XEGTAO_UNORM_NORMALS == 1
float3 NormalsNormalize(float3 x) { return x * 0.5f + 0.5f; }
float3 NormalsDenormalize(float3 x) { return x * 2.f - 1.f; }
#elif XEGTAO_UNORM_NORMALS == 2
float3 NormalsNormalize(float3 x) { return (x * 0.5f + 0.5f) * 32768.f; }
float3 NormalsDenormalize(float3 x) { return (x / 32768.f) * 2.f - 1.f; }
#elif XEGTAO_UNORM_NORMALS == 3
float3 NormalsNormalize(float3 x) { x = x * 0.5f + 0.5f; return sqrt(x); }
float3 NormalsDenormalize(float3 x) { x *= x; return x * 2.f - 1.f; }
#endif

bool IsEvenFrame() { return LumaSettings.FrameIndex % 2 == 0; }

void GetViewport(Texture2D viewportTexture, out float2 viewportSize, out float2 viewportSizeInv, bool isHalf = true) 
{
#if XEGTAO_MANUALSIZE == 1
    uint w, h;
    viewportTexture.GetDimensions(w, h);
    viewportSize = float2(w, h);
    viewportSizeInv = 1.0 / viewportSize;
#else
    if (!isHalf) {
        viewportSize = LumaSettings.SwapchainSize;
        viewportSizeInv = LumaSettings.SwapchainInvSize;
    } else {
        viewportSize = LumaSettings.SwapchainSize * 2.f;
        viewportSizeInv = LumaSettings.SwapchainInvSize * 2.f;
    }
#endif
}

GTAOConstants GetNDCToView(GTAOConstants c)
{
#if 0
    // NDC to View (test)
    float tanHalfFOV = tan(30 * XE_GTAO_PI_OVER_360);
    float aspect = c.ViewportSize.x / c.ViewportSize.y;
    c.NDCToViewMul = float2(2.0, -2.0) * float2(aspect * tanHalfFOV, tanHalfFOV);
    c.NDCToViewAdd = float2(-1.0, 1.0) * float2(aspect * tanHalfFOV, tanHalfFOV);
    c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;
#else
    // NDC to View
    float Pxx = dot(g_projection_view[0].xyz, g_view[0].xyz);
    float Pyy = dot(g_projection_view[1].xyz, g_view[1].xyz);
    float tanHalfFovX = 1.0 / Pxx;
    float tanHalfFovY = 1.0 / Pyy;
    c.NDCToViewMul = float2(2.0, -2.0) * float2(tanHalfFovX, tanHalfFovY);
    c.NDCToViewAdd = float2(-1.0, 1.0) * float2(tanHalfFovX, tanHalfFovY);
    c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;
#endif

    return c;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////
// Depth PreFilter //
/////////////////////

// This is also a good place to do non-linear depth conversion for cases where one wants the 'radius' (effectively the threshold between near-field and far-field GI), 
// is required to be non-linear (i.e. very large outdoors environments).
float XeGTAO_ClampDepth(float depth)
{
    return clamp(depth, 0.0, /* DEPTH_LINEAR_MAX * 1.01 */ 3.402823466e+38);
}

float XeGTAO_ScreenSpaceToViewSpaceDepth(float x)
{
    // x = DVS4 / x;
    // x = 0.1 / x;
    // x = 1 / x;

    x = rcp(x * 19.9998 + 0.000166667);
    // x = 20 / (x * 19.9998 + 0.000166667);
    // // from DoF
    // // 19.9998 (1/0.05)
    // // 0.000166667 (1/6000)
    // // -0
    // // 0

    // //"-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"
    // x = 1 - x;
    // float cameraClipNear = 0.1;
    // float cameraClipFar = 1000.0;
    // return -cameraClipNear / (cameraClipFar - x * (cameraClipFar - cameraClipNear)) * cameraClipFar;

    return x;
}
float4 XeGTAO_ScreenSpaceToViewSpaceDepth(float4 x) {
    return float4(XeGTAO_ScreenSpaceToViewSpaceDepth(x.x), XeGTAO_ScreenSpaceToViewSpaceDepth(x.y), XeGTAO_ScreenSpaceToViewSpaceDepth(x.z), XeGTAO_ScreenSpaceToViewSpaceDepth(x.w));
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
void XeGTAO_PrefilterDepths16x16CS(uint2 dispatchThreadID, uint2 groupThreadID, const GTAOConstants consts)
{
    Texture2D sourceNDCDepth = t0;
    RWTexture2D<float> outDepth32 = out_working_depth_32;
    RWTexture2D<float> outDepth0 = out_working_depth_mip0;
    RWTexture2D<float> outDepth1 = out_working_depth_mip1;
    RWTexture2D<float> outDepth2 = out_working_depth_mip2;
    RWTexture2D<float> outDepth3 = out_working_depth_mip3;
    RWTexture2D<float> outDepth4 = out_working_depth_mip4;

    // MIP 0
    const uint2 baseCoord = dispatchThreadID;
    const uint2 pixCoord = baseCoord * 2;

#if XEGTAO_HALFRES == 0
    float4 depths4 = sourceNDCDepth.GatherRed(s0, float2((pixCoord /* + 0.5 */) * consts.ViewportPixelSize), int2(1, 1));
    float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.w));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.z));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.x));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depths4.y));
#else
    // basically skip original mip0
    // float2 pixSize = consts.ViewportPixelSize;
    // #if XEGTAO_MANUALSIZE == 1
    //     pixSize *= 2;
    // #endif
    // float4 depthsA = sourceNDCDepth.GatherRed(s0, float2(((pixCoord + 0.5) + uint2(0, 0)) * pixSize), int2(1, 1));
    // float4 depthsB = sourceNDCDepth.GatherRed(s0, float2(((pixCoord + 0.5) + uint2(1, 0)) * pixSize), int2(1, 1));
    // float4 depthsC = sourceNDCDepth.GatherRed(s0, float2(((pixCoord + 0.5) + uint2(0, 1)) * pixSize), int2(1, 1));
    // float4 depthsD = sourceNDCDepth.GatherRed(s0, float2(((pixCoord + 0.5) + uint2(1, 1)) * pixSize), int2(1, 1));
    // float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depthsA.w));
    // float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depthsB.w));
    // float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depthsC.w));
    // float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(depthsD.w));

    uint2 texCoord = (pixCoord /* + 0.5 */) * 2;
    float depth0 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(sourceNDCDepth.Load(int3(texCoord + uint2(0, 0), 0)).x));
    float depth1 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(sourceNDCDepth.Load(int3(texCoord + uint2(1, 0), 0)).x));
    float depth2 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(sourceNDCDepth.Load(int3(texCoord + uint2(0, 1), 0)).x));
    float depth3 = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(sourceNDCDepth.Load(int3(texCoord + uint2(1, 1), 0)).x));
#endif
    outDepth0[pixCoord + uint2(0, 0)] = depth0;
    outDepth0[pixCoord + uint2(1, 0)] = depth1;
    outDepth0[pixCoord + uint2(0, 1)] = depth2;
    outDepth0[pixCoord + uint2(1, 1)] = depth3;

    outDepth32[pixCoord + uint2(0, 0)] = depth0;
    outDepth32[pixCoord + uint2(1, 0)] = depth1;
    outDepth32[pixCoord + uint2(0, 1)] = depth2;
    outDepth32[pixCoord + uint2(1, 1)] = depth3;

    // MIP 1
    float dm1 = XeGTAO_DepthMIPFilter(depth0, depth1, depth2, depth3);
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
        outDepth2[baseCoord / 2] = dm2;
        g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
    }

    GroupMemoryBarrierWithGroupSync();

    // MIP 3
    [branch]
    if (all(( groupThreadID.xy % 4) == 0)) {
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
    if (all((groupThreadID.xy % 8) == 0)) {
        float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
        float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
        float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
        float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

        float dm4 = XeGTAO_DepthMIPFilter(inTL, inTR, inBL, inBR);
        outDepth4[baseCoord / 8] = dm4;
        g_scratchDepths[ groupThreadID.x ][ groupThreadID.y ] = dm4;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////
// Normals Generation //
////////////////////////
float3 XeGTAO_CalculateNormal( const float4 edgesLRTB, float3 pixCenterPos, float3 pixLPos, float3 pixRPos, float3 pixTPos, float3 pixBPos )
{
    // Get this pixel's viewspace normal
    float4 acceptedNormals = saturate(float4( edgesLRTB.x*edgesLRTB.z, edgesLRTB.z*edgesLRTB.y, edgesLRTB.y*edgesLRTB.w, edgesLRTB.w*edgesLRTB.x ) + 0.01);

    pixLPos = normalize(pixLPos - pixCenterPos);
    pixRPos = normalize(pixRPos - pixCenterPos);
    pixTPos = normalize(pixTPos - pixCenterPos);
    pixBPos = normalize(pixBPos - pixCenterPos);

    float3 pixelNormal =  acceptedNormals.x * cross(pixLPos, pixTPos) +
                        + acceptedNormals.y * cross(pixTPos, pixRPos) +
                        + acceptedNormals.z * cross(pixRPos, pixBPos) +
                        + acceptedNormals.w * cross(pixBPos, pixLPos);
    pixelNormal = normalize(pixelNormal);

    return pixelNormal;
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
	// integer version:
	// edgesLRTB = saturate(edgesLRTB) * 2.9.xxxx + 0.5.xxxx;
	// return (((uint)edgesLRTB.x) << 6) + (((uint)edgesLRTB.y) << 4) + (((uint)edgesLRTB.z) << 2) + (((uint)edgesLRTB.w));
	// 
	// optimized, should be same as above
	edgesLRTB = round(saturate(edgesLRTB) * 2.9);
	return dot(edgesLRTB, float4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 XeGTAO_ComputeViewspacePosition( const float2 screenPos, const float viewspaceDepth, const GTAOConstants consts )
{
    float3 ret;
    ret.xy = (consts.NDCToViewMul * screenPos.xy + consts.NDCToViewAdd) * viewspaceDepth;
    ret.z = viewspaceDepth;
    return ret;
}

// Generic viewspace normal generate pass
void XeGTAO_ComputeViewspaceNormal(const uint2 pixCoord, const GTAOConstants consts)
{
    Texture2D sourceViewspaceDepth32 = t0;
    Texture2D sourceViewspaceDepth16 = t1;
    SamplerState depthSampler = s0;
    RWTexture2D<unorm float4> outputNormal = normals;
    // RWTexture2D<float3> outputNormal = normals_11_11_10f;

    float2 normalizedScreenPos = (pixCoord + 0.5.xx) * consts.ViewportPixelSize;
    float2 sampleUV =            (pixCoord + 0.5.xx) * consts.ViewportPixelSize;

    // Requires 32 bit!
    float4 valuesUL   = sourceViewspaceDepth32.GatherRed(depthSampler, sampleUV            );
    float4 valuesBR   = sourceViewspaceDepth32.GatherRed(depthSampler, sampleUV, int2(1, 1));
    float viewspaceZ  = valuesUL.y;

    // viewspace Zs left
    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;

    float4 edgesLRTB  = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    float edgesPacked = 0/* XeGTAO_PackEdges(edgesLRTB)*/;  //TODO: bruh... rbg10a2 doesnt have enough bits

    // skip sky
    [branch] if (viewspaceZ > DEPTH_LINEAR_MAX || g_chara_color1.w >= 0.9999)
    {
        outputNormal[pixCoord] = float4(NormalsNormalize(0), edgesPacked); // alpha is 0 starts here!!!
        return;
    }

    float3 CENTER   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, consts);
    float3 LEFT     = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1,  0) * consts.ViewportPixelSize, pixLZ, consts);
    float3 RIGHT    = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 1,  0) * consts.ViewportPixelSize, pixRZ, consts);
    float3 TOP      = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0, -1) * consts.ViewportPixelSize, pixTZ, consts);
    float3 BOTTOM   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0,  1) * consts.ViewportPixelSize, pixBZ, consts);

    float3 normal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);

    outputNormal[pixCoord] = float4(NormalsNormalize(normal), edgesPacked); //fit into unorm
    // outputNormal[pixCoord] = NormalsNormalize(normal);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////
// Normals Smoothing //
///////////////////////

// Smooths viewspace normal using its neighbors.
// TODO: modded from AI fart, there must be better algorithm? but is it also faster?
void XeGTAO_SmoothViewspaceNormal(const uint2 pixCoord, const GTAOConstants consts)
{
    Texture2D sourceViewspaceDepth32 = t0;
    Texture2D sourceViewspaceDepth16 = t1;
    SamplerState depthSampler = s0;
    Texture2D sourceViewspaceNormal = t2;
    SamplerState normalSampler = s0;
    RWTexture2D<unorm float4> outputNormal = normals;
    // RWTexture2D<float3> outputNormal = normals_11_11_10f;

    const float2 normalizedScreenPos = (pixCoord + 0.5.xx) * consts.ViewportPixelSize; 
    const float2 sampleUV = (pixCoord + 0.5.xx) * consts.ViewportPixelSize; 

    // viewspace normal & skip sky
    float4 viewspaceNormal4 = sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV, 0); 
    // [branch] if (viewspaceNormal4.w == 0)
    // {
    //     outputNormal[pixCoord] = viewspaceNormal4.xyzw; 
    //     return;
    // }
    float3 centerNormal = NormalsDenormalize(viewspaceNormal4.xyz);

    // center depth
    const float centerZ = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV, 0/* DEPTH_MIP_SAMPLING_NORMALSSMOOTH */).x;
    [branch] if (centerZ > DEPTH_LINEAR_MAX || g_chara_color1.w >= 0.9999)
    {
        outputNormal[pixCoord] = viewspaceNormal4.xyzw;
        return;
    }

    // stepUV
    const float normalSmoothViewRadius = (EFFECT_RADIUS + (centerZ * EFFECT_RADIUS_DISTANCE_SCALE)) * RADIUS_MULTIPLIER * NORMAL_SMOOTH_SCALE; // TODO: dont copy main pass?
    const float2 viewspacePixelSizeAtDepth = centerZ * abs(consts.NDCToViewMul);
    const float2 stepUV = normalSmoothViewRadius * rcp(max(viewspacePixelSizeAtDepth, 1e-6));
#if XE_GTAO_NORMALSMOOTH_2ND
    const float2 stepUVScaled = stepUV;
#else
    const float2 stepUVScaled = stepUV * 2.0;
#endif

    // accumulator
    float3 smoothedNormal = 0; 

    // 1x stepUV
    {
        const float leftZ   = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2(-1,  0) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;
        const float rightZ  = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2( 1,  0) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;
        const float topZ    = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2( 0, -1) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;
        const float bottomZ = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2( 0,  1) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;

        const float3 leftN   = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2(-1,  0) * stepUVScaled, 0).xyz);
        const float3 rightN  = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2( 1,  0) * stepUVScaled, 0).xyz);
        const float3 topN    = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2( 0, -1) * stepUVScaled, 0).xyz);
        const float3 bottomN = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2( 0,  1) * stepUVScaled, 0).xyz);

        const float4 depthDiff = abs(float4(leftZ, rightZ, topZ, bottomZ) - centerZ);
        const float4 depthWeights = saturate(depthDiff + 1.0);

        // normal-similarity term catches silhouettes/corners where depth stays smooth but the normal doesn't (plain depth bilateral would miss these)
        float4 normalWeights = saturate(float4(dot(centerNormal, leftN), dot(centerNormal, rightN), dot(centerNormal, topN), dot(centerNormal, bottomN)));
        normalWeights *= normalWeights;

        const float4 weights = depthWeights * normalWeights;

        float3 normalSum = centerNormal + leftN * weights.x + rightN * weights.y + topN * weights.z + bottomN * weights.w;
        float weightSum = 1.0 + dot(weights, 1.0.xxxx);

        smoothedNormal = normalize(normalSum * rcp(weightSum));
    }

#if XEGTAO_NORMALSMOOTH_QUALITY == 2 //TODO: this doesnt do crap
    stepUV *= 0.66; 
    { 
        const float leftZ   = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2(-1,  0) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;
        const float rightZ  = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2( 1,  0) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;
        const float topZ    = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2( 0, -1) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;
        const float bottomZ = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleUV + float2( 0,  1) * stepUVScaled, DEPTH_MIP_SAMPLING_NORMALSSMOOTH).x;

        const float3 leftN   = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2(-1,  0) * stepUVScaled, 0).xyz);
        const float3 rightN  = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2( 1,  0) * stepUVScaled, 0).xyz);
        const float3 topN    = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2( 0, -1) * stepUVScaled, 0).xyz);
        const float3 bottomN = NormalsDenormalize(sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV + float2( 0,  1) * stepUVScaled, 0).xyz);

        const float4 depthDiff = abs(float4(leftZ, rightZ, topZ, bottomZ) - centerZ);
        const float4 depthWeights = saturate(depthDiff + 1.0);

        // normal-similarity term catches silhouettes/corners where depth stays smooth but the normal doesn't (plain depth bilateral would miss these)
        float4 normalWeights = saturate(float4(dot(centerNormal, leftN), dot(centerNormal, rightN), dot(centerNormal, topN), dot(centerNormal, bottomN)));
        normalWeights *= normalWeights;

        const float4 weights = depthWeights * normalWeights;

        float3 normalSum = centerNormal + leftN * weights.x + rightN * weights.y + topN * weights.z + bottomN * weights.w;
        float weightSum = 1.0 + dot(weights, 1.0.xxxx);

        float3 smoothedNormal1 = normalize(normalSum * rcp(weightSum));   

        // blend with 1st
        smoothedNormal = normalize(smoothedNormal + smoothedNormal1);
    }
#endif

    outputNormal[pixCoord] = float4(NormalsNormalize(smoothedNormal), viewspaceNormal4.w);
    // outputNormal[pixCoord] = NormalsNormalize(smoothedNormal);  
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////
// Visibility & Edges //
////////////////////////

// http://h14s.p5r.org/2012/09/0x5f3759df.html, [Drobot2014a] Low Level Optimizations for GCN, https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf slide 63
float XeGTAO_FastSqrt(float x)
{
	return asfloat(0x1fbd1df5 + (asint(x) >> 1));
}

// input [-1, 1] and output [0, PI], from https://seblagarde.wordpress.com/2014/12/01/inverse-trigonometric-functions-gpu-optimization-for-amd-gcn-architecture/
float XeGTAO_FastACos(float inX)
{ 
	float x = abs(inX); 
	float res = -0.156583 * x + XE_GTAO_PI_HALF;
	res *= XeGTAO_FastSqrt(1.0 - x);
	return inX >= 0 ? res : XE_GTAO_PI - res;
}

void XeGTAO_MainPassCS(uint2 pixCoord, float2 localNoise, const GTAOConstants consts)
{
    Texture2D sourceViewspaceDepth32 = t0;
    Texture2D sourceViewspaceDepth16 = t1;
    Texture2D sourceViewspaceDepthFull32 = t2;
    SamplerState depthSampler = s0;
    Texture2D sourceViewspaceNormal = t3;
    SamplerState normalSampler = s0;
    RWTexture2D<unorm float2> outWorkingAOTermAndEdges = ao_term_and_edges;

    float2 normalizedScreenPos = (pixCoord + 0.5.xx) * consts.ViewportPixelSize;
    float2 sampleUV = (pixCoord + 0.5) * consts.ViewportPixelSize;

    // viewspace normal & skip sky
    float4 viewspaceNormal4 = sourceViewspaceNormal.SampleLevel(normalSampler, sampleUV, 0);
    // [branch] if (viewspaceNormal4.w == 0) 
    // {
    //     outWorkingAOTermAndEdges[pixCoord] = float2(1, 0); 
    //     return;
    // }
    float3 viewspaceNormal = NormalsDenormalize(viewspaceNormal4.xyz);
    // outWorkingAOTermAndEdges[pixCoord] = float2(viewspaceNormal.x * 0.5 + 0.5, 1); return;

#if 1
    // depth Gather
    float4 valuesUL   = sourceViewspaceDepth16.GatherRed(depthSampler, sampleUV            );
    float4 valuesBR   = sourceViewspaceDepth16.GatherRed(depthSampler, sampleUV, int2(1, 1));
    float viewspaceZ  = valuesUL.y;

    // Calculate edges
    const float pixLZ = valuesUL.x;
    const float pixTZ = valuesUL.z;
    const float pixRZ = valuesBR.z;
    const float pixBZ = valuesBR.x;
    float4 edgesLRTB  = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    float edges = XeGTAO_PackEdges(edgesLRTB);

    // // Generate Normals on the fly
    // float3 CENTER   = XeGTAO_ComputeViewspacePosition( normalizedScreenPos, viewspaceZ, consts );
    // float3 LEFT     = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2(-1,  0) * consts.ViewportPixelSize, pixLZ, consts );
    // float3 RIGHT    = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2( 1,  0) * consts.ViewportPixelSize, pixRZ, consts );
    // float3 TOP      = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2( 0, -1) * consts.ViewportPixelSize, pixTZ, consts );
    // float3 BOTTOM   = XeGTAO_ComputeViewspacePosition( normalizedScreenPos + float2( 0,  1) * consts.ViewportPixelSize, pixBZ, consts );
    // float3 viewspaceNormal1 = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);
    // viewspaceNormal = normalize(viewspaceNormal1 * 0.5 + viewspaceNormal);
#else
    // float viewspaceZ = sourceViewspaceDepth16.SampleLevel(s0, (pixCoord + 1) * consts.ViewportPixelSize, 0).x;
    // float viewspaceZ = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(sourceViewspaceDepthFull32.SampleLevel(s0, (pixCoord + float2(DVS2, DVS3)) * consts.ViewportPixelSize, 0).x));
    float4 valuesUL   = sourceViewspaceDepth16.GatherRed(depthSampler, sampleUV);
    float viewspaceZ  = valuesUL.y;
    float edges = viewspaceNormal4.w;
#endif
    // outWorkingAOTermAndEdges[pixCoord] = float2(edges, 1); return;
    // edges = sourceViewspaceNormal.SampleLevel(normalSampler, (pixCoord + float2(DVS9, DVS10)) * consts.ViewportPixelSize, 0).w;
    [branch] if (viewspaceZ > DEPTH_LINEAR_MAX || g_chara_color1.w >= 0.9999) 
    {
        outWorkingAOTermAndEdges[pixCoord] = float2(1, edges); 
        return;
    }

    // Move center pixel slightly towards camera to avoid imprecision artifacts due to depth buffer imprecision; offset depends on depth texture format used
    // viewspaceZ *= 0.99999; // this is good for FP32 depth buffer
    viewspaceZ *= 0.99920; // this is good for FP16 depth buffer

    const float3 pixCenterPos = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, consts);
    const float3 viewVec = normalize(-pixCenterPos);

    // prevents normals that are facing away from the view vector - xeGTAO struggles with extreme cases, but in Vanilla it seems rare so it's disabled by default
    viewspaceNormal = normalize( viewspaceNormal + max( 0, -dot( viewspaceNormal, viewVec ) ) * viewVec );

    const float baseRadius = EFFECT_RADIUS + (viewspaceZ * EFFECT_RADIUS_DISTANCE_SCALE); // Distance scaled https://github.com/BarbatosBachiko/Reshade-Shaders/blob/main/Shaders/BaBa_XeGTAO.fx
    const float effectRadius = baseRadius * RADIUS_MULTIPLIER; 
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
        const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * consts.NDCToViewMul_x_PixelSize;
        // return pixelDirRBViewspaceSizeAtCenterZ.xy * 0.5 + 0.5;

        float screenspaceRadius = effectRadius * rcp(pixelDirRBViewspaceSizeAtCenterZ.x);

        // fade out for small screen radii 
        visibility += saturate((10.0 - screenspaceRadius) / 100.0) * 0.5;

        // this is the min distance to start sampling from to avoid sampling from the center pixel (no useful data obtained from sampling center pixel)
        const float minS = pixelTooCloseThreshold * rcp(screenspaceRadius);

        //[unroll]
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
            float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcp(projectedNormalVecLength));

            // line 15 from the paper
            float n = signNorm * XeGTAO_FastACos(cosNorm);

            // this is a lower weight target; not using -1 as in the original paper because it is under horizon, so a 'weight' has different meaning based on the normal
            const float lowHorizonCos0 = cos(n + XE_GTAO_PI_HALF);
            const float lowHorizonCos1 = cos(n - XE_GTAO_PI_HALF);

            // lines 17, 18 from the paper, manually unrolled the 'side' loop
            float horizonCos0 = lowHorizonCos0; //-1;
            float horizonCos1 = lowHorizonCos1; //-1;

            [unroll]
            for (float step = 0.0; step < STEPS_PER_SLICE; step++) {
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
                const float mipLevel = clamp(log2(sampleOffsetLength) - DEPTH_MIP_SAMPLING_OFFSET, DEPTH_MIP_SAMPLING_MINIMUM_LEVEL, XE_GTAO_DEPTH_MIP_LEVELS);

                // Snap to pixel center (offset is in pixels)
                sampleOffset = round(sampleOffset) * consts.ViewportPixelSize;
                
                float2 sampleScreenPos0 = normalizedScreenPos + sampleOffset;
                float2 sampleScreenPos1 = normalizedScreenPos - sampleOffset;

                float SZ0 = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleScreenPos0, mipLevel).x;
                float3 samplePos0 = XeGTAO_ComputeViewspacePosition(sampleScreenPos0, SZ0, consts);

                float SZ1 = sourceViewspaceDepth16.SampleLevel(depthSampler, sampleScreenPos1, mipLevel).x;
                float3 samplePos1 = XeGTAO_ComputeViewspacePosition(sampleScreenPos1, SZ1, consts);

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

        // average
        visibility /= SLICE_COUNT;

#if XEGTAO_FOG == 1
        // fog (decrease if fog is bright) (some material skip fog by g_shader_flags) (some materials use height color, while others depth, all by g_shader_flags)
        #if 0
            float fogHLuma = GetLuminance(g_fog_height_color.xyz) * g_fog_height_color.w; // color can be > 1 //TODO: is w even used?
            float fogLuma = fogHLuma;
            fogLuma = saturate(fogLuma); //clean

            float fogNear = max(g_fog_height_params.y, g_fog_state_params.y);
            float fogFar = max(g_fog_height_params.z, g_fog_state_params.z);
            float fogScore = smoothstep(fogNear, fogFar, viewspaceZ) * fogLuma;
            fogScore = sqrt(fogScore);
        #elif 0
            float fogHLuma = GetLuminance(g_fog_height_color.xyz) * g_fog_height_color.w;
            float fogDLuma = GetLuminance(g_fog_depth_color.xyz) * g_fog_depth_color.w;

            float fogHScore = smoothstep(g_fog_height_params.y, g_fog_height_params.z, viewspaceZ) * fogHLuma;
            float fogSScore = smoothstep(g_fog_state_params.y , g_fog_state_params.z, viewspaceZ) * fogDLuma;
            float fogScore = min(fogHScore, fogSScore);
            fogScore = sqrt(fogScore);
        #elif 1
            float fogHLuma = GetLuminance(g_fog_height_color.xyz) * g_fog_height_color.w;
            float fogDLuma = GetLuminance(g_fog_depth_color.xyz) * g_fog_depth_color.w;
            float fogLuma = lerp(fogHLuma, fogDLuma, fogHLuma > fogDLuma ? 0.1 : 0.9);

            float fogHScore = smoothstep(g_fog_height_params.y, g_fog_height_params.z, viewspaceZ) * fogLuma;
            float fogSScore = smoothstep(g_fog_state_params.y, g_fog_state_params.z, viewspaceZ) * fogLuma;
            float fogScore = min(fogHScore, fogSScore);
            fogScore = sqrt(fogScore);
        #endif


        // fogScore = saturate(fogScore); //clean
        // fogScore *= fogScore; //curved
        visibility = max(visibility, fogScore);
#endif

        // Final visibility
		visibility = pow(visibility, /* FINAL_VALUE_POWER * */ GS.XeGTAOFinalPower); 
		visibility = max(MINIMUM_AO_OUTPUT, visibility); // disallow total occlusion (which wouldn't make any sense anyhow since pixel is visible but also helps with packing bent normals)
    }

    visibility = saturate(visibility /* / XE_GTAO_OCCLUSION_TERM_SCALE */);
    outWorkingAOTermAndEdges[pixCoord] = float2(visibility, edges);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////
// Denoiser //
//////////////

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

void XeGTAO_DenoiseCS(uint2 pixCoordBase, Texture2D sourceAOTermAndEdges, SamplerState texSampler, RWTexture2D<unorm float2> outputTexture, const GTAOConstants consts)
{
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

    // Gather edge and visibility quads from working AO texture (uses RenderPixelSize)
    const float2 gatherCenter = float2(pixCoordBase.x, pixCoordBase.y) * consts.RenderPixelSize;

    float4 edgesQ0 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(0, 0));
    float4 edgesQ1 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(2, 0));
    float4 edgesQ2 = sourceAOTermAndEdges.GatherGreen(texSampler, gatherCenter, int2(1, 2));

    float visQ0[4];
    float visQ1[4];
    float visQ2[4];
    float visQ3[4];
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(0, 0)), visQ0);
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(2, 0)), visQ1);
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(0, 2)), visQ2);
    XeGTAO_DecodeGatherPartial(sourceAOTermAndEdges.GatherRed(texSampler, gatherCenter, int2(2, 2)), visQ3);

    [unroll]
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

#if 1   // this allows some small amount of AO leaking from neighbours if there are 3 or 4 edges; this reduces both spatial and temporal aliasing
		const float leak_threshold = 0.001 /* 2.5 */;
		const float leak_strength = 1-ssaoValue /* 0.5 */; // favor dark bleedout to close gaps
		float edginess = (saturate(4.0 - leak_threshold - dot(edgesC_LRTB[side], 1.0)) * rcp(4.0 - leak_threshold)) * leak_strength;
		edgesC_LRTB[side] = saturate(edgesC_LRTB[side] + edginess);
#endif

		// for diagonals; used by first and second pass
		weightTL[side] = diagWeight * (edgesC_LRTB[side].x * edgesL_LRTB.z + edgesC_LRTB[side].z * edgesT_LRTB.x);
		weightTR[side] = diagWeight * (edgesC_LRTB[side].z * edgesT_LRTB.y + edgesC_LRTB[side].y * edgesR_LRTB.z);
		weightBL[side] = diagWeight * (edgesC_LRTB[side].w * edgesB_LRTB.x + edgesC_LRTB[side].x * edgesL_LRTB.w);
		weightBR[side] = diagWeight * (edgesC_LRTB[side].y * edgesR_LRTB.w + edgesC_LRTB[side].w * edgesB_LRTB.y);

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

		aoTerm[side] = sum * rcp(sumWeight);

        outputTexture[pixCoord] = float2(aoTerm[side], side == 0 ? edgesQ0.y : edgesQ1.x);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////
// Implementation / Entry //
////////////////////////////

// // https://blog.demofox.org/2022/01/01/interleaved-gradient-noise-a-different-kind-of-low-discrepancy-sequence/
// float InterleavedGradientNoise(float2 pixCoord) { return frac(52.9829189 * frac(dot(pixCoord, float2(0.06711056, 0.00583715)))); }
// float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
// {
//     temporalIndex %= 64; // need to periodically reset frame to avoid numerical issues
// 
//     // float2 p = float2(pixCoord) + 5.588238f * temporalIndex;
//     // return float2(InterleavedGradientNoise(p.x), InterleavedGradientNoise(p.y));
// 
//     float2 p = float2(pixCoord);
//     float x = InterleavedGradientNoise(p);
//     float y = InterleavedGradientNoise(p + 5.588238);
// 
//     // AI recommended this idk
//     float rot = frac(temporalIndex * 0.61803398875); // golden ratio
//     x = frac(x + rot);
//     y = frac(y + rot);
// 
//     return float2(x, y);
// }

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
    GTAOConstants c = (GTAOConstants)0;

    GetViewport(t0, c.ViewportSize, c.ViewportPixelSize, XEGTAO_HALFRES);

    XeGTAO_PrefilterDepths16x16CS(dtid, gtid, c);
}

[numthreads(XEGTAO_THREADS_NORMALSGEN_X, XEGTAO_THREADS_NORMALSGEN_Y, 1)]
void normal_generate_cs(uint2 dtid : SV_DispatchThreadID)
{
    GTAOConstants c = (GTAOConstants)0;

    // checkboard
    uint2 pixCoord = dtid;
    #if XEGTAO_CHECKBOARD == 1 // half
        pixCoord.y = dtid.y;
        pixCoord.x = dtid.x * 2 + ((dtid.y + LumaSettings.FrameIndex) & 1);
    #elif XEGTAO_CHECKBOARD == 2 // quarter
        uint fi = LumaSettings.FrameIndex;
        pixCoord.x = dtid.x * 2 + ((dtid.y + fi) & 1);
        pixCoord.y = dtid.y * 2 + (((dtid.x + (fi >> 1)) & 1));
    #endif

    // size
    GetViewport(t0, c.ViewportSize, c.ViewportPixelSize, XEGTAO_HALFRES);

    // NDC to View
    c = GetNDCToView(c);

    XeGTAO_ComputeViewspaceNormal(pixCoord, c);
}

[numthreads(XEGTAO_THREADS_NORMALSSMOOTH_X, XEGTAO_THREADS_NORMALSSMOOTH_Y, 1)]
void normal_smooth_cs(uint2 dtid : SV_DispatchThreadID)
{
    GTAOConstants c = (GTAOConstants)0;

    // checkboard
    uint2 pixCoord = dtid;
    #if XEGTAO_CHECKBOARD == 1 // half
        pixCoord.y = dtid.y;
        pixCoord.x = dtid.x * 2 + ((dtid.y + LumaSettings.FrameIndex) & 1);
    #elif XEGTAO_CHECKBOARD == 2 // quarter
        uint fi = LumaSettings.FrameIndex;
        pixCoord.x = dtid.x * 2 + ((dtid.y + fi) & 1);
        pixCoord.y = dtid.y * 2 + (((dtid.x + (fi >> 1)) & 1));
    #endif

    // size
    GetViewport(t0, c.ViewportSize, c.ViewportPixelSize, XEGTAO_HALFRES);

    // NDC to View
    c = GetNDCToView(c);

    XeGTAO_SmoothViewspaceNormal(pixCoord, c);
}

[numthreads(XEGTAO_THREADS_AO_X, XEGTAO_THREADS_AO_Y, 1)]
void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    GTAOConstants c = (GTAOConstants)0;

    // checkboard
    uint2 pixCoord = dtid;
    #if XEGTAO_CHECKBOARD == 1 // half
        pixCoord.y = dtid.y;
        pixCoord.x = dtid.x * 2 + ((dtid.y + LumaSettings.FrameIndex) & 1);
    #elif XEGTAO_CHECKBOARD == 2 // quarter
        uint fi = LumaSettings.FrameIndex;
        pixCoord.x = dtid.x * 2 + ((dtid.y + fi) & 1);
        pixCoord.y = dtid.y * 2 + (((dtid.x + (fi >> 1)) & 1));
    #endif

    // size
    GetViewport(t0, c.ViewportSize, c.ViewportPixelSize, XEGTAO_HALFRES);

    // NDC to View
    c = GetNDCToView(c);

    // Noise "Unclamped Phases", "Static", "2 Phases", "3 Phases", "4 Phases", "5 Phases"
#if XEGTAO_NOISE == 0
    const uint frameIndex = LumaSettings.FrameIndex;
#elif XEGTAO_NOISE == 1
    const uint frameIndex = 0;
#elif XEGTAO_NOISE > 1
    const uint frameIndex = LumaSettings.FrameIndex % XEGTAO_NOISE;
#endif
    float2 localNoise = SpatioTemporalNoise(pixCoord, frameIndex); 

    XeGTAO_MainPassCS(pixCoord, localNoise, c);
}

[numthreads(XEGTAO_THREADS_DENOISE_X, XEGTAO_THREADS_DENOISE_Y, 1)]
void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
    GTAOConstants c = (GTAOConstants)0;

    // size
    GetViewport(t0, c.ViewportSize, c.ViewportPixelSize, XEGTAO_HALFRES);
    c.RenderPixelSize = c.ViewportPixelSize;

    const uint2 pix_coord_base = dtid * uint2(2, 1); // we're computing 2 horizontal pixels at a time (performance optimization)
    XeGTAO_DenoiseCS(pix_coord_base, t0, s0, final_output, c);
}

float4 apply_ps(float4 sv_pos : SV_Position0) : SV_Target0
{
    int2 pixCoord = sv_pos.xy;

    float2 viewportSize;
    float2 viewportPixelSize;
    GetViewport(t0, viewportSize, viewportPixelSize, false);
    float2 uv = (pixCoord + float2(-0.5, -0.5)) * viewportPixelSize;
    float2 normalizedScreenPos = (pixCoord + 0.5) * viewportPixelSize;

    // ps_srv0, ao_srv, FoundResource::Depth::srv.get(), Resource::PreFilteredDepth::srv.get(), normals_srv_effective
    Texture2D tColor = t0;
    Texture2D tAO = t1;
    Texture2D tFullDepth32 = t2;
    Texture2D tHalfDepth16 = t3;
    Texture2D tHalfNormals = t4;
    SamplerState sPoint = s0; //s1 not available

    // color
    float4 color = tColor.Load(int3(pixCoord.xy, 0)).xyzw;
    // float4 color = tColor.SampleLevel(sPoint, uv, 0);
    color = max(0, color); color.w = min(color.w, 1); //REQUIRED!

    // ao
#if XEGTAO_HALFRES == 0
    float ao = tAO.Load(int3(max(2, pixCoord.xy - 1), 0)).x; //TODO: bruh offset
#else
    // float ao = tAO.Load(int3(pixCoord.xy / 2, 0)).x;

    #if XEGTAO_UPSAMPLE == 1
        // Joint Bilateral Upsample (but super frugal depth only ahh)
        // Weights modded from: https://github.com/BarbatosBachiko/Reshade-Shaders/blob/2a68ea7f2c22620f0ef93c19ebf1b7094b897747/Shaders/BaBa_XeGTAO.fx#L536
        #if 0
            float highDepth = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(tFullDepth32.Load(int3(pixCoord, 0)).x)); //center
            const float depth_weight_factor = highDepth *  100; // distance scaled
            
            float4 aoQuad = tAO.GatherRed(sPoint, uv);
            float ao = aoQuad.y; //center
            float4 depthQuad = tHalfDepth16.GatherRed(sPoint, uv);

            float sumAO = 0.0;
            float sumWeight = 0.0;
            [unroll] for (int i = 0; i < 4; i++)
            {
                float sampleAO = aoQuad[i];
                float lowDepth = depthQuad[i];
                float weight = highDepth < lowDepth ? exp2(-abs(highDepth - lowDepth) * depth_weight_factor) : 0;
                sumAO += sampleAO * weight;
                sumWeight += weight;
            }

            ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
        #elif 0
            float highDepth = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(tFullDepth32.Load(int3(pixCoord, 0)).x)); //center
            const float depth_weight_factor = highDepth * 100; // distance scaled

            float4 aoQuadA = tAO.GatherRed(sPoint, uv);
            float4 aoQuadB = tAO.GatherRed(sPoint, uv, int2(DVS1, DVS1));
            float ao = aoQuadA.y; //center
            float4 depthQuadA = tHalfDepth16.GatherRed(sPoint, uv);
            float4 depthQuadB = tHalfDepth16.GatherRed(sPoint, uv, int2(DVS1, DVS1)); // 2nd quad not as good as using normals

            float sumAO = 0.0;
            float sumWeight = 0.0;
            [unroll] for (int i = 0; i < 4; i++)
            {
                float weightA = highDepth > depthQuadA[i] ? exp2(-abs(highDepth - depthQuadA[i]) * depth_weight_factor) : 0;
                sumAO += aoQuadA[i] * weightA;
                sumWeight += weightA;

                float weightB = highDepth < depthQuadB[i] ? exp2(-abs(highDepth - depthQuadB[i]) * depth_weight_factor) : 0;
                sumAO += aoQuadB[i] * weightB;
                sumWeight += weightB;
            }

            ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
        #elif 0
            float highDepth = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(tFullDepth32.Load(int3(pixCoord, 0)).x)); //center
            const float depth_weight_factor = highDepth * 100; // distance scaled
            float ao = 1;
            {
                float4 aoQuad = tAO.GatherRed(sPoint, uv); ao = aoQuad.y; //center
                float4 depthQuad = tHalfDepth16.GatherRed(sPoint, uv);

                float sumAO = 0.0;
                float sumWeight = 0.0;
                [unroll] for (int i = 0; i < 4; i++)
                {
                    float sampleAO = aoQuad[i];
                    float lowDepth = depthQuad[i];
                    float weight = highDepth < lowDepth ? exp2(-abs(highDepth - lowDepth) * depth_weight_factor) : 0;
                    sumAO += sampleAO * weight;
                    sumWeight += weight;
                }

                ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
            }
            {
                float4 aoQuad = tAO.GatherRed(sPoint, uv, int2(DVS1, DVS1));
                float4 depthQuad = tHalfDepth16.GatherRed(sPoint, uv, int2(DVS1, DVS1));

                float sumAO = 0.0;
                float sumWeight = 0.0;
                [unroll] for (int i = 0; i < 4; i++)
                {
                    float sampleAO = aoQuad[i];
                    float lowDepth = depthQuad[i];
                    float weight = highDepth > lowDepth ? exp2(-abs(highDepth - lowDepth) * depth_weight_factor) : 0;
                    sumAO += sampleAO * weight;
                    sumWeight += weight;
                }

                ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
            }
        #elif 0
            // NDC to View
            GTAOConstants c = (GTAOConstants)0;
            c.ViewportSize = viewportSize;
            c.ViewportPixelSize = viewportPixelSize;
            c = GetNDCToView(c);

            // full res depth & normals
            float3 viewspaceNormal;
            float viewspaceZ;
            {
                float4 valuesUL   = finalpass_origdepth.GatherRed(sPoint, float2(uv));
                float4 valuesBR   = finalpass_origdepth.GatherRed(sPoint, float2(uv), int2(1, 1));
                viewspaceZ        = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.y));
                float pixLZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.x));
                float pixTZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.z));
                float pixRZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.z));
                float pixBZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.x));
                float4 edgesLRTB  = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
                float3 CENTER   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, c);
                float3 LEFT     = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1,  0) * c.ViewportPixelSize, pixLZ, c);
                float3 RIGHT    = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 1,  0) * c.ViewportPixelSize, pixRZ, c);
                float3 TOP      = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0, -1) * c.ViewportPixelSize, pixTZ, c);
                float3 BOTTOM   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0,  1) * c.ViewportPixelSize, pixBZ, c);
                viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);
            }
            // return float4(viewspaceNormal.xyz * 0.5 + 0.5, 1);
            // ao *= max3(viewspaceNormal.xyz) * 0.0001; // debug test perf (nearly free)

            float highDepth = viewspaceZ;
            float3 highNormal = viewspaceNormal;
            float ao = tAO.SampleLevel(sPoint, uv, 0).x;

            float sumAO = 0.0;
            float sumWeight = 0.0;

            float4 aoQuad = tAO.GatherRed(sPoint, uv);
            float4 depthQuad = tHalfDepth16.GatherRed(sPoint, uv);
            float4 normalRQuad = tHalfNormals.GatherRed(sPoint, uv); // TOO SLOW!
            float4 normalGQuad = tHalfNormals.GatherGreen(sPoint, uv); // TOO SLOW!
            float4 normalBQuad = tHalfNormals.GatherBlue(sPoint, uv); // TOO SLOW!

            const float wSpatial = exp2(-0.5 * 0.5);
            const float depth_weight_factor = viewspaceZ * 100;

            [unroll] for (int i = 0; i < 4; i++)
            {
                float sampleAO = aoQuad[i];
                float lowDepth = depthQuad[i];
                float3 lowNormal = NormalsDenormalize(float3(normalRQuad[i], normalGQuad[i], normalBQuad[i]));

                float wDepth = exp2(-abs(highDepth - lowDepth) * depth_weight_factor);
                float dotN = max(0.0, dot(highNormal, lowNormal));
                float wNormal = pow(dotN, 16.0);

                float weight = wDepth * wNormal * wSpatial;
                sumAO += sampleAO * weight;
                sumWeight += weight;
            }

            ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
        #elif 1
            // NDC to View
            GTAOConstants c = (GTAOConstants)0;
            c.ViewportSize = viewportSize;
            c.ViewportPixelSize = viewportPixelSize;
            c = GetNDCToView(c);

            // full res depth & normals
            float3 viewspaceNormal;
            float viewspaceZ;
            {
                float4 valuesUL   = finalpass_origdepth.GatherRed(sPoint, float2(uv));
                float4 valuesBR   = finalpass_origdepth.GatherRed(sPoint, float2(uv), int2(1, 1));
                viewspaceZ        = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.y));
                float pixLZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.x));
                float pixTZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.z));
                float pixRZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.z));
                float pixBZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.x));
                float4 edgesLRTB  = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
                float3 CENTER   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, c);
                float3 LEFT     = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1,  0) * c.ViewportPixelSize, pixLZ, c);
                float3 RIGHT    = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 1,  0) * c.ViewportPixelSize, pixRZ, c);
                float3 TOP      = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0, -1) * c.ViewportPixelSize, pixTZ, c);
                float3 BOTTOM   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0,  1) * c.ViewportPixelSize, pixBZ, c);
                viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);
            }
            // return float4(viewspaceNormal.xyz * 0.5 + 0.5, 1);
            // ao *= max3(viewspaceNormal.xyz) * 0.0001; // debug test perf (nearly free)

            // Joint Bilateral Upsample
            // https://github.com/BarbatosBachiko/Reshade-Shaders/blob/2a68ea7f2c22620f0ef93c19ebf1b7094b897747/Shaders/BaBa_XeGTAO.fx#L536
            float ao = 1.0; // accumulator
            {
                float highDepth = viewspaceZ;
                float3 highNormal = viewspaceNormal;

                float sumAO = 0.0;
                float sumWeight = 0.0;

                float2 baseUV = uv;

                float depth_weight_factor = /* viewspaceZ * */ 100;

                // 3x3
                [unroll] for (int x = -1; x <= 1; x++)
                {
                    [unroll] for (int y = -1; y <= 1; y++)
                    {                        
                        float2 sampleUV = uv + float2(x,y) * (viewportPixelSize * 2); // offset (2x of viewport res moves 1x half res)

                        float sampleAO = tAO.SampleLevel(sPoint, sampleUV, 0).x; // ao at offset //TODO: potential cut down sample calls by packing both AO and normals? but depth needs to be float...
                        float3 lowNormal = NormalsDenormalize(tHalfNormals.SampleLevel(sPoint, sampleUV, 0).xyz); // half res normals at offset
                        float lowDepth = tHalfDepth16.SampleLevel(sPoint, sampleUV, 0).x; // half res depth at offset
                        
                        // crazy maths for weights
                        float wDepth = exp2(-abs(highDepth - lowDepth) * depth_weight_factor);
                        float dotN = max(0.0, dot(highNormal, lowNormal));
                        float wNormal = pow(dotN, 16.0);
                        float wSpatial = exp2(-0.5 * float(x * x + y * y));

                        float weight = wDepth * wNormal * wSpatial;
                        sumAO += sampleAO * weight;
                        sumWeight += weight;

                        if (x == 0 && y == 0) ao = sampleAO; // center, should unroll
                    }
                }

                ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
            }
        #endif
    #else
        float ao = tAO.SampleLevel(sPoint, uv, 0).x;
    #endif
#endif

    // return float4(ao.xxx, 1);

#if XE_GTAO_DEBUG_NORMALS == 1
    float3 x = tHalfNormals.SampleLevel(sPoint, uv, 0).xyz;
    x = NormalsDenormalize(x);
    x = x * 0.5 + 0.5;
    color.w = 1;
#elif XE_GTAO_DEBUG_DEPTH == 1
    // float d = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(tFullDepth32.SampleLevel(sPoint, uv, 0).x));
    float d = tHalfDepth16.SampleLevel(sPoint, uv, DEVELOPMENT ? floor(DVS10 * XE_GTAO_DEPTH_MIP_LEVELS) : 0).x;
    // d = 0.1 * d;
    d = d / ((d / 2) + 1);
    // d *= color.w;
    d = max(0, d);
    float3 x = d;
    color.w = 1;
#else
    // color
    float3 x = color.xyz;
    x = pow(x, 2.2); //gamma decode

    // scale to ambient (biggest offender: Tokyo Teddy Bear)
    float aLuma = GetLuminance(g_shadow_ambient.xyz) /* * g_shadow_ambient.w */;
    float bLuma = GetLuminance(g_shadow_ambient1.xyz) /* * g_shadow_ambient1.w */; //w is usally 0
    float ambientLuma = max(aLuma, bLuma);
    // return float4((ambientLuma - DVS1).xxx, 1);

    ambientLuma = max(ambientLuma, 0.0001); //safe
    ambientLuma = pow(ambientLuma, 2.2); //gamma decode
    ambientLuma = 1 / ambientLuma; //compute scale
    x /= ambientLuma; //ambient scale

    // reduce ao for brighter color
    float l = GetLuminance(x);
    l *= l; //curved
    l *= LUMINANCE_DODGE;
    ao = remap(ao, 0, 1, l, 1);
    ao = saturate(ao);

    x *= ao; //apply
    x *= ambientLuma; //ambient scale inverse
    x = pow(x, 1.0 / 2.2); //gamma encode

#if XE_GTAO_DEBUG_AO == 1
    x = ao;
    color.w = 1;
#endif
#endif

    return float4(x, color.w);
}

//     // Single 2x2-quad Gather layout (per HLSL spec, components ordered
//     // starting bottom-left, going counter-clockwise: x=BL, y=TL, z=TR, w=BR)
//     // aoUL covers the quad whose top-left is at 'uv'
//     // aoBR covers the quad offset by (1,1) texels
//     float4 aoUL = t1.GatherRed(s1, uv, 0          /* + 1 */); // BL, TL, TR, BR of upper-left quad
//     float4 aoBR = t1.GatherRed(s1, uv, int2(1, 1) /* + 1 */); // BL, TL, TR, BR of lower-right quad
//     float4 aoUR = t1.GatherRed(s1, uv, int2(1, 0) /* + 1 */); // needed for TR/BR corner diagonals
//     float4 aoBL = t1.GatherRed(s1, uv, int2(0, 1) /* + 1 */); // needed for BL/TL corner diagonals
// 
//     // float4 eUL = t1.GatherGreen(s1, uv,            + 1);
//     // float4 eBR = t1.GatherGreen(s1, uv, int2(1, 1) + 1);
//     // float4 eUR = t1.GatherGreen(s1, uv, int2(1, 0) + 1);
//     // float4 eBL = t1.GatherGreen(s1, uv, int2(0, 1) + 1);
// 
//     // Cross taps (same as before)
//     float aoC = aoUL.z; // TR of upper-left quad == center-ish texel
//     float aoL = aoUL.y; // TL
//     float aoT = aoUL.w; // wait — verify against your Gather() component order below
//     float aoR = aoBR.z;
//     float aoB = aoBR.y;
// 
//     // NOTE: Gather() component ordering differs by API/compiler docs you may be
//     // using (some engines document x=TL,y=TR,z=BR,w=BL). Confirm against your
//     // existing working cross-taps above before trusting the corner extraction —
//     // match whichever ordering makes your ORIGINAL aoC/aoL/aoT/aoR/aoB taps
//     // line up with real neighbors, then apply the same ordering below.
// 
//     // Diagonal / corner taps — free, from the same 4 Gather calls
//     float aoTL = aoUL.x;
//     float aoTR = aoUR.y;
//     float aoBL_ = aoBL.w;
//     float aoBR_ = aoBR.x;
// 
//     float eC   = 1/* eUL.z */;
//     float eL   = 1/* eUL.y */;
//     float eT   = 1/* eUL.w */;
//     float eR   = 1/* eBR.z */;
//     float eB   = 1/* eBR.y */;
//     float eTL  = 1/* eUL.x */;
//     float eTR  = 1/* eUR.y */;
//     float eBL_ = 1/* eBL.w */;
//     float eBR_ = 1/* eBR.x */;
// 
//     // --- Bilateral-style weighted blend (replaces flat /5 average) ---
//     // Gaussian spatial weights for a 3x3 kernel (sigma ~1), corners lighter
//     static const float wCenter = 0.25;
//     static const float wCross  = 0.125; // 4 of these
//     static const float wCorner = 0.0625; // 4 of these
//     // (0.25 + 4*0.125 + 4*0.0625 == 1.0)
// 
//     // static const float wCenter = 1/9.f;
//     // static const float wCross  = 4/9.f; // 4 of these
//     // static const float wCorner = 4/9.f; // 4 of these
// 
//     float sumW  = wCenter * eC;
//     float sumAO = aoC * wCenter * eC;
// 
//     sumW  += wCross * eL; sumAO += aoL * wCross * eL;
//     sumW  += wCross * eR; sumAO += aoR * wCross * eR;
//     sumW  += wCross * eT; sumAO += aoT * wCross * eT;
//     sumW  += wCross * eB; sumAO += aoB * wCross * eB;
// 
//     sumW  += wCorner * eTL; sumAO += aoTL * wCorner * eTL;
//     sumW  += wCorner * eTR; sumAO += aoTR * wCorner * eTR;
//     sumW  += wCorner * eBL_; sumAO += aoBL_ * wCorner * eBL_;
//     sumW  += wCorner * eBR_; sumAO += aoBR_ * wCorner * eBR_;
// 
//     float ao0 = sumAO / max(sumW, 1e-5); // edge-aware 3x3 blend, now includes diagonals
//     float ao1 = aoC;                     // slight edge bleed (unchanged)
//     // float ao2 = max3(max3(max3(aoC, aoL, aoT), max3(aoR, aoB, aoTL), aoTR), aoBL_, aoBR_); // heavy edge bleed, now over full 3x3
//     // float ao2 = max3(max3(aoC, aoL, aoT), aoR, aoB); // heavy edge bleed
// 
//     // merge ao (unchanged blend logic)
//     float ao = saturate(ao0);
//     ao = max((ao + (ao1 * 6)) / (1 + 6), ao);
//     // ao = max((ao + (ao2 * DVS1)) / (1 + DVS1), ao);

    // GTAOConstants c = (GTAOConstants)0;

    // // NDC to View
    // c.ViewportSize = viewportSize;
    // c.ViewportPixelSize = viewportPixelSize;
    // float Pxx = dot(g_projection_view[0].xyz, g_view[0].xyz);
    // float Pyy = dot(g_projection_view[1].xyz, g_view[1].xyz);
    // float tanHalfFovX = 1.0 / Pxx;
    // float tanHalfFovY = 1.0 / Pyy;
    // c.NDCToViewMul = float2(2.0, -2.0) * float2(tanHalfFovX, tanHalfFovY);
    // c.NDCToViewAdd = float2(-1.0, 1.0) * float2(tanHalfFovX, tanHalfFovY);
    // c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;

    // // full res edges
    // float4 valuesUL   = finalpass_origdepth.GatherRed(s0, float2(uv));
    // float4 valuesBR   = finalpass_origdepth.GatherRed(s0, float2(uv), int2(1, 1));
    //     float viewspaceZ  = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.y);
    //     float pixLZ       = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.x);
    //     float pixTZ       = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.z);
    //     float pixRZ       = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.z);
    //     float pixBZ       = XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.x);
    // float4 edgesLRTB  = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
    // // edgesLRTB = sqrt(edgesLRTB);
    // // edgesLRTB = max(0.22, edgesLRTB);
    // 
    // float ao1 = 0/* (aoC) + (aoL * edgesLRTB.x) + (aoT * edgesLRTB.z) + (aoR * edgesLRTB.y) + (aoB * edgesLRTB.w) */;

//     // Joint Bilateral Upsample
//     // https://github.com/BarbatosBachiko/Reshade-Shaders/blob/2a68ea7f2c22620f0ef93c19ebf1b7094b897747/Shaders/BaBa_XeGTAO.fx#L536
//     {
//         float highDepth = viewspaceZ;
//         float3 highNormal = viewspaceNormal;
// 
//         float sumAO = 0.0;
//         float sumWeight = 0.0;
//         
//         float2 baseUV = uv;
//         
//         float depth_weight_factor = /* 0.1 * */ viewspaceZ /* 1.0 / (0.1 * viewspaceZ + 1e-6) */;
// 
//         [unroll] for (int x = -1; x <= 1; x++)
//         {
//             [unroll] for (int y = -1; y <= 1; y++)
//             {
//                 if (x == 0 && y == 0) continue; // center
//                 if (x == 1 && y == 1) continue; // corner
//                 if (x == 1 && y == -1) continue; // corner
//                 if (x == -1 && y == 1) continue; // corner
//                 if (x == -1 && y == -1) continue; // corner
//                 
//                 float2 sampleUV = uv + float2(x,y) * (viewportPixelSize * 2); // offset (2x of viewport res moves 1x half res)
// 
//                 float sampleAO = tAO.SampleLevel(sPoint, sampleUV, 0).x; // ao at offset
//                 //   return float4(sampleAO.xxx, 1);
//                 float3 lowNormal = NormalsDenormalize(tHalfNormals.SampleLevel(sPoint, sampleUV, 0).xyz); // half res normals at offset
//                 float lowDepth = tHalfDepth16.SampleLevel(sPoint, sampleUV, 0).x; // half res normals at offset
//                 
//                 // crazy maths for weights
//                 float wDepth = exp(-abs(highDepth - lowDepth) * depth_weight_factor);
//                 float dotN = max(0.0, dot(highNormal, lowNormal));
//                 float wNormal = pow(dotN, 16.0);
//                 float wSpatial = exp(-0.5 * float(x * x + y * y));
// 
//                 float weight = wDepth * wNormal * wSpatial;
//                 sumAO += sampleAO * weight;
//                 sumWeight += weight;
//             }
//         }
// 
//         ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
//     }


//     // NDC to View
//     GTAOConstants c = (GTAOConstants)0;
//     c.ViewportSize = viewportSize;
//     c.ViewportPixelSize = viewportPixelSize;
// #if 1
//     float Pxx = dot(g_projection_view[0].xyz, g_view[0].xyz); //TODOL: why is this not working!?!?
//     float Pyy = dot(g_projection_view[1].xyz, g_view[1].xyz);
//     float tanHalfFovX = 1.0 / Pxx;
//     float tanHalfFovY = 1.0 / Pyy;
//     c.NDCToViewMul = float2(2.0, -2.0) * float2(tanHalfFovX, tanHalfFovY);
//     c.NDCToViewAdd = float2(-1.0, 1.0) * float2(tanHalfFovX, tanHalfFovY);
//     c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;
// #else       
//     float tanHalfFOV = tan(20 * XE_GTAO_PI_OVER_360);
//     float aspect = c.ViewportSize.x / c.ViewportSize.y;
//     c.NDCToViewMul = float2(2.0, -2.0) * float2(aspect * tanHalfFOV, tanHalfFOV);
//     c.NDCToViewAdd = float2(-1.0, 1.0) * float2(aspect * tanHalfFOV, tanHalfFOV);
//     c.NDCToViewMul_x_PixelSize = c.NDCToViewMul * c.ViewportPixelSize;
// #endif
//
//     // full res depth & normals
//     float3 viewspaceNormal;
//     float viewspaceZ;
//     {
//         float4 valuesUL   = finalpass_origdepth.GatherRed(sPoint, float2(uv));
//         float4 valuesBR   = finalpass_origdepth.GatherRed(sPoint, float2(uv), int2(1, 1));
//         viewspaceZ        = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.y));
//         float pixLZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.x));
//         float pixTZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesUL.z));
//         float pixRZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.z));
//         float pixBZ       = XeGTAO_ClampDepth(XeGTAO_ScreenSpaceToViewSpaceDepth(valuesBR.x));
//         float4 edgesLRTB  = XeGTAO_CalculateEdges(viewspaceZ, pixLZ, pixRZ, pixTZ, pixBZ);
//         float3 CENTER   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos, viewspaceZ, c);
//         float3 LEFT     = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2(-1,  0) * c.ViewportPixelSize, pixLZ, c);
//         float3 RIGHT    = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 1,  0) * c.ViewportPixelSize, pixRZ, c);
//         float3 TOP      = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0, -1) * c.ViewportPixelSize, pixTZ, c);
//         float3 BOTTOM   = XeGTAO_ComputeViewspacePosition(normalizedScreenPos + float2( 0,  1) * c.ViewportPixelSize, pixBZ, c);
//         viewspaceNormal = XeGTAO_CalculateNormal(edgesLRTB, CENTER, LEFT, RIGHT, TOP, BOTTOM);
//     }
//     // return float4(viewspaceNormal.xyz * 0.5 + 0.5, 1);
//     // ao *= max3(viewspaceNormal.xyz) * 0.0001; // debug test perf (nearly free)
// 
//     // Joint Bilateral Upsample (super frugal ahh)
//     // Weights modded from: https://github.com/BarbatosBachiko/Reshade-Shaders/blob/2a68ea7f2c22620f0ef93c19ebf1b7094b897747/Shaders/BaBa_XeGTAO.fx#L536
//     {
//         float highDepth = viewspaceZ;
//         float3 highNormal = viewspaceNormal;
// 
//         float sumAO = 0.0;
//         float sumWeight = 0.0;
// 
//         float4 aoQuad = tAO.GatherRed(sPoint, uv);
//         float4 depthQuad = tHalfDepth16.GatherRed(sPoint, uv);
//         // float4 normalRQuad = tHalfNormals.GatherRed(sPoint, uv); // TOO SLOW!
//         // float4 normalGQuad = tHalfNormals.GatherGreen(sPoint, uv);
//         // float4 normalBQuad = tHalfNormals.GatherBlue(sPoint, uv);
// 
//         const float wSpatial = exp(-0.5 * 0.5);
//         const float depth_weight_factor = viewspaceZ * 100;
// 
//         [unroll] for (int i = 0; i < 4; i++)
//         {
//             float sampleAO = aoQuad[i];
//             float lowDepth = depthQuad[i];
//             // float3 lowNormal = NormalsDenormalize(float3(normalRQuad[i], normalGQuad[i], normalBQuad[i]));
// 
//             // wDepth
//             float wDepth = exp(-abs(highDepth - lowDepth) * depth_weight_factor);
// 
//             // float dotN = max(0.0, dot(highNormal, lowNormal));
//             // float wNormal = pow(dotN, 16.0);
// 
//             float weight = wDepth /* * wNormal */ * wSpatial;
//             sumAO += sampleAO * weight;
//             sumWeight += weight;
//         }
// 
//         ao = (sumWeight >= 1e-6) ? (sumAO / sumWeight) : ao;
//     }

#endif // __XE_GTAO_HLSLI__