#ifndef SRC_COLOR_HLSL
#define SRC_COLOR_HLSL

#include "Math.hlsl"

// TODO: this will spread everywhere, make sure it's localized
// Needed by "linearToLog()" and "logToLinear()"
//#pragma warning( disable : 4122 )

// SDR linear mid gray.
// This is based on the commonly used value, though perception space mid gray (0.5) in sRGB or Gamma 2.2 would theoretically be ~0.2155 in linear.
static const float MidGray = 0.18f;
#ifdef CUSTOM_SDR_GAMMA
static const float DefaultGamma = CUSTOM_SDR_GAMMA;
#else
static const float DefaultGamma = 2.2f;
#endif
static const float3 Rec709_Luminance = float3(0.2126f, 0.7152f, 0.0722f);
static const float3 Rec2020_Luminance = float3(0.2627066f, 0.6779996f, 0.0592938f);
static const float3 AP1_Luminance = float3(0.2722287168f, 0.6740817658f, 0.0536895174f);
static const float HDR10_MaxWhiteNits = 10000.0f;
static const float ITU_WhiteLevelNits = 203.0f;
static const float Rec709_WhiteLevelNits = 100.0f;
static const float sRGB_WhiteLevelNits = 80.0f;

// "Gamma" clamp type "enum":
// 0 None
// 1 Remove negative numbers
// 2 Remove numbers beyond 0-1
// 3 Mirror negative numbers before and after encoding
#define GCT_NONE 0
#define GCT_POSITIVE 1
#define GCT_SATURATE 2
#define GCT_MIRROR 3

#ifndef GCT_DEFAULT
#define GCT_DEFAULT GCT_NONE
#endif

static const float3x3 BT709_2_XYZ = float3x3
  (0.412390798f,  0.357584327f, 0.180480793f,
   0.212639003f,  0.715168654f, 0.0721923187f, // ~same as "Rec709_Luminance"
   0.0193308182f, 0.119194783f, 0.950532138f);

static const float3x3 BT2020_2_XYZ = float3x3
  (0.6369580483f, 0.1446169036f, 0.1688809752f,
   0.2627002120f, 0.6779980715f, 0.0593017165f,
   0.0000000000f, 0.0280726930f, 1.0609850577f);

static const float3x3 AP1_2_XYZ = float3x3
	(0.6624541811f, 0.1340042065f, 0.1561876870f,
    0.2722287168f, 0.6740817658f, 0.0536895174f,
    -0.0055746495f, 0.0040607335f, 1.0103391003f);

static const float3x3 XYZ_2_BT2020 = float3x3
	(1.7166511880f, -0.3556707838f, -0.2533662814f,
    -0.6666843518f, 1.6164812366f, 0.0157685458f,
    0.0176398574f, -0.0427706133f, 0.9421031212f);

static const float3x3 XYZ_2_BT709 = float3x3
	(3.2409699419f, -1.5373831776f, -0.4986107603f,
    -0.9692436363f, 1.8759675015f, 0.0415550574f,
    0.0556300797f, -0.2039769589f, 1.0569715142f);



#define CS_BT709 0
#define CS_BT2020 1
#define CS_AP1 2

#define CS_DEFAULT CS_BT709

float GetLuminance(float3 color, uint colorSpace = CS_DEFAULT)
{
	if (colorSpace == CS_BT2020)
	{
		return dot(color, Rec2020_Luminance);
	}
	else if (colorSpace == CS_AP1)
	{
		// AP1 is basically DCI-P3 with a D65 white point
		return dot(color, AP1_Luminance);
	}
	return dot(color, Rec709_Luminance);
}

// Sets the luminance of the source on the target
float3 RestoreLuminance(float3 targetColor, float sourceColorLuminance, bool safe = false, uint colorSpace = CS_DEFAULT)
{
  float targetColorLuminance = GetLuminance(targetColor, colorSpace);
  // Handles negative values and gives more tolerance for divisions by small numbers
  if (safe)
  {
#if 0 // Disabled as it doesn't seem to help (we'd need to set the threshold to "0.001" (which is too high) for this to pick up the cases where divisions end up denormalizing the number etc)
    if (abs(targetColorLuminance - sourceColorLuminance) <= FLT_EPSILON)
    {
      return targetColor;
    }
#endif
    targetColorLuminance = max(targetColorLuminance, 0.0);
    sourceColorLuminance = max(sourceColorLuminance, 0.0);
#if 1
    return targetColor * (targetColorLuminance <= (FLT_EPSILON * 10.0) ? 0.0 : (sourceColorLuminance / targetColorLuminance)); // Empyrically found threshold. Note that this will zero the target color, flattining its RGB ratio (it might have had dithering)
#else
    return targetColor * safeDivision(sourceColorLuminance, targetColorLuminance, 0);
#endif
  }
  return targetColor * safeDivision(sourceColorLuminance, targetColorLuminance, 1);
}
float3 RestoreLuminance(float3 targetColor, float3 sourceColor, bool safe = false, uint colorSpace = CS_DEFAULT)
{
  return RestoreLuminance(targetColor, GetLuminance(sourceColor, colorSpace), safe, colorSpace);
}

// Returns the mathematical chrominance (it's more like saturation, not necessarily perceptual)
// Note: the result might depend on the color space
float GetChrominance(float3 color)
{
    float maxVal = max3(color);
    float minVal = min3(color);
    // If the minimum value was out of gamut, chrominance would be bigger than 1.
    // If the color rgb ratio was invalid (negative luminance, all negative channels etc),
    // the chrominance would be found to be around 1 anyway, which is fine, unless we wanted to force it to 0.
    float chrominance = (maxVal - minVal) / maxVal;
    return (maxVal == 0.0) ? 0.0 : chrominance;
}

