#include "common1.hlsl"

Texture2D<float4> codeTexture0 : register(t0);
// Texture2D<float> OITTexture : register(t1);

void main(float4 v1 : SV_Position0, out float4 o0 : SV_Target0)
{
  float3 x = codeTexture0.Load(int3(v1.xy, 0)).xyz;
  
  x = Trade_Out_NoCS(x);

  o0.xyz = x;

  // o0.w = TonemapShader_Alpha(OITTexture.Load(int3(v1.xy, 0)).x);
}