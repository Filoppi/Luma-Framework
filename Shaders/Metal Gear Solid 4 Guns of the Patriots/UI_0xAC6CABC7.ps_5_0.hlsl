#include "../Includes/Common.hlsl"

// TODO1: disable
#ifndef ENABLE_EMULATED_HARDWARE_BLENDS
#define ENABLE_EMULATED_HARDWARE_BLENDS 1
#endif

#ifndef ENABLE_HIGH_QUALITY_NIGHT_VISION
#define ENABLE_HIGH_QUALITY_NIGHT_VISION 1
#endif

// Main shader function body is here
// ROV support is disabled here, it's just the main original shader as it was, made for hardware blends, with no relevant extra slow code
#include "Includes/MainUI_PS.hlsl"