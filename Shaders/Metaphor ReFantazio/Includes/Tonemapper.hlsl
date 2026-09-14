#include "Common.hlsl"
#include "../../Includes/Reinhard.hlsl"
#include "ictcp_portable.hlsl"

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

static const float3x3 sRGB_2_AP0 = float3x3(
	0.4397010, 0.3829780, 0.1773350,
	0.0897923, 0.8134230, 0.0967616,
	0.0175440, 0.1115440, 0.8707040);
	
float ExponentialRollOff(float input, float rolloff_start = 0.20f, float output_max = 1.0f)
{
	float rolloff_size = output_max - rolloff_start;
	float overage = -max((float)0, input - rolloff_start);							 
	float rolloff_value = (float)1.0f - exp(overage / rolloff_size);
	float new_overage = mad(rolloff_size, rolloff_value, overage);
	return input + new_overage;
}

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

/// Applies Exponential Roll-Off tonemapping using the maximum channel.
/// Used to fit the color into a 0–output_max range for SDR LUT compatibility.
float3 ToneMapMaxCLL(float3 color, float rolloff_start = 0.375f, float output_max = 1.f)
{
	// color = min(color, 100.f);
	float peak = max(color.r, max(color.g, color.b));
	peak = min(peak, 100.f);
	float log_peak = log2(peak);

	// Apply exponential shoulder in log space
	float log_mapped = ExponentialRollOff(log_peak, log2(rolloff_start), log2(output_max));
	float scale = exp2(log_mapped - log_peak);	// How much to compress all channels

	return min(output_max, color * scale);
}

/// Vanilla sdr is in AP1
/// untonemapped is using sRGB primaries (or they used the wrong matrix which is likely)
float3 UpgradeTonemap(float3 untonemapped, float3 vanillaSDR)
{
	float3 outputColor = vanillaSDR;

	if (LumaSettings.DisplayMode == 1)
	{
		const float ACES_MID_GRAY = 0.10f;
		const float ACES_MIN = 0.0001f;
		float aces_min = ACES_MIN / LumaSettings.GamePaperWhiteNits;
		float aces_max = (LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits);
		aces_max = gamma_sRGB_to_linear1(linear_to_gamma1(aces_max));
		aces_min = gamma_sRGB_to_linear1(linear_to_gamma1(aces_min));

		untonemapped = ACES::RRTAndODT(untonemapped, aces_min * 48.f, aces_max * 48.f) / 48.f;
		// untonemapped = renodx::color::bt709::from::AP1(untonemapped);
		vanillaSDR = mul(ACES::AP1_TO_BT709_MAT, vanillaSDR);

		outputColor = UpgradeToneMapByLuminance(untonemapped, ToneMapMaxCLL(untonemapped), vanillaSDR, 1.f);

		// Mimic them and return AP1?
		outputColor = mul(ACES::BT709_TO_AP1_MAT, outputColor);
	}

	return outputColor;
}

float3 ApplyFakeHDR(float3 color, uint mask)
{
#if ENABLE_HDR_BOOST
	if(LumaSettings.DisplayMode == 1)
	{
		if(mask & 128) //sky
		{
			float normalizationPoint = 0.2; // Found empirically
			float fakeHDRIntensity = 0.3;
			color.xyz = FakeHDR(color.xyz, normalizationPoint, fakeHDRIntensity);
		}
		else if((mask & 8) == 0) //background
		{
			float normalizationPoint = 0.12; // Found empirically
			float fakeHDRIntensity = 0.08;
			color.xyz = FakeHDR(color.xyz, normalizationPoint, fakeHDRIntensity);
		}
		else //character and objects
		{
			float normalizationPoint = 0.1; // Found empirically
			float fakeHDRIntensity = 0.08;
			color.xyz = FakeHDR(color.xyz, normalizationPoint, fakeHDRIntensity);
		}
	}
#endif
	return color;
}

float3 ApplyUserTonemap(float3 untonemapped)
{
	float3 outputColor = untonemapped;

	if (LumaSettings.DisplayMode == 1)
	{
		const float reference_white = 100.0f;
		const float white_clip = 100.0f;
		const float mid_gray_nits = 18.0f;
		const float hue_correction_min = 0.0f;
		const float hue_correction_max = 0.6f;

		float reno_drt_max = (LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits);
		reno_drt_max = gamma_sRGB_to_linear1(linear_to_gamma1(reno_drt_max));
		float nitsPeak = reno_drt_max * 100.0f;
		float peak = (nitsPeak / reference_white);

		float y = GetLuminance(untonemapped);
		float y_new = Reinhard::ReinhardScalableExtended(
			y,
			max(white_clip, peak),
			peak,
			0.0f,
			0.18f,
			mid_gray_nits / 100.0f);

		float scale = (y > 0 ? (y_new / y) : 1.0f);
		outputColor = untonemapped * scale;
		outputColor = min(outputColor, peak);

		float max_channel = max(untonemapped.x, max(untonemapped.y, untonemapped.z));
		
		float3 clamped = saturate(untonemapped);
		float t = smoothstep(1.5f, 3.0f, max_channel);
		
		float3 hue_shifted_color = clamped;
		
		float3 perceptual_new = renodx::color::ictcp::To(outputColor, 0);
		float3 perceptual_old = renodx::color::ictcp::To(hue_shifted_color, 0);

		// Save chrominance to apply back
		float chrominance_pre_adjust = length(perceptual_new.yz);
		
		float hue_correction = lerp(hue_correction_min, hue_correction_max, t);

		perceptual_new.yz = lerp(perceptual_new.yz, perceptual_old.yz, hue_correction);

		float chrominance_post_adjust = length(perceptual_new.yz);

		// Apply back previous chrominance

		perceptual_new.yz *= chrominance_post_adjust != 0.0f ? chrominance_pre_adjust / chrominance_post_adjust : 1.0f;
		outputColor = renodx::color::ictcp::From(perceptual_new, 0);
	}

	return outputColor;
}
