#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
	// Define the game specific cbuffer settings here. They don't need 4 bytes alignment.
	struct LumaGameSettings
	{
		float HighlightSaturation;
		float GamutExpansionHueLock;
		float BloomStrength;
		uint LumaBloom;
		float LumaBloomStrength;
		float VanillaGlareStrength;
		float FilmGrainStrength;
		float SkyHDRBoost;
		uint SkyDebanding;
		float Exposure;
		float Contrast;
		float Saturation;
		float Highlights;
		float Shadows;
		float BlackFloorRemoval;
		float HighlightRolloff;
		float DistanceHaze;
		float HighlightHuePreservation;
		float TopGradientStrength;
		float VignetteStrength;
		float ShadowLuminanceGrading;
		float GammaCorrectionByLuminance;
		float GamutExpansion;
		uint DisplayMapping;
		float ShoulderStart;
		float HighlightPurity;
		float EmissiveHDRBoost;
		float SunShaftHDRBoost; // WIP: only read by the .disabled SunGlare/SunShaftAdd shaders; keep the slot until they are re-enabled or deleted
		float VideoAutoHDR;
		float VanillaHueRestore;
		uint AutoExposureMode;
		float AutoExposureSpeed;
		float AutoExposureRange;
		float HighlightCompression;
		float AutoExposureKey;
		uint ModernLensFlares;
		uint BokehDOF;
		float GIStrength;
		float GIOcclusion;
		uint BokehDOFFiltering;
		float BokehDOFRotation;
		float AODarkness;
		uint BokehDOFTapLimit;
		uint TonemapVersion; // 0 = legacy extended-LUT grade, 1 = neutral-compressed LUT grade upgraded to HDR (UpgradeToneMap)
		float HazeModel; // 0 = sqrt-space curve blend, 1 = linear HDR experiment
		uint VideoAutoHDREnable; // 0/1. 1 = Video AutoHDR expansion on the video pass; 0 = flat vanilla-range video.
	};
	
	// Define the game specific cbuffer (instance/pass) data here
	struct LumaGameData
	{
		float Dummy; // hlsl doesn't support empty structs
	};
}

#endif // LUMA_GAME_CB_STRUCTS
