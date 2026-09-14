#include "Includes/Common.hlsl"

cbuffer CB_MOTION_VECTOR : register(b0)
{
	float4x4 reprojectionMatrix;
}

Texture2D<float4> g_velocityTex : register(t0);
Texture2D<float> g_depthTex : register(t1);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(LumaSettings.GameSettings.RenderRes)))
	{
		return;
	}

	float2 velocity = g_velocityTex[tid].xy;
	velocity *= 0.5f;
	velocity.x *= -1.0f;
	
	float depth = g_depthTex[tid];
	if(g_depthTex[tid] == 1.0f)
	{
		float2 texCoord = tid / LumaSettings.GameSettings.RenderRes;
		float4 prevTS = mul(float4(texCoord * 2.0f - 1.0f, depth, 1.0f), reprojectionMatrix);
		prevTS /= prevTS.w;
		prevTS = (prevTS + 1.0f) * 0.5f;
		velocity = prevTS.xy - texCoord;
	}
	
	g_updatedVelocityTex[tid] = velocity * LumaSettings.GameSettings.RenderRes;
}