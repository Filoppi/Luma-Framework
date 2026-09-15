// Constant-colour fill with an INTERPOLATED alpha, sibling of UI_SolidFill_0xDE418D30 (constant alpha); the dump
// sweep found only those two. Pre-emptive: an unclamped constant plus SrcAlpha extrapolation is the 10k-nit HUD.
#include "Includes/GFxUI.hlsl"

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl). This shader reads only
// TEXCOORD5.w (v10.w), its alpha.
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
   // The original declares no dgVoodoo mask cbuffer (CB4 only), so there is no ApplyDgvMask here: it samples nothing.
   o0 = saturate(float4(GFxSolidColor.xyz, v10.w));
}
