// ME2LE stage-1 tonemap: LUT grade and film grain; no motion blur or filmic LUT.
#define TM_HAS_MOTIONBLUR 0
#define TM_HAS_GRAIN      1
#define TM_HAS_FILMIC     0
#include "Tonemap_ME12LE_LUT_Body.hlsl"
