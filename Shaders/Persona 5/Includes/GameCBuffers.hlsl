#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
	{
		float2 RenderRes;
		float2 InvRenderRes;
		float2 OutputRes;
		float2 InvOutputRes;
		float RenderScale;
		float InvRenderScale;
		float ShadowRes;
		float InvShadowRes;
		float2 JitterOffset;
    };

    struct LumaGameData
    {
        float Dummy;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
