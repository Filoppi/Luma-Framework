// PS_BinkNV12: Y' plane (R8, t5) and interleaved CbCr (R8G8, t6), opaque. See "Includes/Video.hlsl".

#include "Includes/Video.hlsl"

SamplerState g_BinkMap0Sampler : register(s5);
SamplerState g_BinkMap1Sampler : register(s6);
Texture2D<float4> g_BinkMap0 : register(t5);
Texture2D<float4> g_BinkMap1 : register(t6);

void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float4 o0 : SV_TARGET0)
{
   const float2 cbcr = g_BinkMap1.Sample(g_BinkMap1Sampler, v1).xy;
   const float y = g_BinkMap0.Sample(g_BinkMap0Sampler, v1).x;
   o0 = float4(VideoOutput(YUVtoRGB(y, cbcr.y, cbcr.x, 3)), 1.0);
}
