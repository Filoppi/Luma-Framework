#include "Includes/Common.hlsl"

cbuffer CB_PREPARE_OCEAN : register(b0)
{
	float4x4 mtxLocalToWorldPrev;
    float4x4 mtxViewProjPrev;
	bool useCurrentTexShift;
	uint TexShiftOffset;
}

cbuffer GFD_VSCONST_OCEAN : register(b1)
{
	float4x4 mtxInvLocalToWorld : packoffset(c0);
	float2 TexShift : packoffset(c4);
	float TCScale : packoffset(c4.z);
	float WaterWidthScale : packoffset(c4.w);
	float WaterHeightScale : packoffset(c5);
	float VertexPitch : packoffset(c5.y);
}

struct VSCONST_OCEAN_PREV_DATA
{
    float4x4 mtxLocalToWorldPrev;
    float4x4 mtxViewProjPrev;
	float2 TexShiftPrev;
	float2 Pad;
};

ByteAddressBuffer CachedTexShift : register(t0);

RWStructuredBuffer<VSCONST_OCEAN_PREV_DATA> g_GFD_VSCONST_OCEAN_PREV_DATA : register(u0);

[numthreads(1, 1, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	g_GFD_VSCONST_OCEAN_PREV_DATA[0].mtxLocalToWorldPrev = mtxLocalToWorldPrev;
	g_GFD_VSCONST_OCEAN_PREV_DATA[0].mtxViewProjPrev = mtxViewProjPrev;
	if(useCurrentTexShift)
	{
		g_GFD_VSCONST_OCEAN_PREV_DATA[0].TexShiftPrev = TexShift;
	}
	else
	{
		g_GFD_VSCONST_OCEAN_PREV_DATA[0].TexShiftPrev = asfloat(CachedTexShift.Load2(TexShiftOffset)).xy;
	}
}