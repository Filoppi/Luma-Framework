#ifndef LUMA_TW2_GAME_BINDINGS
#define LUMA_TW2_GAME_BINDINGS

#include "../../Includes/DgVoodoo.hlsl"

// The Witcher 2 - dgVoodoo bindings and ops the replaced passes share, and nothing else. Included by the tonemap, the
// final grade, the glow and light-shaft blends and the video pass.
// Deliberately holds no textures or samplers, and no alias for a game-content cb4 row: row meaning is per pass (cb4[60]
// is vHighlight in the grade but the glow UV clamp rect in the glow blend). Name rows in the pass that reads them.
// Needs no includes of its own: its helpers use intrinsics only, so this file stays out of the load-bearing include
// ordering that Luma_TW2_Tonemap.hlsl documents.

// b3/b4 are dgVoodoo's D3D9 constant mirrors, declared at the original's sizes. b4 rows are DX9 cN at cb4[N + 8].
cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

// dgVoodoo texture-format emulation masks: one (mask, fill) pair per sampler slot, s0 at 44/45, s1 at 46/47, s2 at
// 48/49. Every texture fetch in a translated shader is followed by this pair (e.g. forcing alpha to 1 on X8 formats);
// dropping it shifts colour.
#define DgvMaskT0 cb3[44]
#define DgvFillT0 cb3[45]
#define DgvMaskT1 cb3[46]
#define DgvFillT1 cb3[47]
#define DgvMaskT2 cb3[48]
#define DgvFillT2 cb3[49]

#endif // LUMA_TW2_GAME_BINDINGS
