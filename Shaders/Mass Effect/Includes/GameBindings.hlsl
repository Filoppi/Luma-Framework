#ifndef LUMA_ME1_GAME_BINDINGS
#define LUMA_ME1_GAME_BINDINGS

// Bindings shared by the replaced passes (tonemap, gather, Bink video, GFx HUD), and nothing else. NO textures,
// samplers or cbuffer-row aliases: meaning is per pass (t0 = scene here, Y plane in Video; cb4[8] = DoFParams vs matrix
// row).

// b3/b4 are dgVoodoo's D3D9 constant mirrors, declared at the original's sizes (CB3[77], CB4[236]).
cbuffer DgVoodooState : register(b3)
{
   float4 DgvConstants[77] : packoffset(c0);
}

cbuffer PixelShaderConstants : register(b4)
{
   float4 PsConstants[236] : packoffset(c0);
}

// dgVoodoo texture-format emulation masks: one (mask, fill) pair per sampler slot, s0 at 44/45, s1 at 46/47, s2 at 48/49.
// Every texture fetch in a translated shader is followed by this pair; dropping it shifts colour.
#define DgvMaskT0 DgvConstants[44]
#define DgvFillT0 DgvConstants[45]
#define DgvMaskT1 DgvConstants[46]
#define DgvFillT1 DgvConstants[47]
#define DgvMaskT2 DgvConstants[48]
#define DgvFillT2 DgvConstants[49]

float4 ApplyDgvMask(float4 value, float4 mask, float4 fill)
{
   return asfloat((asuint(value) & asuint(mask)) | asuint(fill));
}

// The grade's pow() as the original computes it. The tiny floor replaces the compiler's own log2(0) guard.
float3 PowUE3(float3 base, float3 exponent)
{
   return exp2(exponent * log2(max(abs(base), 1e-30)));
}

// UE3's depth-of-field blur weight at linear `depth`, as both the gather and the uber grade compute it. The rows are
// passed in, each pass naming them from its own register map: `dofParams` .x focus distance, .y 1/range, .z falloff
// exponent; `dofMaxBlur` .x max blur near, .y max blur far.
float DoFBlurAmount(float depth, float4 dofParams, float4 dofMaxBlur)
{
   const float signedDistance = depth - dofParams.x;
   const float normalizedDistance = saturate(abs(signedDistance) * dofParams.y);
   const float maxBlur = (signedDistance >= 0.0) ? dofMaxBlur.y : dofMaxBlur.x;
   return min(PowUE3(normalizedDistance.xxx, dofParams.zzz).x, maxBlur);
}

#endif // LUMA_ME1_GAME_BINDINGS
