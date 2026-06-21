// Borderlands GOTY Enhanced — final color pass (FXAA path: writes SV_Target1 luma). HDR replacement.
// See Luma_BL_Tonemap.hlsl for the implementation.
#include "Luma_BL_Tonemap.hlsl"

void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  out float4 o0 : SV_Target0,
  out float o1 : SV_Target1)
{
  float3 color;
  float luma;
  RunBLTonemap(v0, v1, color, luma);
  o0 = float4(color, GetLuminance(color));
  o1 = luma;
}
