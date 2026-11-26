// Hitman Absolution GI gather pixel shader (ps_4_0), four permutations:
// 0x0EA2360C: 2 samples (MSAA depth/normal/diffuse), no edge pass
// 0x8A0F6018: single sample (non-MSAA textures), no edge pass
// 0xADD46D0F: 4 samples (MSAA), no edge pass
// 0xF074469E: 8 samples (MSAA), no edge pass
// The shared body lives in Includes/GIGather.hlsl.

#if _0EA2360C
#define GI_SINGLE 0
#define GI_EDGE 0
#define GI_SAMPLES 2
#elif _8A0F6018
#define GI_SINGLE 1
#define GI_EDGE 0
#elif _ADD46D0F
#define GI_SINGLE 0
#define GI_EDGE 0
#define GI_SAMPLES 4
#elif _F074469E
#define GI_SINGLE 0
#define GI_EDGE 0
#define GI_SAMPLES 8
#endif

#include "Includes/GIGather.hlsl"
