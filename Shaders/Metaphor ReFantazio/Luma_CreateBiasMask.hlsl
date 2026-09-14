#include "Includes/Common.hlsl"

Texture2D<float4> g_particleTex : register(t0);
RWTexture2D<float2> g_biasMaskTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(LumaSettings.GameSettings.RenderRes)))
	{
		return;
	}
	
	g_biasMaskTex[tid] = 1.0f - g_particleTex[tid].a;
}