// Borderlands GOTY Enhanced — lens-flare/glare composite (PS 0x010371F2).
//
// Vanilla adds an additive flare/glare overlay (FinalTexture) on top of the scene: o0 = scene + flare.
// We keep that verbatim but scale the additive term by a user "Flare" slider (LumaSettings.GameSettings.FlareOut;
// 1 = vanilla, 0 = no flare). Faithful to the game's additive behavior; lets users dial down the bloom-like glare.

#include "Includes/Common.hlsl" // game-local: LumaSettings.GameSettings

SamplerState SceneColorTextureSampler_s : register(s0);
SamplerState FinalTextureSampler_s : register(s1);
Texture2D<float4> SceneColorTexture : register(t0);
Texture2D<float4> FinalTexture : register(t1);

void main(
  float2 v0 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 scene = SceneColorTexture.Sample(SceneColorTextureSampler_s, v0.xy);
  float4 flare = FinalTexture.Sample(FinalTextureSampler_s, v0.xy);
  o0 = scene + flare * LumaSettings.GameSettings.FlareOut;
}
