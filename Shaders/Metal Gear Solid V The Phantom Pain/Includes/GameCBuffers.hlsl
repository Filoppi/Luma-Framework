#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
	struct LumaGameSettings
	{
	};

	// The velocity pass's camera of the frame, without our jitter (see "Luma_MGSV_DecodeMotionVectors.hlsl")
	struct LumaGameData
	{
		float4 ViewToPrevClip[4];     // View to previous clip rows: the velocity pass's previous view projection ("m_view") times its view to world ("m_shadowProjection2")
		float4 ProjectionParam;       // "m_projectionParam": view position reconstruction terms
		float2 CameraCenterOffset;    // NDC offset of the frame's projection: the game's own one plus our jitter
		float2 RenderSize;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
