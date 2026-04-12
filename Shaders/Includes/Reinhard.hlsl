#include "Color.hlsl"

// Erik Reinhard, Michael Stark, Peter Shirley, and James Ferwerda.
// "Photographic Tone Reproduction for Digital Images."
// ACM Transactions on Graphics (SIGGRAPH), 2002.
namespace Reinhard 
{
  float ReinhardSimple(float x, float peak = 1.0)
  {
    return x / ((abs(x) / peak) + 1.0);
  }

  float3 ReinhardSimple(float3 x, float peak = 1.0)
  {
    return x / ((abs(x) / peak) + 1.0);
  }
  
  // Compresses the range from "ShoulderStart" to "In_Peak", onto "ShoulderStart" to "Out_Peak".
  // If "In_Peak" is <= 0, it will then start compressing from "infinite".
  // 
  // This is useful to compress back an HDR display mapped image to SDR, or any other color that needs to left intact below a certain threshold, and has a peak value different from ~infinite.
  // 
  // Don't pre-offset inputs by the shoulder start.
  // TODO: this can actually cause the output color to be greater if the in peak was low, so maybe prevent that?
  float3 ReinhardRange(float3 Color, float ShoulderStart = MidGray, float In_Peak = -1.0, float Out_Peak = 1.0, bool ClampOutput = false)
  {
    const float3 compressableColor = Color - ShoulderStart;
    const float compressableRange = In_Peak - ShoulderStart;
    const float compressedRange = Out_Peak - ShoulderStart;

    const float3 compressedColor = ReinhardSimple(compressableColor, compressedRange); // This will be between 0 and "compressedRange"
    float3 restoreRangeCompressedColor = compressedColor;
    // Restore the "lost" range from the peak
    if (In_Peak > 0.0)
    {
      const float restoreRangeScale = ReinhardSimple(FLT16_MAX - ShoulderStart, compressedRange) / ReinhardSimple(compressableRange, compressedRange); // Use "FLT16_MAX" instead of "FLT_MAX" as that one gives issue, the difference is ~0 anyway
      restoreRangeCompressedColor *= restoreRangeScale;
    }

    float3 possibleOutValue = ShoulderStart + restoreRangeCompressedColor; // This will be between "ShoulderStart" and "Out_Peak" (possibly going beyond it)
    if (ClampOutput) // Optionally clamp as it could be beyond "Out_Peak" if the input was beyond "In_Peak"
    {
      possibleOutValue = min(possibleOutValue, Out_Peak);
      Color = max(Color, 0.0);
    }
    return (Color <= ShoulderStart) ? Color : possibleOutValue;
  }
  
  // This converts a color compressed with Reinhard with a white level to another white level
  // (it's a shortcut to avoid having to do inverse tonemapping and then tonemapping again with a different white level)
  float3 ReinhardRebalancePerComponent(float3 L, float L_oldWhite, float L_newWhite, uint clampType = 0)
  {
    float3 L_negative = 0.0;
    if (clampType >= 1)
    {
      L_negative = min(L, 0.0);
      L = max(L, 0.0);
    }
    L *= (1.0 + (L / sqr(L_newWhite))) / (1.0 + (L / sqr(L_oldWhite)));
    if (clampType == 1 )
    {
      L += L_negative;
    }
    else if (clampType >= 2)
    {
      L = saturate(L);
    }
    return L;
  }

  float ReinhardExtended(float color, float white_max = 1000.f / ITU_WhiteLevelNits, float peak = 1.f) {
    return ReinhardSimple(color, peak) * (1.f + (peak * color) / (white_max * white_max));
  }

  float3 ReinhardExtended(float3 color, float white_max = 1000.f / ITU_WhiteLevelNits, float peak = 1.f) {
    return ReinhardSimple(color, peak) * (1.f + (peak * color) / (white_max * white_max));
  }

  float ComputeReinhardScale(float channel_max = 1.f, float channel_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
    return (channel_max * (channel_min * gray_out + channel_min - gray_out))
          / (gray_in * (gray_out - channel_max));
  }

  float ReinhardScalable(float x, float x_max = 1.f, float x_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
    float exposure = ComputeReinhardScale(x_max, x_min, gray_in, gray_out);
    return mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
  }

  float3 ReinhardScalable(float3 x, float x_max = 1.f, float x_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
    float exposure = ComputeReinhardScale(x_max, x_min, gray_in, gray_out);
    return mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
  }

  float ComputeReinhardExtendableScale(float w = 100.f, float p = 1.f, float m = 0.f, float x = MidGray, float y = MidGray) {
    // y = (sx / (sx/p + 1) * (1 + (psx)/(sw*sw))
    // solve for s (scale)
    // Min not currently supported
    return p * (w * w * y - (p * x * x)) / (w * w * x * (p - y));
  }

  float ReinhardScalableExtended(float x, float white_max = 100.f, float x_max = 1.f, float x_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
    float exposure = ComputeReinhardExtendableScale(white_max, x_max, x_min, gray_in, gray_out);
    float extended = ReinhardExtended(x * exposure, white_max * exposure, x_max);
    return min(extended, x_max);
  }

  float3 ReinhardScalableExtended(float3 x, float white_max = 100.f, float x_max = 1.f, float x_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
    float exposure = ComputeReinhardExtendableScale(white_max, x_max, x_min, gray_in, gray_out);
    float3 extended = ReinhardExtended(x * exposure, white_max * exposure, x_max);
    return min(extended, x_max);
  }
}

struct ReinhardSettings
{
  float mid_grey_value;
  float mid_grey_nits;
  float white_clip;
  float reference_white;
  bool by_luminance;
};

ReinhardSettings DefaultReinhardSettings()
{
  ReinhardSettings settings;
  settings.mid_grey_value = MidGray;
  settings.mid_grey_nits = 18.f;
  settings.white_clip = 100.f;
  settings.reference_white = 100.f;
  settings.by_luminance = true;
  return settings;
}

// TODO: align with other tonemappers mode and add call to Tonemap.hlsl. Also add Lottes?
float3 ReinhardTonemap(float3 color, float peak_nits, float diffuse_nits, ReinhardSettings settings)
{
    // this equation should also involve reference white 
    float peak = peak_nits / diffuse_nits; 

    if (settings.by_luminance)
    {
      float y = GetLuminance(color, CS_BT709);
      float peak = peak_nits / diffuse_nits; 

      float3 y_new = Reinhard::ReinhardScalableExtended(
            y,
            settings.white_clip,
            peak,
            0.f,
            settings.mid_grey_value,
            settings.mid_grey_nits / settings.reference_white);

      return color * (y > 0 ? (y_new / y) : 0);
    }
    else
    {
      float3 color_output = abs(color);
      
      color_output = Reinhard::ReinhardScalableExtended(
          color_output,
          settings.white_clip,
          peak,
          0,
          settings.mid_grey_value,
          settings.mid_grey_nits / settings.reference_white);

      color_output *= Sign_Fast(color);

      return color_output;
    };
}
