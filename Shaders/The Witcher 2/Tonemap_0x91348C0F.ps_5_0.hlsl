// The Witcher 2 tonemap, adaptive NO-TINT permutation / HDR injection point (dgVoodoo -> ps_5_0, hash
// 0x91348C0F; DX9 origin 0xC5ADBC35). Exposure + post-scale only, alpha passthrough, adaptation at t1/s1.
// Thin wrapper over the shared impl. See Luma_TW2_Tonemap.hlsl.
#define TM_HAS_TINT 0
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
