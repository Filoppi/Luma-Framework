#ifndef LUMA_ME1_GFX_UI
#define LUMA_ME1_GFX_UI

// Scaleform GFx HUD shaders, dgVoodoo transcription; the ONLY change is the final saturate, restoring the vanilla
// 8-bit canvas clamp. The HUD blends after 0x17CE0932 onto the fp16 mirror: >1 alpha extrapolates, measured ~10k nits.

#include "GameBindings.hlsl"

#define GFxSolidColor PsConstants[8]
#define GFxCxformMul  PsConstants[10]
#define GFxCxformAdd  PsConstants[11]

// The Flash colour transform, applied to the whole float4 (alpha included) exactly as the originals do.
float4 GFxCxform(float4 color)
{
   return color * GFxCxformMul + GFxCxformAdd;
}

#endif // LUMA_ME1_GFX_UI
