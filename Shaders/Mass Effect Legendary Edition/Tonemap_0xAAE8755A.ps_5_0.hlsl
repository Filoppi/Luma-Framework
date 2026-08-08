// ME1 stage-1 tonemap: analytic scene grade, no LUT, white point, or vignette. Verified against the dumped CSO:
// after the native gamma curve it clamps to 1 and goes straight to the metering dot product and o0, with none of
// the 0.832050323/3.25/pow-100 radial vignette constants the ME1 LUT permutations carry. ME2's 0xCC76075F matches.
#define TM_VIGNETTE_TYPE 0
#include "Tonemap_ME_Analytic_Body.hlsl"
