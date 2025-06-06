#include "Includes/Common.hlsl"

#if _65CDC30B
// 1 LUT (no blend)
#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 0
#elif _3B759722
// 2 LUTs blend
#define _RT_SAMPLE0 1
#define _RT_SAMPLE1 0
#elif _446A3AC5
// 3 LUTs blend
#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 1
#elif _
//LUMA TODO: this shader is missing
// 4 LUTs blend
#define _RT_SAMPLE0 0
#define _RT_SAMPLE1 1
#endif

cbuffer PER_BATCH : register(b0)
{
  float4 LayerBlendAmount : packoffset(c0);
}

SamplerState layer0Sampler_s : register(s0);
Texture2D<float4> layer0Sampler : register(t0);
SamplerState layer1Sampler_s : register(s1);
Texture2D<float4> layer1Sampler : register(t1);
SamplerState layer2Sampler_s : register(s2);
Texture2D<float4> layer2Sampler : register(t2);
SamplerState layer3Sampler_s : register(s3);
Texture2D<float4> layer3Sampler : register(t3);

struct v2f_cch
{
	float4 HPosition : SV_Position0;	
	float2 baseTC : TEXCOORD0;
	float3 Color : TEXCOORD1;
};

// MergeColorChartsPS
// This happens before "CombineColorGradingWithColorChart", so at this point LUTs are still in gamma space (and possibly output to FP16 instead of UNORM8, thanks to Luma upgrades).
// This doesn't seem to be always run though, for some reason it's often skipped, maybe when there are no LUTs at all loaded (it's seemengly run even when there is a single LUT, so nothing to "merge").
float4 main(v2f_cch IN) : SV_Target0
{
	// LUMA FT: we have made these LUTs blend in linear space as opposed to gamma space, for "higher quality"
	// (it avoids hue shifts, though in case widely different LUTs were blended (e.g. black and white), it would make a 50% value less perceptually accurate,
	// but there doesn't seem to be such a use case for Prey).
	// For simplicity we use sRGB gamma, independently of "GAMMA_CORRECTION_TYPE", LUT correction is applied at the end anyway, after extrapolation, and so it should always be.
	// Note that we don't linearize the LUT here independently of "ENABLE_LINEAR_COLOR_GRADING_LUT" because this shader doesn't always run
	// (we don't know when it does, we could cache that information and re-use it later, but it's not worth the trouble).

#if !_RT_SAMPLE1 && !_RT_SAMPLE0 // Optimized branch for the passthrough version of this shader (skip the gamma back and forth conversions)
	float3 col = layer0Sampler.Sample(layer0Sampler_s, IN.baseTC.xy).rgb * LayerBlendAmount.x;
	
	return float4(col, 1);
#else
	float3 col = gamma_sRGB_to_linear(layer0Sampler.Sample(layer0Sampler_s, IN.baseTC.xy).rgb) * LayerBlendAmount.x;
	
	#if _RT_SAMPLE1 || _RT_SAMPLE0
		col += gamma_sRGB_to_linear(layer1Sampler.Sample(layer1Sampler_s, IN.baseTC.xy).rgb) * LayerBlendAmount.y;
	#endif
	
	#if _RT_SAMPLE1
		col += gamma_sRGB_to_linear(layer2Sampler.Sample(layer2Sampler_s, IN.baseTC.xy).rgb) * LayerBlendAmount.z;
	#endif
	
	#if _RT_SAMPLE1 && _RT_SAMPLE0
		col += gamma_sRGB_to_linear(layer3Sampler.Sample(layer3Sampler_s, IN.baseTC.xy).rgb) * LayerBlendAmount.w;
	#endif
 
	return float4(linear_to_sRGB_gamma(col), 1);
#endif
}