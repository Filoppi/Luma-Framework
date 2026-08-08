// ME2 stage-1 tonemap: filmic and color-grade LUTs with film grain; no motion blur.
#define TM_HAS_MOTIONBLUR 0
#define TM_HAS_GRAIN      1
#define TM_HAS_FILMIC     1
#include "Tonemap_ME12_LUT_Body.hlsl"
