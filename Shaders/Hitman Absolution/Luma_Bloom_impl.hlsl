// Hitman Absolution - Luma HDR bloom pyramid (core DrawBloom). Input = the tonemap pass's HDR scene (PS t1 of 0x46C50FC2),
// Karis-averaged, linear, BEFORE g_fTonemapScale exposure (~0.45 in daylight, so SDR white is ~2.2 here).

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaSettings) before the shared includes
// clang-format on

#define LUMA_BLOOM_THRESHOLD 2.5 // ~SDR white before exposure (1/0.39-0.47 in daylight); 1.0 bloomed the mid-tones
#define LUMA_BLOOM_SOFT_KNEE 0.5

#include "../Includes/Bloom.hlsl"
