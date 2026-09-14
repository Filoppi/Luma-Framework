#include "../Includes/Common.hlsl"
#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Reinhard.hlsl"

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

#ifndef ENABLE_COLOR_TINTING
#define ENABLE_COLOR_TINTING 1
#endif

#ifndef ENABLE_IMPROVED_COLOR_GRADING
#define ENABLE_IMPROVED_COLOR_GRADING 1
#endif

// 0 conservative
// 1 less conservative, but "better" looking
// 2 even less conservative, arguable, but more "realistic"/accurate
#ifndef IMPROVED_COLOR_GRADING_TYPE
#define IMPROVED_COLOR_GRADING_TYPE 1
#endif

#ifndef ENABLE_SOFT_HIGHLIGHTS
#define ENABLE_SOFT_HIGHLIGHTS 1
#endif

#ifndef ENABLE_HDR_BOOST
#define ENABLE_HDR_BOOST 1
#endif

#ifndef ENABLE_FILM_GRAIN
#define ENABLE_FILM_GRAIN 1
#endif

#ifndef ENABLE_VIGNETTE
#define ENABLE_VIGNETTE 1
#endif

Texture2D<float4> SceneTexture    : register(t0);
Texture2D<float> LUTIndexTexture  : register(t1);
Texture2D<float4> ColorLUT        : register(t2); // Actually float1. This is flagged as render target but it's actually loaded from disk.

SamplerState SceneSampler    : register(s0);
SamplerState LUTIndexSampler : register(s1);
SamplerState ColorLUTSampler : register(s2);

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

// TODO: move both to library (and all the other copies in MGS4)
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
/// Identity through anchor to every derivative; then approaches peak
/// monotonically and concave down. Requires anchor < peak and compression_strength >= 1.
float3 ApplyAnchoredCInfinityShoulder(float3 color, float3 anchor = MidGray, float3 peak = 1.0, float3 compression_strength = 1.0)
{               
    float3 shoulder_range = peak - anchor;                                                                     
    float3 distance_from_anchor = max(color - anchor, 0.0);                                                 
    float3 flat_weight = exp2(-shoulder_range / (compression_strength * distance_from_anchor));                
    float3 response_denominator = mad(distance_from_anchor, flat_weight, shoulder_range);                      
    return mad(shoulder_range, distance_from_anchor / response_denominator, color - distance_from_anchor);
}

