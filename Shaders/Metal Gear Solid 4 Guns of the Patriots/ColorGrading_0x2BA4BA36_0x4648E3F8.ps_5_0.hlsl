#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"

#if _2BA4BA36
#define HAS_COLOR_PALETTE 1
#endif

#ifndef HAS_COLOR_PALETTE
#define HAS_COLOR_PALETTE 0
#endif

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

#ifndef ENABLE_COLOR_GRADING
#define ENABLE_COLOR_GRADING 1
#endif

#ifndef ENABLE_IMPROVED_COLOR_GRADING
#define ENABLE_IMPROVED_COLOR_GRADING 1
#endif

#ifndef ENABLE_AUTO_HDR
#define ENABLE_AUTO_HDR 1
#endif

#ifndef ENABLE_FILM_GRAIN
#define ENABLE_FILM_GRAIN 1
#endif

#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

Texture2D<float4> SceneTexture    : register(t0);
Texture2D<float> LUTIndexTexture  : register(t1);
Texture2D<float4> ColorLUT        : register(t2); // Actually float1

SamplerState SceneSampler    : register(s0);
SamplerState LUTIndexSampler : register(s1);
SamplerState ColorLUTSampler : register(s2);

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

// TODO: move to library
// From RenoDX
float3 NeutwoRanged(float3 X, float3 ShoulderStart = MidGray, float3 PeakOut = 1.0)
{
	float3 linear_part = min(X, ShoulderStart);
	float3 shifted_x = max(0.0, X - ShoulderStart);
	float3 p = PeakOut - ShoulderStart;
	float3 numerator = p * shifted_x;
	float3 denominator_squared = mad(shifted_x, shifted_x, p * p);

	return linear_part + (numerator * rsqrt(denominator_squared));
}

// -----------------------------------------------------------------------------
// Applies the 64x64 2D LUT independently to R, G and B.
//
// X = input channel value
// Y = LUT/palette selector
// -----------------------------------------------------------------------------
float3 ApplyColorLUT(float3 color, float lutY)
{
#if ENABLE_LUMA
    // The lut is per channel so simply let it clip and do the remapping of any color
    // that is beyond 1 as if it was 1, then reproject the scale.
    // The only issue with this is in case e.g. 0.8 already mapped to 1, in that case this would
    // generate a hard step in gradients.
    float3 colorMax1 = max(color, 1.0);
#endif

#if ENABLE_IMPROVED_COLOR_GRADING // Luma: fix missing half texel offset for LUT
    float2 LUTSize2D = 64.0;
    // Likely always 64x64 but checking won't hurt
    ColorLUT.GetDimensions(LUTSize2D.x, LUTSize2D.y);
    float LUTSize  = LUTSize2D.x;
    float LUTScale = (LUTSize - 1.0) / LUTSize; // 63 / 64
    float LUTBias  = 0.5 / LUTSize;             // 0.5 / 64

    float3 rawColor = color;
    color = color * LUTScale + LUTBias;
    lutY  = lutY  * LUTScale + LUTBias;
#endif

#if ENABLE_LUMA && ENABLE_IMPROVED_COLOR_GRADING && 0 // TODO: finish?
    float neutralFilmGrainOffset = 0.0; // Neutral
    float highlightsRemapScale = ColorLUT.Sample(ColorLUTSampler, float2(0.75 * LUTScale + LUTBias, neutralFilmGrainOffset)).x / 0.75;
    float whiteRemapScale = ColorLUT.Sample(ColorLUTSampler, float2(1.0, neutralFilmGrainOffset)).x;
#endif

    float3 result;
#if ENABLE_IMPROVED_COLOR_GRADING
    // Fixes banding added by harsh steps between LUT positions

    float LUTXMax = LUTSize - 1.0;
    float LUTXActualMax = LUTXMax;
#if 0 // TODO: finish! Also disable "colorMax1" mult below if doing this!
    // Crawl the LUT to find the texel that turns the output to max
    float3 ClippingEdge = Find1DLUTClippingEdge(ColorLUT, uint(LUTSize + 0.5));

    float LUTXMaxScale = LUTXActualMax / LUTXMax;
    rawColor = max(rawColor, LUTXMaxScale);
#endif

    uint lutYi = (uint)(lutY * LUTSize);
    // TODO: ignore film grain here and re-do our own one?
    result = Sample1DLUTWithSmoothing(ColorLUT, LUTXMax, rawColor, lutYi, lutYi, lutYi, 0u, 0u, 0u, 0u, false).rgb;

#if TEST // TODO: test for raised blacks
    // Print purple if the LUT has raised blacks!
    if (ColorLUT.Load(int3(0, 0, 0)).x != 0)
    {
        result = float3(1.0, 0.0, 1.0);
    }
#endif
#else
    result.r = ColorLUT.Sample(ColorLUTSampler, float2(color.r, lutY)).x;
    result.g = ColorLUT.Sample(ColorLUTSampler, float2(color.g, lutY)).x;
    result.b = ColorLUT.Sample(ColorLUTSampler, float2(color.b, lutY)).x;
#endif
    
#if ENABLE_IMPROVED_COLOR_GRADING && 0 // TODO: handle raised shadow. Disabled as it's tiny and not worth handling. Also, this is going in the opposite direction???
    float clippedAmount = 0.5 / LUTSize; // The first and last half texels of the LUT were clipped away (in gamma space)
    // The wrong sampling math would have clipped shadow (and highlights),
    // increasing contrast globally. Here we try to restore the lost contrast without clipping shadow.
    // We ignore the highlights boost as it doesn't really seem to matter.
    result.rgb = EmulateShadowClip(result.rgb, false, 0.02);
#endif

#if ENABLE_LUMA
    result.rgb *= colorMax1;
#endif

    return result;
}

