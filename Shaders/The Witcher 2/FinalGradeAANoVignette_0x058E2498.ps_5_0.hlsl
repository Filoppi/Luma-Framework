// The Witcher 2 final grade, FXAA + NO-VIGNETTE permutation (dgVoodoo -> ps_5_0, hash 0x058E2498): the fourth
// corner of the 2x2 matrix the engine compiles (FXAA in/out x vignette in/out). Disassembly diff vs
// 0xDE5CF9CD: byte-for-byte the same shader minus the t2/s2 mask sample, the cb3[48..49] fixup and the
// cb4[66..67] weight/color lerp; the FXAA block, the grade tail and the o0.w = 0 write are unchanged.
//
// Without this file the pass fell through unreplaced whenever the user ran the game's Anti-aliasing ON with
// the vignette OFF, which silently costs the whole Luma tail on those frames: no HDR block, no SMAA (the
// post-draw callback keys on the grade hash) and no Hide UI gate.
//
// No dgVoodoo 2.81.3 counterpart is keyed: that build's dump was captured with the vignette on and never
// produced this permutation. It needs a re-dump under 2.81.3 with the vignette off.

#define LUMA_TW2_NO_VIGNETTE_PERM 1
#include "FinalGrade_0xDE5CF9CD.ps_5_0.hlsl"