// -----------------------------------------------------------------------------
// Applies the 64x64 2D LUT independently to R, G and B.
//
// X = input channel value
// Y = LUT/palette selector
// -----------------------------------------------------------------------------
float3 ApplyColorLUT(float3 color, float lutY, bool forceVanillaSDR)
{
    bool forceVanillaLUT = true;
#if ENABLE_IMPROVED_COLOR_GRADING
    forceVanillaLUT = false;
#endif
    forceVanillaLUT = forceVanillaLUT || forceVanillaSDR;

    // The lut is per channel so simply let it clip and do the remapping of any color
    // that is beyond 1 as if it was 1, then reproject the scale.
    // The only issue with this is in case e.g. 0.8 already mapped to 1, in that case this would
    // generate a hard step in gradients.
    float3 colorMax1 = max(color, 1.0);

    // Luma: fix missing half texel offset for LUT
    float2 LUTSize2D = 64.0;
    // Likely always 64x64 but checking won't hurt
    ColorLUT.GetDimensions(LUTSize2D.x, LUTSize2D.y);
    float LUTSize  = LUTSize2D.x;
    float LUTScale = (LUTSize - 1.0) / LUTSize; // 63 / 64
    float LUTBias  = 0.5 / LUTSize;             // 0.5 / 64

    float3 rawColor = color;
    if (!forceVanillaLUT)
    {
        color = color * LUTScale + LUTBias;
        lutY  = lutY  * LUTScale + LUTBias;
    }

    static const float contrastMidPoint = 0.5;

    float3 result;
    if (forceVanillaLUT)
    {
        result.r = ColorLUT.Sample(ColorLUTSampler, float2(color.r, lutY)).x;
        result.g = ColorLUT.Sample(ColorLUTSampler, float2(color.g, lutY)).x;
        result.b = ColorLUT.Sample(ColorLUTSampler, float2(color.b, lutY)).x;
    }
    // Fixes banding added by harsh steps between LUT positions
    else
    {
        float LUTXMax = LUTSize - 1.0;
        float LUTXActualMax = LUTXMax;
#if 0 // TODO: finish! Also disable "colorMax1" mult below if doing this!
        // Crawl the LUT to find the texel that turns the output to max
        float3 ClippingEdge = Find1DLUTClippingEdge(ColorLUT, uint(LUTSize + 0.5));

        float LUTXMaxScale = LUTXActualMax / LUTXMax;
        rawColor = max(rawColor, LUTXMaxScale);
#endif

        uint lutYi = (uint)(lutY * LUTSize);
        result = Sample1DLUTWithSmoothing(ColorLUT, LUTXMax, rawColor, lutYi, lutYi, lutYi, 0u, 0u, 0u, 0u, false).rgb;

#if TEST // TODO: test for raised blacks.
        // Print purple if the LUT has raised blacks!
        if (ColorLUT.Load(int3(0, 0, 0)).x != 0)
        {
            result = float3(1.0, 0.0, 1.0);
        }
#endif
    }
    
#if 0 // TODO: handle raised shadow. Disabled as it's tiny and not worth handling. Also, this is going in the opposite direction??? It doesn't seem to ever be a problem.
    if (!forceVanillaLUT) 
    {
        float clippedAmount = 0.5 / LUTSize; // The first and last half texels of the LUT were clipped away (in gamma space)
        // The wrong sampling math would have clipped shadow (and highlights),
        // increasing contrast globally. Here we try to restore the lost contrast without clipping shadow.
        // We ignore the highlights boost as it doesn't really seem to matter.
        result.rgb = EmulateShadowClip(result.rgb, false, 0.02);
    }
#endif

    if (!forceVanillaSDR)
        result.rgb *= colorMax1;

    return result;
}

float3 ApplyTonemap(float3 color)
{
    color = gamma_to_linear(color, GCT_MIRROR);
    
    const float relativePeakWhite = LumaSettings.PeakWhiteNits / LumaSettings.GamePaperWhiteNits;
#if ENABLE_SOFT_HIGHLIGHTS
    // Reinhard has a softer curve that often looks less jarring, and often also desaturates more which ends up having a closer hue to raw clipping. This also looks a lot better in SDR.
	// Lower means softer highlights (more compression). 1 is neutral.
    float highlightsStrength = 2.0;
	color = Reinhard::ReinhardAdvanced(color, MidGray, FLT_MAX, relativePeakWhite, highlightsStrength);
#else
    // Use Newtwo given the game was raw clipped (it has a hash curve that preserves that look, but not always)
    color = NeutwoRanged(color, MidGray, relativePeakWhite);
#endif

    color = linear_to_gamma(color, GCT_MIRROR);
    return color;
}

