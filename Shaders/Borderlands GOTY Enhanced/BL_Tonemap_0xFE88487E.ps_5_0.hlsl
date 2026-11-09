// UE3 final color pass without FXAA: 0xB030BAA6 minus the SV_Target1 luma output. See Luma_BL_Tonemap.hlsl.
#include "Luma_BL_Tonemap.hlsl"

void main(
    float4 v0 : TEXCOORD0,
    float2 v1 : TEXCOORD1,
    out float4 o0 : SV_Target0)
{
   float3 color;
   float luma; // Unused: no FXAA target
   RunBLTonemap(v0, v1, color, luma);
   o0 = float4(color, 0.0); // Alpha 0 as native (mov o0.w, 0); nothing downstream reads it.
}
