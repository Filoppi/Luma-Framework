#include "Includes/Bloom.hlsl"

cbuffer GFD_PSCONST_SAMPLE_OFFSETS : register(b12)
{
	float4 sampleOffsets[15] : packoffset(c0);
}

cbuffer GFD_PSCONST_SAMPLE_WEIGHTS : register(b13)
{
	float4 sampleWeights[15] : packoffset(c0);
}

SamplerState sampler0_s : register(s0);
Texture2D<float4> texture0 : register(t0);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_Target0)
{
	float4 r0,r1;
	uint4 bitmask, uiDest;
	float4 fDest;

	float2 bloomScale = GetBloomScale(texture0);
	r0.xy = sampleOffsets[1].xy * bloomScale + v1.xy;
	r0.xyzw = texture0.Sample(sampler0_s, r0.xy).xyzw;
	r0.xyzw = sampleWeights[1].xyzw * r0.xyzw;
	r1.xy = sampleOffsets[0].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[0].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[2].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[2].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[3].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[3].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[4].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[4].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[5].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[5].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[6].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[6].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[7].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[7].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[8].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[8].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[9].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[9].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[10].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[10].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[11].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[11].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[12].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[12].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[13].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	r0.xyzw = sampleWeights[13].xyzw * r1.xyzw + r0.xyzw;
	r1.xy = sampleOffsets[14].xy * bloomScale + v1.xy;
	r1.xyzw = texture0.Sample(sampler0_s, r1.xy).xyzw;
	o0.xyzw = sampleWeights[14].xyzw * r1.xyzw + r0.xyzw;
	return;
}