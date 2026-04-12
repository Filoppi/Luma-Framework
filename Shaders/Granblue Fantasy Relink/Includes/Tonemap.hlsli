#ifndef TONEMAP_HLSLI
#define TONEMAP_HLSLI

#include "./Common.hlsl"
#include "./macleod_boynton.hlsli"
#include "./neutwo.hlsl"
#include "./psycho_test11.hlsl"
#include "../../Includes/Tonemap.hlsl"

float1 CorrectGamma1(float uncorrected, float gamma = 2.2f) {
    return gamma_to_linear1(linear_to_gamma1(uncorrected, GCT_MIRROR), GCT_MIRROR, gamma);
}

float3 CorrectGamma(float3 color, float gamma = 2.2f) {
    return gamma_to_linear(linear_to_gamma(color, GCT_MIRROR), GCT_MIRROR, gamma);
}

float3 CorrectLuminance(float3 color, float incorrect_y, float correct_y, float strength = 1.f) {
  return color * lerp(1.f, safeDivision(correct_y, incorrect_y, 1), strength);
}

float3 Select(float condition, float3 trueValue, float3 falseValue) {
  [flatten]
  if (condition) {
    return trueValue;
  } else {
    return falseValue;
  }
}

float3 ApplyVanillaToneMap(float3 untonemapped) {
  float3 r0, r1, r2;
  r0.rgb = untonemapped;

  r1.xyz = r0.xyz * 0.15 + 0.05;
  r1.xyz = r0.xyz * r1.xyz + 0.004;
  r2.xyz = r0.xyz * 0.15 + 0.5;
  r0.xyz = r0.xyz * r2.xyz + 0.06;
  r0.xyz = r1.xyz / r0.xyz;
  r0.xyz = -0.0666666701 + r0.xyz;
  r0.rgb *= 1.37906432;

  return r0;
}

float Highlights(float x, float highlights, float mid_gray) {
  if (highlights == 1.f) return x;

  if (highlights > 1.f) {
    return max(x, lerp(x, mid_gray * pow(x / mid_gray, highlights), min(x, 1.f)));
  } else {  // highlights < 1.f
    float b = mid_gray * pow(x / mid_gray, 2.f - highlights);
    float t = min(x, 1.f);  // clamp extreme influence
    return min(x, safeDivision(x * x, lerp(x, b, t), x));
  }
}

float Shadows(float x, float shadows, float mid_gray) {
  if (shadows == 1.f) return x;

  const float ratio = max(safeDivision(x, mid_gray, 0.f), 0.f);
  const float base_term = x * mid_gray;
  const float base_scale = safeDivision(base_term, ratio, 0.f);

  if (shadows > 1.f) {
    float raised = x * (1.f + safeDivision(base_term, pow(ratio, shadows), 0.f));
    float reference = x * (1.f + base_scale);
    return max(x, x + (raised - reference));
  } else {  // shadows < 1.f
    float lowered = x * (1.f - safeDivision(base_term, pow(ratio, 2.f - shadows), 0.f));
    float reference = x * (1.f - base_scale);
    return clamp(x + (lowered - reference), 0.f, x);
  }
}

float ContrastAndFlare(float x, float contrast, float contrast_highlights, float contrast_shadows, float flare, float mid_gray = 0.18f) {
  if (contrast == 1.f && flare == 0.f && contrast_highlights == 1.f && contrast_shadows == 1.f) return x;

  const float x_normalized = x / mid_gray;

  const float split_contrast = Select(x < mid_gray, contrast_shadows, contrast_highlights).x;
  float flare_ratio = safeDivision(x_normalized + flare, x_normalized, 1.f);
  float exponent = contrast * split_contrast * flare_ratio;
  return pow(x_normalized, exponent) * mid_gray;
}

