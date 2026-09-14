#ifndef LUMA_MOHA_GAME_BINDINGS
#define LUMA_MOHA_GAME_BINDINGS

// Medal of Honor: Airborne - bindings the replaced passes share, and nothing else. Included by the tonemap and by
// the DoF/bloom gather, which needs them but none of the tonemap itself.
// Deliberately holds no textures or samplers, and no alias for a game-content cbuffer row: slot and row meaning is
// per pass. t0 is the fp16 scene in the post chain but the Y plane in Video_0x1AAC12AD, and PsConstants[8] is
// DoFParams in the grade but the gamma exponent in that same video pass. Name rows in the pass that reads them.
// Needs no includes of its own: both helpers use intrinsics only, so this file stays out of the load-bearing
// include ordering that Luma_MOHA_Tonemap.hlsl documents.

// b3/b4 are dgVoodoo's D3D9 constant mirrors, declared at the original's sizes (CB3[77], CB4[236]).
cbuffer DgVoodooState : register(b3)
{
   float4 DgvConstants[77] : packoffset(c0);
}

cbuffer PixelShaderConstants : register(b4)
{
   float4 PsConstants[236] : packoffset(c0);
}

// dgVoodoo texture-format emulation masks: one (mask, fill) pair per sampler slot, s0 at 44/45, s1 at 46/47.
// Every texture fetch in a translated shader is followed by this pair; dropping it shifts colour.
#define DgvMaskT0 DgvConstants[44]
#define DgvFillT0 DgvConstants[45]
#define DgvMaskT1 DgvConstants[46]
#define DgvFillT1 DgvConstants[47]

float4 ApplyDgvMask(float4 value, float4 mask, float4 fill)
{
   return asfloat((asuint(value) & asuint(mask)) | asuint(fill));
}

// The grade's pow() as the original computes it. The tiny floor replaces the compiler's own log2(0) guard.
float3 PowUE3(float3 base, float3 exponent)
{
   return exp2(exponent * log2(max(abs(base), 1e-30)));
}

#endif // LUMA_MOHA_GAME_BINDINGS
