#include "../../Includes/Common.hlsl"
#include "../../Includes/Reinhard.hlsl"
#include "../../Includes/HardwareBlendsEmulation.hlsl"

#ifndef ENABLE_ROV_UI
#define ENABLE_ROV_UI 1
#endif

#ifndef ENABLE_VANILLA_UI
#define ENABLE_VANILLA_UI 0
#endif

#ifndef ENABLE_UI_TONEMAP
#define ENABLE_UI_TONEMAP 0
#endif

#ifndef ENABLE_ADVANCED_UI_ROV_BLENDS
#define ENABLE_ADVANCED_UI_ROV_BLENDS (ENABLE_VANILLA_UI || ENABLE_UI_TONEMAP)
#endif

#if ENABLE_ROV_UI
RasterizerOrderedTexture2D<float4> u1 : register(u1);
#else
RWTexture2D<float4> u1 : register(u1);
#endif

// Luma function
float4 EmulateHardwareBlendMGS4(uint2 pixelPosition, float4 outColor, bool preTonemapped = false)
{
  // Luma: ROV implementation to avoid having to sanitize the background every time there's a subtractive blend.
  // There's no render target bound in this case, so the fixed function output merger is out of the equation and we have to run the game's blend state ourselves.
#if ENABLE_ADVANCED_UI_ROV_BLENDS
  const bool emulateBlend = IsRenderTargetBlendEnabled(LumaData.CustomData1);
#else
  const bool emulateBlend = LumaData.CustomData1 != 0;
#endif // ENABLE_ADVANCED_UI_ROV_BLENDS

#if !ENABLE_ROV_UI // ROV cannot be inside of a condition]
  [branch]
  if (emulateBlend)
#endif // !ENABLE_ROV_UI
  {
    const float4 backgroundColor = u1[pixelPosition];
    const float4 uiColor = float4(max(outColor.rgb, 0.0), saturate(outColor.a));
    float4 composedColor = float4(0.0, 0.0, 0.0, 1.0);

#if ENABLE_ROV_UI // Second branch for ROV
    [branch]
    if (emulateBlend)
#endif // ENABLE_ROV_UI
    {
#if !ENABLE_ADVANCED_UI_ROV_BLENDS // Simple game background subtraction formula, this is what it usually (always?) uses. Potential optimization.
        composedColor.rgb = backgroundColor.rgb - (uiColor.rgb * uiColor.a);
        composedColor.a = uiColor.a;
#else
        const HardwareBlendState blendState = UnpackRenderTargetBlendState(LumaData.CustomData1, LumaData.CustomData2, LumaData.CustomData3);
        composedColor = EmulateHardwareBlend(blendState, uiColor, backgroundColor, false);
#endif

        // The vanilla render target was UNORM, so the hardware clamped every blend result to 0-1.
        // We keep the upgraded (float) range on the way up, but still prevent subtractions from going below 0, or anyway, from expanding the gamut.
        composedColor.rgb = max(composedColor.rgb, min(backgroundColor.rgb, 0.0));
        // Alpha carries no HDR range, so it can be clipped like vanilla did on UNORM
        composedColor.a = saturate(composedColor.a);

        if (!preTonemapped)
        {
#if ENABLE_ADVANCED_UI_ROV_BLENDS
#if ENABLE_VANILLA_UI
          // Given that the UI sometimes additive, it can go beyond 1 and change color, use this to simulate the original clipped look (at the cost of clipping the game scene behind it too)
          float3 newComposedColor = saturate(composedColor.rgb);
          composedColor.rgb = lerp(composedColor.rgb, newComposedColor.rgb, saturate(GetPositiveSourceRGBInfluence(blendState, uiColor, backgroundColor)));
#elif ENABLE_UI_TONEMAP
          const float relativePeakWhite = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
          float3 newComposedColor = Reinhard::ReinhardRanged(composedColor.rgb, MidGray, relativePeakWhite);
          // Apply the clamping only by the percentage the UI color applied with
          composedColor.rgb = lerp(composedColor.rgb, newComposedColor.rgb, saturate(GetPositiveSourceRGBInfluence(blendState, uiColor, backgroundColor)) * saturate(uiColor.rgb));
#endif
#endif // ENABLE_ADVANCED_UI_ROV_BLENDS
        }
    }

    u1[pixelPosition] = emulateBlend ? composedColor : backgroundColor;

    // 0 just for safety (alpha is replaced)
    if (emulateBlend)
      outColor = float4(0.0, 0.0, 0.0, 1.0);
  }

  return outColor;
}