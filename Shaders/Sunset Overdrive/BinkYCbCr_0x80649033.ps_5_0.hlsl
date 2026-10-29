// PS_BinkYCbCr: three A8 planes (Y' t5, Cr t6, Cb t7), opaque. See "Includes/Video.hlsl".

#include "Includes/Video.hlsl"

SamplerState g_BinkMap0Sampler : register(s5);
SamplerState g_BinkMap1Sampler : register(s6);
SamplerState g_BinkMap2Sampler : register(s7);
Texture2D<float4> g_BinkMap0 : register(t5);
Texture2D<float4> g_BinkMap1 : register(t6);
Texture2D<float4> g_BinkMap2 : register(t7);

void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float4 o0 : SV_TARGET0)
{
   const float y = g_BinkMap0.Sample(g_BinkMap0Sampler, v1).w;
   const float cr = g_BinkMap1.Sample(g_BinkMap1Sampler, v1).w;
   const float cb = g_BinkMap2.Sample(g_BinkMap2Sampler, v1).w;
   o0 = float4(VideoOutput(YUVtoRGB(y, cr, cb, 3)), 1.0);
}
