// #include "../Includes/Color.hlsl"

//from RenoDX clshortfuse
float RenoDX_Contrast(float x, float contrast, float mid_gray = 0.18f) {
  return pow(max(0, x / mid_gray), contrast) * mid_gray;
}
float3 RenoDX_Contrast(float3 x, float contrast, float mid_gray = 0.18f) {
  return pow(max(0, x / mid_gray), contrast) * mid_gray;
}
float RenoDX_Shadows(float x, float shadows, float mid_gray) {
  float value;
  if (shadows > 1.f) {
    value = max(x, x * (1.f + (x * mid_gray / pow(x / mid_gray, shadows))));
  } else if (shadows < 1.f) {
    value = clamp(x * (1.f - (x * mid_gray / pow(x / mid_gray, 2.f - shadows))), 0.f , x);
  } else {
    value = x;
  }
  return value;
}
float RenoDX_Highlights(float x, float highlights, float mid_gray) {
  float value;
  if (highlights > 1.f) {
    value = max(x, lerp(x, mid_gray * pow(x / mid_gray, highlights), x));
  } else if (highlights < 1.f) {
    value = min(x, x / (1.f + mid_gray * pow(x / mid_gray, 2.f - highlights) - x));
  } else {
    value = x;
  }
  return value;
}

float3 RenoDX_ColorGrade(
  float3 x, 
  float contrast = 1, float contrast_mid = 0.18f,
  float highlights = 1, float highlights_mid = 0.18f,
  float shadows = 1, float shadows_mid = 0.18f,
  float saturation = 1,
  uint colorspace = CS_DEFAULT,
  bool clampCs = false
) {
  float l = GetLuminance(x, colorspace);
  float lOrig = l;

  // Contrast
  l = RenoDX_Contrast(l, contrast, contrast_mid);

  // Highlights
  l = RenoDX_Highlights(l, highlights, highlights_mid);

  // Shadows
  l = RenoDX_Shadows(l, shadows, shadows_mid);

  x *= safeDivision(l, lOrig, 0);

  // Saturation
  if (saturation != 1.f) x = Saturation(x, saturation, colorspace);

  // clamp cs
  if (clampCs) x = max(0, x);

  return x;
}

/*
  x = RenoDX_ColorGrade(
    x, 
    GS.CGContrast, GS.CGContrastMidGray, 
    GS.CGHighlightsStrength, GS.CGHighlightsMidGray, 
    GS.CGShadowsStrength, GS.CGShadowsMidGray, 
    GS.CGSaturation, 
    CS_BT2020
  );
*/

float3 CorrectPerChannelTonemapHiglightsDesaturationFixed(float3 color, float peakBrightness, float desaturationExponent = 2.0, uint colorSpace = CS_DEFAULT)
{    
  float sourceChrominance = GetChrominance(color);

  float maxBrightness = max3(color); 
  float midBrightness = GetMidValue(color);
	float minBrightness = min3(color);
	float brightnessRatio = saturate(maxBrightness / peakBrightness);

  brightnessRatio = lerp(brightnessRatio, sqrt(brightnessRatio), safeSqrt(InverseLerp(minBrightness, maxBrightness, midBrightness)));
  brightnessRatio *= brightnessRatio; // skewed towards highlights only

  float chrominancePow = lerp(1.0, 1.0 / desaturationExponent, brightnessRatio);
  
  float targetChrominance = sourceChrominance > 1.0 ? pow(sourceChrominance, chrominancePow) : (1.0 - pow(1.0 - sourceChrominance, chrominancePow));
  float chrominanceRatio = safeDivision(targetChrominance, sourceChrominance, 1);

  return RestoreLuminance(SetChrominance(color, chrominanceRatio), color, true, colorSpace);
}