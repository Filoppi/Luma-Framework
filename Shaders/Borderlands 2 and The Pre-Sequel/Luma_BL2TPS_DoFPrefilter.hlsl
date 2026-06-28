#include "../Includes/Color.hlsl"

// Half-res, firefly-tamed prefilter that feeds the separable "Luma Gaussian" DoF blur (Luma_BL2TPS_DoFGaussian).
// Point-tap blurring the full-res scene turns high-frequency detail into discrete sample dots (grain/sparkle).
// Fix (downsampled-gather DoF): blur a half-res buffer where each texel is the Karis-weighted average of a 4x4
// full-res footprint (overlapping -> gap-free downsample; Karis stops one bright pixel dominating) -> taps = areas.

Texture2D<float4> t0 : register(t0);                // full-res scene color (fp16)
RWTexture2D<float4> out_prefiltered : register(u0); // half-res Karis-tamed scene

float karis_w(float3 c)
{
   return rcp(1.0 + GetLuminance(c)); // same firefly weight as Shaders/Global/Luma_KarisAverage.hlsl
}

[numthreads(8, 8, 1)] void dof_prefilter_cs(uint3 dtid : SV_DispatchThreadID) {
   const int2 base = int2(dtid.xy) * 2; // half-res texel -> full-res block origin
   float3 sum = 0.0;
   float wsum = 0.0;
   // 4x4 full-res footprint (offsets -1..2) centered on the 2x2 block -> overlaps neighbouring half-res
   // texels so the downsampled result is gap-free for the Gaussian blur.
   [unroll] for (int dy = -1; dy <= 2; dy++)
       [unroll] for (int dx = -1; dx <= 2; dx++)
   {
      float3 c = t0.Load(int3(base + int2(dx, dy), 0)).rgb;
      float w = karis_w(c);
      sum += c * w;
      wsum += w;
   }
   out_prefiltered[dtid.xy] = float4(sum * rcp(max(wsum, 1e-6)), 1.0);
}
