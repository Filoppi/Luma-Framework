// ME1 stage-1 tonemap: LUT grade and motion blur; no film grain.
// Shares the ME1/ME2 body; ME1 has no filmic axis and uses a power-100 vignette with a weaker blue floor.
#define TM_HAS_MOTIONBLUR 1
#define TM_HAS_GRAIN      0
#define TM_HAS_FILMIC     0
#define TM_VIG_POW        100.0
#define TM_VIG_FLOOR      kMELE_ME1VignetteFloor
#include "Tonemap_ME12_LUT_Body.hlsl"
