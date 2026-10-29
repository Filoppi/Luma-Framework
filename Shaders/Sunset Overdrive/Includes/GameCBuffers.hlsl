#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
struct LumaGameSettings
{
   float Dithering;             // 0/1
   float ColorGradingIntensity; // 1 = vanilla, 0 = no 3D LUT
   float BloomIntensity;        // 1 = vanilla
   float FilmGrainIntensity;    // 1 = vanilla
   float Exposure;              // Scene multiplier, 1 = vanilla
   float Contrast;              // HDR, 1 = vanilla
   float Saturation;            // HDR, 1 = vanilla
   float VideoAutoHDREnable;    // 0/1
   float VideoAutoHDRBoost;     // 0 = SDR peak, 1 = full AutoHDR peak
};

struct LumaGameData
{
   float Dummy;
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
