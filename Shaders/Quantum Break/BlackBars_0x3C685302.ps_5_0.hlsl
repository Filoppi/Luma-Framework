#include "../Includes/Common.hlsl"

#ifndef DISABLE_BLACK_BARS
#define DISABLE_BLACK_BARS 1
#endif

void main(
  float3 v0 : TEXCOORD0,
  float4 v1 : COLOR0,
  out float4 o0 : SV_Target0)
{
#if DISABLE_BLACK_BARS
  if (all(v1.xyz == 0.0))
  {
    v1.w = 0.0;
  }
#endif // DISABLE_BLACK_BARS
  o0.xyzw = v1.xyzw;
}