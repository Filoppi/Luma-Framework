// The Witcher 2 final grade, NO-FXAA + NO-VIGNETTE permutation (dgVoodoo -> ps_5_0, hash 0xBABBFFAD): the
// engine drops the vignette stage entirely in this variant. Identical by disassembly diff
// vs 0xCF3B72A9: byte-for-byte the same shader minus the t2/s2 mask sample, cb3[48..49] fixup and the
// cb4[66..67] weight/color lerp — everything from the desaturation through the tint lerps is identical, and
// the output alpha likewise carries the scene alpha.
//
// Without this file the pass fell through unreplaced, which silently costs the whole Luma tail on any frame
// that uses it: no HDR block, no SMAA (the post-draw callback keys on the grade hash) and no Hide UI gate.
//
// Both differences are gated inside the main file (LUMA_TW2_NO_FXAA_PERM + LUMA_TW2_NO_VIGNETTE_PERM), so the
// vanilla grade tail and the Luma HDR output block still have exactly one implementation.

#define LUMA_TW2_NO_FXAA_PERM     1
#define LUMA_TW2_NO_VIGNETTE_PERM 1
#include "FinalGrade_0xDE5CF9CD.ps_5_0.hlsl"
