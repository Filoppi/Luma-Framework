// ME2 stage-1 tonemap: LUT grade, motion blur, and film grain; no filmic LUT.
#define TM_HAS_MOTIONBLUR 1
#define TM_HAS_GRAIN      1
#define TM_HAS_FILMIC     0
#include "Tonemap_ME12_LUT_Body.hlsl"
