#include "./Includes/Common.hlsl"

Texture2D<float4> tex0 : register(t0);

void main(float4 v1 : SV_Position0, out float3 o0 : SV_Target0)
{
  float3 x = tex0.Load(int3(v1.xy, 0)).xyz;
  x = RenderIntermediatePass(x);
  o0.xyz = x;
}