// Note: this sets the relative chrominance, not absolute, so an input value of 1 leaves the color unchanged.
// Note: this changes the luminance of a color, possibly increasing it by a good amount
// Note: the result might depend on the color space
float3 SetChrominance(float3 color, float chrominance)
{
    float maxVal = max3(color);
    float minVal = min3(color);
	// The actual color that wasn't either the min nor max doesn't matter, what matter is the mid point of min/max
    float midVal = lerp(minVal, maxVal, 0.5);
    return lerp(midVal, color, chrominance);
}

// Returns 1 if the color is a greyscale, more otherwise
// Note: the result of this depends on the color space
float GetSaturation(float3 color, uint colorSpace = CS_DEFAULT)
{
	float luminance = GetLuminance(color, colorSpace);
	return (luminance == 0.0) ? 1.0 : max3(color) / luminance; // Find the ratio of the color against the luminance
}

float3 Saturation(float3 color, float saturation, uint colorSpace = CS_DEFAULT)
{
	float luminance = GetLuminance(color, colorSpace);
	return lerp(luminance, color, saturation);
}

float3 RestoreSaturation(float3 sourceColor, float3 targetColor, uint colorSpace = CS_DEFAULT)
{
	float sourceSaturation = GetSaturation(sourceColor, colorSpace);
	float targetSaturation = GetSaturation(targetColor, colorSpace);
	float saturationRatio = safeDivision(sourceSaturation, targetSaturation, 1);
	return Saturation(targetColor, saturationRatio, colorSpace);
}

// Note: the result of this depends on the color space
float3 RestoreChrominance(float3 sourceColor, float3 targetColor, uint colorSpace = CS_DEFAULT)
{
	float sourceChrominance = GetChrominance(sourceColor);
	float targetChrominance = GetChrominance(targetColor);
	float chrominanceRatio = safeDivision(sourceChrominance, targetChrominance, 1);
	return SetChrominance(targetColor, chrominanceRatio);
}

// TODO: move to a global color tonemapping/grading file?
// Emulates the highlights desaturation from a per channel tonemapper (a generic one, the math here isn't specific), up to a certain peak brightness (it doesn't need to match your display, it can be picked for consistent results independently of the user calibration).
// This doesn't perfectly match the hue shifts from games that purely lacked tonemapping and simply clipped to 0-1, but it might help with them too.
// This can also be used to increase highlights saturation ("desaturationExponent" < 1), in a way that would have matched a per channel tonemaper desaturation, in an "AutoHDR" inverse tonemapping fashion.
// Note: the result of this depends on the color space, and that is intentional as it wants to keep within the target gamut.
float3 CorrectPerChannelTonemapHiglightsDesaturation(float3 color, float peakBrightness, float desaturationExponent = 2.0, uint colorSpace = CS_DEFAULT)
{
    float sourceChrominance = GetChrominance(color);

    float maxBrightness = max3(color); // Do it by rgb max as opposed to by luminance (or average), otherwise blue would get almost no influence, this is a mathematical formula, not strictly perceptual
	float midBrightness = GetMidValue(color);
	float minBrightness = min3(color);
	float brightnessRatio = saturate(maxBrightness / peakBrightness);
	// Desaturate more if the mid brightness was close to the max brightness, as that's how per channel tonemappers work too.
	// Do a sqrt on the brightness ratio to get closer to perception (pow 1/3 might be even better but whatever, we can expose it if necessary).
	// Note that we could go even more aggressive here than a sqrt.
	brightnessRatio = lerp(brightnessRatio, sqrt(brightnessRatio), sqrt(InverseLerp(minBrightness, maxBrightness, midBrightness)));

    // Use pow to modulate chrominance, because if it was ~1, we need to keep it intact, given that per channel tonemapping wouldn't affect it.
    // We only desaturate highlights, to keep mid tones punchy.
	// Beyond 1 we flip the direction, otherwise the math would boost chrominance.
	// Chrominance can't be below 0 so we don't consider that case.
	float chrominancePow = lerp(1.0, 1.0 / desaturationExponent, brightnessRatio);
    float targetChrominance = sourceChrominance > 1.0 ? pow(sourceChrominance, chrominancePow) : (1.0 - pow(1.0 - sourceChrominance, chrominancePow));
    float chrominanceRatio = safeDivision(targetChrominance, sourceChrominance, 1);
#if 1 // Keeping the original luminance just looks better compared to not doing it
    return RestoreLuminance(SetChrominance(color, chrominanceRatio), color, true, colorSpace);
#elif 1
    return SetChrominance(color, chrominanceRatio);
#else
    // We can't simply change the min, max or mid colors independently to change chrominance, or we'd heavily shift the luminance, so we use the saturation formula.
    return Saturation(color, chrominanceRatio, colorSpace);
#endif
}

