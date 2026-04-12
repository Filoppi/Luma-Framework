#include "renodx/tonemap.hlsl"
#include "renodx/effects.hlsl"
#include "renodx/hermite_spline.hlsl"
#include "renodx/neutwo.hlsl"
#include "../../Includes/Color.hlsl"
#include "../../Includes/Oklab.hlsl"
#include "../../Includes/Reinhard.hlsl"

float UpgradeToneMapRatio(float ap1_color_hdr, float ap1_color_sdr, float ap1_post_process_color) {
  if (ap1_color_hdr < ap1_color_sdr) {
    // If substracting (user contrast or paperwhite) scale down instead
    // Should only apply on mismatched HDR
    return ap1_color_hdr / ap1_color_sdr;
  } else {
    float ap1_delta = ap1_color_hdr - ap1_color_sdr;
    ap1_delta = max(0, ap1_delta);  // Cleans up NaN
    const float ap1_new = ap1_post_process_color + ap1_delta;

    const bool ap1_valid = (ap1_post_process_color > 0);  // Cleans up NaN and ignore black
    return ap1_valid ? (ap1_new / ap1_post_process_color) : 0;
  }
}

// Restores the source color hue (and optionally brightness) through Oklab (this works on colors beyond SDR in brightness and gamut too).
// The strength sweet spot for a strong hue restoration seems to be 0.75, while for chrominance, going up to 1 is ok.
float3 RestoreHueAndChrominance2(float3 targetColor, float3 sourceColor, float hueStrength = 0.75, float chrominanceStrength = 1.0, float minChrominanceChange = 0.0, float maxChrominanceChange = FLT_MAX, float lightnessStrength = 0.0, float clampChrominanceLoss = 0.0, bool useRamp = false, float rampStart = 0.5f, float rampEnd = 1.0f) {
    if (hueStrength == 0.0 && chrominanceStrength == 0.0 && lightnessStrength == 0.0) // Static optimization (useful if the param is const)
        return targetColor;

    // Invalid or black colors fail oklab conversions or ab blending so early out
    if (GetLuminance(targetColor) <= FLT_MIN)
        return targetColor; // Optionally we could blend the target towards the source, or towards black, but there's no need until proven otherwise

    const float3 sourceOklab = Oklab::linear_srgb_to_oklab(sourceColor);
    float3 targetOklab = Oklab::linear_srgb_to_oklab(targetColor);

    targetOklab.x = lerp(targetOklab.x, sourceOklab.x, lightnessStrength); // TODOFT5: the alt method was used by Bioshock 2, did it make sense? Should it be here?

    float currentChrominance = length(targetOklab.yz);

    if (hueStrength != 0.0) {
        // First correct both hue and chrominance at the same time (oklab a and b determine both, they are the color xy coordinates basically).
        // As long as we don't restore the hue to a 100% (which should be avoided?), this will always work perfectly even if the source color is pure white (or black, any "hueless" and "chromaless" color).
        // This method also works on white source colors because the center of the oklab ab diagram is a "white hue", thus we'd simply blend towards white (but never flipping beyond it (e.g. from positive to negative coordinates)),
        // and then restore the original chrominance later (white still conserving the original hue direction, so likely spitting out the same color as the original, or one very close to it).
        const float chrominancePre = currentChrominance;
        targetOklab.yz = lerp(targetOklab.yz, sourceOklab.yz, hueStrength);
        const float chrominancePost = length(targetOklab.yz);
        // Then restore chrominance to the original one
        float chrominanceRatio = safeDivision(chrominancePre, chrominancePost, 1);
        targetOklab.yz *= chrominanceRatio;
        // currentChrominance = chrominancePre; // Redundant
    }

    if (chrominanceStrength != 0.0) {
        const float sourceChrominance = length(sourceOklab.yz);
        // Scale original chroma vector from 1.0 to ratio of target to new chroma
        // Note that this might either reduce or increase the chroma.
        float targetChrominanceRatio = safeDivision(sourceChrominance, currentChrominance, 1);
        // Optional safe boundaries (0.333x to 2x is a decent range)
        targetChrominanceRatio = clamp(targetChrominanceRatio, minChrominanceChange, maxChrominanceChange);
        float chromaScale = lerp(1.0, targetChrominanceRatio, chrominanceStrength);

        if (clampChrominanceLoss > 0.0) {
            float needsClamp = 1.0f - step(1.0f, chromaScale); // 1 when chromaScale < 1
            float ramp = 1.0;
            if (useRamp == true) {
                ramp = saturate((targetOklab.x - rampStart) / (rampEnd - rampStart));
            }
            float clamp_strength = clampChrominanceLoss * ramp;
            chromaScale = lerp(chromaScale, 1.0, needsClamp * clamp_strength);
        }

        targetOklab.yz *= chromaScale;
    }

    return Oklab::oklab_to_linear_srgb(targetOklab);
}

