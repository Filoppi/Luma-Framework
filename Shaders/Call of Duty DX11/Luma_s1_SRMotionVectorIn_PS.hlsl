#include "./Includes/Common.hlsl"

Texture2D<float4> tex0 : register(t);
void main(float4 v1 : SV_Position0, out float2 o0 : SV_Target0)
{
  float2 x = tex0.Load(int3(v1.xy, 0)).xy;
  o0.xy = x * -(1/0.00787401572);
}