#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
struct LumaGameSettings
{
   float custom_lens_dirt;
   float custom_bloom;
   float custom_film_grain_strength;
   float custom_hdr_videos;
   float custom_random;

   // Color Grading
   float exposure;
   float highlights;
   float shadows;
   float contrast;
   float flare;
   float saturation;
   float highlight_saturation;
   float blowout;
   float highlight_contrast;
   float shadow_contrast;
};

struct LumaGameData
{
   float dummy;
};
}

#endif // LUMA_GAME_CB_STRUCTS