float3 ApplyLuminosityGrading(float3 untonemapped, float lum, CB::UserGradingConfig config, float mid_gray = 0.18f) {
  if (config.Exposure == 1.f && config.Shadows == 1.f && config.Highlights == 1.f && config.Contrast == 1.f
      && config.HighlightContrast == 1.f && config.ShadowContrast == 1.f && config.Flare == 0.f && config.Gamma == 1.f) {
    return untonemapped;
  }
  float3 color = untonemapped;

  color *= config.Exposure;
  // gamma
  float lum_gamma_adjusted = Select(lum < 1.f, pow(lum, config.Gamma), lum).x;

  // contrast & flare
  const float lum_contrasted = ContrastAndFlare(lum_gamma_adjusted, config.Contrast, config.HighlightContrast, config.ShadowContrast, config.Flare, mid_gray);

  // highlights
  float lum_highlighted = Highlights(lum_contrasted, config.Highlights, mid_gray);

  // shadows
  float lum_shadowed = Shadows(lum_highlighted, config.Shadows, mid_gray);

  const float lum_final = lum_shadowed;

  color = CorrectLuminance(color, lum, lum_final);

  return color;
}

float3 ApplyHueAndPurityGrading(
    float3 ungraded_bt2020,
    float3 reference_bt2020,
    float lum,
    CB::UserGradingConfig config,
    float curve_gamma = 1.f,
    float2 mb_white_override = float2(-1.f, -1.f),
    float t_min = 1e-7f) {
  float3 color_bt2020 = ungraded_bt2020;
  if (config.Saturation == 1.f && config.Dechroma == 0.f && config.HueEmulation == 0.f && config.PurityEmulation == 0.f && config.HighlightSaturation == 0.f) {
    return color_bt2020;
  }

  const float kNearWhiteEpsilon = renodx_custom::color::macleod_boynton::MB_NEAR_WHITE_EPSILON;
  const float2 white = (mb_white_override.x >= 0.f && mb_white_override.y >= 0.f)
                           ? mb_white_override
                           : renodx_custom::color::macleod_boynton::MB_White_D65();

  float color_purity01 = renodx_custom::color::macleod_boynton::ApplyBT2020(
                             color_bt2020, 1.f, 1.f, mb_white_override, t_min)
                             .purityCur01;

  // MB hue + purity emulation (analog of OkLab hue/chrominance section).
  if (config.HueEmulation != 0.f || config.PurityEmulation != 0.f) {
    float reference_purity01 = renodx_custom::color::macleod_boynton::ApplyBT2020(
                                   reference_bt2020, 1.f, 1.f, mb_white_override, t_min)
                                   .purityCur01;

    float purity_current = color_purity01;
    float purity_ratio = 1.f;
    float3 hue_seed_bt2020 = color_bt2020;

    if (config.HueEmulation != 0.f) {
      float3 target_lms = mul(renodx_custom::color::macleod_boynton::XYZ_TO_LMS_2006,
                              mul(BT2020_2_XYZ, color_bt2020));
      float3 reference_lms = mul(renodx_custom::color::macleod_boynton::XYZ_TO_LMS_2006,
                                 mul(BT2020_2_XYZ, reference_bt2020));

      float target_t = target_lms.x + target_lms.y;
      if (target_t > t_min) {
        float2 target_direction = renodx_custom::color::macleod_boynton::MB_From_LMS(target_lms) - white;
        float2 reference_direction = renodx_custom::color::macleod_boynton::MB_From_LMS(reference_lms) - white;

        float target_len_sq = dot(target_direction, target_direction);
        float reference_len_sq = dot(reference_direction, reference_direction);

        if (target_len_sq > kNearWhiteEpsilon || reference_len_sq > kNearWhiteEpsilon) {
          float2 target_unit = (target_len_sq > kNearWhiteEpsilon)
                                   ? target_direction * rsqrt(target_len_sq)
                                   : float2(0.f, 0.f);
          float2 reference_unit = (reference_len_sq > kNearWhiteEpsilon)
                                      ? reference_direction * rsqrt(reference_len_sq)
                                      : target_unit;

          if (target_len_sq <= kNearWhiteEpsilon) {
            target_unit = reference_unit;
          }

          float2 blended_unit = lerp(target_unit, reference_unit, config.HueEmulation);
          float blended_len_sq = dot(blended_unit, blended_unit);
          if (blended_len_sq <= kNearWhiteEpsilon) {
            blended_unit = (config.HueEmulation >= 0.5f) ? reference_unit : target_unit;
            blended_len_sq = dot(blended_unit, blended_unit);
          }
          blended_unit *= rsqrt(max(blended_len_sq, 1e-20f));

          float seed_len = sqrt(max(target_len_sq, 0.f));
          if (seed_len <= 1e-6f) {
            seed_len = sqrt(max(reference_len_sq, 0.f));
          }
          seed_len = max(seed_len, 1e-6f);

          hue_seed_bt2020 = mul(
              XYZ_2_BT2020,
              mul(renodx_custom::color::macleod_boynton::LMS_TO_XYZ_2006,
                  renodx_custom::color::macleod_boynton::LMS_From_MB_T(white + blended_unit * seed_len, target_t)));

          float purity_post = renodx_custom::color::macleod_boynton::ApplyBT2020(
                                  hue_seed_bt2020, 1.f, 1.f, mb_white_override, t_min)
                                  .purityCur01;
          purity_ratio = safeDivision(purity_current, purity_post, 1.f);
          purity_current = purity_post;
        }
      }
    }

    if (config.PurityEmulation != 0.f) {
      float target_purity_ratio = safeDivision(reference_purity01, purity_current, 1.f);
      purity_ratio = lerp(purity_ratio, target_purity_ratio, config.PurityEmulation);
    }

    float applied_purity01 = saturate(purity_current * max(purity_ratio, 0.f));
    color_bt2020 = renodx_custom::color::macleod_boynton::ApplyBT2020(
                       hue_seed_bt2020, applied_purity01, curve_gamma, mb_white_override, t_min)
                       .rgbOut;
    color_purity01 = applied_purity01;
  }

  float purity_scale = 1.f;

  // dechroma
  if (config.Dechroma != 0.f) {
    purity_scale *= lerp(1.f, 0.f, saturate(pow(lum / (10000.f / 100.f), (1.f - config.Dechroma))));
  }

  // highlight saturation
  if (config.HighlightSaturation != 0.f) {
    float percent_max = saturate(lum * 100.f / 10000.f);
    // positive = 1 to 0, negative = 1 to 2
    float blowout_strength = 100.f;
    float blowout_change = pow(1.f - percent_max, blowout_strength * abs(config.HighlightSaturation));
    if (config.HighlightSaturation < 0) {
      blowout_change = (2.f - blowout_change);
    }

    purity_scale *= blowout_change;
  }

  // saturation
  purity_scale *= config.Saturation;

  if (purity_scale != 1.f) {
    float scaled_purity01 = saturate(color_purity01 * max(purity_scale, 0.f));
    color_bt2020 = renodx_custom::color::macleod_boynton::ApplyBT2020(
                       color_bt2020, scaled_purity01, curve_gamma, mb_white_override, t_min)
                       .rgbOut;
  }

  return color_bt2020;
}

