Texture2D<float4> sourceTexture : register(t0);
SamplerState sourceSampler : register(s0);

// Bilinear scaling copy: samples the source texture with normalized UVs, so it can
// copy between same-aspect different-resolution resources (e.g. render-res <-> output-res mirrors).
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
	return sourceTexture.SampleLevel(sourceSampler, uv, 0);
}
