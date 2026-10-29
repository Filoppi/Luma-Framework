// PS_BinkYCbCrA: four R8 planes (Y' t5, Cr t6, Cb t7, alpha t8), all scaled by g_BinkConst. See "Includes/Video.hlsl".

#include "Includes/Video.hlsl"

cbuffer PSBinkAlphaCB : register(b2)
{
   float4 g_BinkConst;
}

SamplerState g_BinkMap0Sampler : register(s5);
SamplerState g_BinkMap1Sampler : register(s6);
SamplerState g_BinkMap2Sampler : register(s7);
SamplerState g_BinkMap3Sampler : register(s8);
Texture2D<float4> g_BinkMap0 : register(t5);
Texture2D<float4> g_BinkMap1 : register(t6);
Texture2D<float4> g_BinkMap2 : register(t7);
Texture2D<float4> g_BinkMap3 : register(t8);

void main(float4 v0 : SV_POSITION0, float2 v1 : TEXCOORD0, out float4 o0 : SV_TARGET0)
{
   const float cb = g_BinkMap2.Sample(g_BinkMap2Sampler, v1).x;
   const float cr = g_BinkMap1.Sample(g_BinkMap1Sampler, v1).x;
   const float y = g_BinkMap0.Sample(g_BinkMap0Sampler, v1).x;
   const float alpha = g_BinkMap3.Sample(g_BinkMap3Sampler, v1).x;
   o0 = float4(YUVtoRGB(y, cr, cb, 3), alpha) * g_BinkConst;
   o0.rgb = VideoOutput(o0.rgb);
}