float3 ApplyCustomGrading(float3 ungraded_bt2020) {
  float3 graded = ungraded_bt2020;

  const CB::UserGradingConfig cg_config = {
    LumaSettings.GameSettings.Exposure,                             // float exposure;
    LumaSettings.GameSettings.Highlights,                           // float highlights;
    LumaSettings.GameSettings.HighlightContrast,                   // float highlight_contrast;
    LumaSettings.GameSettings.Shadows,                              // float shadows;
    LumaSettings.GameSettings.ShadowContrast,                      // float shadow_contrast;
    LumaSettings.GameSettings.Contrast,                             // float contrast;
    0.10f * pow(LumaSettings.GameSettings.Flare, 10.f),             // float flare;
    LumaSettings.GameSettings.Gamma,                                // float gamma;
    LumaSettings.GameSettings.Saturation,                           // float saturation;
    LumaSettings.GameSettings.Dechroma,                             // float dechroma;
    -1.f * (LumaSettings.GameSettings.HighlightSaturation - 1.f),  // float highlight_saturation;
    0.f,                                                  // float hue_emulation;
    0.f                                                   // float purity_emulation;
  };

// const CB::UserGradingConfig cg_config = {
//     1.f,                             // float exposure;
//     1.f,                             // float highlights;
//     1.f,                   // float highlight_contrast;
//     1.f,                              // float shadows;
//     1.f,                      // float shadow_contrast;
//     1.f,                             // float contrast;
//     0.10f * pow(0.f, 10.f),             // float flare;
//     1.f,                                // float gamma;
//     1.f,                           // float saturation;
//     0.2f,                             // float dechroma;
//     -1.f * (1.f - 1.f),  // float highlight_saturation;
//     0.f,                                                  // float hue_emulation;
//     0.f                                                   // float purity_emulation;
//   };

  float luminosity = LuminosityFromBT2020LuminanceNormalized(ungraded_bt2020);
  float3 graded_bt2020 = ApplyLuminosityGrading(ungraded_bt2020, luminosity, cg_config, 0.1f);
  graded_bt2020 = ApplyHueAndPurityGrading(graded_bt2020, ungraded_bt2020, luminosity, cg_config);

  graded_bt2020 = max(0, graded_bt2020);
  return graded_bt2020;
}