// This basically does gamut mapping, however it's not focused on gamut as primaries, but on peak white (MaxRange).
// The color is expected to be in the specified color space and in linear; ideally previously tonemapped by luminance to the same peak value.
// Tonemapping by luminance can cause blue and red values to "overshoot" beyond the RGB peak of the display, especially blue, given that it's roughly 10 times less luminous than green (green can never overshoot, as it's the brightest color).
// 
// "PositivesDesaturationVsDarkeningRatio" determines how much we do desaturation vs darkening to get the highlights back in range ("FixPositives" only). We always correct up to a 100%, but we can get there in both ways.
// For best results, it's generally better to do 50% desaturation or more. If darkening is done, highlights will simply flatten and lose detail, making them look out of place; while with desaturation at least their color will change, giving a more natural look.
// However if desaturation is done to 100%, this would turn too white (and kinda hue shift, given that doing desaturation in RGB doesn't closely match perception), so doing some darkening is a good workaround to avoid heavy desaturation or clipping.
// 
// "PositivesSmoothingRatio" should be from 0 to 1, mapping the smoothing start from "0" to "MaxRange"; usually a something like "0.2" would be a good start.
// It's there to prevent a step in gradients, when highlights start having out of range values.
// It starts smoothing in the darkening and desaturation earlier, so the results appear more natural (smooth).
float3 CorrectOutOfRangeColor(float3 Color, bool FixNegatives = true, bool FixPositives = true, float PositivesDesaturationVsDarkeningRatio = 1.0, float MaxRange = 1.0, float PositivesSmoothingRatio = 0.0, uint ColorSpace = CS_DEFAULT)
{
  if (FixNegatives && any(Color < 0.0)) // Optional "optimization" branch
  {
    float colorLuminance = GetLuminance(Color, ColorSpace);

    float3 positiveColor = max(Color, 0.0);
	float3 negativeColor = min(Color, 0.0);
	float positiveLuminance = GetLuminance(positiveColor, ColorSpace);
	float negativeLuminance = GetLuminance(negativeColor, ColorSpace);
	// Desaturate until we are not out of gamut anymore
	if (colorLuminance > FLT_MIN)
	{
	  // Desaturate (move towards luminance/grayscale) until no channel is below 0
	  float minChannel = min3(Color);
	  float desaturateAlpha = safeDivision(minChannel, minChannel - colorLuminance, 0); // Both division elements are meant to be negative so the ratio resolves to a positive value
	  Color = lerp(Color, colorLuminance, desaturateAlpha);
	}
#if 0 // Disabled as this won't actually constrain the results within the gamut, and there's no proper way for it to do it really (not without raising the brightness), given that it sets luminance to 0 while allowing some positive and negative values
	// Increase luminance until it's 0 if we were below 0 (it will clip out the negative gamut)
	else if (colorLuminance < -FLT_MIN)
	{
	  float negativePositiveLuminanceRatio = positiveLuminance / -negativeLuminance;
	  negativeColor *= negativePositiveLuminanceRatio;
	  Color = positiveColor + negativeColor;
	}
#endif
	// Snap to 0 if the overall luminance was zero (or possibly less), there's nothing to savage, no valid information on rgb ratio
	// (though we need to be careful with this as sometimes colors got subtracted and then expected the final result to clip away negative values, due to storing the result in UNORM render targets)
	else
	{
	  Color = 0.0;
	}
  }

  float colorPeak = max3(Color); // This is guaranteed to be >= "colorLuminance"
  float startRange = MaxRange * (1.0 - PositivesSmoothingRatio);
  float smoothedRange = PositivesSmoothingRatio > 0.0 ? clamp(colorPeak, startRange, MaxRange) : MaxRange; // Smooth the range by rgb max, not luminance, as luminance could be very low if we only have blue, and yet the rgb max could be out of range
  if (FixPositives && colorPeak > startRange) // Optional "optimization" branch
  {	
	// Find out the required darkening and desaturation amounts to contain the color within the max range value.
  	float colorLuminance = GetLuminance(Color, ColorSpace); // Expected to be > 0 if we got here, otherwise run "FixNegatives".
  	float targetLuminance = min(colorLuminance, smoothedRange);
    float colorLuminanceInExcess = targetLuminance - smoothedRange; // This would be ~0 if we previously tonemapped by luminance to the same peak, otherwise it might be negative...
    float maxColorInExcess = colorPeak - smoothedRange; // This is guaranteed to be >= "colorLuminanceInExcess"
#if 0
    float desaturateAlpha = saturate(maxColorInExcess / (maxColorInExcess - colorLuminanceInExcess));
#else // Extra safety possibly not needed but do it for now, float has precision loss...
    float desaturateAlpha = saturate(safeDivision(maxColorInExcess, maxColorInExcess - colorLuminanceInExcess, 0)); // Fall back to zero in case of division by zero
#endif
	
	// The sum of these need to be 1 to properly contain the color peak channel within the max allowed range,
	// though theoretically we could set these values independently (as long as their sum is <= 1)\.
	float DarkeningAmount = 1.0 - PositivesDesaturationVsDarkeningRatio;
	float DesaturationAmount = PositivesDesaturationVsDarkeningRatio;

	// Desaturate to contain rgb within the peak, on each channel
    float3 newColor = lerp(Color, targetLuminance, desaturateAlpha * DesaturationAmount);

	// If desaturation didn't fully contain rgb within the peak, shrink color to peak, maintaining the hue at the cost of brightness.
#if 0
    float darkeningInvAlpha = saturate(smoothedRange / max3(newColor));
#else // Extra safety possibly not needed but do it for now, float has precision loss...
    float darkeningInvAlpha = saturate(safeDivision(smoothedRange, max3(newColor), 1)); // Fall back to one in case of division by zero
#endif
	newColor *= darkeningInvAlpha;
	
	if (PositivesSmoothingRatio <= 0.0)
	{
		Color = newColor;
	}
	else
	{
		// At 0 we at the beginning of the smoothing range, at 1 at the end (meaning we don't smooth anymore, the correction applies at 100% intensity)
#if 0 // TODO: try both versions, smooth step is a bit random here, but might actually look better
		float smoothingProgress = smoothstep(startRange, MaxRange, colorPeak);
#else
		float smoothingProgress = saturate(InverseLerp(startRange, MaxRange, colorPeak));
#endif
		Color = lerp(Color, newColor, smoothingProgress);
	}
  }

  return Color;
}

