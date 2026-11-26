// Brutal Legend - Luma bloom prefilter (feeds core DrawBloom). Reads the tonemap's scene (t0, gamma-space FP16, what the
// game's own bright pass sees), decodes it to linear, Karis-averages each 2x2 block into a half-res texel and applies a
// soft-knee threshold on the max channel. No clamp: the energy above 1 is what the bloom is for.

#include "Includes/Common.hlsl"

Texture2D<float4> t0 : register(t0); // Scene (gamma-space FP16)

// Sign-preserving gamma 2.2 decode, same as the tonemap's SignPow
float3 DecodeGamma22(float3 x)
{
   return pow(abs(x), 2.2) * sign(x);
}

// Soft knee on the max channel (Unreal-style quadratic knee). Threshold t, knee width = kneeFrac * t.
// Above t + knee the colour passes unchanged (scaled by (br - t) / br); below t - knee it is zero.
float3 BloomSoftKneeThreshold(float3 c, float t, float kneeFrac)
{
   if (t <= 0.0)
      return max(c, 0.0);
   const float br = max(c.r, max(c.g, c.b));
   const float knee = max(kneeFrac * t, 1e-4);
   float soft = clamp(br - t + knee, 0.0, 2.0 * knee);
   soft = soft * soft / (4.0 * knee);
   const float contribution = max(soft, br - t) / max(br, 1e-4);
   return c * max(contribution, 0.0);
}

float4 main(float4 pos : SV_Position) : SV_Target
{
   const int2 dst = int2(pos.xy);
   const int2 src = dst * 2;

   // 2x2 Karis average in linear: each sample weighted 1 / (1 + max channel) so a single firefly cannot dominate
   float3 sum = 0.0;
   float wsum = 0.0;
   [unroll]
   for (int y = 0; y < 2; y++)
   {
      [unroll]
      for (int x = 0; x < 2; x++)
      {
         const float3 c = DecodeGamma22(t0.Load(int3(src + int2(x, y), 0)).rgb);
         const float w = rcp(1.0 + max(max(c.r, c.g), max(c.b, 0.0)));
         sum += c * w;
         wsum += w;
      }
   }
   const float3 avg = sum / max(wsum, 1e-6);

   const float3 bright = BloomSoftKneeThreshold(avg, LumaSettings.GameSettings.LumaBloomThreshold, LumaSettings.GameSettings.LumaBloomKnee);
   return float4(bright, 1.0);
}
