// Hitman Absolution - Luma HDR bloom pyramid (core DrawBloom). Input = the tonemap pass's HDR scene (PS t1 of 0x46C50FC2),
// Karis-averaged, linear, BEFORE g_fTonemapScale exposure (so SDR white is above 1 in this input).

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaSettings) before the shared includes
// clang-format on

#define LUMA_BLOOM_THRESHOLD 2.5 // SDR white before exposure
#define LUMA_BLOOM_SOFT_KNEE 0.5

#include "../Includes/Bloom.hlsl"