float3 linear_to_gamma(float3 Color, int ClampType = GCT_DEFAULT, float Gamma = DefaultGamma)
{
	float3 colorSign = Sign_Fast(Color);
	if (ClampType == GCT_POSITIVE)
		Color = max(Color, 0.f);
	else if (ClampType == GCT_SATURATE)
		Color = saturate(Color);
	else if (ClampType == GCT_MIRROR)
		Color = abs(Color);
	Color = pow(Color, 1.f / Gamma);
	if (ClampType == GCT_MIRROR)
		Color *= colorSign;
	return Color;
}

// 1 component
float gamma_to_linear1(float Color, int ClampType = GCT_DEFAULT, float Gamma = DefaultGamma)
{
	float colorSign = Sign_Fast(Color);
	if (ClampType == GCT_POSITIVE)
		Color = max(Color, 0.f);
	else if (ClampType == GCT_SATURATE)
		Color = saturate(Color);
	else if (ClampType == GCT_MIRROR)
		Color = abs(Color);
	Color = pow(Color, Gamma);
	if (ClampType == GCT_MIRROR)
		Color *= colorSign;
	return Color;
}

// 1 component
float linear_to_gamma1(float Color, int ClampType = GCT_DEFAULT, float Gamma = DefaultGamma)
{
	float colorSign = Sign_Fast(Color);
	if (ClampType == GCT_POSITIVE)
		Color = max(Color, 0.f);
	else if (ClampType == GCT_SATURATE)
		Color = saturate(Color);
	else if (ClampType == GCT_MIRROR)
		Color = abs(Color);
	Color = pow(Color, 1.f / Gamma);
	if (ClampType == GCT_MIRROR)
		Color *= colorSign;
	return Color;
}

float3 gamma_to_linear(float3 Color, int ClampType = GCT_DEFAULT, float Gamma = DefaultGamma)
{
	float3 colorSign = Sign_Fast(Color);
	if (ClampType == GCT_POSITIVE)
		Color = max(Color, 0.f);
	else if (ClampType == GCT_SATURATE)
		Color = saturate(Color);
	else if (ClampType == GCT_MIRROR)
		Color = abs(Color);
	Color = pow(Color, Gamma);
	if (ClampType == GCT_MIRROR)
		Color *= colorSign;
	return Color;
}

float gamma_sRGB_to_linear1(float Channel, int ClampType = GCT_DEFAULT)
{
	float channelSign = Sign_Fast(Channel);
	if (ClampType == GCT_POSITIVE)
		Channel = max(Channel, 0.f);
	else if (ClampType == GCT_SATURATE)
		Channel = saturate(Channel);
	else if (ClampType == GCT_MIRROR)
		Channel = abs(Channel);

	if (Channel <= 0.04045f)
		Channel = Channel / 12.92f;
	else
		Channel = pow((Channel + 0.055f) / 1.055f, 2.4f);
		
	if (ClampType == GCT_MIRROR)
		Channel *= channelSign;

	return Channel;
}

// The sRGB gamma formula already works beyond the 0-1 range but mirroring (and thus running the pow below 0 too) makes it look better
float3 gamma_sRGB_to_linear(float3 Color, int ClampType = GCT_DEFAULT)
{
	float3 colorSign = Sign_Fast(Color);
	if (ClampType == GCT_POSITIVE)
		Color = max(Color, 0.f);
	else if (ClampType == GCT_SATURATE)
		Color = saturate(Color);
	else if (ClampType == GCT_MIRROR)
		Color = abs(Color);
	Color = float3(gamma_sRGB_to_linear1(Color.r, GCT_NONE), gamma_sRGB_to_linear1(Color.g, GCT_NONE), gamma_sRGB_to_linear1(Color.b, GCT_NONE));
	if (ClampType == GCT_MIRROR)
		Color *= colorSign;
	return Color;
}

float linear_to_sRGB_gamma1(float Channel, int ClampType = GCT_DEFAULT)
{
	float channelSign = Sign_Fast(Channel);
	if (ClampType == GCT_POSITIVE)
		Channel = max(Channel, 0.f);
	else if (ClampType == GCT_SATURATE)
		Channel = saturate(Channel);
	else if (ClampType == GCT_MIRROR)
		Channel = abs(Channel);

	if (Channel <= 0.0031308f)
		Channel = Channel * 12.92f;
	else
		Channel = 1.055f * pow(Channel, 1.f / 2.4f) - 0.055f;
		
	if (ClampType == GCT_MIRROR)
		Channel *= channelSign;

	return Channel;
}

// The sRGB gamma formula already works beyond the 0-1 range but mirroring (and thus running the pow below 0 too) makes it look better
float3 linear_to_sRGB_gamma(float3 Color, int ClampType = GCT_DEFAULT)
{
	float3 colorSign = Sign_Fast(Color);
	if (ClampType == GCT_POSITIVE)
		Color = max(Color, 0.f);
	else if (ClampType == GCT_SATURATE)
		Color = saturate(Color);
	else if (ClampType == GCT_MIRROR)
		Color = abs(Color);
	Color = float3(linear_to_sRGB_gamma1(Color.r, GCT_NONE), linear_to_sRGB_gamma1(Color.g, GCT_NONE), linear_to_sRGB_gamma1(Color.b, GCT_NONE));
	if (ClampType == GCT_MIRROR)
		Color *= colorSign;
	return Color;
}

// Optimized gamma<->linear functions (don't use unless really necessary, they are not accurate)
// TODO: move these to Math.hlsl? Also merge them with the others that are already there...
float sqr_mirrored(float x)
{
	return sqr(x) * Sign_Fast(x);
}
float sqrt_mirrored(float x)
{
	return sqrt(abs(x)) * Sign_Fast(x);
}
float pow_mirrored(float x, float y)
{
	return pow(abs(x), y) * Sign_Fast(x);
}
float3 sqr_mirrored(float3 x)
{
	return sqr(x) * Sign_Fast(x);
}
float3 sqrt_mirrored(float3 x)
{
	return sqrt(abs(x)) * Sign_Fast(x);
}
float3 pow_mirrored(float3 x, float3 y)
{
	return pow(abs(x), y) * Sign_Fast(x);
}
float4 sqr_mirrored(float4 x)
{
	return sqr(x) * Sign_Fast(x);
}
float4 sqrt_mirrored(float4 x)
{
	return sqrt(abs(x)) * Sign_Fast(x);
}
float4 pow_mirrored(float4 x, float4 y)
{
	return pow(abs(x), y) * Sign_Fast(x);
}

