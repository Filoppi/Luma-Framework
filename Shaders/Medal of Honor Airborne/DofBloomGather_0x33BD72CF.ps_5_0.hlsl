// Medal of Honor: Airborne — UE3 DOFAndBloomGather (VS 0xE3D90B67). Replacement, for ONE purpose: to switch the
// game's own bloom off at its source when the Luma HDR bloom pyramid replaces it.
//
// WHY THIS PASS AND NOT THE GRADE. This shader writes a single quarter-res target that carries BOTH the depth-of
// -field blur and the bloom, summed into .xyz, with the DoF weight in .w:
//    o0.xyz = (blurAmount * avgOfFourTaps + bloom * 0.25) * 0.25,   o0.w = blurAmount * 0.25
// so there is nothing downstream that can drop the bloom without taking DoF with it — the grade's Bloom
// Intensity scales that whole numerator, which dims the defocus colour too. Here the bloom is still a separate
// summand, so scaling it away leaves the DoF math untouched, bit for bit.
//
// Everything else is a transcription of the original disassembly. Two details of the vanilla bright-pass are
// deliberately reproduced rather than "fixed", because with Luma bloom off this pass must stay vanilla:
//  - it keeps the WHOLE tap when any channel exceeds 1.0 (not the excess over the threshold), and
//  - only the FIRST TWO of the four taps feed it, while all four feed the DoF average.

// clang-format off
// ORDER IS LOAD-BEARING - do not sort. The game-local "Includes/Common.hlsl" MUST come first: it defines
// LUMA_GAME_CB_STRUCTS (via GameCBuffers.hlsl) BEFORE any shared header pulls Settings.hlsl, so
// LumaSettings.GameSettings resolves to the real grade struct rather than the empty dummy.
#include "Includes/Common.hlsl"       // game-local: LumaSettings.GameSettings.LumaBloomEnable
#include "Includes/GameBindings.hlsl" // b3/b4, the dgVoodoo masks, ApplyDgvMask, PowUE3
// clang-format on

// Only what this pass samples; the grade's t1/t6 are none of its business.
SamplerState SceneColorTextureSampler_s : register(s0);
Texture2D<float4> SceneColorTexture : register(t0); // fp16 scene color; .w carries SCENE DEPTH, not alpha

// cb4 is dgVoodoo's SHARED constant mirror, so the same row means different things per pass: row 10 is the grade's
// shadows lift, but HERE it is the engine's bloom scale (measured 1.0). Named per pass on purpose.
#define DoFBloomScale PsConstants[10] // .x = bloom scale
#define DoFParams     PsConstants[8]  // .x focus distance, .y 1/range, .z falloff exponent
#define DoFMaxBlur    PsConstants[9]  // .x max blur near, .y max blur far

// Vanilla bright-pass: pass the tap through when any channel is above 1.0, otherwise zero.
float3 BrightPassUE3(float3 c)
{
   const bool3 above = c > 1.0;
   return any(above) ? c : (float3)0.0;
}

// Full 13-entry interpolator layout, declared in order even where unread: linkage is by REGISTER (see
// Luma_MOHA_Tonemap.hlsl). The four tap positions are (v5.xy, v6.xy, v5.wz, v6.wz) - note the swapped second pair.
void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0,
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   const float4 s0 = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, v5.xy), DgvMaskT0, DgvFillT0);
   const float4 s1 = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, v6.xy), DgvMaskT0, DgvFillT0);
   const float4 s2 = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, v5.wz), DgvMaskT0, DgvFillT0);
   const float4 s3 = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, v6.wz), DgvMaskT0, DgvFillT0);

   // Bloom, from the first two taps only, at 2x gain and scaled by the engine's bloom scale (cb4[10].x).
   // LumaBloomEnable switches it off: the Luma pyramid then owns the glow, and this buffer is pure defocus.
   float3 bloom = (BrightPassUE3(s0.xyz) + BrightPassUE3(s1.xyz)) * 2.0 * DoFBloomScale.x;
   // Read as a BOOLEAN, like the tonemap does: the field is fed by a C++ bool, and a weight here would leave half the
   // vanilla glow in this buffer. This is also the ONLY switch the vanilla glow has - BloomIntensity cannot reach it.
   if (LumaSettings.GameSettings.LumaBloomEnable > 0.5)
      bloom = 0.0;

   // Depth of field, from all four taps. The alpha carries scene depth (UE3 packs it in the fp16 alpha).
   const float4 sum = s0 + s1 + s2 + s3;
   const float signedDistance = sum.w * 0.25 - DoFParams.x;
   const float normalizedDistance = saturate(abs(signedDistance) * DoFParams.y);
   const float maxBlur = (signedDistance >= 0.0) ? DoFMaxBlur.y : DoFMaxBlur.x;
   const float blurAmount = min(PowUE3(normalizedDistance.xxx, DoFParams.zzz).x, maxBlur);

   // The target is quarter-res and the grade multiplies it back by 4, hence the trailing 0.25 (the engine stores
   // this buffer pre-divided so it fits an 8-bit range; the fp16 upgrade lifted that ceiling but not the scale).
   o0 = float4((blurAmount * sum.xyz * 0.25 + bloom * 0.25) * 0.25, blurAmount * 0.25);
}
