#include "Includes/Common.hlsl"
#include "Includes/Sample.hlsl"
#include "Includes/Tonemapper.hlsl"

SamplerState linearSampler_s : register(s0);
SamplerState nearestSampler_s : register(s1);
Texture2D<float4> colorTexture : register(t0);
Texture2D<float4> bloomTexture : register(t1);
Texture2D<float4> effectTexture : register(t2);

void main(
	float4 v0 : SV_POSITION0,
	float2 v1 : TEXCOORD0,
	out float4 o0 : SV_Target0)
{
	float4 bloom = sample_bicubic(bloomTexture, linearSampler_s, v1.xy);
	float4 effect = effectTexture.Sample(nearestSampler_s, v1.xy).xyzw;
	float4 color = colorTexture.Sample(nearestSampler_s, v1.xy).xyzw;
	#if ENABLE_HDR_BOOST
	if(LumaSettings.DisplayMode == 1)
	{
		float normalizationPoint = 0.01; // Found empirically
		float fakeHDRIntensity = 0.08;
		effect.xyz = FakeHDR(effect.xyz, normalizationPoint, fakeHDRIntensity);
	}
	#endif
	o0.xyz = bloom.xyz * bloom.w + color.xyz * effect.w + effect.xyz;
	o0.w = color.w;

	o0.rgb = ApplyUserTonemap(o0.rgb);
	return;
}