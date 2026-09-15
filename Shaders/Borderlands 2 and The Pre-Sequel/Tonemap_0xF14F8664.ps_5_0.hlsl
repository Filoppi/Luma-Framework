// Borderlands 2 tonemap / HDR injection point (dgVoodoo 2.81.3 -> ps_4_0, hash 0xF14F8664).
// Older dgVoodoo builds emit ps_4_0 -> a different CSO hash for the same DX9 shader. Identical I/O and slot
// map to the 2.87.3 variant (0xD00AA2A7); blind-include its body, as the UIEmissive and Video pairs do.
#include "Tonemap_0xD00AA2A7.ps_5_0.hlsl"
