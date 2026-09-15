// Mass Effect (2007) - UE3 UberPostProcessBlend final color pass. HDR replacement, and the only permutation of this
// pass in the menu + gameplay shader dump (constant-fingerprint audit, dgVoodoo 2.87.3). See Luma_ME1_Tonemap.hlsl.
#include "Luma_ME1_Tonemap.hlsl"

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl).
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
   // Alpha: the original writes -0.0 and nothing of the game's reads it. Luma does - the copy pass 0x1E37D75B carries
   // it into scene A for the SMAA predication CS, and with -0.0 the mask measured all-zero.
   float sceneDepth;
   const float3 graded = RunME1Tonemap(v5.xy, v6.xy, sceneDepth);
   o0 = float4(graded, sceneDepth);
}