static const float PQ_constant_M1 =  0.1593017578125f;
static const float PQ_constant_M2 = 78.84375f;
static const float PQ_constant_C1 =  0.8359375f;
static const float PQ_constant_C2 = 18.8515625f;
static const float PQ_constant_C3 = 18.6875f;

// PQ (Perceptual Quantizer - ST.2084) encode/decode used for HDR10 BT.2100.
// Input is expected to be pre-normalized in 0-1 range, supposedly, but not necessarily in the HDR10 range ("HDR10_MaxWhiteNits").
float3 Linear_to_PQ(float3 LinearColor, int clampType = GCT_DEFAULT, float Exponent = 1.0)
{
	float3 LinearColorSign = Sign_Fast(LinearColor);
	if (clampType == GCT_POSITIVE)
		LinearColor = max(LinearColor, 0.f);
	else if (clampType == GCT_SATURATE)
		LinearColor = saturate(LinearColor);
	else if (clampType == GCT_MIRROR)
		LinearColor = abs(LinearColor);
	float3 colorPow = pow(LinearColor, PQ_constant_M1);
	float3 numerator = PQ_constant_C1 + PQ_constant_C2 * colorPow;
	float3 denominator = 1.f + PQ_constant_C3 * colorPow;
	float3 pq = pow(numerator / denominator, PQ_constant_M2 * Exponent);
	if (clampType == GCT_MIRROR)
		return pq * LinearColorSign;
	return pq;
}

float Linear_to_PQ(float LinearColor, int clampType = GCT_DEFAULT, float Exponent = 1.0)
{
	float colorSign = Sign_Fast(LinearColor);
	if (clampType == GCT_POSITIVE)
		LinearColor = max(LinearColor, 0.f);
	else if (clampType == GCT_SATURATE)
		LinearColor = saturate(LinearColor);
	else if (clampType == GCT_MIRROR)
		LinearColor = abs(LinearColor);
	float colorPow = pow(LinearColor, PQ_constant_M1);
	float numerator = PQ_constant_C1 + PQ_constant_C2 * colorPow;
	float denominator = 1.f + PQ_constant_C3 * colorPow;
	float pq = pow(numerator / denominator, PQ_constant_M2 * Exponent);
	if (clampType == GCT_MIRROR)
		return pq * colorSign;
	return pq;
}

float3 PQ_to_Linear(float3 ST2084Color, int clampType = GCT_DEFAULT, float Exponent = 1.0)
{
	float3 ST2084ColorSign = Sign_Fast(ST2084Color);
	if (clampType == GCT_POSITIVE)
		ST2084Color = max(ST2084Color, 0.f);
	else if (clampType == GCT_SATURATE)
		ST2084Color = saturate(ST2084Color);
	else if (clampType == GCT_MIRROR)
		ST2084Color = abs(ST2084Color);
	float3 colorPow = pow(ST2084Color, 1.f / (PQ_constant_M2 * Exponent));
	float3 numerator = max(colorPow - PQ_constant_C1, 0.f);
	float3 denominator = PQ_constant_C2 - (PQ_constant_C3 * colorPow);
	float3 linearColor = pow(numerator / denominator, 1.f / PQ_constant_M1);
	if (clampType == GCT_MIRROR)
		return linearColor * ST2084ColorSign;
	return linearColor;
}

float PQ_to_Linear(float ST2084Color, int clampType = GCT_DEFAULT, float Exponent = 1.0)
{
	float colorSign = Sign_Fast(ST2084Color);
	if (clampType == GCT_POSITIVE)
		ST2084Color = max(ST2084Color, 0.f);
	else if (clampType == GCT_SATURATE)
		ST2084Color = saturate(ST2084Color);
	else if (clampType == GCT_MIRROR)
		ST2084Color = abs(ST2084Color);
	float colorPow = pow(ST2084Color, 1.f / (PQ_constant_M2 * Exponent));
	float numerator = max(colorPow - PQ_constant_C1, 0.f);
	float denominator = PQ_constant_C2 - (PQ_constant_C3 * colorPow);
	float linearColor = pow(numerator / denominator, 1.f / PQ_constant_M1);
	if (clampType == GCT_MIRROR)
		return linearColor * colorSign;
	return linearColor;
}

// This defines the range you want to cover under log2: 2^14 = 16384,
// 14 is the minimum value to cover 10k nits.
static const float LogLinearRange = 14.f;
// This is the grey point you want to adjust with the "exposure grey" parameter
static const float LogLinearGrey = 0.18f;
// This defines what an input matching the "linear grey" parameter will end up at in log space
static const float LogGrey = 1.f / 3.f;

// Note that an input of zero will not match and output of zero.
float3 linearToLog_internal(float3 linearColor, float3 logGrey = LogGrey)
{
	return (log2(linearColor) / LogLinearRange) - (log2(LogLinearGrey) / LogLinearRange) + logGrey;
}
// "logColor" is expected to be != 0 (is it...? Why?).
float3 logToLinear_internal(float3 logColor, float3 logGrey = LogGrey)
{
#pragma warning( disable : 4122 ) // Note: this doesn't work here
	return exp2((logColor - logGrey) * LogLinearRange) * LogLinearGrey;
#pragma warning( default : 4122 )
}


