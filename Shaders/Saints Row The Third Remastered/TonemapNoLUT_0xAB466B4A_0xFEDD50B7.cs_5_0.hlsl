// hdr_filter perms 48 / 49 (no colour grading LUT, 49 adds OUTPUT_LUMINANCE for the native HDR compose, which
// Luma replaces, so its StoredLuminance UAV is left unwritten).
#define SRTTR_TM_HAS_LUT 0
#include "Luma_SRTTR_Tonemap.hlsl"
