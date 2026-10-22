#include "GameCBuffers.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Common.hlsl"
#include "../Includes/Oklab.hlsl"

float3 SampleMetroLUT(Texture3D<float4> lut, SamplerState samplerState, float3 lutInput)
{
   const float3 lutSample = lut.SampleLevel(samplerState, lutInput.zyx, 0.f).xyz;
   return lutSample.zyx * 2.f + lutInput - 1.f;
}

float GetMetroLUTFadeToWhiteAmount(Texture3D<float4> lut, SamplerState samplerState)
{
   const float3 blackOutput = SampleMetroLUT(lut, samplerState, 0.f);
   const float raisedBlack = saturate(GetLuminance(max(blackOutput, 0.f)));

   // Ignore one 8-bit step near black and consider one step below white complete.
   // Range-collapse and chroma checks can prevent a real fade from ever reaching 1,
   // which leaves part of the HDR path visible at the white endpoint.
   const float quantizationTolerance = 1.f / 255.f;
   return saturate(
       (raisedBlack - quantizationTolerance) / (1.f - 2.f * quantizationTolerance));
}

float3 ApplyMetroLUTWithFadeToWhite(
    Texture3D<float4> lut,
    SamplerState samplerState,
    float3 color,
    float hdrScale)
{
   const float fadeToWhiteAmount = GetMetroLUTFadeToWhiteAmount(lut, samplerState);

   const float3 hdrInput = color * hdrScale;
   const float3 hdrOutput = safeDivision(
       SampleMetroLUT(lut, samplerState, hdrInput),
       hdrScale,
       2);

   const float3 vanillaInput = saturate(color);
   const float3 vanillaOutput = saturate(SampleMetroLUT(lut, samplerState, vanillaInput));

   return lerp(hdrOutput, vanillaOutput, fadeToWhiteAmount);
}

float CInfinityTransition(float position)
{
   position = saturate(position);
   return 1.f / (1.f + exp2((1.f - 2.f * position) / (position * (1.f - position))));
}

float3 CInfinityTransition(float3 position)
{
   position = saturate(position);
   return 1.f / (1.f + exp2((1.f - 2.f * position) / (position * (1.f - position))));
}

// C-infinity anchored tonal grading in the selected working space. The
// shoulder/rolloff stage is intentionally omitted; the caller applies its
// chosen display transform afterward.
float3 ApplyCInfinityGrading(
    float3 color,
    float3 anchor_in,
    float3 anchor_out,
    float contrast,
    float flare,
    float highlight_contrast,
    float shadow_contrast,
    float highlights,
    float shadows)
{
   if (contrast == 1.f && flare == 0.f && highlight_contrast == 1.f && shadow_contrast == 1.f && highlights == 1.f && shadows == 1.f && all(anchor_in == anchor_out))
   {
      return color;
   }

   const float3 normalized = abs(color) / anchor_in;
   float3 graded = normalized;

   if (contrast != 1.f || flare > 0.f)
   {
      float3 exponent = contrast;
      if (flare > 0.f)
      {
         const float3 shadow_distance = saturate(1.f - normalized);
         const float3 flat_shadow_weight = exp2(-normalized / shadow_distance);
         exponent *= 1.f + flat_shadow_weight * flare / (normalized + flare);
      }

      const float3 input_stops = log2(normalized);
      const float3 highlight_stops = max(input_stops, 0.f);
      float3 output_highlight_stops = highlight_stops;
      if (contrast != 1.f)
      {
         const float3 displacement = (contrast - 1.f) * highlight_stops;
         const float3 displacement_magnitude = abs(displacement);
         output_highlight_stops += displacement / (displacement_magnitude * exp2(-1.f / displacement_magnitude) + 1.f);
      }
      graded = exp2(exponent * min(input_stops, 0.f) + output_highlight_stops);
   }

   if (highlight_contrast != 1.f)
   {
      const float3 highlight_distance = max(graded - 1.f, 0.f);
      const float3 highlight_distance_squared = highlight_distance * highlight_distance;
      const float3 flat_highlight_distance =
          (1.f + highlight_distance_squared) * exp2(-1.f / highlight_distance_squared);
      graded += highlight_distance * (pow(1.f + flat_highlight_distance, 0.5f * (highlight_contrast - 1.f)) - 1.f);
   }

   if (shadow_contrast != 1.f)
   {
      const float3 shadow_distance = saturate(1.f - graded);
      const float3 shadow_distance_squared = shadow_distance * shadow_distance;
      const float3 flat_shadow_distance = shadow_distance_squared * shadow_distance * exp2(1.f - 1.f / shadow_distance_squared);
      graded *= pow(1.f + flat_shadow_distance, shadow_contrast - 1.f);
   }

   if (highlights != 1.f || shadows != 1.f)
   {
      const float3 tonal_stops = log2(graded);
      float3 tonal_displacement = 0.f;
      if (highlights != 1.f)
      {
         const float adjustment = highlights - 1.f;
         const float displacement = adjustment * (1.5f * abs(adjustment) + 0.5f);
         tonal_displacement += displacement * CInfinityTransition((tonal_stops - 1.f) * 0.125f);
      }
      if (shadows != 1.f)
      {
         const float adjustment = shadows - 1.f;
         const float displacement = adjustment * (1.5f * abs(adjustment) + 0.5f);
         tonal_displacement += displacement * CInfinityTransition((-1.f - tonal_stops) * 0.125f);
      }
      graded *= exp2(tonal_displacement);
   }

   return sign(color) * graded * anchor_out;
}

// Preserve the separate artistic saturation/blowout controls after tonemapping.
float3 ApplyMetroChromaControls(float3 color, float saturation, float dechroma, float blowout)
{
   const float y = GetLuminance(color);
   if (saturation == 1.f && dechroma == 0.f && blowout == 0.f)
      return color;

   float3 perceptual = Oklab::linear_srgb_to_oklab(color);
   if (dechroma != 0.f)
   {
      perceptual.yz *= lerp(1.f, 0.f, saturate(pow(y * 100.f / 10000.f, 1.f - dechroma)));
   }
   if (blowout != 0.f)
   {
      const float percent_max = saturate(y * 100.f / 10000.f);
      float blowout_change = pow(1.f - percent_max, 100.f * abs(blowout));
      if (blowout < 0.f)
         blowout_change = 2.f - blowout_change;
      perceptual.yz *= blowout_change;
   }
   perceptual.yz *= saturation;
   return Oklab::oklab_to_linear_srgb(perceptual);
}
