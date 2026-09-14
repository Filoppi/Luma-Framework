#include "Includes/Common.hlsl"
#include "Includes/Tonemapper.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

cbuffer GFD_PSCONST_SYSTEM : register(b0) {
	float2 resolution : packoffset(c0);
	float2 resolutionRev : packoffset(c0.z);
	float4x4 mtxView : packoffset(c1);
	float4x4 mtxInvView : packoffset(c5);
	float4x4 mtxProj : packoffset(c9);
	float4x4 mtxInvProj : packoffset(c13);
	float4 invProjParams : packoffset(c17);
}

cbuffer GFD_PSCONST_LUT : register(b11) {
	float weight : packoffset(c0);
}

SamplerState pointClampSampler_s : register(s0);
Texture2D<float4> texture0 : register(t0);
Texture2D<float4> LUTTexture : register(t1);
Texture2D<float4> gbuffer1Texture : register(t2);

// Sometimes runs after output, sometimes never and sometimes before
void main(float4 v0: SV_POSITION0, float2 v1: TEXCOORD0, out float4 o0: SV_Target0) {
	float4 r0, r1, r2;
	uint4 bitmask, uiDest;
	float4 fDest;

	r0.xyzw = texture0.Sample(pointClampSampler_s, v1.xy).xyzw;
	float3 untonemapped = r0.rgb;

	r1.xy = resolution.xy * v1.xy;
	r1.xy = (int2)r1.xy;
	r1.zw = float2(0, 0);
	r1.x = gbuffer1Texture.Load(r1.xyz).w;
	r1.x = 255 * r1.x;
	r1.x = (uint)r1.x;
	r1.x = (int)r1.x & 16;

	// Condition decides if LUT should be applied (background env)
	if (r1.x == 0)
	{
		if (1)
		{
			LUTExtrapolationData extrapolationData = DefaultLUTExtrapolationData();
			extrapolationData.inputColor = untonemapped.rgb;
			extrapolationData.vanillaInputColor = saturate(untonemapped.rgb);

			LUTExtrapolationSettings extrapolationSettings = DefaultLUTExtrapolationSettings();
			extrapolationSettings.enableExtrapolation = true;
			extrapolationSettings.extrapolationQuality = LUT_EXTRAPOLATION_QUALITY;
			extrapolationSettings.lutSize = 32;
			
			extrapolationSettings.inputLinear = true;
			extrapolationSettings.lutInputLinear = false;
			extrapolationSettings.lutOutputLinear = false;
			extrapolationSettings.outputLinear = true;

			extrapolationSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
			extrapolationSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_GAMMA_2_2;
			extrapolationSettings.neutralLUTRestorationAmount = 1.0f - weight;

			float3 hdrLUTOutput = SampleLUTWithExtrapolation(
				LUTTexture, 					       // LUT
				pointClampSampler_s,                    // samplerState
				extrapolationData,
				extrapolationSettings
			);
			r0.rgb = UpgradeToneMap(
				untonemapped, ToneMapMaxCLL(untonemapped), ToneMapMaxCLL(hdrLUTOutput), 1.f);
		}
		else
		{
			// We run original code for vanilla
			// 1/2.2 so linear => gamma space
			/* r1.xyz = log2(abs(r0.xyz));
			r1.xyz = float3(0.454545468, 0.454545468, 0.454545468) * r1.xyz;
			r1.xyz = exp2(r1.xyz); */
			r0.rgb = saturate(r0.rgb);
			r1.rgb = linear_to_gamma(r0.rgb);

			// LUT might take in AP1 2.2 gamma and output linear, cause tonemapper output is AP1 but only for backgrounds
			// LUT doesn't always run so they just leave background in AP1 lol
			r1.xyz = min(float3(1, 1, 1), r1.xyz);
			r1.yzw = r1.xyz * float3(0.96875, 0.96875, 0.96875) + float3(0.015625, 0.015625, 0.015625);
			r1.w = r1.w * 32 + -0.5;
			r2.x = floor(r1.w);
			r1.w = -r2.x + r1.w;
			r1.y = r2.x + r1.y;
			r1.x = 0.03125 * r1.y;
			r2.xyz = LUTTexture.Sample(pointClampSampler_s, r1.xz).xyz;
			r1.xy = float2(0.03125, 0) + r1.xz;
			r1.xyz = LUTTexture.Sample(pointClampSampler_s, r1.xy).xyz;
			r1.xyz = r1.xyz + -r2.xyz;
			r1.xyz = r1.www * r1.xyz + r2.xyz;

			// gamma space => linear
			/* r1.xyz = log2(abs(r1.xyz));
			r1.xyz = float3(2.20000005, 2.20000005, 2.20000005) * r1.xyz;
			r1.xyz = exp2(r1.xyz); */
			r1.rgb = gamma_to_linear(r1.rgb);

			// (t * (b-a)) + a = lerp(a, b, t)
			// float3(1.05) is extra
			r1.xyz = r1.xyz * float3(1.04999995, 1.04999995, 1.04999995) + -r0.xyz;
			r0.xyz = weight * r1.xyz + r0.xyz;
		}
	}

	o0.xyzw = r0.xyzw;

	return;
}
