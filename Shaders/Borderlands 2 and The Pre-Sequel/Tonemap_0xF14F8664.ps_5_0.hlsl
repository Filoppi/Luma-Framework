// Borderlands 2 tonemap / HDR injection point (dgVoodoo 2.81.3 -> ps_4_0, hash 0xF14F8664).
// Older dgVoodoo builds emit ps_4_0 -> a different CSO hash for the same DX9 shader. Identical I/O + slot map to
// the 2.87.3 variant (0xD00AA2A7), so this is the same identity wrapper over the shared grade impl.
#include "Luma_BL2TPS_Tonemap.hlsl"

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
   o0 = RunTonemap(v5, v6);
}
