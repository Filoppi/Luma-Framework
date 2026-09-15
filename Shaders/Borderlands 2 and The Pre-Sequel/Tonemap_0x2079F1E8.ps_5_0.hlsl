// Borderlands: The Pre-Sequel - tonemap / HDR injection point (dgVoodoo 2.81.3 -> ps_4_0, hash 0x2079F1E8).
// Older dgVoodoo builds emit ps_4_0 -> a different CSO hash for the same DX9 shader. Identical I/O and slot
// map (LightShaft@t1, +1 shift, Luma bloom -> t8) to the 2.87.3 variant (0xFCFE623E); blind-include its body,
// which carries the TPS slot macros with it.
#include "Tonemap_0xFCFE623E.ps_5_0.hlsl"
