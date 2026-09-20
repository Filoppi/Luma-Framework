#include "Includes/Common.hlsl"

cbuffer GFD_PSCONST_2D : register(b7)
{
	float alphaKillThreshold : packoffset(c0);
	float gamma : packoffset(c0.y);
}

SamplerState diffuseSampler_s : register(s0);
Texture2D<float4> diffuseTexture : register(t0);



void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_Target0)
{
	float4 r0;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.xyzw = diffuseTexture.Sample(diffuseSampler_s, v1.xy).xyzw;
	o0.xyz = max(float3(0,0,0), r0.xyz);
	o0.w = r0.w;
	if (LumaSettings.DisplayMode == 1)
	{
		float gamePaperWhite = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
		o0.xyz = gamma_to_linear(linear_to_sRGB_gamma(r0.xyz, GCT_MIRROR), GCT_MIRROR) * gamePaperWhite;
	}
	else
	{
		r0.xyz = log2(max(r0.xyz, 0.0f));
		r0.xyz = gamma * r0.xyz;
		o0.xyz = exp2(r0.xyz);
	}
	
	return;
}