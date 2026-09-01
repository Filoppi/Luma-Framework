// Medal of Honor: Airborne - UE3 FGammaCorrectionPixelShader (VS 0xA2F269CA). HDR replacement: this pass, not
// UberPostProcessBlend, writes the final LDR canvas when "bAllowDepthOfField = False". See Luma_MOHA_Tonemap.hlsl.
#include "Luma_MOHA_Tonemap.hlsl"

// Full 13-entry interpolator layout, declared in order even where unread: linkage is by REGISTER (see
// Luma_MOHA_Tonemap.hlsl). This pass reads only TEXCOORD0, the scene UV in v5.
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
   // Alpha is written as 1.0 by the original (UberPostProcessBlend writes -0.0 instead). This canvas' alpha is never
   // read: the HUD blends srcAlpha=zero/destAlpha=one and the final blit into the scRGB swapchain ignores it.
   o0 = float4(RunMOHAGammaCorrection(v5.xy), 1.0);
}
