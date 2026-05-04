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
		float GameSetting01;
		uint GameSetting02;
	};
	
	// Define the game specific cbuffer (instance/pass) data here
	struct LumaGameData
	{
        float2 CurrJitters;
        float2 PrevJitters;
		float4x4 CameraSpaceToPreviousProjectedSpace;
        float4x4 PreviousViewRotProjectionMatrix;
        float4 PreviousCameraPosition;  // W is dummy
	};
}

#endif // LUMA_GAME_CB_STRUCTS
