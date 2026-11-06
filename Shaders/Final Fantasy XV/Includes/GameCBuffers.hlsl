#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
struct LumaGameSettings
{
   uint UseVanillaGamutRatio; // gates the game's HDR gamut-ratio blend (was "FakeHDR")
   float BloomStrength;
   float Sharpness;
   float VignetteStrength;
   uint EnableDoF;
   uint EnableVBAO; // 1: the ambient occlusion pass uses GTVBAO (visibility bitmask), 0: the game's SAO
   float VBAOThicknessScale; // GTVBAO: the thickness of the depth buffer surfaces grows linearly with the depth, as this fraction of the ray reach in world units (0: constant thickness)
   uint EnableVBAOFoliageMask; // GTVBAO: foliage (stencil material class) is skipped as an occluder, and its own AO is blended toward unoccluded
   float VBAOFoliageBlend; // GTVBAO: how much the AO of the foliage pixels is blended toward 1 (unoccluded)
};

struct LumaGameData
{
   int IsUpscaling;
   float2 TemporalJitterUV;
};
}

#endif // LUMA_GAME_CB_STRUCTS
