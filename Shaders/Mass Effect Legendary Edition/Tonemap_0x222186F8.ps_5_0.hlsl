// ME2 stage-1 tonemap: filmic and color-grade LUTs; no motion blur or film grain.
#define TM_HAS_MOTIONBLUR 0
#define TM_HAS_GRAIN      0
#define TM_HAS_FILMIC     1
#include "Tonemap_ME12_LUT_Body.hlsl"
