// The Witcher 2 final grade, NO-FXAA permutation (dgVoodoo -> ps_5_0, hash 0xCF3B72A9): what the engine runs
// with the game's Anti-aliasing setting turned off. Identical to 0xDE5CF9CD from
// the desaturation onward (desat cb4[62], gamma-slider log2/pow/exp2 cb4[61], scale cb4[60], highlight and
// shadow tint lerps cb4[68..71], vignette t2 + cb4[66..67]); the FXAA neighbourhood is replaced by a single
// scene tap at v5.xy, and the output alpha carries the scene alpha instead of 0.
//
// Both differences are gated on LUMA_TW2_NO_FXAA_PERM inside the main file, so the vanilla grade tail and the
// Luma HDR output block have exactly one implementation. Users can now turn the in-game AA off and keep both
// HDR and Luma SMAA.

#define LUMA_TW2_NO_FXAA_PERM 1
#include "FinalGrade_0xDE5CF9CD.ps_5_0.hlsl"
