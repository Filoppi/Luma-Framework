// Luma: reflection chain sqrt encode pass 0xF5492D6A (o0 = sqrt(t0)).
// The deferred lights write some negative values into the fp16 reflection target; sqrt of a negative is NaN, which the
// reflective floor then feeds back frame to frame (yellow blocks). Vanilla's 8 bit reflection texture dropped them.
// TONEMAP_TYPE 0 keeps the vanilla math exact.

#include "Includes/Common.hlsl"

Texture2D<float4> mapTLB0 : register(t0);
SamplerState samplerTLB0 : register(s0);

float4 main(float4 v0 : SV_POSITION0, float4 v1 : COLOR0, float2 v2 : TEXCOORD0) : SV_TARGET0
{
  float4 c = mapTLB0.Sample(samplerTLB0, v2.xy);
#if TONEMAP_TYPE >= 1
  c = max(c, 0.0); // also turns NaN into 0
#endif
  return sqrt(c);
}
