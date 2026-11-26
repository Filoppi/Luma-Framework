// Hitman Absolution GI gather pixel shader (ps_4_1), three permutations, all with edge pass (SV_SampleIndex):
// 0x626315E7: 4 samples (MSAA)
// 0xA3EDF879: 2 samples (MSAA)
// 0xCED3C761: 8 samples (MSAA)
// The shared body lives in Includes/GIGather.hlsl.

#if _626315E7
#define GI_SINGLE 0
#define GI_EDGE 1
#define GI_SAMPLES 4
#elif _A3EDF879
#define GI_SINGLE 0
#define GI_EDGE 1
#define GI_SAMPLES 2
#elif _CED3C761
#define GI_SINGLE 0
#define GI_EDGE 1
#define GI_SAMPLES 8
#endif

#include "Includes/GIGather.hlsl"
