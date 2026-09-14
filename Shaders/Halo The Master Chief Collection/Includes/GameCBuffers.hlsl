#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
   struct LumaGameSettings
   {
      int SubGame;
      int UIBlurDown0Count; //when > 0, front end pause is most likely active

      float Bloom;
      float FilmGrain;
      float WhiteClip;
      float AmbientOcclusion;
      float MotionBlur;
   };
   
   struct LumaGameData
   {
      float Dummy;
   };
}

#endif // LUMA_GAME_CB_STRUCTS
