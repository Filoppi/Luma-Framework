#ifndef LUMA_QB_COLORGRADE_HLSLI
#define LUMA_QB_COLORGRADE_HLSLI

#include "../../Includes/Common.hlsl"
#include "../../Includes/Oklab.hlsl"

struct Config
{
   float exposure;
   float highlights;
   float shadows;
   float contrast;
   float flare;
   float saturation;
   float dechroma;
   float highlight_saturation;
   float mid_gray;
};

float3 ApplyUserColorGrading(float3 bt709, Config config)
{
   if (config.exposure == 1.f && config.saturation == 1.f && config.dechroma == 0.f && config.shadows == 1.f && config.highlights == 1.f && config.contrast == 1.f && config.flare == 0.f && config.highlight_saturation == 0.f)
   {
      return bt709;
   }

   float3 color = bt709;
   color *= config.exposure;

   float y = GetLuminance(color, CS_BT709);
   const float y_normalized = max(y / config.mid_gray, 0.f);

   // Piecewise contrast around mid gray:
   // highlights applies above mid gray.
   // shadows is inverted below mid gray so lower values darken shadows.
   const float shadows_inverted = max(0.f, 2.f - config.shadows);
   const float contrast_split = (y_normalized >= 1.f) ? config.highlights : shadows_inverted;
   const float flare = safeDivision(y_normalized + config.flare, y_normalized, 1);
   const float exponent = config.contrast * contrast_split * flare;
   const float y_final = pow(y_normalized, exponent) * config.mid_gray;
   color *= (y > 0.f) ? (y_final / y) : 0.f;

   if (config.saturation != 1.f || config.dechroma != 0.f || config.highlight_saturation != 0.f)
   {
      float3 perceptual_new = Oklab::linear_srgb_to_oklab(color);

      if (config.dechroma != 0.f)
      {
         perceptual_new.yz *= lerp(1.f, 0.f, saturate(pow(y / (HDR10_MaxWhiteNits / Rec709_WhiteLevelNits), (1.f - config.dechroma))));
      }

      if (config.highlight_saturation != 0.f)
      {
         float percent_max = saturate(y * Rec709_WhiteLevelNits / HDR10_MaxWhiteNits);
         float blowout_strength = 100.f;
         float blowout_change = pow(1.f - percent_max, blowout_strength * abs(config.highlight_saturation));
         if (config.highlight_saturation < 0.f)
         {
            blowout_change = 2.f - blowout_change;
         }

         perceptual_new.yz *= blowout_change;
      }

      perceptual_new.yz *= config.saturation;

      color = Oklab::oklab_to_linear_srgb(perceptual_new);

      // Match RenoDX AP1-space clamp behavior before converting back to BT.709.
      float3 ap1 = BT709_To_AP1(color);
      ap1 = max(0, ap1);
      color = AP1_To_BT709(ap1);
   }

   return color;
}

#endif // LUMA_QB_COLORGRADE_HLSLI