float3 UpgradeToneMapByLuminance(float3 color_hdr, float3 color_sdr, float3 post_process_color, float post_process_strength) {
  float3 bt2020_hdr = max(0, BT709_To_BT2020(color_hdr));
  float3 bt2020_sdr = max(0, BT709_To_BT2020(color_sdr));
  float3 bt2020_post_process = max(0, BT709_To_BT2020(post_process_color));

  float ratio = UpgradeToneMapRatio(
      GetLuminance(bt2020_hdr, CS_BT2020),
      GetLuminance(bt2020_sdr, CS_BT2020),
      GetLuminance(bt2020_post_process, CS_BT2020));

  float3 color_scaled = max(0, bt2020_post_process * ratio);
  color_scaled = BT2020_To_BT709(color_scaled);
  color_scaled = RestoreHueAndChrominance(color_scaled, post_process_color, 1.f, 0.f);
  return lerp(color_hdr, color_scaled, post_process_strength);
}

float3 UpgradeToneMapPerChannel(float3 color_hdr, float3 color_sdr, float3 post_process_color, float post_process_strength) {
  float3 ap1_hdr = max(0, BT709_To_AP1(color_hdr));
  float3 ap1_sdr = max(0, BT709_To_AP1(color_sdr));
  float3 ap1_post_process = max(0, BT709_To_AP1(post_process_color));

  float3 ratio = float3(
    UpgradeToneMapRatio(ap1_hdr.r, ap1_sdr.r, ap1_post_process.r),
    UpgradeToneMapRatio(ap1_hdr.g, ap1_sdr.g, ap1_post_process.g),
    UpgradeToneMapRatio(ap1_hdr.b, ap1_sdr.b, ap1_post_process.b));

  float3 color_scaled = max(0, ap1_post_process * ratio);
  color_scaled = AP1_To_BT709(color_scaled);
  float peak_correction = saturate(1.f - GetLuminance(ap1_post_process, CS_AP1));
  color_scaled = RestoreHueAndChrominance(color_scaled, post_process_color, peak_correction, 0.75f);
  return lerp(color_hdr, color_scaled, post_process_strength);
}

float3 applyACES(float3 untonemapped, float midGray = 0.1f, float peak_nits = 1000.f, float game_nits = 250.f) {
  renodx::tonemap::Config aces_config = renodx::tonemap::config::Create();
  aces_config.peak_nits = peak_nits;
  aces_config.game_nits = game_nits;
  aces_config.type = 2u;
  aces_config.mid_gray_value = midGray;
  aces_config.mid_gray_nits = midGray * 100.f;
  aces_config.gamma_correction = 0;
  return renodx::tonemap::config::ApplyACES(untonemapped, aces_config);
}

float3 applyReferenceACES(float3 untonemapped, float midGray = 0.1f) {
  return applyACES(untonemapped, midGray, 1000.f, 250.f);
}

struct ColorGradeConfig {
  float exposure;
  float highlights;
  float shadows;
  float contrast;
  float flare;
  float saturation;
  float dechroma;
  float hue_correction_strength;
  float3 hue_correction_source;
  float hue_correction_type; // 0 = input, 1 = output
  float blowout;
};

ColorGradeConfig DefaultColorGradeConfig() {
    ColorGradeConfig config;
    config.exposure = 1.f;
    config.contrast = 1.f;
    config.flare = 0.f;
    config.highlights = 1.f;
    config.shadows = 1.f;
    config.saturation = 1.f;
    config.dechroma = 0.f;
    config.hue_correction_strength = 0.f;
    config.hue_correction_source = 0;
    config.hue_correction_type = 0.f;
    config.blowout = 0.f;
    return config;
}