float3 ApplyPsychoTest11(float3 color_bt709) {
  // float3 psychotm_test11(
//     float3 bt709_linear_input,
//     float peak_value = 1000.f / 203.f,
//     float exposure = 1.f,
//     float highlights = 1.f,
//     float shadows = 1.f,
//     float contrast = 1.f,
//     float purity_scale = 1.f,
//     float bleaching_intensity = 0.f,
//     float clip_point = 100.f,
//     float hue_restore = 1.f,
//     float adaptation_contrast = 1.f,
//     int white_curve_mode = 0,
//     float cone_response_exponent = 1.f)
  return renodx::tonemap::psycho::psychotm_test11(
    color_bt709,
    LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits,
    1.f, // exposure is handled by the separate exposure function
    1.f, // config.highlights,
    1.f, // config.shadows,
    1.f, // config.contrast,
    1.0f, // purity_scale
    0.f, // bleaching_intensity
    100.f, // clip_point
    1.f, // hue_restore
    1.f, // adaptation_contrast
    1, // white_curve_mode
    1.f // cone_response_exponent
  );
}

float3 ApplyGammaCorrection(float3 color_input) {
  float3 color_corrected;
  if (LumaSettings.GameSettings.Gamma == 1.f) {
    color_corrected = CorrectGamma(color_input);
  } else if (LumaSettings.GameSettings.Gamma == 2.f) {
    float y_in = LuminosityFromBT709LuminanceNormalized(color_input);
    float y_out = CorrectGamma1(max(0, y_in));
    float3 color_corrected_lum = CorrectLuminance(color_input, y_in, y_out);

    float3 color_corrected_ch = CorrectGamma(color_input);

    color_corrected = CorrectPurityMBBT709WithBT2020(color_corrected_lum, color_corrected_ch, 1.f);
  } else {
    color_corrected = color_input;
  }

  return color_corrected;
}

float4 GenerateOutput(float3 color_bt709, float diffuse_nits, float peak_nits) {
//   color_bt709 = renodx::color::bt709::clamp::AP1(color_bt709);

  if (LumaSettings.GameSettings.Gamma != 0.f) {
    color_bt709 = CorrectGamma(color_bt709);
  }

//   float3 color_bt2020 = BT709ToBT2020(color_bt709);

//   float3 color_pq = renodx::color::pq::EncodeSafe(color_bt2020, RENODX_GRAPHICS_WHITE_NITS);

//   return float4(color_pq, 1.f);
    return float4(color_bt709, 1.f);
}

