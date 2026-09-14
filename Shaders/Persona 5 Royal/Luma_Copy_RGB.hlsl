SamplerState sourceSampler_s : register(s0);
Texture2D<float4> sourceTexture : register(t0);

void main(
	float4 v0 : SV_POSITION0,
	float2 uv : TEXCOORD0,
	out float4 o0 : SV_TARGET0)
{
	o0.xyz = sourceTexture.SampleLevel(sourceSampler_s, uv, 0).xyz;
	o0.w = 1.0f;
	return;
}