float Highlights(float x, float highlights, float mid_gray) {
    if (highlights == 1.f) return x;

    if (highlights > 1.f) {
        return max(x, lerp(x, mid_gray * pow(x / mid_gray, highlights), min(x, 5.f)));
    } else { // highlights < 1.f
        x /= mid_gray;
        return lerp(x, pow(x, highlights), step(1.f, x)) * mid_gray;
    }
}

float Shadows(float x, float shadows, float mid_gray) {
    if (shadows == 1.f) return x;

    const float ratio = max(safeDivision(x, mid_gray, 0), 0.f);
    const float base_term = x * mid_gray;
    const float base_scale = safeDivision(base_term, ratio, 0);

    if (shadows > 1.f) {
        float raised = x * (1.f + safeDivision(base_term, pow(ratio, shadows), 0));
        float reference = x * (1.f + base_scale);
        return max(x, x + (raised - reference));
    } else { // shadows < 1.f
        float lowered = x * (1.f - safeDivision(base_term, pow(ratio, 2.f - shadows), 0));
        float reference = x * (1.f - base_scale);
        return clamp(x + (lowered - reference), 0.f, x);
    }
}

float3 ApplyExposureContrastFlareHighlightsShadowsByLuminance(float3 untonemapped, float y, ColorGradeConfig config, float mid_gray = 0.18f) {
    if (config.exposure == 1.f && config.shadows == 1.f && config.highlights == 1.f && config.contrast == 1.f && config.flare == 0.f) {
        return untonemapped;
    }
    float3 color = untonemapped;

    color *= config.exposure;
    y *= config.exposure;

    // contrast & flare
    const float y_normalized = y / mid_gray;
    float flare = safeDivision(y_normalized + config.flare, y_normalized, 1);
    float exponent = config.contrast * flare;
    const float y_contrasted = pow(y_normalized, exponent) * mid_gray;

    // highlights
    float y_highlighted = Highlights(y_contrasted, config.highlights, mid_gray);

    // shadows
    float y_shadowed = Shadows(y_highlighted, config.shadows, mid_gray);

    const float y_final = y_shadowed;

    color = RestoreLuminance(color, y_final);

    return color;
}

float3 ApplySaturationBlowoutHueCorrectionHighlightSaturation(float3 tonemapped, float3 hue_reference_color, float y, ColorGradeConfig config) {
    float3 color = tonemapped;
    if (config.saturation != 1.f || config.dechroma != 0.f || config.hue_correction_strength != 0.f || config.blowout != 0.f) {
        float3 perceptual_new = Oklab::linear_srgb_to_oklab(color);

        if (config.hue_correction_strength != 0.f) {
            float3 perceptual_old = Oklab::linear_srgb_to_oklab(hue_reference_color);

            // Save chrominance to apply black
            float chrominance_pre_adjust = distance(perceptual_new.yz, 0);

            perceptual_new.yz = lerp(perceptual_new.yz, perceptual_old.yz, config.hue_correction_strength);

            float chrominance_post_adjust = distance(perceptual_new.yz, 0);

            // Apply back previous chrominance
            perceptual_new.yz *= safeDivision(chrominance_pre_adjust, chrominance_post_adjust, 1);
        }

        if (config.dechroma != 0.f) {
            perceptual_new.yz *= lerp(1.f, 0.f, saturate(pow(y / (10000.f / 100.f), (1.f - config.dechroma))));
        }

        if (config.blowout != 0.f) {
            float percent_max = saturate(y * 100.f / 10000.f);
            // positive = 1 to 0, negative = 1 to 2
            float blowout_strength = 100.f;
            float blowout_change = pow(1.f - percent_max, blowout_strength * abs(config.blowout));
            if (config.blowout < 0) {
                blowout_change = (2.f - blowout_change);
            }

            perceptual_new.yz *= blowout_change;
        }

        perceptual_new.yz *= config.saturation;

        color = Oklab::oklab_to_linear_srgb(perceptual_new);

        // color = renodx::color::bt709::clamp::AP1(color);
    }
    return color;
}

