#include "Includes/Common.hlsl"

Texture2D<float4> g_dsrResult : register(t0);
Texture2D<float4> g_origColor : register(t1);
RWTexture2D<float4> g_outputTex : register(u0);
SamplerState g_linearSampler: register(s0);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID, uint3 gid : SV_GroupId, uint gix : SV_GroupIndex)
{
	if(any(tid >= uint2(LumaSettings.GameSettings.OutputRes)))
	{
		return;
	}
	float2 uv = tid.xy * LumaSettings.GameSettings.InvOutputRes;
	
	float alpha = 0.0f;
	[branch]
	if(LumaSettings.GameSettings.RenderScale < 1.0f)
	{	
		//the alpha channel is used to mask out elements from bloom, when upscaling we won't get perfect coverage anyway so filter a bit more
		//conservatively to prevent light leaking
		for(int x = -1; x <= 1; ++x)
		{
			for(int y = -1; y <= 1; ++y)
			{
				alpha = max(alpha, g_origColor.SampleLevel(g_linearSampler, uv + float2(x, y) * LumaSettings.GameSettings.InvRenderRes, 0).a);
			}
		}
	}
	else
	{
		alpha = g_origColor.SampleLevel(g_linearSampler, uv, 0).a;
	}

	g_outputTex[tid] = float4(g_dsrResult[tid].rgb, alpha);
}