// Perceptual encoding functions (more "accurate" than HDR10 PQ).
// "linearColor" is expected to be >= 0 and with a white point around 80-100.
// These function are "normalized" so that they will map a linear color value of 0 to a log encoding of 0.
float3 linearToLog(float3 linearColor, int clampType = GCT_DEFAULT, float3 logGrey = LogGrey)
{
	float3 linearColorSign = Sign_Fast(linearColor);
	if (clampType == GCT_POSITIVE || clampType == GCT_SATURATE)
		linearColor = max(linearColor, 0.f);
	else if (clampType == GCT_MIRROR)
		linearColor = abs(linearColor);
    float3 normalizedLogColor = linearToLog_internal(linearColor + logToLinear_internal(FLT_MIN, logGrey), logGrey);
	if (clampType == GCT_MIRROR)
		normalizedLogColor *= linearColorSign;
	return normalizedLogColor;
}
float3 logToLinear(float3 normalizedLogColor, int clampType = GCT_DEFAULT, float3 logGrey = LogGrey)
{
	float3 normalizedLogColorSign = Sign_Fast(normalizedLogColor);
	if (clampType == GCT_MIRROR)
		normalizedLogColor = abs(normalizedLogColor);
	float3 linearColor = max(logToLinear_internal(normalizedLogColor, logGrey) - logToLinear_internal(FLT_MIN, logGrey), 0.f);
	if (clampType == GCT_MIRROR)
		linearColor *= normalizedLogColorSign;
	return linearColor;
}

static const float3x3 BT709_2_BT2020 = {
	0.627403914928436279296875f,      0.3292830288410186767578125f,  0.0433130674064159393310546875f,
	0.069097287952899932861328125f,   0.9195404052734375f,           0.011362315155565738677978515625f,
	0.01639143936336040496826171875f, 0.08801330626010894775390625f, 0.895595252513885498046875f };

static const float3x3 BT2020_2_BT709 = {
	 1.66049098968505859375f,          -0.58764111995697021484375f,     -0.072849862277507781982421875f,
	-0.12455047667026519775390625f,     1.13289988040924072265625f,     -0.0083494223654270172119140625f,
	-0.01815076358616352081298828125f, -0.100578896701335906982421875f,  1.11872971057891845703125f };

// SMPTE 170M - BT.601 (NTSC-M) (USA) -> BT.709
static const float3x3 BT601_2_BT709 = {
    0.939497225737661f,					0.0502268452914346f,			0.0102759289709032f,
    0.0177558637510127f,				0.965824605885027f,				0.0164195303639603f,
   -0.0016216320996701f,				-0.00437400622653655f,			1.00599563832621f };

// ARIB TR-B9 (9300K+27MPCD with vK20 and CAT02 chromatic adaptation) (NTSC-J) -> BT.709
static const float3x3 NTSCJ_2_BT709 = {
    0.768497526f, -0.210804164f, 0.000297427177f,
    0.0397904068f, 1.04825413f, 0.00555809540f,
    0.00147510506f, 0.0328789241f, 1.36515128f };

// EBU - BT.470BG/BT.601 (PAL) -> BT.709
static const float3x3 PAL_2_BT709 = {
	1.04408168421813,		-0.0440816842181253,	0.000000000000000,
	0.000000000000000,	1.00000000000000,			0.000000000000000,
	0.000000000000000,	0.0118044782106489,		0.988195521789351 };

float3 BT709_To_BT2020(float3 color)
{
	return mul(BT709_2_BT2020, color);
}

float3 BT2020_To_BT709(float3 color)
{
	return mul(BT2020_2_BT709, color);
}

float3 BT601_To_BT709(float3 color)
{
	return mul(BT601_2_BT709, color);
}

float3 FromColorSpaceToColorSpace(float3 color, uint colorSpaceIn, uint colorSpaceOut)
{
	if (colorSpaceIn == CS_BT709 && colorSpaceOut == CS_BT2020)
	{
		return BT709_To_BT2020(color);
	}
	else if (colorSpaceIn == CS_BT2020 && colorSpaceOut == CS_BT709)
	{
		return BT2020_To_BT709(color);
	}
	else if (colorSpaceIn == colorSpaceOut)
	{
		return color;
	}
	return float3(1, 0, 1); // Not implemented, return purple
}

static const float2 D65xy = float2(0.3127f, 0.3290f);

static const float2 R2020xy = float2(0.708f, 0.292f);
static const float2 G2020xy = float2(0.170f, 0.797f);
static const float2 B2020xy = float2(0.131f, 0.046f);

static const float2 R709xy = float2(0.64f, 0.33f);
static const float2 G709xy = float2(0.30f, 0.60f);
static const float2 B709xy = float2(0.15f, 0.06f);

static const float3x3 BT2020_To_XYZ = {
	0.636958062648773193359375f, 0.144616901874542236328125f,    0.1688809692859649658203125f,
	0.26270020008087158203125f,  0.677998065948486328125f,       0.0593017153441905975341796875f,
	0.f,                         0.028072692453861236572265625f, 1.060985088348388671875f};

static const float3x3 XYZ_To_BT2020 = {
	 1.7166512012481689453125f,       -0.3556707799434661865234375f,   -0.253366291522979736328125f,
	-0.666684329509735107421875f,      1.61648118495941162109375f,      0.0157685466110706329345703125f,
	 0.0176398567855358123779296875f, -0.0427706129848957061767578125f, 0.9421031475067138671875f };

static const float3x3 BT709_To_XYZ = {
	0.4123907983303070068359375f,    0.3575843274593353271484375f,   0.18048079311847686767578125f,
	0.2126390039920806884765625f,    0.715168654918670654296875f,    0.072192318737506866455078125f,
	0.0193308182060718536376953125f, 0.119194783270359039306640625f, 0.950532138347625732421875f };

