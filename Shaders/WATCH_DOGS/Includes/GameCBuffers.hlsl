#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
	{
        float2 InvOutputRes;
		float Sharpening;
    	// Camera jitters in NCD space (based on the rendering resolution, but relative to the output resolution full range UVs, so apply these before "CV_HPosScale.xy")
    	// (not in projection matrix space, so they don't need to be divided by the rendering resolution). You might need to multiply this by 0.5 and invert the vertical axis before using it, if it's targeting UV space.
		// These are not strictly settings but... they are set once per frame so they are better here.
		float2 CameraJitters;
		//uint LumaSR; // TODO: add?
	};
	
	struct LumaGameData
	{
    	column_major float4x4 ViewProjectionMatrix;
    	column_major float4x4 PrevViewProjectionMatrix;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
