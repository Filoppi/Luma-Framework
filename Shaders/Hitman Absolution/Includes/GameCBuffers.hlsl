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
		float SunBrightness;
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
		float SunShaftHDRBoost;
		float VideoAutoHDR;
		float VanillaHueRestore;
	};
	
	// Define the game specific cbuffer (instance/pass) data here
	struct LumaGameData
	{
		float Dummy; // hlsl doesn't support empty structs
	};
}

#endif // LUMA_GAME_CB_STRUCTS
