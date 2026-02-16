#include "Includes/Common.hlsl"

Texture2D<float4> g_velocityTex : register(t0);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(LumaSettings.GameSettings.RenderRes)))
	{
		return;
	}
	float4 encoded = g_velocityTex[tid];
    uint quadrant = (uint)round(encoded.a * 3.0);
    float signX = (quadrant & 2) ? 1.0 : -1.0;
    float signY = (quadrant & 1) ? 1.0 : -1.0;

    float2 mag = encoded.xy;

    mag = mag * mag;

    mag = mag * float2(signX, signY);

	g_updatedVelocityTex[tid] = mag * LumaSettings.GameSettings.RenderRes;
}