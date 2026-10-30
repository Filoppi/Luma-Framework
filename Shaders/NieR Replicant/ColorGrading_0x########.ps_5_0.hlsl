#include "Includes/Common.hlsl"

#if _02D70327 || _151DE180 || _444114F8 || _45E46B73 || _6140A5AC || _906B6848 || _9716339C || _EF8E0BE4
#define COLOR_CORRECTION
#endif

#if _1620C24C || _444114F8 || _6140A5AC || _690C605C || _906B6848 || _9716339C || _CE45E40C || _E45D3F6F
#define RADIAL_BLUR
#endif

#if _02D70327 || _444114F8 || _45E46B73 || _906B6848 || _CE45E40C || _D207899C || _E45D3F6F || _E59C431D
#define CHROMATIC_ABERRATION
#endif

#if _02D70327 || _1620C24C || _444114F8 || _6140A5AC || _70A11A47 || _CE45E40C || _E59C431D || _EF8E0BE4
#define GRADATION
#endif

#define NEXT_REGISTER 0
#define DECLARE_REGISTER(name) b##name

#ifdef COLOR_CORRECTION
cbuffer CbColorCorrection : register(DECLARE_REGISTER(NEXT_REGISTER))
{
	float4x4 color_matrix : packoffset(c0);
}
#undef NEXT_REGISTER
#define NEXT_REGISTER 1
#endif

#ifdef RADIAL_BLUR
cbuffer CbRadialBlur : register(DECLARE_REGISTER(NEXT_REGISTER))
{
	float4 center_uv_pos_power : packoffset(c0);
	float4 uv_offset_and_scale : packoffset(c1);
	float aspect_ratio : packoffset(c2);
	float enable_radial : packoffset(c2.y);
	float distance_degree_ : packoffset(c2.z);
	float blur_fade_alpha : packoffset(c2.w);
}
#if NEXT_REGISTER == 0
#undef NEXT_REGISTER
#define NEXT_REGISTER 1
#else
#undef NEXT_REGISTER
#define NEXT_REGISTER 2
#endif
#endif

#ifdef CHROMATIC_ABERRATION
cbuffer CbChromaticAberration : register(DECLARE_REGISTER(NEXT_REGISTER))
{
	float ca_scale : packoffset(c0);
	float ca_power : packoffset(c0.y);
	float ca_aspect : packoffset(c0.z);
}
#if NEXT_REGISTER == 0
#undef NEXT_REGISTER
#define NEXT_REGISTER 1
#elif NEXT_REGISTER == 1
#undef NEXT_REGISTER
#define NEXT_REGISTER 2
#else
#undef NEXT_REGISTER
#define NEXT_REGISTER 3
#endif
#endif

#ifdef GRADATION
cbuffer CbGradationMap : register(DECLARE_REGISTER(NEXT_REGISTER))
{
	float max_luma : packoffset(c0);
	float min_luma : packoffset(c0.y);
	float blend_factor : packoffset(c0.z);
	float control_gamma : packoffset(c0.w);
}
#if NEXT_REGISTER == 0
#undef NEXT_REGISTER
#define NEXT_REGISTER 1
#elif NEXT_REGISTER == 1
#undef NEXT_REGISTER
#define NEXT_REGISTER 2
#elif NEXT_REGISTER == 2
#undef NEXT_REGISTER
#define NEXT_REGISTER 3
#else
#undef NEXT_REGISTER
#define NEXT_REGISTER 4
#endif
#endif

cbuffer CbColorGrading : register(DECLARE_REGISTER(NEXT_REGISTER))
{
	float3 lut_size : packoffset(c0);
	float max_range : packoffset(c0.w);
}

SamplerState tex_Color_s_s : register(s0);
SamplerState GradationMap_s_s : register(s1);
SamplerState texGradingLUT_s_s : register(s2);
Texture2D<float4> tex_Color : register(t0);
Texture2D<float4> GradationMap : register(t1);
Texture3D<float4> texGradingLUT : register(t2);

float UpgradeToneMapRatio(float ap1_color_hdr, float ap1_color_sdr, float ap1_post_process_color)
{
	if (ap1_color_hdr < ap1_color_sdr)
	{
		// If substracting (user contrast or paperwhite) scale down instead
		// Should only apply on mismatched HDR
		return ap1_color_hdr / ap1_color_sdr;
	}
	else
	{
		float ap1_delta = ap1_color_hdr - ap1_color_sdr;
		ap1_delta = max(0, ap1_delta);	// Cleans up NaN
		const float ap1_new = ap1_post_process_color + ap1_delta;

		const bool ap1_valid = (ap1_post_process_color > 0);	// Cleans up NaN and ignore black
		return ap1_valid ? (ap1_new / ap1_post_process_color) : 0;
	}
}

float3 UpgradeToneMapByLuminance(float3 color_hdr, float3 color_sdr, float3 post_process_color, float post_process_strength)
{
	float3 bt2020_hdr = max(0, BT709_To_BT2020(color_hdr));
	float3 bt2020_sdr = max(0, BT709_To_BT2020(color_sdr));
	float3 bt2020_post_process = max(0, BT709_To_BT2020(post_process_color));

	float ratio = UpgradeToneMapRatio(
			GetLuminance(bt2020_hdr, CS_BT2020),
			GetLuminance(bt2020_sdr, CS_BT2020),
			GetLuminance(bt2020_post_process, CS_BT2020));

	float3 color_scaled = max(0, bt2020_post_process * ratio);
	color_scaled = BT2020_To_BT709(color_scaled);
	color_scaled = RestoreHueAndChrominance(color_scaled, post_process_color, 1.f, 0.f);
	return lerp(color_hdr, color_scaled, post_process_strength);
}

