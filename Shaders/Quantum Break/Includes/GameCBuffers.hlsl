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

   float LUTStrength;
   float LUTScaling;
   float Highlights;
   float Shadows;
   float Contrast;
   float Saturation;
   float HighlightSaturation;
   float Dechroma;
   float Flare;
   float GrainType;
   float GrainStrength;
}; // struct LumaGameSettings

struct LumaGameData
{
   float Dummy;
}; // struct LumaGameData

} // namespace CB

#ifndef __cplusplus
#define CUSTOM_HIGHLIGHTS           LumaSettings.GameSettings.Highlights
#define CUSTOM_SHADOWS              LumaSettings.GameSettings.Shadows
#define CUSTOM_CONTRAST             LumaSettings.GameSettings.Contrast
#define CUSTOM_SATURATION           LumaSettings.GameSettings.Saturation
#define CUSTOM_HIGHLIGHT_SATURATION LumaSettings.GameSettings.HighlightSaturation
#define CUSTOM_DECHROMA             LumaSettings.GameSettings.Dechroma
#define CUSTOM_FLARE                LumaSettings.GameSettings.Flare

#define CUSTOM_LUT_STRENGTH         LumaSettings.GameSettings.LUTStrength
#define CUSTOM_LUT_SCALING          LumaSettings.GameSettings.LUTScaling

#define CUSTOM_GRAIN_TYPE           LumaSettings.GameSettings.GrainType
#define CUSTOM_GRAIN_STRENGTH       LumaSettings.GameSettings.GrainStrength

#define TONE_MAP_TYPE               1.f
#endif

#endif // LUMA_GAME_CB_STRUCTS
