#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
    {
        float HDRHighlightsHuePreservation;
        float HDRHighlightsChrominancePreservation;
        float HDRChrominance;
	};

	struct LumaGameData
    {
        float4x4 ClipToPrevClip;
        float4 RenderResolution;
        float4 ViewportRect;
		float2 JitterOffset;
		int ClipToPrevClipIndex;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
