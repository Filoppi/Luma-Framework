// hdr_filter perms 52 / 53 (APPLY_COLOR_GRADING_LUT, 53 adds OUTPUT_LUMINANCE for the native HDR compose, which
// Luma replaces, so its StoredLuminance UAV is left unwritten).
#define SRTTR_TM_HAS_LUT 1
#include "Luma_SRTTR_Tonemap.hlsl"
