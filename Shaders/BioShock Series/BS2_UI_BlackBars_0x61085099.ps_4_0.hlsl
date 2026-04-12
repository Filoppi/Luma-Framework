#include "Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float4 v1 : COLOR0,
  out float4 o0 : SV_Target0)
{
#if DISABLE_BLACK_BARS
  // Check is the color black.
  // Alpha blending is used.
  //
  // Besides black bars this renders some elenments on the map,
  // including black elemets. But it may not be a significant issue (eg. black outline to red arrow).
  o0.xyzw = length(v1.xyz) <= 1e-6 ? 0.0 : v1.xyzw;
#else
  o0.xyzw = v1.xyzw;
#endif
}