void main(
	float4 v0 : SV_POSITION0,
	linear noperspective float2 uv : TEXCOORD0,
	out float4 o0 : SV_TARGET0)
{
	float3 color = tex_Color.SampleLevel(tex_Color_s_s, uv, 0).xyz;

#ifdef CHROMATIC_ABERRATION
	float2 aberrationOffset = float2((uv.x - 0.5f) * ca_aspect, (uv.y - 0.5f));
	aberrationOffset = normalize(aberrationOffset) * pow(length(aberrationOffset), ca_power);
	
	float3 caSample1 = tex_Color.SampleLevel(tex_Color_s_s, uv - aberrationOffset * ca_scale, 0).xyz;
	float3 caSample2 = tex_Color.SampleLevel(tex_Color_s_s, uv - aberrationOffset * ca_scale * 2.0f, 0).xyz;
	float3 caSample3 = tex_Color.SampleLevel(tex_Color_s_s, uv - aberrationOffset * ca_scale * 3.0f, 0).xyz;
	float3 caSample4 = tex_Color.SampleLevel(tex_Color_s_s, uv - aberrationOffset * ca_scale * 4.0f, 0).xyz;
	float3 caSample5 = tex_Color.SampleLevel(tex_Color_s_s, uv - aberrationOffset * ca_scale * 5.0f, 0).xyz;
	float3 caSample6 = tex_Color.SampleLevel(tex_Color_s_s, uv - aberrationOffset * ca_scale * 6.0f, 0).xyz;
	
	color = color * float3(0.162499994f, 0.0f, 0.0f) +
				caSample1 *  float3(0.5625f, 0.0f, 0.0f) +
				caSample2 *  float3(0.25f, 0.25f, 0.0f) +
				caSample3 *  float3(0.0f, 0.5f, 0.0f) +
				caSample4 *  float3(0.0f, 0.25f, 0.25f) +
				caSample5 *  float3(0.0f, 0.0f, 0.5f) +
				caSample6 *  float3(0.0250000004f, 0.0f, 0.25f);
#endif				

#ifdef RADIAL_BLUR	
	float2 radialOrigin = uv_offset_and_scale.xy + uv * uv_offset_and_scale.zw;
	float2 radialOffset = uv_offset_and_scale.xy + center_uv_pos_power.xy * uv_offset_and_scale.zw - enable_radial * radialOrigin;
	radialOffset = radialOffset / uv_offset_and_scale.zw;
	radialOffset = normalize(radialOffset) * pow(length(radialOffset), distance_degree_) * center_uv_pos_power.w * 0.125f;
	float3 radialColor = 0.0f;
	float sampleWeight = 0.00999999978f;
	for(int i = 7; i > 0; --i)
	{
		radialColor += sampleWeight * tex_Color.SampleLevel(tex_Color_s_s, radialOrigin + i * radialOffset, 0).xyz;
		sampleWeight += 0.0299999993;
	}
	color = color + blur_fade_alpha * ((0.219999999f * color + radialColor) * 1.08695662f - color);
#endif
	
#ifdef COLOR_CORRECTION
	float3 convertedColor;
	convertedColor.x = dot(float4(color, 0.0f), color_matrix._m00_m10_m20_m30);
	convertedColor.y = dot(float4(color, 0.0f), color_matrix._m01_m11_m21_m31);
	convertedColor.z = dot(float4(color, 0.0f), color_matrix._m02_m12_m22_m32);
	color = max(0.0f, convertedColor);
#endif

	float3 ungradedColor = color;
	
#ifdef GRADATION
	float luminance = dot(color, float3(0.298999995f, 0.587000012f, 0.114f));
	float adjustedLuminance = pow(abs(clamp(luminance, min_luma, max_luma)), control_gamma);
	float2 gradationSamplePos;
	gradationSamplePos.x = saturate(adjustedLuminance / max_luma);
	gradationSamplePos.y = 0.5f;
	float3 gradiationColor = GradationMap.SampleLevel(GradationMap_s_s, gradationSamplePos, 0).xyz;
	color = lerp(color, gradiationColor, blend_factor);
#endif

	float3 normalizedColor = color / max_range;
	float3 pqPower = pow(abs(normalizedColor), 0.159301758f);
	float3 pqNumerator = pqPower * 18.8515625f + 0.8359375f;
    float3 pqDenominator = pqPower * 18.6875f + 1.0f;
	float3 pqRatio = pqNumerator / pqDenominator;
	float3 pqColor = pow(pqRatio, 78.84375);

	float3 lutSamplePos = 1.0f / (lut_size.xyz + lut_size.xyz) + pqColor * (lut_size.xyz - 1.0f) / lut_size.xyz;
	float3 gradedColor = texGradingLUT.SampleLevel(texGradingLUT_s_s, lutSamplePos, 0).xyz;
	
	pqPower = pow(abs(gradedColor), 0.0126833133f);
	pqNumerator = max(0.0f, -0.8359375f + pqPower);
	pqDenominator = 18.8515625f - pqPower * 18.6875f;
	o0.xyz = pow(abs(pqNumerator / pqDenominator), 6.27739477f) * max_range;
	o0.w = 1;
	
	if (LumaSettings.DisplayMode == 1)
	{
		float3 referenceTonemap = Reinhard::ReinhardScalable(ungradedColor, 1.0f, 0.0f, 0.18f, 0.18f);
		o0.xyz = UpgradeToneMapByLuminance(ungradedColor, referenceTonemap, o0.xyz, 1.0f);
	}
	
	return;
}