void main(
    float4 position      : SV_POSITION,
    float4 vertexColor   : COLOR0,
    float2 sceneUV       : TEXCOORD0,
    float4 vignetteCoord : TEXCOORD1,
    out float4 output    : SV_TARGET0)
{
    // -------------------------------------------------------------------------
    // Parameters
    // -------------------------------------------------------------------------

    const float3 colorScale       = cb0[0].xyz;
    const float  desaturation     = cb0[0].w;

    const float  contrast         = cb0[1].x;
    float  brightnessOffset       = cb0[1].y;

    float3 colorMin               = cb0[2].xyz;
    float3 colorMax               = cb0[3].xyz;

    const float2 lutIndexOffset   = cb0[4].xy;
    const float  lutIndexScale    = cb0[4].w;

    const float3 fadeColor        = cb0[5].xyz;
    const float  fadeBlend        = cb0[5].w;

    const float  vignetteScale    = cb0[6].x;
    const float  vignetteBias     = cb0[6].y;

    const float2 renderSize       = cb0[8].xy;

    // -------------------------------------------------------------------------
    // Film grain
    // -------------------------------------------------------------------------

#if HAS_COLOR_PALETTE && ENABLE_FILM_GRAIN
    float2 nativeAspectRatios = float2(16.0, 9.0);
    float nativeAspectRatio = nativeAspectRatios.x / nativeAspectRatios.y;
#if 1
    float2 sceneSize;
    // Likely always 64x64 but checking won't hurt
    SceneTexture.GetDimensions(sceneSize.x, sceneSize.y);
#if 1 // Dynamic. The game's renders with pillar boxes in UW (especially without UW compatibility mods).
    float gameAspectRatio = sceneSize.x / sceneSize.y;
#else
    float gameAspectRatio = LumaSettings.SwapchainSize.x * LumaSettings.SwapchainInvSize.y;
#endif
    float relativeAspectRatio = gameAspectRatio / nativeAspectRatio;
    // Handle the FoV scaling direction changing from horizontal (16:9+) to vertical (16:9-), at least with Lyall mod.
    float2 currentAspectRatios = nativeAspectRatios * float2(max(relativeAspectRatio, 1.0), min(relativeAspectRatio, 1.0));
#else
    float2 currentAspectRatios = nativeAspectRatios;
#endif

    float2 lutIndexUV = position.xy * (currentAspectRatios / renderSize);
    lutIndexUV = lutIndexUV * 0.5 + lutIndexOffset;

    // This offsets the 2D LUT sampling coordinates to generate a ~per pixel film grain effect.
    float lutY = LUTIndexTexture.Sample(LUTIndexSampler, lutIndexUV);
    lutY *= lutIndexScale;
#else
    float lutY = 0.0;
#endif // HAS_COLOR_PALETTE && ENABLE_FILM_GRAIN

    // -------------------------------------------------------------------------
    // Scene color
    // -------------------------------------------------------------------------

    float3 sceneColor = SceneTexture.Sample(SceneSampler, sceneUV).rgb;
#if ENABLE_LUMA

#if 1
    sceneColor = gamma_to_linear(sceneColor, GCT_MIRROR);

    FixColorGradingLUTNegativeLuminance(sceneColor); // Fix up any possible invalid luminance that might have made it here, before we pass through LUT etc

#if ENABLE_AUTO_HDR // TODO: rename
    float normalizationPoint = 0.025; // Found empyrically
    float fakeHDRIntensity = 0.075; // Hardcoded for now, no other value looked balance so there's not much need to expose it
    float fakeHDRSaturation = 0.25;
    sceneColor = BT2020_To_BT709(FakeHDR(BT709_To_BT2020(sceneColor), normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT2020));
#endif

    sceneColor = linear_to_gamma(sceneColor, GCT_MIRROR);
#else
    // Clamp negative values as they are likely garbage
    sceneColor = max(sceneColor, 0.0);
#endif

#if 0 // Not needed until proven otherwise // TODO: all here
    if (max3(sceneColor) > 1.0)
    {
        float3 sceneColorLinear = gamma_to_linear(sceneColor, GCT_MIRROR);
        float3 clippedSceneColorLinear = gamma_to_linear(saturate(sceneColor), GCT_MIRROR);
        sceneColorLinear = RestoreHueAndChrominance(sceneColorLinear, clippedSceneColorLinear, 0.75, 0.0);
        sceneColor = linear_to_gamma(sceneColorLinear, GCT_MIRROR);
    }
#endif
#else // Emulate UNORM
    sceneColor = saturate(sceneColor);
#endif

    float3 color = sceneColor;

#if ENABLE_COLOR_GRADING

    // -------------------------------------------------------------------------
    // Optional single channel palette
    // -------------------------------------------------------------------------

#if HAS_COLOR_PALETTE
    color = ApplyColorLUT(color, lutY);
#endif

    // -------------------------------------------------------------------------
    // Saturation
    // -------------------------------------------------------------------------

#if ENABLE_IMPROVED_COLOR_GRADING && 0 // TODO: desat in linear (not always better actually)

    color = gamma_to_linear(color, GCT_MIRROR);

#if HAS_COLOR_PALETTE
    float luminance = GetLuminance(color.yxz); // TODO: calc in linear etc?
#else
    float luminance = GetLuminance(color.xyz);
#endif
    color = lerp(color, luminance, desaturation);

    color = linear_to_gamma(color, GCT_MIRROR);

#else

#if HAS_COLOR_PALETTE
    float luminance = dot(color.yxz, float3(0.300000012, 0.589999974, 0.109999999)); // TODO: actual order?
#else
    float luminance = dot(color, float3(0.300000012, 0.589999974, 0.109999999));
#endif
    color = lerp(color, luminance, desaturation);

#endif

    // -------------------------------------------------------------------------
    // Color scaling
    // -------------------------------------------------------------------------

    color *= colorScale;

    // -------------------------------------------------------------------------
    // Contrast + brightness
    // -------------------------------------------------------------------------

    float contrastMidPoint = 0.5;
#if ENABLE_IMPROVED_COLOR_GRADING // Luma modern contrast method that doesn't raise blacks not generate invalid colors

	// Empirical value to match the original game constrast formula look more.
	// This has been carefully researched and applies to both positive and negative contrast.
	const float adjustedContrast = pow(contrast, 1.5); // TODO: tweak more? main menu background doesn't look so good
	// Do abs() to avoid negative power, even if it doesn't make 100% sense, these formulas are fine as long as they look good
	color = pow(abs(color) / contrastMidPoint, adjustedContrast) * contrastMidPoint * sign(color);
    
    float3 prevColor = color;
    // This was mostly used to compensate contrast generating negative values.
    // Only add negative offsets, as they are used in black and white cutscenes etc.
    brightnessOffset = min(brightnessOffset, 0.0);

#if 0 // TODO!
    // These just don't look right... Hue changes too much
    //color = EmulateShadowOffset(color, brightnessOffset, false);
    //color = AddColorOffsetDampened(color, brightnessOffset, 0.25, true);
    //color = RemapColorOffsetAsContrast(color, brightnessOffset, 1.0, contrastMidPoint, true, true, 0.25);
    
    float preOffsetLuminance = GetLuminance(color); // TODO: calc luminance in linear!
    float3 offsettedColor = color + brightnessOffset;
    float postOffsetLuminance = GetLuminance(offsettedColor);
    if (DVS1 && preOffsetLuminance > 0)
    {
        color *= postOffsetLuminance / preOffsetLuminance;
    }
    else
    {
        color = offsettedColor;
    }
#else
    color += brightnessOffset;
#endif

    // Don't expand gamut beyond what it already was
    color = max(color, min(prevColor, 0.0));
#else
    color = ((color - contrastMidPoint) * contrast) + contrastMidPoint;
    
    color += brightnessOffset;
#endif

    // -------------------------------------------------------------------------
    // Clamp ranges
    // -------------------------------------------------------------------------

#if ENABLE_IMPROVED_COLOR_GRADING
    // Apply the min but preserve the original luminance
    float preMinLuminance = GetLuminance(color); // TODO: calc luminance in linear!
    color = max(colorMin, color);
    float postMinLuminance = GetLuminance(color);
    if (postMinLuminance != 0.0)
    {
        color *= preMinLuminance / postMinLuminance;
    }

    // Preserve the color max clamp tint, but make it HDR compatibile.
    // First, map the max channel of the clamp value to 1,
    // then, scale it to the current scene max channel value.
    float colorMin1 = min(max3(colorMax), 1.0);
    colorMax /= colorMin1;

    float colorMax1 = max(max3(color), 1.0);
    colorMax *= colorMax1;
    
    // Note: this can look a bit weird on snow levels
    color = min(colorMax, color);
#else
    color = max(colorMin, color);
#if !ENABLE_LUMA
    color = min(colorMax, color);
#endif
#endif

#endif // ENABLE_COLOR_GRADING

#if ENABLE_VIGNETTE
    // -------------------------------------------------------------------------
    // Radial vignette
    // -------------------------------------------------------------------------

    float radius = length(vignetteCoord.xy);
    float vignette = saturate(radius * vignetteScale + vignetteBias);
    vignette = pow(vignette, 1.4);
    float sceneWeight = 1.0 - vignette;
    color *= sceneWeight;
#endif // ENABLE_VIGNETTE

#if ENABLE_LUMA
    // -------------------------------------------------------------------------
    // Tonemap
    // -------------------------------------------------------------------------

    color = gamma_to_linear(color, GCT_MIRROR);
    const float relativePeakWhite = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
    // Use Newtwo given the game was raw clipped (it has a hash curve that preserves that look)
    color = NeutwoRanged(color, MidGray, relativePeakWhite);
    color = linear_to_gamma(color, GCT_MIRROR);
#endif

    // -------------------------------------------------------------------------
    // Fade
    // -------------------------------------------------------------------------

    // Hopefully the target color is always <= 1
    color = lerp(color, fadeColor, fadeBlend); // TODO: improve?

    
#if !ENABLE_LUMA
    color = saturate(color);
#endif

    output = float4(color, 1.0);
}
