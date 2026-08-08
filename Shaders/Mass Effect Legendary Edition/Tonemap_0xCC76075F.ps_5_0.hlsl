// ME2 stage-1 tonemap: analytic scene grade with a blue-tinted white point; no LUT or vignette.
#define TM_ANALYTIC_WHITEPOINT float3(1.01036298, 1.00000572, 1.16309249)
#define TM_VIGNETTE_TYPE       0
#include "Tonemap_ME_Analytic_Body.hlsl"
