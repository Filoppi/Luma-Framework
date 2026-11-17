#include "Luma_MafiaDE_HDRProxy.hlsli"

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Proxy : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 index : SV_DispatchThreadID)
{
   uint width, height;
   Proxy.GetDimensions(width, height);
   if (index.x >= width || index.y >= height) return;
   Proxy[index.xy] = float4(MafiaDE_EncodeHDRProxy(Source.Load(int3(index.xy, 0)).rgb), 1.0);
}
