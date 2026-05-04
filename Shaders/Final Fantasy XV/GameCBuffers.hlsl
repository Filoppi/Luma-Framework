#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
struct LumaGameSettings
{
   float tonemap_type;
};
// struct MotMat
// {
//     float4x4 g_motionMatrix;           // Offset:    0 Size:    64
//     float4 g_jitterOfs;                // Offset:   64 Size:    16
// };

struct LumaGameData
{
   // MotMat motMat;
   int isUpscaling;
};
}

#endif // LUMA_GAME_CB_STRUCTS
