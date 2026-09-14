#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
struct LumaGameSettings
{
   float BloomStrength;    // 1 = vanilla, 0 = disabled.
   float LensDirtStrength; // 1 = vanilla, 0 = disabled.
   float LensFlareStrength;// 1 = vanilla, 0 = disabled.
   float VignetteStrength; // 1 = vanilla, 0 = disabled.
};

struct LumaGameData
{
   float Dummy;
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS