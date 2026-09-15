#ifndef LUMA_ME1_DOF_BLOOM_GATHER
#define LUMA_ME1_DOF_BLOOM_GATHER

// UE3 DOFAndBloomGather, per-tap body of both QualityBloom perms (TRUE 0x56854256, 16 taps x1/64; FALSE 0x28F8DB16,
// 4 taps x1/16) plus a 1/4 pre-divide for the UNORM clamp; 2.87.3 transcription, also included by the 2.81.3 ps_4_0
// twins (0x4B65EEAE, 0xAA369C00). Replaced only to zero vanilla bloom.

// clang-format off
// ORDER IS LOAD-BEARING - game-local Common.hlsl first, so LUMA_GAME_CB_STRUCTS is defined before Settings.hlsl.
#include "Common.hlsl"       // game-local: LumaSettings.GameSettings.LumaBloomEnable
#include "GameBindings.hlsl" // b3/b4, the dgVoodoo masks, ApplyDgvMask, DoFBlurAmount
// clang-format on

// Only what this pass samples.
SamplerState SceneColorTextureSampler_s : register(s0);
Texture2D<float4> SceneColorTexture : register(t0); // fp16 scene color; .w carries SCENE DEPTH, not alpha

// cb4 is dgVoodoo's SHARED constant mirror, so the same row means different things per pass: row 11 is the grade's
// shadows lift, but HERE it is the engine's bloom scale. Named per pass on purpose. Rows 8/10 match the uber pass.
#define DoFParams     PsConstants[8]  // .x focus distance, .y 1/range, .z falloff exponent
#define DoFMaxBlur    PsConstants[10] // .x max blur near, .y max blur far
#define DoFBloomScale PsConstants[11] // .x = bloom scale

// Vanilla glow gain, or zero when the Luma pyramid owns the glow. Read as a BOOLEAN (C++ bool; a weight would leave
// half the vanilla glow) and it is that glow's ONLY switch. Hoisted out of GatherTap so it is not re-evaluated per tap.
static const float LumaGatherBloomScale = (LumaSettings.GameSettings.LumaBloomEnable > 0.5) ? 0.0 : DoFBloomScale.x;

// One tap: (blur * colour + bloom, blur). Vanilla bright-pass: the tap passes through when any channel is above 1.0.
float4 GatherTap(float2 uv)
{
   const float4 s = ApplyDgvMask(SceneColorTexture.Sample(SceneColorTextureSampler_s, uv), DgvMaskT0, DgvFillT0);

   const float3 bloom = any(s.xyz > 1.0) ? s.xyz * LumaGatherBloomScale : (float3)0.0;

   const float blurAmount = DoFBlurAmount(s.w, DoFParams, DoFMaxBlur); // vanilla DoF weight, per tap

   return float4(blurAmount * s.xyz + bloom, blurAmount);
}

#endif // LUMA_ME1_DOF_BLOOM_GATHER
