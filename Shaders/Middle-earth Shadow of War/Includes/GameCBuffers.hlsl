#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
   struct LumaGameSettings
   {
      float WhiteClip;
      float BlowoutCorrection;
      float GodRays;
      float Bloom;
      float RCAS;
   };
   
   struct LumaGameData
   {
      float Dummy;
   };
}

#endif // LUMA_GAME_CB_STRUCTS
