#ifndef LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI
#define LUMA_QB_QUANTUM_BREAK_COMMON_HLSLI

#define GCT_DEFAULT 3

#include "./GameCBuffers.hlsl"

#include "../../Includes/ColorGradingLUT.hlsl"
#include "../../Includes/Common.hlsl"
#include "../../Includes/Tonemap.hlsl"
#include "./CBuffer_cb_update_1.hlsli"
#include "./colorgrade.hlsli"
#include "./filmgrain.hlsli"

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

      float3 ch = Tonemap::CInfinityRollOff::Apply(output, peak_ratio);
      output = ch;

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
