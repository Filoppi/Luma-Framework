// Tonemapped MSAA resolve for the 4x fp16 scene targets (replaces the hardware resolve).
// Weighted Karis-style average: each sample is weighted by 1 / (1 + peak * E), where E is the Luma auto-exposure
// (the scene multiplier, same as GetHMAExposure), so very bright samples don't dominate the edges. Alpha is a plain average.
// HMA_RESOLVE_2X2 = 1: some drivers expose the 4x MS surface as a 2x2 grid in sample 0 (see Luma_HMA_TonemappedResolve2x2.hlsl).

#ifndef HMA_RESOLVE_2X2
#define HMA_RESOLVE_2X2 0
#endif

Texture2DMS<float4, 4> sourceMS : register(t0);
// Luma auto-exposure meter (1x1 R32_FLOAT), bound on t1 by main.cpp. Falls back to 1 when missing.
Texture2D<float> exposureTex : register(t1);

float4 main(float4 pos : SV_Position) : SV_Target
{
   float E = exposureTex.Load(int3(0, 0, 0));
   if (!(E > 0.0f) || !isfinite(E))
      E = 1.0f;

   float3 sum = 0.0f;
   float weight_sum = 0.0f;
   float alpha_sum = 0.0f;

   [unroll]
   for (int i = 0; i < 4; i++)
   {
      float4 c = HMA_RESOLVE_2X2 ? sourceMS.Load(int2(pos.xy) * 2 + int2(i & 1, i >> 1), 0) : sourceMS.Load(int2(pos.xy), i);
      // NaN/Inf guard: treat invalid samples as black with zero weight contribution
      if (!isfinite(c.r) || !isfinite(c.g) || !isfinite(c.b) || !isfinite(c.a))
         c = float4(0.0f, 0.0f, 0.0f, 0.0f);

      float3 abs_rgb = abs(c.rgb); // abs: large negative samples must not keep full weight either
      float peak = max(max(abs_rgb.r, abs_rgb.g), abs_rgb.b);
      float w = 1.0f / (1.0f + peak * E);

      sum += c.rgb * w;
      weight_sum += w;
      alpha_sum += c.a;
   }

   float3 rgb = sum / max(weight_sum, 1e-6f);
   float a = alpha_sum * 0.25f;
   return float4(rgb, a);
}
