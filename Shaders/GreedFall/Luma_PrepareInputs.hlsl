#include "Includes/Common.hlsl"

Texture2D<float2> g_velocityTex : register(t0);
Texture2D<float2> g_exposureTex : register(t1);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);
RWTexture2D<float> g_updatedExposureTex: register(u1);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(LumaSettings.GameSettings.Resolution)))
	{
		return;
	}
	if(all(tid == 1))
	{
		g_updatedExposureTex[uint2(0, 0)] = g_exposureTex[uint2(0, 0)].y;
	}
	float2 velocity = g_velocityTex[tid];
	g_updatedVelocityTex[tid] = velocity * float2(-1.0f, -1.0f) * LumaSettings.GameSettings.Resolution;
}