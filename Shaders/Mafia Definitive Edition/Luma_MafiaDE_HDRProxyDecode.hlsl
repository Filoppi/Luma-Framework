#include "Luma_MafiaDE_HDRProxy.hlsli"

cbuffer OutputRange : register(b0)
{
   float4 MaximumColour; // w: 1 for R11G11B10_FLOAT, 0 for FP16.
};
Texture2D<float4> Proxy : register(t0);
RWTexture2D<float4> Destination : register(u0);

[numthreads(16, 16, 1)]
void main(uint3 index : SV_DispatchThreadID)
{
   uint width, height;
   Destination.GetDimensions(width, height);
   if (index.x >= width || index.y >= height) return;
   // Quantization or reconstruction can reach a proxy code above the source's
   // finite maximum. Clamp to the destination format before its float conversion.
   float3 colour = min(MafiaDE_DecodeHDRProxy(Proxy.Load(int3(index.xy, 0)).rgb), MaximumColour.rgb);
   if (MaximumColour.w != 0.0)
   {
      // Typed float UAV stores truncate. Round to an exact R11 value first so an
      // identity proxy cannot lose one ULP; denormals use the minimum normal exponent.
      float3 step = asfloat(max(asuint(colour) & 0x7F800000u, 0x38800000u)) / float3(64.0, 64.0, 32.0);
      colour = round(colour / step) * step;
   }
   else
   {
      // The AA destination can be FP16 even when its current colour source is R11.
      // Remove the same directed store bias without imposing R11 precision on it.
      float3 step = asfloat(max(asuint(colour) & 0x7F800000u, 0x38800000u)) / 1024.0;
      colour = round(colour / step) * step;
   }
   Destination[index.xy] = float4(colour, 1.0);
}
