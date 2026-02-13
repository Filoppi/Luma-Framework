//From RenoDX clshortfuse
#include "../../Includes/Math.hlsl"
#include "../../Includes/Color.hlsl"
#include "../../Includes/JzAzBz.hlsl" //Orig is ICtCp. Close enough I guess.

struct ApplyPerChannelCorrectionResult {
  float3 color;
  float tonemapped_luminance;
};

ApplyPerChannelCorrectionResult ApplyPerChannelCorrectionInternal(
    float3 untonemapped,
    float3 per_channel_color,
    uint colorSpace = CS_DEFAULT,
    float blowout_restoration = 0.5f,
    float hue_correction_strength = 1.f,
    float chrominance_correction_strength = 1.f,
    float hue_shift_strength = 0.5f) {
  ApplyPerChannelCorrectionResult result;

  const float tonemapped_luminance = GetLuminance(per_channel_color, colorSpace);
  result.tonemapped_luminance = tonemapped_luminance;
  // if (tonemapped_luminance <= 0.000001f) {
  //   result.color = per_channel_color;
  //   return result;
  // }

  const float AUTO_CORRECT_BLACK = 0.02f;
  // Fix near black
  const float untonemapped_luminance = GetLuminance(untonemapped, colorSpace);
  float ratio = tonemapped_luminance / untonemapped_luminance;
  float auto_correct_ratio = lerp(ratio, 1.f, saturate(untonemapped_luminance / AUTO_CORRECT_BLACK));
  untonemapped *= auto_correct_ratio;

  const float3 tonemapped_perceptual = JzAzBz::rgbToJzazbz(per_channel_color, colorSpace);
  const float3 untonemapped_perceptual = JzAzBz::rgbToJzazbz(untonemapped);

  float2 untonemapped_chromas = untonemapped_perceptual.yz;
  float2 tonemapped_chromas = tonemapped_perceptual.yz;

  float untonemapped_chrominance = length(untonemapped_perceptual.yz);  // eg: 0.80
  float tonemapped_chrominance = length(tonemapped_perceptual.yz);      // eg: 0.20

  // clamp saturation loss

  float chrominance_ratio = min(safeDivision(tonemapped_chrominance, untonemapped_chrominance, 1.f), 1.f);
  chrominance_ratio = max(chrominance_ratio, blowout_restoration);

  // Untonemapped hue, tonemapped chrominance (with limit)
  float2 reduced_untonemapped_chromas = untonemapped_chromas * chrominance_ratio;

  // pick chroma based on per-channel luminance (supports not oversaturating crushed areas)
  const float2 reduced_hue_shifted = lerp(
      tonemapped_chromas,
      reduced_untonemapped_chromas,
      saturate(tonemapped_luminance / 0.36));

  // Tonemapped hue, restored chrominance (with limit)
  const float2 blowout_restored_chromas = tonemapped_chromas
                                          * safeDivision(
                                              length(reduced_hue_shifted),
                                              length(tonemapped_chromas), 1.f);

  const float2 hue_shifted_chromas = lerp(reduced_hue_shifted, blowout_restored_chromas, hue_shift_strength);

  // Pick untonemapped hues for shadows/midtones
  const float2 hue_correct_chromas = untonemapped_chromas
                                     * safeDivision(
                                         length(hue_shifted_chromas),
                                         length(untonemapped_chromas), 1.f);

  const float2 selectable_hue_correct_range = lerp(
      hue_correct_chromas,
      hue_shifted_chromas,
      saturate(tonemapped_luminance / 0.36f));

  const float2 hue_corrected_chromas = lerp(hue_shifted_chromas, selectable_hue_correct_range, hue_correction_strength);

  const float2 chroma_correct_chromas = hue_corrected_chromas
                                        * (length(untonemapped_chromas) /
                                        max(length(hue_corrected_chromas), 0.004f));

  const float2 selectable_chroma_correct_range = lerp(
      chroma_correct_chromas,
      hue_corrected_chromas,
      saturate(tonemapped_luminance / 0.36f));

  const float2 chroma_corrected_chromas = lerp(
      hue_correct_chromas,
      selectable_chroma_correct_range,
      chrominance_correction_strength);

  float2 final_chromas = chroma_corrected_chromas;

  const float3 final_color = JzAzBz::jzazbzToRgb(float3(
      tonemapped_perceptual.x,
      final_chromas));

  result.color = final_color;
  result.color = max(0, result.color);
  return result;
}

float3 ApplyPerChannelCorrection(
    float3 untonemapped,
    float3 per_channel_color,
    uint colorSpace = CS_DEFAULT,
    float blowout_restoration = 0.5f,
    float hue_correction_strength = 1.f,
    float chrominance_correction_strength = 1.f,
    float hue_shift_strength = 0.5f) {

    ApplyPerChannelCorrectionResult result = ApplyPerChannelCorrectionInternal(
        untonemapped,
        per_channel_color,
        colorSpace,
        blowout_restoration,
        hue_correction_strength,
        chrominance_correction_strength,
        hue_shift_strength);
    return result.color;
}

float3 ApplyPerChannelCorrectionHighlightOnly(
    float3 untonemapped,
    float3 per_channel_color,
    float strengthTotal, float strengthHightlightOnly,
    uint colorSpace = CS_DEFAULT,
    float blowout_restoration = 0.5f,
    float hue_correction_strength = 1.f,
    float chrominance_correction_strength = 1.f,
    float hue_shift_strength = 0.5f) {

   if (strengthTotal == 0) return per_channel_color;

    ApplyPerChannelCorrectionResult pccResult = ApplyPerChannelCorrectionInternal(untonemapped, per_channel_color, colorSpace, blowout_restoration, hue_correction_strength, chrominance_correction_strength, hue_shift_strength);

    float strength = pccResult.tonemapped_luminance;
    strength = pow(strength, strengthHightlightOnly);
    strength *= strengthTotal;
    strength = saturate(strength);

    per_channel_color = lerp(per_channel_color, pccResult.color, strength);

    return per_channel_color;
}