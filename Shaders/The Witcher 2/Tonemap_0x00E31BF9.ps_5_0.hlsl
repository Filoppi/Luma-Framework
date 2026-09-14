// The Witcher 2 tonemap, adaptive TINT permutation / HDR injection point (dgVoodoo -> ps_5_0, hash
// 0x00E31BF9; DX9 origin 0xF01A691E). Exposure + fade ramp + saturation + color tint, adaptation at t2/s2
// (t1 holds an unused depth SRV in this permutation — the vanilla CSO does not declare it either).
// Thin wrapper over the shared impl. See Luma_TW2_Tonemap.hlsl.
#define TM_HAS_TINT 1
#include "Luma_TW2_Tonemap.hlsl"

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
   o0 = RunTonemap(v5);
}
