#include "./Includes/Common.hlsl"

Texture2D<float4> codeTexture0 : register(t0);

void main(float4 v1 : SV_Position0, out float4 o0 : SV_Target0)
{
  float3 x = codeTexture0.Load(int3(v1.xy, 0)).xyz;
  x = sRGB_Encode(x);
  // x = float3(0.2, 0, 0);
  o0.xyz = x;

  o0.w = 1;
}