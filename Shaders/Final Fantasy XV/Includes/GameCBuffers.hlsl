#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
struct LumaGameSettings
{
   uint UseSDROverHDR;        // 1: derive and use the game's SDR curve tuning even when the game runs in HDR mode (default)
   uint UseVanillaGamutRatio; // gates the game's HDR gamut-ratio blend (was "FakeHDR")
   float BloomStrength;
   float Sharpness;
};

struct LumaGameData
{
   int IsUpscaling;
};
}

#endif // LUMA_GAME_CB_STRUCTS
