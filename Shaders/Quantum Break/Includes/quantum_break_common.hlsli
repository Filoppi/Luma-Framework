#ifndef LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI
#define LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI

#define GCT_DEFAULT 3

#include "./GameCBuffers.hlsl"

#include "../../Includes/ColorGradingLUT.hlsl"
#include "../../Includes/Common.hlsl"
#include "./CBuffer_cb_update_1.hlsli"
#include "./colorgrade.hlsli"
#include "./filmgrain.hlsli"

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

float3 ApplyDisplayMapAndScale(float3 input, float2 texcoord, float random)
{
   float3 output;
   if (TONE_MAP_TYPE != 0.f)
   {
      output = gamma_sRGB_to_linear(input);

      Config config;
      config.exposure = 1.f;
      config.highlights = CUSTOM_HIGHLIGHTS;
      config.shadows = CUSTOM_SHADOWS;
      config.contrast = CUSTOM_CONTRAST;
      config.flare = 0.10f * pow(CUSTOM_FLARE, 10.f);
      config.saturation = CUSTOM_SATURATION;
      config.dechroma = CUSTOM_DECHROMA;
      config.highlight_saturation = -1.f * (CUSTOM_HIGHLIGHT_SATURATION - 1.f);
      config.mid_gray = 0.1;

      output = ApplyUserColorGrading(output, config);

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

      if (CUSTOM_GRAIN_STRENGTH > 0.f && CUSTOM_GRAIN_TYPE != 0.f)
      {
         output = ApplyFilmGrain(
             output,
             texcoord,
             random,
             CUSTOM_GRAIN_STRENGTH * 0.015f,
             1.f,
             false, BT2020_To_XYZ);
      }

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
