#define GCT_DEFAULT 3

#include "../Includes/Common.hlsl"

#define LUT_STRENGTH 1.f
#define LUT_SCALING 1.f
#define TONE_MAP_TYPE 1.f
#define CUSTOM_GRAIN_TYPE 0.f

// f_{p}\left(x\right)=\frac{px}{\sqrt{xx+pp}}
float Neutwo(float x, float peak)
{
   // also written as x * rhypot(x, peak)
   float p = peak;

   float numerator = p * x;
   float denominator_squared = mad(x, x, p * p);
   return numerator * rsqrt(denominator_squared);
}

float NeutwoComputeMaxChannelScale(float3 color, float peak)
{
   float max_channel = max3(abs(color.rgb));
   float new_max = Neutwo(max_channel, peak);
   float scale = max_channel != 0 ? (new_max / max_channel) : 1.f;
   return scale;
}

float3 NeutwoMaxChannel(float3 color, float peak)
{
   return color * NeutwoComputeMaxChannelScale(color, peak);
}

float3 ApplyDisplayMapAndScale(float3 color, float2 texcoord)
{
   if (TONE_MAP_TYPE != 0.f)
   {
      color = gamma_sRGB_to_linear(color);
      color = BT709_To_BT2020(color);
      color = max(0, color);
      color = NeutwoMaxChannel(color, PeakWhiteNits / GamePaperWhiteNits);
      color = BT2020_To_BT709(color);
      color = linear_to_sRGB_gamma(color);
   }
   else
   {
      color = saturate(color);
   }
   return color;
}
