#include "Includes/Common.hlsl"
#include "../Includes/Tonemap.hlsl"

cbuffer PER_BATCH : register(b0)
{
  float4 HDRColorBalance : packoffset(c0); // Unused here
  float4 Param1 : packoffset(c1); // Could be "SunShafts_SunCol" or "HDREyeAdaptation"
  float4 Param2 : packoffset(c2); // Could be "HDREyeAdaptation" or "HDRFilmCurve" (the latter one is unused so it's garbage)
  float4 Param3 : packoffset(c3); // Could be "HDRFilmCurve" or "HDRBloomColor"
  float4 Param4 : packoffset(c4); // Could be "HDRBloomColor" or "ArkDistanceSat" or none
}

Texture2D<float2> adaptedLumTex : register(t1);

// Custom Luma shader to draw the final exposure value from textures+cbuffers (copied from HDRPostProcesss HDRFinalScene shader)
float main() : SV_Target0
{
	// The HDR tonemap shader (HDRPostProcess HDRFinalScene) has branches for sun shafts, which shift the "HDREyeAdaptation" cbuffer value.
	bool hasSunshafts = LumaData.CustomData1;
	float4 HDREyeAdaptation = hasSunshafts ? Param2 : Param1;
	
	float vAdaptedLum = adaptedLumTex.Load(0).x; // This is a 1x1 texture, so any UV will return the same value

    // Legacy exposure mode (always used in Prey) (HDREyeAdaptation.x isn't used and is always 0.18)
	const float fSceneKey = 1.03 - 2.0 / (2.0 + log2(vAdaptedLum.x + 1.0));
	float fExposure = fSceneKey / vAdaptedLum.x;
#if ENABLE_EXPOSURE_CLAMPING
	fExposure = clamp(fExposure, HDREyeAdaptation.y /*MinExposure*/, HDREyeAdaptation.z /*MaxExposure*/);
#endif
#if SR_RELATIVE_PRE_EXPOSURE >= 1
	float fExposureMidPoint = lerp(HDREyeAdaptation.y, HDREyeAdaptation.z, 0.5);
	// What's important for DLSS is the relative exposure value (how much it changes from the "baseline", most commonly used value in a level, or the average anyway),
	// so we divide it by the mid point, hoping it'd match that (it probably doesn't, but it's a good start).
	return fExposure / fExposureMidPoint;
#else
	return fExposure;
#endif
}