static const float3x3 XYZ_To_BT709 = {
	 3.2409698963165283203125f,      -1.53738319873809814453125f,  -0.4986107647418975830078125f,
	-0.96924364566802978515625f,      1.875967502593994140625f,     0.0415550582110881805419921875f,
	 0.055630080401897430419921875f, -0.2039769589900970458984375f, 1.05697154998779296875f };

float3 XYZToxyY(float3 XYZ)
{
	const float xyz = XYZ.x + XYZ.y + XYZ.z;
	float x = XYZ.x / xyz;
	float y = XYZ.y / xyz;
	return float3(x, y, XYZ.y);
}

float3 xyYToXYZ(float3 xyY)
{
	float X = (xyY.x / xyY.y) * xyY.z;
	float Z = ((1.f - xyY.x - xyY.y) / xyY.y) * xyY.z;
	return float3(X, xyY.z, Z);
}

float GetM(float2 A, float2 B)
{
	return (B.y - A.y) / (B.x - A.x);
}

float2 LineIntercept(float MP, float2 FromXYCoords, float2 ToXYCoords, float2 WhitePointXYCoords = D65xy)
{
	const float m = GetM(FromXYCoords, ToXYCoords);
	const float m_mul_xyx = m * FromXYCoords.x;

	const float m_minus_MP = m - MP;
	const float MP_mul_WhitePoint_xyx = MP * WhitePointXYCoords.x;

	float x = (-MP_mul_WhitePoint_xyx + WhitePointXYCoords.y - FromXYCoords.y + m_mul_xyx) / m_minus_MP;
	float y = (-WhitePointXYCoords.y * m + m * MP_mul_WhitePoint_xyx + FromXYCoords.y * MP - m_mul_xyx * MP) / -m_minus_MP;
	return float2(x, y);
}

// convert hue, saturation, value to RGB
// https://en.wikipedia.org/wiki/HSL_and_HSV
float3 HSV_To_RGB(float3 HSV)
{
    float h1 = HSV.x * 6.f;
    float c = HSV.z * HSV.y;
    float x = c * ( 1.f - abs(fmod(h1, 2.f) - 1.f));
    float3 rgb = 0.f;
    if( h1 <= 1.f )
        rgb = float3( c, x, 0.f );
    else if( h1 <= 2.f )
        rgb = float3( x, c, 0.f );
    else if( h1 <= 3.f )
        rgb = float3( 0.f, c, x );
    else if( h1 <= 4.f )
        rgb = float3( 0.f, x, c );
    else if( h1 <= 5.f )
        rgb = float3( x, 0.f, c );
    else if( h1 <= 6.f )
        rgb = float3( c, 0.f, x );
    float m = HSV.z - c;
    return float3(rgb.x + m, rgb.y + m, rgb.z + m);
}
// Works in every color space
float3 HueToRGB(float h, bool hideWhite = false, float inverseChrominance = 0.0)
{
    const uint raw_N = 7;
    uint N = raw_N;
	if (hideWhite) N--;
    float interval = 1.0 / N;

	if (!hideWhite) // Start from white
		h -= interval;
    float t = frac(h); // Loop around
    //float t = abs(frac(h * 0.5) * 2.0 - 1.0); // Loop around

    // 7 non-empty RGB combinations
	// From left to right (smaller "h" to bigger "h")
    float3 combos[raw_N] = {
        float3(1,0,0), // R: Red
        float3(1,1,inverseChrominance), // R+G: Yellow
        float3(0,1,0), // G: Green
        float3(inverseChrominance,1,1), // G+B: Cyan
        float3(0,0,1), // B: Blue
        float3(1,inverseChrominance,1), // R+B: Magenta
        float3(1,1,1)  // R+G+B: White (optional)
    };
    
    uint i = min(uint(t / interval), N-1);
    uint next = (i + 1) % N;
    
    float localT = (t - i * interval) / interval;
	
	float powStrength = 0.666; // Makes transitions smoother with higher values
	localT = (localT - 0.5) * 2.0;
	localT = pow(abs(localT), powStrength) * sign(localT);
	localT = (localT * 0.5) + 0.5;
    
    // Blend between current and next combination
    return lerp(combos[i], combos[next], localT);
}

// With Bradford
static const float3x3 BT709_TO_AP1_MAT = float3x3(
    0.6130974024, 0.3395231462, 0.0473794514,
    0.0701937225, 0.9163538791, 0.0134523985,
    0.0206155929, 0.1095697729, 0.8698146342);

// With Bradford
static const float3x3 BT2020_TO_AP1_MAT = float3x3(
    0.9748949779f, 0.0195991086f, 0.0055059134f,
    0.0021795628f, 0.9955354689f, 0.0022849683f,
    0.0047972397f, 0.0245320166f, 0.9706707437f);

// With Bradford
static const float3x3 AP1_TO_BT709_MAT = float3x3(
    1.7050509927, -0.6217921207, -0.0832588720,
    -0.1302564175, 1.1408047366, -0.0105483191,
    -0.0240033568, -0.1289689761, 1.1529723329);

// With Bradford
static const float3x3 AP1_TO_BT2020_MAT = float3x3(
    1.0258247477f, -0.0200531908f, -0.0057715568f,
    -0.0022343695f, 1.0045865019f, -0.0023521324f,
    -0.0050133515f, -0.0252900718f, 1.0303034233f);

float3 BT709_To_AP1(float3 color)
{
	return mul(BT709_TO_AP1_MAT, color);
}

float3 AP1_To_BT709(float3 color)
{
	return mul(AP1_TO_BT709_MAT, color);
}

float3 BT2020_To_AP1(float3 color)
{
	return mul(BT2020_TO_AP1_MAT, color);
}

