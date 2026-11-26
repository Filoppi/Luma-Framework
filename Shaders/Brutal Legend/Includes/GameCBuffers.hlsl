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
		float HighlightsStrength; // 0 = vanilla clip, 1 = full overbright range
		float VanillaHueRestore; // 0-1
		float BloomIntensity; // 1 = vanilla
		float GamutExpansion; // 0-1
		float XeGTAOActive; // set by c++ each frame: 1 when the XeGTAO texture is bound
		float XeGTAOIntensity;
		float XeGTAORadius;
		float ShadowSoftness; // 1 = vanilla penumbra width
		float VideoAutoHDREnable; // 0/1
		float VideoAutoHDR; // 0-1 strength
		float VideoDrawActive; // set by c++ per draw, never saved
		float SkyBrightness; // sky highlight headroom above SDR white, in paper white units (0 = vanilla clip)
		float SkyDepthActive; // set by c++ per tonemap draw: 1 when the scene depth is bound at t8
		float BlackFloor; // 0 = vanilla, 1 = strongest toe
		float XeGTAODarkness; // 1 = neutral, 0 = no AO, 2 = twice as dark
		float Tonemapper; // 0 = DICE, 1 = PsychoV35
		float PsychoVShoulder; // PsychoV35 compression, 0 = auto
		float PsychoVGamutFit; // 0 = exact, 1 = smooth
		float PsychoVHueLock; // 0-1 MacLeod-Boynton hue-only restore after PsychoV35
		float FogDataActive; // set by c++ per tonemap draw: 1 when the game's fog constants are bound at b6 (VS cb) and b7 (PS cb)
		float FogStart; // added to the game's fog start distance, world units (0 = vanilla)
		float FogDensity; // multiplier on the game's fog density (1 = vanilla)
		float FogColorSaturation; // 1 = vanilla fog colour, 0 = grey
		float FogColorBrightness; // 1 = vanilla
		float FogKeepColor; // 0 = vanilla, 1 = fogged surfaces keep their own chroma
		float ShadowPCSS; // 0/1: percentage-closer soft shadows, the blocker search sets the penumbra width
		float ShadowPCSSLightSize; // scales the PCSS blocker search and penumbra, 1 = default
		float ShadowPCSSPenumbraScale; // multiplies the PCSS penumbra width per depth gap, 1 = default
		float ShadowPCSSMaxPenumbra; // PCSS max penumbra in texels per 1024 px of atlas width, 8 = default
		float ShadowPCSSMinPenumbra; // PCSS min penumbra in texels (contact hardness), 1 = default
		float ShadowCascadeBlend; // 0/1: blend into the next cascade over the last 10% of each cascade's depth range
		float ShadowReceiverBias; // 0/1: receiver-plane depth bias per atlas tap
		float ShadowAtlasMSBound; // 0/1: set by c++ per draw when an MS8 copy of the atlas is bound at t9, taps use Load(sample 0)
			float LumaBloomEnable; // 0/1: Luma's bloom replaces the game's bloom in the tonemap (0 = game bloom)
			float LumaBloomIntensity; // 1 = default, linear, added to the scene above SDR white
			float LumaBloomThreshold; // linear scene brightness where bloom starts, 0 = no threshold
			float LumaBloomKnee; // soft knee width, fraction of the threshold
			float LumaBloomRadius; // scales the blur sigmas, 1 = default
			float FogHighlightLimit; // 0/1: the part of a fogged pixel above SDR white fades with its fog amount (thick fog matches vanilla's clip)
			float PsychoVHighlightSaturation; // 0-1: restores the colour purity PsychoV removes from very bright highlights (keeps bright reds red instead of pink). 0 = PsychoV default
			float PsychoVHueClip; // 0-1: emulates the hue shift of SDR per-channel clipping (RenoDX hue shift "clip"): bright reds/oranges turn yellow, blues turn cyan. 0 = off
				float FilmGrainStrength; // 0-1: RenoDX film grain on the HDR tonemap output. 0 = off
	};
	
	// Define the game specific cbuffer (instance/pass) data here
	struct LumaGameData
	{
		float Dummy; // hlsl doesn't support empty structs
	};
}

#endif // LUMA_GAME_CB_STRUCTS
