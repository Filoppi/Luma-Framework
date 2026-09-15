// The Witcher 2 tonemap, adaptive BLOOM BRIGHT-PASS permutation (dgVoodoo -> ps_5_0, hash 0x00E31BF9; DX9
// origin 0xF01A691E). Exposure + threshold ramp + saturation + colour (CEnvBloomParameters), adaptation at t2/s2
// (t1 holds an unused depth SRV in this permutation — the vanilla CSO does not declare it either).
// Thin wrapper over the shared impl. See Luma_TW2_Tonemap.hlsl.
#define TM_BRIGHT_PASS 1
#include "Luma_TW2_Tonemap.hlsl"
