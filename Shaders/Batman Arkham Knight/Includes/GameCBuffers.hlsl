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
   // Rows (row vector convention) of the matrix taking the current jittered NDC + device depth (camera velocity pass) to the previous frame's jittered clip space.
   // The previous view projection is relative to the previous view origin, so the camera translation is baked in here.
   float4 ReprojectionMatrix[4];
   // xy: render resolution / output resolution, for the post processes that run at output resolution after super resolution upscaling,
   // but still read the render resolution depth and velocity (1 when not upscaling). zw: unused.
   float4 PostRenderScale;
   // xy: size, zw: origin of the view rect rendered by the camera velocity pass (render resolution).
   // Its own constants have the full buffer size instead, which only matches when not upscaling.
   float4 CameraVelocityViewRect;
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
