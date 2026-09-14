#include "../Includes/Common.hlsl"

// That's the point of this shader.
#ifndef ENABLE_EMULATED_HARDWARE_BLENDS
#define ENABLE_EMULATED_HARDWARE_BLENDS 1
#endif

// We know this won't trigger for the ROV cases, so skip the checks for it
#ifdef ENABLE_HIGH_QUALITY_NIGHT_VISION
#undef ENABLE_HIGH_QUALITY_NIGHT_VISION
#endif
#define ENABLE_HIGH_QUALITY_NIGHT_VISION 0

// Main shader function body is here
// This is the same as 0xAC6CABC7, with with custom ROV support
#include "Includes/MainUI_PS.hlsl"