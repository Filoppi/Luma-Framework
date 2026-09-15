// The Witcher 2 tonemap, adaptive EXPOSURE permutation (dgVoodoo -> ps_5_0, hash 0x91348C0F; DX9 origin
// 0xC5ADBC35). Exposure + post-scale only, alpha passthrough, adaptation at t1/s1.
// Thin wrapper over the shared impl. See Luma_TW2_Tonemap.hlsl.
#define TM_BRIGHT_PASS 0
#include "Luma_TW2_Tonemap.hlsl"