// Almost always the last shader if there's a scene on screen, though sometimes it's skipped, and usually there's a "stretch"/vertex tint pass instead
void main(
    float4 position      : SV_POSITION,
    float4 vertexColor   : COLOR0,
    float2 sceneUV       : TEXCOORD0,
    float4 vignetteCoord : TEXCOORD1,
    out float4 output    : SV_TARGET0)
{
#if 0 // Passthrough
    output = SceneTexture.Load(uint3(position.xy, 0));
    return;
#endif

    // -------------------------------------------------------------------------
    // Parameters
    // -------------------------------------------------------------------------

    const float3 colorScale       = cb0[0].xyz;
    float  desaturation     = cb0[0].w;

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
    
    bool forceVanillaSDR = ShouldForceSDR(sceneUV);
#if !ENABLE_LUMA
    forceVanillaSDR = true;
#endif

    // -------------------------------------------------------------------------
    // Film grain
    // -------------------------------------------------------------------------

#if HAS_COLOR_PALETTE && ENABLE_FILM_GRAIN
    float2 nativeAspectRatios = float2(16.0, 9.0);
    float nativeAspectRatio = nativeAspectRatios.x / nativeAspectRatios.y;
#if 1
#if 0 // Dynamic. The game's renders with pillar boxes in UW (especially without UW compatibility mods).
    float2 sceneSize;
    // Likely always 64x64 but checking won't hurt
    SceneTexture.GetDimensions(sceneSize.x, sceneSize.y);
    float gameAspectRatio = sceneSize.x / sceneSize.y;
#else // We now have a runtime render size that matches the game's rendering within black bars
    float gameAspectRatio = LumaSettings.RenderSize.x * LumaSettings.RenderInvSize.y;
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
    if (forceVanillaSDR)
    {
        // Emulate UNORM
        sceneColor = saturate(sceneColor);
    }
    else
    {
        sceneColor = gamma_to_linear(sceneColor, GCT_MIRROR);

#if 0 // Breaks the night vision which uses subtractive blends just before this draw call, we'd need UAV to fix it properly
        FixColorGradingLUTNegativeLuminance(sceneColor); // Fix up any possible invalid luminance that might have made it here, before we pass through LUT etc. There's likely none that isn't accidental anyway...
#else
        // Clamp negative values as they are likely garbage
        sceneColor = max(sceneColor, 0.0);
#endif

#if ENABLE_HDR_BOOST
        float normalizationPoint = 0.025; // Found empyrically
        float fakeHDRIntensity = 0.075; // Hardcoded for now, no other value looked balance so there's not much need to expose it
        float fakeHDRSaturation = 0.25;
        sceneColor = FakeHDR(sceneColor, normalizationPoint, fakeHDRIntensity, fakeHDRSaturation, 0, CS_BT709);
#endif // ENABLE_HDR_BOOST

        sceneColor = linear_to_gamma(sceneColor, GCT_MIRROR);

#if 0 // Not needed until proven otherwise, the game seems fine without this with a few exceptions (also this would generate negative values so make sure they'd be supported below)
        if (max3(sceneColor) > 1.0)
        {
            float3 sceneColorLinear = gamma_to_linear(sceneColor, GCT_MIRROR);
            float3 clippedSceneColorLinear = gamma_to_linear(saturate(sceneColor), GCT_MIRROR);
            sceneColorLinear = RestoreHueAndChrominance(sceneColorLinear, clippedSceneColorLinear, 0.75, 0.0);
            sceneColor = linear_to_gamma(sceneColorLinear, GCT_MIRROR);
        }
#endif
    }

    float3 color = sceneColor;

#if ENABLE_COLOR_GRADING

    // -------------------------------------------------------------------------
    // Optional single channel palette
    // -------------------------------------------------------------------------

#if HAS_COLOR_PALETTE
    // Note: this might clip negative values.
    color = ApplyColorLUT(color, lutY, forceVanillaSDR);
#endif

    // -------------------------------------------------------------------------
    // Saturation
    // -------------------------------------------------------------------------

    bool forceVanillaContrast = true;
#if ENABLE_IMPROVED_COLOR_GRADING
    forceVanillaContrast = false;
#endif
    forceVanillaContrast = forceVanillaContrast || forceVanillaSDR;

    // Compensate for our new contrast method increasing saturation
    // TODO: for more accurate results, we'd do it after contrast...
#if IMPROVED_COLOR_GRADING_TYPE == 0// || 1 // TODO1
    if (!forceVanillaContrast)
    {
        desaturation *= lerp(contrast, 1.0, 0.667);
    }
#endif // IMPROVED_COLOR_GRADING_TYPE == 0

    bool desatInLinear = false;
// Desat in linear (not always perfect, it's still not always perceptual (thought generally more consistent), and desaturates "more" than doing it in gamma space, preventing certain tints from appearing)
#if ENABLE_IMPROVED_COLOR_GRADING && IMPROVED_COLOR_GRADING_TYPE >= 1 && (!ENABLE_COLOR_TINTING || IMPROVED_COLOR_GRADING_TYPE >= 2)
    desatInLinear = true;
#endif // ENABLE_IMPROVED_COLOR_GRADING
    desatInLinear = desatInLinear && !forceVanillaSDR;

    if (desatInLinear)
        color = gamma_to_linear(color, GCT_MIRROR);

    float luminance = dot(color, float3(0.300000012, 0.589999974, 0.109999999));
#if ENABLE_IMPROVED_COLOR_GRADING // Luma: fix Rec.601 luminance and it being in gamma space
    if (!forceVanillaSDR)
        luminance = desatInLinear ? GetLuminance(color.xyz, GCT_POSITIVE) : linear_to_gamma(GetLuminance(gamma_to_linear(color.xyz, GCT_POSITIVE))).x;
#endif
#if HAS_COLOR_PALETTE // Luma: the original shader had a bug (supposedly not intentional) that swapped the red and green weights.
    if (forceVanillaSDR)
        luminance = dot(color.yxz, float3(0.300000012, 0.589999974, 0.109999999));
#endif
    color = lerp(color, luminance, desaturation);

    if (desatInLinear)
        color = linear_to_gamma(color, GCT_MIRROR);

    // -------------------------------------------------------------------------
    // Color scaling (tint)
    // -------------------------------------------------------------------------

#if ENABLE_COLOR_TINTING
    color *= colorScale;
#else
    // Keep the scaled luminance, without the tint
    float preScaleLuminance = desatInLinear ? linear_to_gamma(GetLuminance(gamma_to_linear(color.xyz, GCT_POSITIVE))).x : luminance;
    float postScaleLuminance = linear_to_gamma(GetLuminance(gamma_to_linear(color.xyz * colorScale, GCT_POSITIVE))).x;
    if (preScaleLuminance > 0)
    {
        color *= postScaleLuminance / preScaleLuminance;
    }
#endif

    // -------------------------------------------------------------------------
    // Contrast + brightness
    // -------------------------------------------------------------------------

    float contrastMidPoint = 0.5;

    if (forceVanillaContrast)
    {
        color = ((color - contrastMidPoint) * contrast) + contrastMidPoint;
        
        color += brightnessOffset;
    }
    // Luma modern contrast method that doesn't raise blacks not generate invalid colors
    else
    {
        // Further boosts saturation (a tiny bit). Hue distortion from contrast might bet more "accurate".
        bool doContrastInBT2020 = false;
#if IMPROVED_COLOR_GRADING_TYPE >= 2
        doContrastInBT2020 = LumaSettings.DisplayMode == 1;
#endif
        if (doContrastInBT2020)
        {
            contrastMidPoint = pow(contrastMidPoint, DefaultGamma); // Adjust mid point to run in linear, result will be identical
            color = BT709_To_BT2020(gamma_to_linear(color, GCT_MIRROR));
        }

	    // Empirical value to match the original game constrast formula look more.
	    // This has been carefully researched and applies to both positive and negative contrast.
	    const float adjustedContrast = pow(contrast, 1.667); // TODO1: tweak more? Shadow are too bright in some dark scenes. Also, this increases saturation quite a lot compared to the vanilla code, maybe we should add some desat pass after to compensate?
	    // Do abs() to avoid negative power, even if it doesn't make 100% sense, these formulas are fine as long as they look good
	    color = pow(abs(color) / contrastMidPoint, adjustedContrast) * contrastMidPoint * sign(color);

        if (doContrastInBT2020)
            color = linear_to_gamma(BT2020_To_BT709(color), GCT_MIRROR);

        float3 prevColor = color;
        // This was mostly used to compensate contrast generating negative values.
        // Only add negative offsets, as they are used in black and white cutscenes etc.
        brightnessOffset = min(brightnessOffset, 0.0);

#if 0 // TODO? It seems fine for now. Not needed.
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
    }

    // -------------------------------------------------------------------------
    // Tonemap
    // -------------------------------------------------------------------------
    if (!forceVanillaSDR)
        color = ApplyTonemap(color);

    // -------------------------------------------------------------------------
    // Clamp ranges
    // -------------------------------------------------------------------------

#if !ENABLE_COLOR_TINTING
    // Make it greyscale!
    colorMin = linear_to_gamma(GetLuminance(gamma_to_linear(colorMin), GCT_POSITIVE));
#endif // ENABLE_COLOR_TINTING

    if (forceVanillaSDR)
    {
        color = max(colorMin, color);
#if ENABLE_COLOR_TINTING
        color = min(colorMax, color);
#endif // ENABLE_COLOR_TINTING
    }
    else
    {
#if ENABLE_IMPROVED_COLOR_GRADING
        // Apply the min but preserve the original luminance
        float preMinLuminance = GetLuminance(gamma_to_linear(color), GCT_POSITIVE);
#if IMPROVED_COLOR_GRADING_TYPE >= 2
        // Add colorMin as offset but smooth it out as we reach "colorMin*2". This makes gradients smoother. However it looks quite different as it tints more midtones.
        // This works in BT.2020 too.
        color += max(0.0, colorMin - max(color, 0.0) * 0.5);
#else
        // Don't apply t he min if it's 0, it'd clip BT.2020 colors.
        // This is fine and could only ever cause snaps in the output color if they interpolated the min from 0 to >0.
        color = (colorMin > 0.0) ? max(colorMin, color) : color;
#endif
        float postMinLuminance = GetLuminance(gamma_to_linear(color), GCT_POSITIVE);
        if (postMinLuminance > 0.0)
        {
            color *= linear_to_gamma(preMinLuminance / postMinLuminance);
        }

#if ENABLE_COLOR_TINTING
        // Preserve the color max clamp tint, but make it HDR compatibile.
        // First, map the max channel of the clamp value to 1,
        // then, scale it to the current scene max channel value.
        float colorMin1 = min(max3(colorMax), 1.0);
        colorMax /= colorMin1;

#if IMPROVED_COLOR_GRADING_TYPE >= 1
        // Max generates broken gradients, smooth it in
        colorMax = lerp(colorMax, 1.0, 0.5); // Halven it, it's too strong usually, looks especially broken in certain levels like the snow ones
        color = lerp(color, color * colorMax, saturate(color / colorMax));
#else
        float colorMax1 = max(max3(color), 1.0);
        colorMax *= colorMax1;

        // Note: this can look a bit weird on snow levels
        color = min(colorMax, color);
#endif // IMPROVED_COLOR_GRADING_TYPE >= 1
#endif // ENABLE_COLOR_TINTING
#else // !ENABLE_IMPROVED_COLOR_GRADING
        // Ignore the min() here.
        color = max(colorMin, color);
#endif // ENABLE_IMPROVED_COLOR_GRADING
    }

#else // !ENABLE_COLOR_GRADING

    // If we have no color grading, just do tonemap
    if (!forceVanillaSDR)
        color = ApplyTonemap(color);

#endif // ENABLE_COLOR_GRADING

#if ENABLE_VIGNETTE
    // -------------------------------------------------------------------------
    // Radial vignette
    // -------------------------------------------------------------------------

    float radius = length(vignetteCoord.xy); // UW friendly (stretched vignette)
    float vignette = saturate(radius * vignetteScale + vignetteBias);
    vignette = pow(vignette, 1.4);
    float sceneWeight = 1.0 - vignette;
    color *= sceneWeight;
#endif // ENABLE_VIGNETTE

    // -------------------------------------------------------------------------
    // Fade
    // -------------------------------------------------------------------------

    // Hopefully the target color is always <= 1
    color = lerp(color, fadeColor, fadeBlend); // TODO: improve? In case it raised blacks? Restore original luminance? It's probably only used for fades to black (but we can't know at runtime which type of fade it is).
    
    if (forceVanillaSDR)
    {
        // Emulate UNORM
        color = saturate(color);
    }

    output = float4(color, 1.0);
}