float3 Uncharted2Extended(float3 color) {
  float A = 0.15, B = 0.5, C = 0.1, D = 1.0, E = 0.004, F = 0.06;
  float W = 1.37906432;

  float coeffs[6] = { A, B, C, D, E, F };
  float white_precompute = W;

  // Uncharted2::Config::Uncharted2ExtendedConfig uc2_config = Uncharted2::Config::CreateUncharted2ExtendedConfig(coeffs, white_precompute);

  // float3 outputColor = Uncharted2::ApplyExtended(color, uc2_config);
  float3 originalColor;
  float3 outputColor = Uncharted2::Tonemap_Uncharted2_Extended(color, false, originalColor, 1, 0.f, 1, white_precompute, coeffs[0], coeffs[1], coeffs[2], coeffs[3], coeffs[4], coeffs[5]);

  return outputColor;
}

float3 ApplyUserGradingAndToneMap(float3 color_bt709, float2 grain_uv) {
//   if (RENODX_TONE_MAP_TYPE == 0.f) return color_bt709;
  // return color_bt709;
  // color_bt709 = ApplyGammaCorrection(color_bt709);
// if (LumaSettings.DevSetting01 == 0.f)
{
  // blow out and hue shift
  float3 purity_and_hue_source = ApplyVanillaToneMap(color_bt709);
  color_bt709 = Uncharted2Extended(color_bt709);
  // color_bt709 = RestoreHueAndChrominance(color_bt709, purity_and_hue_source, 0.75f, 0.f);
    // color_bt709 = BT2020_To_BT709(renodx::tonemap::psycho::psycho11_RestoreHueBT2020(BT709_To_BT2020(purity_and_hue_source), BT709_To_BT2020(color_bt709), 0.5f));
    color_bt709 = CorrectHueAndPurityMBBT709WithBT2020(color_bt709, purity_and_hue_source, 0.75f, 0.f);
  

  float3 color_bt2020 = BT709_To_BT2020(color_bt709);
  color_bt2020 = ApplyCustomGrading(color_bt2020);
  color_bt2020 = renodx::tonemap::neutwo::MaxChannel(color_bt2020, LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits);
  // color_bt2020 = renodx::tonemap::neutwo::PerChannel(color_bt2020, LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits);
  color_bt709 = BT2020_To_BT709(color_bt2020);

//   color_bt709 = renodx::effects::ApplyFilmGrain(
//       color_bt709,
//       grain_uv,
//       CUSTOM_RANDOM,
//       CUSTOM_GRAIN_STRENGTH * 0.06f);

}
#if 0
{
  float3 purity_and_hue_source_bt2020 = BT709_To_BT2020(ApplyVanillaToneMap(color_bt709));
  color_bt709 = Uncharted2Extended(color_bt709);
  float3 color_bt2020 = BT709_To_BT2020(color_bt709);
  color_bt2020 = renodx::tonemap::psycho::psycho11_RestoreHueBT2020(purity_and_hue_source_bt2020, color_bt2020, LumaSettings.DevSetting02);
  color_bt709 = BT2020_To_BT709(color_bt2020);
  color_bt709 = ApplyPsychoTest11(color_bt709);
  
}
#endif

  // if (LumaSettings.GameSettings.gamma != 0.f) {
  //   color_bt709 = CorrectGamma(color_bt709, true);
  // }

  // output as gamma so that ui can blend in gamma space, matching vanilla behavior. This is undone by Luma display composition shader.

// #if TONEMAP_AFTER_TAA
// #if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
// 	ColorGradingLUTTransferFunctionInOutCorrected(color_bt709.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
//   color_bt709.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
// 	ColorGradingLUTTransferFunctionInOutCorrected(color_bt709.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
// #endif
//   color_bt709 = linear_to_sRGB_gamma(color_bt709, GCT_MIRROR);
// #else
//   [branch]
//   if (LumaSettings.SRType == 0){
// #if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
// 	ColorGradingLUTTransferFunctionInOutCorrected(color_bt709.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
//   color_bt709.rgb *= LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits;
// 	ColorGradingLUTTransferFunctionInOutCorrected(color_bt709.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
// #endif
// #endif
//   }
  color_bt709 = linear_to_sRGB_gamma(color_bt709, GCT_MIRROR);

  return color_bt709;
}
#endif // TONEMAP_HLSLI
