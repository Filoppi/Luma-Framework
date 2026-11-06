// Tonemapped MSAA resolve for the 4x fp16 scene targets (replaces the hardware resolve).
// Weighted Karis-style average: each sample is weighted by 1 / (1 + max channel), so very bright samples
// don't dominate the edges. Alpha is a plain average.

Texture2DMS<float4, 4> sourceMS : register(t0);

float4 main(float4 pos : SV_Position) : SV_Target
{
   int2 coords = int2(pos.xy);

   float3 sum = 0.0f;
   float weight_sum = 0.0f;
   float alpha_sum = 0.0f;

   [unroll]
   for (int i = 0; i < 4; i++)
   {
      float4 c = sourceMS.Load(coords, i);
      // NaN/Inf guard: treat invalid samples as black with zero weight contribution
      if (!isfinite(c.r) || !isfinite(c.g) || !isfinite(c.b) || !isfinite(c.a))
         c = float4(0.0f, 0.0f, 0.0f, 0.0f);

      float3 positive_rgb = max(c.rgb, 0.0f);
      float peak = max(max(positive_rgb.r, positive_rgb.g), positive_rgb.b);
      float w = 1.0f / (1.0f + peak);

      sum += c.rgb * w;
      weight_sum += w;
      alpha_sum += c.a;
   }

   float3 rgb = sum / max(weight_sum, 1e-6f);
   float a = alpha_sum * 0.25f;
   return float4(rgb, a);
}
