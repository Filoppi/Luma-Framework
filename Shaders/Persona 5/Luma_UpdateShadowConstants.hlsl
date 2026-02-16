#include "Includes/Common.hlsl"

struct PSCONST_SHADOW
{
  float4 packed;
  float4 shadowColor;
  float4 csmDebugColor[3];
};

cbuffer GFD_PSCONST_SHADOW : register(b0)
{
	PSCONST_SHADOW g_shadow;
}

RWStructuredBuffer<PSCONST_SHADOW> g_updatedGFD_PSCONST_SHADOW : register(u0);

[numthreads(1, 1, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	g_updatedGFD_PSCONST_SHADOW[0].packed.r = g_shadow.packed.r;
	g_updatedGFD_PSCONST_SHADOW[0].packed.g = g_shadow.packed.g;
	g_updatedGFD_PSCONST_SHADOW[0].packed.b = g_shadow.packed.b;
	g_updatedGFD_PSCONST_SHADOW[0].packed.a = LumaSettings.GameSettings.InvShadowRes;
	g_updatedGFD_PSCONST_SHADOW[0].shadowColor = g_shadow.shadowColor;
	g_updatedGFD_PSCONST_SHADOW[0].csmDebugColor[0] = g_shadow.csmDebugColor[0];
	g_updatedGFD_PSCONST_SHADOW[0].csmDebugColor[1] = g_shadow.csmDebugColor[1];
	g_updatedGFD_PSCONST_SHADOW[0].csmDebugColor[2] = g_shadow.csmDebugColor[2];
}