float3 AP1_To_BT2020(float3 color)
{
	return mul(AP1_TO_BT2020_MAT, color);
}

// TODO: clean up
// Converts a video to BT.709 (gamma space)
float3 YUVtoRGB(float Y, float Cr, float Cb, uint type = 0)
{
  float V = Cr;
  float U = Cb;

	float3 color = 0.0;
	// usually in YCbCr the ranges are (in float):
	// Y:   0.0-1.0
	// Cb: -0.5-0.5
	// Cr: -0.5-0.5
	// but since this is a digital signal (in unsinged 8bit: 0-255) it's now:
	// Y:  0.0-1.0
	// Cb: 0.0-1.0
	// Cr: 0.0-1.0
  if (type == 0) { // Rec.709 full range
    color.r = (Y - 0.790487825870513916015625f) + (Cr * 1.5748f);
    color.g = (Y + 0.329009473323822021484375f) - (Cr * 0.46812427043914794921875f) - (Cb * 0.18732427060604095458984375f);
    color.b = (Y - 0.931438446044921875f)       + (Cb * 1.8556f);
  } else if (type == 1) { // Rec.709 limited range
    Y *= 1.16438353f;
    color.r = (Y - 0.972945094f) + (Cr * 1.79274106f);
    color.g = (Y + 0.301482677f) - (Cr * 0.532909333f) - (Cb * 0.213248610f);
    color.b = (Y - 1.13340222f)  + (Cb * 2.11240172f);
  } else if (type == 2) { // Rec.601 full range
    color += Cr * float3(1.59579468, -0.813476563, 0.0); 
    color += Y * 1.16412354;
    color += Cb * float3(0,-0.391448975, 2.01782227);
    color += float3(-0.87065506, 0.529705048, -1.08166885); // Bias offsets
    color = color * 0.858823538 + 0.0627451017; // limited to full range
  } else { // Rec.601 limited range
    Y *= 1.16412353515625f;
    color.r = (Y - 0.870655059814453125f) + (Cr * 1.595794677734375f);
    color.g = (Y + 0.529705047607421875f) - (Cr * 0.8134765625f) - (Cb * 0.391448974609375f);
    color.b = (Y - 1.081668853759765625f) + (Cb * 2.017822265625f);
  }

  return color;
}

// Linear in/out
// Can emulate shadow crush/clip from bad lut sampling, or from interpreting full range videos as limited range videos etc.
float3 EmulateShadowClip(float3 Color, bool LinearInOut = true, float AdjustmentScale = 0.333)
{
  // "AdjustmentScale" is basically the added contrast curve strength
  float adjustmentRange = 1.0 / 3.0;
  float3 colorLinear = LinearInOut ? Color : gamma_to_linear(Color, GCT_MIRROR);
  float3 colorGamma = LinearInOut ? linear_to_gamma(Color, GCT_MIRROR) : Color;
  float3 finalColorLinear = colorLinear * lerp(AdjustmentScale, 1.0, saturate(colorGamma / adjustmentRange));
  return LinearInOut ? finalColorLinear : linear_to_gamma(finalColorLinear, GCT_MIRROR);
}

// Linear or Gamma in/out
// Modernizes old school grading that either clips or raises blacks, not really suitable for modern OLEDs.
// Perceptually raises shadow without raising the black floor.
float3 EmulateShadowOffset(float3 Color, float3 Offset, bool LinearInOut = true, bool SmoothOutLargeOffsets = true)
{
	// Whether the offset was removing color (causing clipping in SDR, and expanding the color range in HDR), or adding to color (raising blacks),
	// for a range matching double of its abs offset, don't fully apply the offset.
	// This means 0 will stay 0, while most of the image will still be affected.
	// This will also prevent levels from accidentally generating invalid negative values in HDR,
	// that would sometimes expand the gamut, but more often simply generate weird or broken colors.
	float range = LinearInOut ? 3.0 : 2.0; // Arbitrary but decent // TODO: try range... Also possibly expand it even more, with little effect beyond *1.5, but at least we'd avoid broken gradients
	float center = LinearInOut ? MidGray : 0.5; // A bit random but should be fine
	float3 alpha = saturate(Color / abs(Offset * range));

	// Another approximation
	// For the positive case, we do a sqrt to shift the intensity and make it look nicer.
	// For the negative case, we might do a sqr, otherwise the result would flatten itself to 0 for the whole range if range was 1,
	// and even if it wasn't, it'd generate negative values for an input of >= 0. Update: somehow doing "sqr" there breaks, so leave it "linear" (it doesn't seem to flatten either, likely because of the "range" making alpha smaller).
	if (!LinearInOut)
		alpha = Offset >= 0.0 ? sqrt(alpha) : alpha;

	// If levels go too high, force apply them anyway
	if (SmoothOutLargeOffsets)
		alpha = lerp(alpha, 1.0, saturate((abs(Offset) - center) * range));

	Color += Offset * alpha;
	return Color;
}

// If our color has any negative values (e.g. scRGB) and we want to multiply it by a factor,
// we should flip the scaling direction when the source color is below 0.
// For example, if the red multiplier was 1.5, meaning the final color would end up being more red,
// if we start from a negative red value (which would expand the gamut on blue/green),
// we don't want to further push the negative red value out (lower it),
// but increase it (bring it closer to 0) by dividing it, which will do its job of making the color more red.
// Do not use this if the scale is "uniform" and meant to simply do brightness scaling, because that should be a normal multiplication.
float3 MultiplyExtendedGamutColor(float3 Color, float3 Scale)
{
	return (Color >= 0.0 || Scale == 0.0) ? (Color * Scale) : (Color / Scale);
}

#endif // SRC_COLOR_HLSL