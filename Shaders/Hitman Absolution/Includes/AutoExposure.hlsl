#ifndef SRC_HMA_AUTO_EXPOSURE_HLSL
#define SRC_HMA_AUTO_EXPOSURE_HLSL

// Luma auto-exposure meter output (1x1), bound on t7.
Texture2D<float> LumaExposureTex : register(t7);

// Returns the exposure to use in place of the game's cb4[41].z.
// Only active with TONEMAP_TYPE >= 1 and AutoExposureMode != 0; otherwise returns the vanilla value.
float GetHMAExposure(float vanillaExposure)
{
  float exposure = vanillaExposure;
#if TONEMAP_TYPE >= 1
  if (LumaSettings.GameSettings.AutoExposureMode != 0)
  {
    float e = LumaExposureTex.Load(int3(0, 0, 0));
    if (e > 0.0 && e < 1e6)
      exposure = e;
  }
#endif
  return exposure;
}

#endif // SRC_HMA_AUTO_EXPOSURE_HLSL
