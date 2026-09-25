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
		float custom_lut_strength;
		float custom_bloom;
		float custom_vignette;
		float custom_film_grain_strength;
		float custom_sharpness_strength;
		float custom_hdr_videos;
        float custom_random;
        float can_sharpen;

        // Color Grading
        float exposure;
        float highlights;
        float shadows;
        float contrast;
        float flare;
        float saturation;
        float highlight_saturation;
        float hue_correction_strength;
        float hue_correction_type; // 0 = input, 1 = output
        float blowout;
    };

    struct LumaGameData
    {
        float4 RenderResolution;
        float4 OutputResolution;
		uint4 ViewportRect;
		float2 ResolutionScale; //Scale, InvScale
		uint DrewUpscaling;
	};
}

#endif // LUMA_GAME_CB_STRUCTS
