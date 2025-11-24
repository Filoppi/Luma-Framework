// XeGTAO implementation adopted for Bioshock Infinite
//
// For the reference see:  https://github.com/GameTechDev/XeGTAO

cbuffer _Globals : register(b0)
{
	float4 OcclusionFadeoutParameters : packoffset(c0);
	float3x3 WorldToView : packoffset(c1);
	float4 g_f4RTSize : packoffset(c4); // (width, height, 1/width, 1/height)
	float g_fTanHalfH : packoffset(c5);
	float g_fTanHalfV : packoffset(c5.y);
}

#define VIEWPORT_PIXEL_SIZE g_f4RTSize.zw
#define NDC_TO_VIEW_MUL float2(g_fTanHalfH * 2.0, g_fTanHalfV * -2.0)
#define NDC_TO_VIEW_ADD float2(-g_fTanHalfH, g_fTanHalfV)
#define EFFECT_RADIUS 0.12
#define DEPTH_MIP_SAMPLING_OFFSET 2.0
#define SLICE_COUNT 15.0
#include "XeGTAO.hlsli"

SamplerState smp : register(s0);

Texture2D tex0 : register(t0);
Texture2D tex1 : register(t1);

RWTexture2D<float> out_working_depth_mip0 : register(u0);
RWTexture2D<float> out_working_depth_mip1 : register(u1);
RWTexture2D<float> out_working_depth_mip2 : register(u2);
RWTexture2D<float> out_working_depth_mip3 : register(u3);
RWTexture2D<float> out_working_depth_mip4 : register(u4);
RWTexture2D<unorm float2> ao_term_and_edges : register(u0);
RWTexture2D<unorm float4> final_output : register(u0);

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
	XeGTAO_PrefilterDepths16x16(dtid, gtid, tex0, smp, out_working_depth_mip0, out_working_depth_mip1, out_working_depth_mip2, out_working_depth_mip3, out_working_depth_mip4);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void main_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
	// tex0 = g_srcWorkingDepth
	// smp = g_samplerPointClamp

	// Sample and decode normals.
	const float2 normalizedScreenPos = (dtid + 0.5) * VIEWPORT_PIXEL_SIZE;
	float3 viewspaceNormal = tex1.SampleLevel(smp, normalizedScreenPos, 0).xyz;
	viewspaceNormal = viewspaceNormal * 2.0 - 1.0;
	viewspaceNormal = normalize(viewspaceNormal);
	viewspaceNormal *= 5.0;
	viewspaceNormal = mul(WorldToView, viewspaceNormal);

	XeGTAO_MainPass(dtid, SpatioTemporalNoise(dtid, 0), viewspaceNormal, tex0, smp, ao_term_and_edges);
}

[numthreads(XE_GTAO_NUMTHREADS_X, XE_GTAO_NUMTHREADS_Y, 1)]
void denoise_pass_cs(uint2 dtid : SV_DispatchThreadID)
{
	// tex0 = g_srcWorkingAOTerm and g_srcWorkingEdges, packed
	// smp = g_samplerPointClamp
	const uint2 pix_coord_base = dtid * uint2(2, 1); // we're computing 2 horizontal pixels at a time (performance optimization)
	XeGTAO_Denoise(pix_coord_base, tex0, smp, final_output, true);
}