float3 ApplyHermiteSplineByMaxChannel(float3 input, float diffuse_nits, float peak_nits) {
  const float peak_ratio = peak_nits / diffuse_nits;
  float white_clip = max(100.f, peak_ratio * 1.5f);

  float max_channel = max3(input.r, input.g, input.b);
  float max_pq = Linear_to_PQ(max_channel * (diffuse_nits / HDR10_MaxWhiteNits));
  float target_white_pq = Linear_to_PQ(peak_nits * (1.f / HDR10_MaxWhiteNits));
  float max_white_pq = Linear_to_PQ(white_clip * (diffuse_nits / HDR10_MaxWhiteNits));
  float target_black_pq = Linear_to_PQ(0.0001f * (1.f / HDR10_MaxWhiteNits));
  float min_black_pq = Linear_to_PQ(0.f * (1.f / HDR10_MaxWhiteNits));

  float scaled_pq = HermiteSplineRolloff(max_pq, target_white_pq, max_white_pq, target_black_pq, min_black_pq);
  float mapped_max = PQ_to_Linear(scaled_pq) * (HDR10_MaxWhiteNits / diffuse_nits);
  mapped_max = min(mapped_max, peak_ratio);
  float scale = safeDivision(mapped_max, max_channel, 0);
  return input * scale; 
}

float3 extractColorGradeAndApplyTonemap(float3 ungraded_bt709, float3 lutOutputColor_bt2020, float midGray, float2 position) {
    // normalize LUT output paper white and convert to BT.709
    // ungraded_bt709 = ungraded_bt709 * 1.5f;
  if (LumaSettings.GameSettings.tonemap_type == 0.f) {
    return lutOutputColor_bt2020;
  }
  float3 graded_aces_bt709 = BT2020_To_BT709(lutOutputColor_bt2020  / 250.f );

  float ACES_MIN;
  float aces_min;
  float aces_max;
  float3 tonemapped_bt709;
  float3 pq_color;

  ColorGradeConfig cg_config = DefaultColorGradeConfig();
  cg_config.exposure = LumaSettings.GameSettings.exposure;
  cg_config.highlights = LumaSettings.GameSettings.highlights;
  cg_config.shadows = LumaSettings.GameSettings.shadows;
  cg_config.contrast = LumaSettings.GameSettings.contrast;
  cg_config.flare = 0.10f * pow(LumaSettings.GameSettings.flare, 10.f);
  cg_config.saturation = LumaSettings.GameSettings.saturation;
  cg_config.dechroma = LumaSettings.GameSettings.blowout;
  cg_config.hue_correction_strength = LumaSettings.GameSettings.hue_correction_strength;
  cg_config.blowout = -1.f * (LumaSettings.GameSettings.highlight_saturation - 1.f);

  float3 reference_tonemap_bt709 = Reinhard::ReinhardScalable(ungraded_bt709, 1000.f / 250.f, 0.f, 0.18f, 0.18f);
  float3 graded_untonemapped_bt709 = UpgradeToneMapPerChannel(ungraded_bt709, reference_tonemap_bt709, graded_aces_bt709, 1.f);
  float y = GetLuminance(graded_untonemapped_bt709, CS_BT709);
  float3 graded_bt709 = ApplyExposureContrastFlareHighlightsShadowsByLuminance(graded_untonemapped_bt709, y, cg_config);
  graded_bt709 = ApplySaturationBlowoutHueCorrectionHighlightSaturation(graded_bt709, graded_aces_bt709, y, cg_config);
  tonemapped_bt709 = graded_bt709;
  if (LumaSettings.GameSettings.custom_lut_strength != 1.f) {
    tonemapped_bt709 = lerp(ungraded_bt709, tonemapped_bt709, LumaSettings.GameSettings.custom_lut_strength);
  }

  if (LumaSettings.GameSettings.custom_film_grain_strength != 0) {
    tonemapped_bt709 = renodx::effects::ApplyFilmGrain(
        tonemapped_bt709.rgb,
        position.xy,
        LumaSettings.GameSettings.custom_random,
        LumaSettings.GameSettings.custom_film_grain_strength * 0.03f,
        1.f);
  }

  float peak_ratio = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;

  tonemapped_bt709 = renodx::tonemap::neutwo::MaxChannel(tonemapped_bt709, peak_ratio);

  float3 tonemapped_bt2020 = BT709_To_BT2020(tonemapped_bt709);

//   tonemapped_bt2020 = ApplyHermiteSplineByMaxChannel(tonemapped_bt2020, LumaSettings.GamePaperWhiteNits, LumaSettings.PeakWhiteNits);

  return tonemapped_bt2020 * (LumaSettings.GamePaperWhiteNits);
  // return lutOutputColor_bt2020;
}