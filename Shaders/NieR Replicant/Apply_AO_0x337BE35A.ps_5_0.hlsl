cbuffer CbDynamicResolution : register(b0)
{
	float2 dynamic_resolution_size : packoffset(c0);
	float2 static_resolution_size : packoffset(c0.z);
	float2 dr_downscale : packoffset(c1);
	float2 dr_inv_downscale : packoffset(c1.z);
}

SamplerState ssaoTex_s_s : register(s0);
Texture2D<float4> ssaoTex : register(t0);
Texture2D<float> depthTex : register(t1);

static const float4 weights[] = {
	float4(0.1458f, 0.5626f, 0.1458f, 0.1458f),
	float4(0.5626f, 0.1458f, 0.1458f, 0.1458f),
	float4(0.1458f, 0.1458f, 0.5626f, 0.1458f),
	float4(0.1458f, 0.1458f, 0.1458f, 0.5626f),
};

void main(
	float4 v0 : SV_POSITION0,
	linear noperspective float2 v1 : TEXCOORD0,
	out float4 o0 : SV_TARGET0)
{
	float4 r0;

	r0.xy = dr_downscale.xy * v1.xy;
	
	uint2 screenPos = v0.xy;
	float4 aoSamples = ssaoTex.GatherRed(ssaoTex_s_s, v1, 0);
	float4 halfResDepths = ssaoTex.GatherGreen(ssaoTex_s_s, v1, 0);
	float fullResDepth = depthTex[screenPos];
	
	float4 depthDifferences = abs(fullResDepth - halfResDepths);

	float aoSum = 0.0f;
	float totalWeigth = 0.0f;
	for(uint i = 0; i < 4; ++i)
	{
		float weight = weights[screenPos.x % 2 + 2 * (screenPos.y % 2)][i] * rcp(max(abs(fullResDepth - halfResDepths[i]), 1e-10));
		aoSum += aoSamples[i] * weight;
		totalWeigth += weight;
	}
	o0.xyz = aoSum / totalWeigth;
	o0.w = 1;
	return;
}