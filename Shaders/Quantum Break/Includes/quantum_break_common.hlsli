#ifndef LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI
#define LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI

#define GCT_DEFAULT 3

#include "../../Includes/ColorGradingLUT.hlsl"
#include "../../Includes/Common.hlsl"
#include "./CBuffer_cb_update_1.hlsli"

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

float3 NeutwoPerChannel(float3 color, float3 peak)
{
   return float3(Neutwo(color.r, peak.r), Neutwo(color.g, peak.g), Neutwo(color.b, peak.b));
}

float3 ApplyDisplayMapAndScale(float3 input, float2 texcoord)
{
   float3 output;
   if (TONE_MAP_TYPE != 0.f)
   {
      output = gamma_sRGB_to_linear(input);
      output = BT709_To_BT2020(output);
      output = max(0, output);

      float peak_ratio = PeakWhiteNits / GamePaperWhiteNits;
#if 0
      float3 maxch = NeutwoMaxChannel(output, peak_ratio);
      output = maxch;
#else
      float3 ch = NeutwoPerChannel(output, peak_ratio);
      output = ch;
#endif

      output = BT2020_To_BT709(output);
      output = linear_to_sRGB_gamma(output);
   }
   else
   {
      output = saturate(input);
   }
   return output;
}

#endif // LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI
