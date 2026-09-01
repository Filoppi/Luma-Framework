// Medal of Honor: Airborne - UE3 UberPostProcessBlend final color pass (VS 0x3C98E35B). HDR replacement, and the only
// permutation of this pass in the dump (constant-fingerprint audit). See Luma_MOHA_Tonemap.hlsl.
#include "Luma_MOHA_Tonemap.hlsl"

// dgVoodoo's fixed interpolator layout: EVERY entry must be declared, in order, even the unread ones — VS->PS
// linkage is by REGISTER, so dropping one shifts every later TEXCOORD (see Luma_MOHA_Tonemap.hlsl).
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
   // Alpha is written as -0.0 by the original (this canvas' alpha is never read: the HUD blends with
   // srcAlpha=zero/destAlpha=one and the final Copy into the scRGB swapchain ignores it).
   o0 = float4(RunMOHATonemap(v5.xy, v6.xy), -0.0);
}
