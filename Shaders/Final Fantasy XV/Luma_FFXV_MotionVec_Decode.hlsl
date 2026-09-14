cbuffer cbTemporalAA : register(b0)
{
	float4 g_screenSize : packoffset(c0);
	float4 g_frameBits : packoffset(c1);
	float4 g_uvJitterOffset : packoffset(c2);
	float4x4 g_motionMatrix : packoffset(c3);
	float4x4 g_reconstructMatrix : packoffset(c7);
	float4 g_unprojectParams : packoffset(c11);
	float g_maxLuminanceInv : packoffset(c12);
	bool g_gamePaused : packoffset(c12.y);
	bool g_hairUseAlphaTest : packoffset(c12.z);
	bool g_waterResponsiveAA : packoffset(c12.w);
}

Texture2D<float2> g_velocityTex : register(t0);
Texture2D<float> g_depthTex : register(t1);
RWTexture2D<float2> g_updatedVelocityTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(g_screenSize.xy)))
	{
		return;
	}
	float2 velocity = g_velocityTex[tid].xy;
	// NOTE: FFXV reports g_gamePaused inverted: 0 = paused, 1 = running.
	if (g_gamePaused == 0)
	{
		g_updatedVelocityTex[tid] = float2(0.0f, 0.0f);
		return;
	}
	if(velocity.y == 1.0f)
	{
		float2 texCoord = float2(tid) / g_screenSize.xy;
		float depth = g_depthTex[tid];
		float4 prevTS = mul(g_motionMatrix, float4(texCoord, depth, 1.0f));
		prevTS /= prevTS.w;
		velocity = prevTS.xy - texCoord;
	}
	// else
	// {
	// 	if(abs(velocity.x) >= 4.0f)
	// 	{
	// 		velocity.x += (velocity.x > 0.0f) ? 4.0f : -4.0f;
	// 	}
	// }
	// velocity = g_uvJitterOffset.xy * 0.5f + velocity;
	g_updatedVelocityTex[tid] = velocity * g_screenSize.xy;
}