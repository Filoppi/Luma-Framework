#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif


namespace CB
{
   struct LumaGameSettings
   {      
      float FMVPaperWhite;
      // float RenderResolutionScale;
      float ExposurePost;
      float ExposurePre;
      float ExpectedMax;

      float Bloom;
      float AllowVanillaColorGrade;
      float AllowFullscreenBlur;

      // float PCCHue;
      // float PCCChrom;
      float PCCPeak;
   };
   
   struct LumaGameData
   {
      float Dummy;
   };
}



#endif // LUMA_GAME_CB_STRUCTS
