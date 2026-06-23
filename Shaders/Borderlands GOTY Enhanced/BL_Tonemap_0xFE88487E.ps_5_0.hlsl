// Borderlands GOTY Enhanced — final color pass (no-FXAA path: SV_Target0 only). HDR replacement.
// Identical grade to 0xB030BAA6; the only difference is this variant has no SV_Target1 luma output.
// See Luma_BL_Tonemap.hlsl for the implementation.
#include "Luma_BL_Tonemap.hlsl"

void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float3 color;
  float luma; // unused (no FXAA luma target on this variant)
  RunBLTonemap(v0, v1, color, luma);
  o0 = float4(color, GetLuminance(color));
}
