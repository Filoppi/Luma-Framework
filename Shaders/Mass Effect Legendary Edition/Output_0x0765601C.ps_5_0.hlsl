// Trilogy-wide stage-2 display map, decoding the gamma intermediate into absolute linear scRGB.
//
// Stage 1 writes gamma(scene / R) with R = UI Paper White / Game Paper White, and the native gamma HUD blends on
// that buffer first, so decoding and multiplying by R restores the scene while leaving HUD white relative to Game
// Paper White. This pass writes the swapchain directly as the frame's last draw, so under EARLY_DISPLAY_ENCODING
// 1 it also applies G = Game Paper White / 80; Display Composition divides G back out when it does run.

// clang-format off
#include "Includes/Common.hlsl"   // Defines game settings; keep first.
#include "../Includes/Color.hlsl" // Gamma transfer helpers.
// clang-format on

Texture2D<float4> SourceTexture : register(t0); // Stage-1 gamma scene with native HUD composition.
SamplerState SourceTextureSampler_s : register(s0);

void main(
    float2 v0 : TEXCOORD0,
    out float4 o0 : SV_Target0)
{
   // Both terms are cbuffer-uniform scalars: fold them so the decode is followed by a single vector multiply.
   const float scale = MELE_GetUIPaperWhiteRelativeToGame() * MELE_GetGamePaperWhiteScale();
   float3 c = SourceTexture.SampleLevel(SourceTextureSampler_s, v0.xy, 0).xyz;
   o0 = float4(gamma_to_linear(c, GCT_MIRROR) * scale, 1.0);
}
