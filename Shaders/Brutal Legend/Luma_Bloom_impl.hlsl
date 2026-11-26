// Brutal Legend - Luma HDR bloom (core DrawBloom). Its input is already linear, half-res and thresholded by
// Luma_BL_BloomPrefilter.hlsl, so the threshold inside Bloom.hlsl is the identity here (no double threshold).

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaSettings) before the shared includes
// clang-format on

#define LUMA_BLOOM_THRESHOLD_FUNCTION(color) (color)
#include "../Includes/Bloom.hlsl"
