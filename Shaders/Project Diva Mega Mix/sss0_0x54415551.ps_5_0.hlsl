#define SSS0_NOT_NATIVE_SHADER 1
#include "./Luma_MegaMix_SSS.hlsl"

void main(
  float4 v0 : SV_POSITION0, // pixel position
  float2 v1 : TEXCOORD0, // uv center
  out float4 o0 : SV_Target0)
{
  sss0(v0, v1, o0);
}