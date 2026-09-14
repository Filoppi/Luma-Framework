// Unity build cpp that aggregates all other cpps so we don't need to manually include them in every project.
// This is not about building speed.

#define STB_IMAGE_IMPLEMENTATION
#include <deps/stb/stb_image.h>

#include "utils/system.cpp"

#include "dlss/DLSS.cpp"
#include "fsr/FSR.cpp"
