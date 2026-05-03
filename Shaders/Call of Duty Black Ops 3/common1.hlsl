#define LUT_3D 1
// #define LUT_SIZE 32u
// #define LUT_MAX 32u
// #define LUT_OUT_MULTIPLIER (1/32768.)

#include "./Includes/Common.hlsl"
#include "../Includes/Math.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "./Includes/ColorGrade.hlsl"
#include "./Includes/ictcp_portable.hlsl"

#define TRADE_SCALE HDR_PEAK * 0.25 /*  Still have no clue why 0.3 is good fudge factor. */

#define cmp -

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 UCSTo(float3 x, uint cs) {
  #if CUSTOM_UCS_TYPE == 0
    return JzAzBz::rgbToJzazbz(x, cs);
  #elif CUSTOM_UCS_TYPE == 1
    return Oklab::rgb_to_oklab(x, cs);
  #elif CUSTOM_UCS_TYPE == 2
    return renodx::color::ictcp::To(x, cs);
  #endif
}
float3 UCSFrom(float3 x, uint cs) {
  #if CUSTOM_UCS_TYPE == 0
    return JzAzBz::jzazbzToRgb(x, cs);
  #elif CUSTOM_UCS_TYPE == 1
    return Oklab::oklab_to_rgb(x, cs);
  #elif CUSTOM_UCS_TYPE == 2
    return renodx::color::ictcp::From(x, cs);
  #endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//REC709
#define DECODEREC709(T)\
T DecodeRec709(T x) {\
  T r0, r2, r3, r4;\
  r0 = x;\
  r2 = 0.0989999995 + r0; \
  r2 = 0.909918129 * r2;\
  r2 = pow(r2, 2.22222233);\
  r3 = cmp(0.0810000002 >= r0);\
  r4 = 0.222222224 * r0;\
  r2 = r3 ? r4 : r2;\
  return r2;\
}
DECODEREC709(float)
DECODEREC709(float3)
DECODEREC709(float4)
#undef DECODEREC709

#define ENCODEREC709(T)\
T EncodeRec709(T x) {\
  T r0, r1, r2;\
  r1 = x;\
  r0 = pow(r1, 0.449999988);\
  r0 = r0 * 1.09899998 + -0.0989999995;\
  r2 = cmp(0.0179999992 >= r1);\
  r1 = 4.5 * r1;\
  r0 = r2 ? r1 : r0;\
  return r0;\
}
ENCODEREC709(float)
ENCODEREC709(float3)
ENCODEREC709(float4)
#undef ENCODEREC709
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//From Musa (I think)
namespace Reinhard {
  float ReinhardPiecewiseExtended(float x, float white_max, float x_max = 1.f, float shoulder = 0.18f)
  {
     const float x_min = 0.f;
     float exposure = Reinhard::ComputeReinhardExtendableScale(white_max, x_max, x_min, shoulder, shoulder);
     float extended = Reinhard::ReinhardExtended(x * exposure, white_max * exposure, x_max);
     extended = min(extended, x_max);

     return lerp(x, extended, step(shoulder, x));
  }
  float3 ReinhardPiecewiseExtended(float3 x, float white_max, float x_max = 1.f, float shoulder = 0.18f)
  {
     const float x_min = 0.f;
     float exposure = Reinhard::ComputeReinhardExtendableScale(white_max, x_max, x_min, shoulder, shoulder);
     float3 extended = Reinhard::ReinhardExtended(x * exposure, white_max * exposure, x_max);
     extended = min(extended, x_max);

     return lerp(x, extended, step(shoulder, x));
  }

  float ComputeReinhardSmoothClampScale(float3 untonemapped, float rolloff_start = 0.5f, float output_max = 1.f, float white_clip = 100.f)
  {
     float peak = max3(untonemapped.r, untonemapped.g, untonemapped.b);
     float mapped_peak = ReinhardPiecewiseExtended(peak, white_clip, output_max, rolloff_start);
     float scale = safeDivision(mapped_peak, peak, 0);

     return scale;
  }

  namespace inverse {
    float3 ReinhardScalable(float3 color, float channel_max = 1.f, float channel_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f) {
      float exposure = (channel_max * (channel_min * gray_out + channel_min - gray_out))
                       / (gray_in * (gray_out - channel_max));

      float3 numerator = -channel_max * (channel_min * color + channel_min - color);
      float3 denominator = (exposure * (channel_max - color));
      return safeDivision(numerator, denominator, FLT16_MAX);
    }

    float ReinhardScalable(float color, float channel_max = 1.f, float channel_min = 0.f, float gray_in = 0.18f, float gray_out = 0.18f) {
      float exposure = (channel_max * (channel_min * gray_out + channel_min - gray_out))
                       / (gray_in * (gray_out - channel_max));

      float numerator = -channel_max * (channel_min * color + channel_min - color);
      float denominator = (exposure * (channel_max - color));
      return safeDivision(numerator, denominator, FLT16_MAX);
    }

    float3 Reinhard(float3 color) {
      return safeDivision(color, (1.f - color), FLT16_MAX);
    }

    float Reinhard(float color) {
      return safeDivision(color, (1.f - color), FLT16_MAX);
    }
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/hermite_spline.hlsl
namespace HermiteSpline {
  float Rescale(float x, float x_min, float x_max, float y_min = 0, float y_max = 1, bool clamp = false) {
    float value = lerp(y_min, y_max, (x - x_min) / (x_max - x_min));
    if (clamp) {
      value = saturate(value);
    }
    return value;
  }
  float HermiteSplineRolloff(float input, float target_white = 1.f, float max_white = 20.f) {
    float l_w = max_white;
    // float l_b = min_black;
    // float l_min = target_black;
    float l_max = target_white;
    float e_1 = Rescale(input, 0, l_w);
    // float min_lum = Rescale(l_min, l_b, l_w);
    float max_lum = Rescale(l_max, 0, l_w);
    float knee_start = 1.5f * max_lum - 0.5f;
    // float b = min_lum;
    float t_b = Rescale(e_1, knee_start, 1.f);

    // float p_e1 = (((2 * t_b * t_b * t_b) - (3 * t_b * t_b) + 1) * knee_start)
    //              + (((t_b * t_b * t_b) - (2 * t_b * t_b) + t_b) * (1.f - knee_start))
    //              + ((-(2 * t_b * t_b * t_b) + (3 * t_b * t_b)) * max_lum);
    float t_b_squared = t_b * t_b;
    float t_b_cubed = t_b_squared * t_b;
    float two_t_b_cubed = 2.f * t_b_cubed;
    float three_t_b_squared = 3.f * t_b_squared;
    float p_e1_h00 = (two_t_b_cubed - three_t_b_squared + 1.f);
    float p_e1_h10 = (t_b_cubed - 2.f * t_b_squared + t_b);
    float p_e1_h01 = (-two_t_b_cubed + three_t_b_squared);
    // float p_e1_h11 = (t_b_cubed - t_b_squared); // Not used since derivative is 0 at max_lum

    float p_e1 = p_e1_h00 * knee_start
                 + p_e1_h10 * (1.f - knee_start)
                 + p_e1_h01 * max_lum;

    float e_2 = (e_1 < knee_start) ? e_1 : p_e1;

    // float e_3 = e_2 + b * pow(1-e_2, 4);
    // float e_3a1 = (1 - e_2) * (1 - e_2);
    // float e_3a2 = e_3a1 * (1 - e_2);
    float e_3 = e_2;

    // Custom: clamp before lerp
    // e_3 = saturate(e_3);

    // float e_4 = lerp(l_b, l_w, e_3);
    float e_4 = l_w * e_3;

    return min(e_4, target_white);
  }
  float HermiteSplineLuminanceRolloff(float luminance, float target_white = 1.f, float max_white = 20.f) {
    if (luminance <= 0) return 0;
    return exp2(HermiteSplineRolloff(log2(luminance), log2(target_white), log2(max_white)));
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/neutwo.hlsl
namespace NeuTwo {
  float NeuTwo(float x, float peak) {
    // also written as x * rhypot(x, peak)
    float p = peak;

    float numerator = p * x;
    float denominator_squared = mad(x, x, p * p);
    return numerator * rsqrt(denominator_squared);
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Piecewise linear + exponential compression to a target value starting from a specified number.
/// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
#define EXPONENTIALROLLOFF_GENERATOR(T)                                                                                \
   T ExponentialRollOff(T input, float rolloff_start = 0.20f, float output_max = 1.0f)                                 \
   {                                                                                                                   \
      T rolloff_size = output_max - rolloff_start;                                                                     \
      T overage = -max((T)0, input - rolloff_start);                                                                   \
      T rolloff_value = (T)1.0f - exp(overage / rolloff_size);                                                         \
      T new_overage = mad(rolloff_size, rolloff_value, overage);                                                       \
      return input + new_overage;                                                                                      \
   }
EXPONENTIALROLLOFF_GENERATOR(float)
EXPONENTIALROLLOFF_GENERATOR(float3)
#undef EXPONENTIALROLLOFF_GENERATOR
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 ClampByMaxChannel(float3 x, float peak) {
  float m = max(x.x, max(x.y, x.z));
  if (m > peak) x *= peak / m;
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 CorrectPerChannelTonemapHiglightsDesaturationBo3(float3 color, float peakBrightness, float desaturationExponent = 2.0, float highlightsOnly = 2, uint colorSpace = CS_DEFAULT)
{
  float sourceChrominance = length(UCSTo(color, CS_BT2020).yz);

  float maxBrightness = max3(color); 
  float midBrightness = GetMidValue(color);
	float minBrightness = min3(color);
	float brightnessRatio = saturate(maxBrightness / peakBrightness);

  brightnessRatio = lerp(brightnessRatio, sqrt(brightnessRatio), sqrt(saturate(InverseLerp(minBrightness, maxBrightness, midBrightness))));
  brightnessRatio = pow(brightnessRatio, highlightsOnly); // skewed towards highlights only

  float chrominancePow = lerp(1.0, 1.0 / desaturationExponent, brightnessRatio);
  
  float targetChrominance = sourceChrominance > 1.0 ? pow(sourceChrominance, chrominancePow) : (1.0 - pow(1.0 - sourceChrominance, chrominancePow));
  float chrominanceRatio = safeDivision(targetChrominance, sourceChrominance, 1);

  // return RestoreLuminance(SetChrominance(color, chrominanceRatio), color, true, colorSpace);
  color = UCSTo(color, CS_BT2020);
  color.yz *= chrominanceRatio;
  color = UCSFrom(color, CS_BT2020);
  color = max(0, color); //clamp BT2020
  return color;
}

float3 RestoreHueAndChrominanceUcsInternal(float3 targetUcs, float3 sourceUcs, float currentChrominance, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  if (targetUcs.x == 0) return targetUcs;

  if (hueStrength != 0.0)
  {
    const float chrominancePre = currentChrominance;
    targetUcs.yz = lerp(targetUcs.yz, sourceUcs.yz, hueStrength);
    const float chrominancePost = length(targetUcs.yz);
    float chrominanceRatio = safeDivision(chrominancePre, chrominancePost, 1);
    targetUcs.yz *= chrominanceRatio;
  }

  if (chrominanceStrength != 0.0)
  {
    const float sourceChrominance = length(sourceUcs.yz);
    float targetChrominanceRatio = safeDivision(sourceChrominance, currentChrominance, 1);
    targetChrominanceRatio = clamp(targetChrominanceRatio, minChromaRatio, FLT_MAX);
    targetUcs.yz *= lerp(1.0, targetChrominanceRatio, chrominanceStrength);
  }

  return targetUcs;
}

float3 RestoreHueAndChrominanceUcs(float3 targetUcs, float3 sourceUcs, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  return RestoreHueAndChrominanceUcsInternal(targetUcs, sourceUcs, length(targetUcs.yz), hueStrength, chrominanceStrength, minChromaRatio);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 TradeSpace_In(float3 x) {
  x = max(0, x);
  x = sqrt(x);
  // x = pow(x, 1/2.2);

  return x;
}
float3 TradeSpace_Out(float3 x) {
  x = max(0, x);
  x *= x;
  // x = pow(x, 2.2);
  
  return x;
}

float3 Trade_In_NoCS(float3 x) {
#if CUSTOM_SDR == 0
  x /= TRADE_SCALE;
  x = TradeSpace_In(x);
#endif 
  x *= 32768.;
  return x;
}
float3 Trade_Out_NoCS(float3 x) {
  x /= 32768.;
#if CUSTOM_SDR == 0
  x = TradeSpace_Out(x);
  x *= TRADE_SCALE;
#endif 
  return x;
}

float3 Trade_In(float3 x) {
  x = BT709_To_BT2020(x);
  x = Trade_In_NoCS(x);
  return x;
}
float3 Trade_Out(float3 x) {
  x = BT2020_To_BT709(x);
  x = Trade_Out_NoCS(x);
  return x;
}

float3 FixFSFX(float3 color, float scale = 1, bool isDoColorSpace = true, bool isScaleDown = true) {
  #if CUSTOM_SDR > 0
    //SDR? Just scale and be done with.
    return color * scale;
  #endif

  if (isScaleDown) color /= 32768.; 

  if (isDoColorSpace) color = Trade_In(color);
  
  color *= scale;

  return color;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 GammaCorrection_Linear(float3 x) {
  #if CUSTOM_SDR > 0
    return x;
  #endif

  #if CUSTOM_GAMMA_CORRECTION_MODE == 1
    float3 xBack = UCSTo(x, CS_BT2020);
  #endif

  //gamma correction
  #if GAMMA_CORRECTION_TYPE > 0 && CUSTOM_HDTVREC709 > 0
    x = EncodeRec709(x);
    x = gamma_to_linear(x, GCT_POSITIVE);
  #elif GAMMA_CORRECTION_TYPE > 0 && CUSTOM_HDTVREC709 == 0
    x = linear_to_sRGB_gamma(x, GCT_POSITIVE);
    x = gamma_to_linear(x, GCT_POSITIVE);
  #elif GAMMA_CORRECTION_TYPE == 0 && CUSTOM_HDTVREC709 > 0
    x = EncodeRec709(x);
    x = gamma_sRGB_to_linear(x, GCT_POSITIVE);
  #elif GAMMA_CORRECTION_TYPE == 0 && CUSTOM_HDTVREC709 == 0
    // x = linear_to_sRGB_gamma(x, GCT_POSITIVE);
    // x = gamma_sRGB_to_linear(x, GCT_POSITIVE);
  #endif

  #if CUSTOM_GAMMA_CORRECTION_MODE == 1
    x = UCSTo(x, CS_BT2020);
    x = RestoreHueAndChrominanceUcs(x, xBack, 1, 0, 1); //hue correct w/ no chrominance loss
    x = UCSFrom(x, CS_BT2020);
  #endif

  return x;
}

float3 GammaCorrection_IntermediateEncode(float3 x) {
  //gamma correct
  #if GAMMA_CORRECTION_TYPE == 0 || CUSTOM_SDR > 0
    x = linear_to_sRGB_gamma(x, GCT_MIRROR);
  #else
    x = linear_to_gamma(x, GCT_MIRROR);
  #endif

  //rec709 encode
  #if CUSTOM_HDTVREC709 > 0
    x = sign(x) * DecodeRec709(abs(x));
    x = linear_to_sRGB_gamma(x, GCT_MIRROR);
  #endif

  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//AI ahh moment
static const float K_A  =  0.0727029592f;
static const float K_B  =  0.598205984f;
static const float K_C  =  0.0669102818f;

float ForwardPoly(float t)
{
    float p = 7.71294689f;
    p = p * t - 19.3115273f;
    p = p * t + 14.2751675f;
    p = p * t - 2.49004531f;
    p = p * t + 0.87808305f;
    return p * t - K_C;
}

float ForwardPolyDeriv(float t)
{
    float dp = 5.0f * 7.71294689f;
    dp = dp * t - 4.0f * 19.3115273f;
    dp = dp * t + 3.0f * 14.2751675f;
    dp = dp * t - 2.0f * 2.49004531f;
    dp = dp * t + 0.87808305f;
    return dp;
}

float InvertPoly(float y)
{
    static const float F0 = -K_C;
    static const float F1 =  0.99715394f;
    float t = saturate((y - F0) / (F1 - F0));

    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float f  = ForwardPoly(t) - y;
        float df = ForwardPolyDeriv(t);
        t -= f / (df + 1e-10f);
        t  = saturate(t);   // clamped — gracefully clips at forward ceiling
    }
    return t;
}

float3 TonemapVanilla_Inverse(float3 y)
{
//     float  k = 0.00872999988f * GS.SDRTonemapFloorRaiseScale;
    float3 result;
// 
//     [unroll]
//     for (int ch = 0; ch < 3; ++ch)
//     {
//         float t     = InvertPoly(y[ch]);
//         float xLog2 = (t - K_B) / K_A;
//         result[ch]  = max(exp2(xLog2) - k, 0.0f);
//     }

    // result = RenoDX_Contrast(y, DVS1, DVS2) * DVS3;
    result = RenoDX_Contrast(y, 3.291, 1.067) * 1.240;

    return result;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 TonemapVanilla_Internal(float3 x) { //https://www.desmos.com/calculator/1hmlnb6z1m (compresses color to about rec709 encode)
  float3 r0, r1;
  r0 = x;

  r0 += 0.00872999988 * GS.SDRTonemapFloorRaiseScale;
  r0 = log2(r0);
  r0 = saturate(r0 * 0.0727029592 + 0.598205984); 
  r1 = r0 * 7.71294689 + -19.3115273;
  r1 = r1 * r0 + 14.2751675;
  r1 = r1 * r0 + -2.49004531;
  r1 = r1 * r0 + 0.87808305;
  r0 = saturate(r1 * r0 + -0.0669102818);

  return r0.xyz;
}
void TonemapVanilla(inout float3 colorT, inout float3 colorU) {
  //SDR Tonemap
  float3 colorTBefore = colorT;
  colorT = TonemapVanilla_Internal(colorT);

  #if CUSTOM_SDR > 0
    return;
  #endif

  // Per Channel Correct
  #if CUSTOM_PCC > 0
    //sample from less blownout exposure
    float3 colorTLessBlow = colorTBefore * GS.PCCLookback; //multiply down, helps gives variance
    colorTLessBlow = TonemapVanilla_Internal(colorTLessBlow);

    // float3 colorTLessBlowG = colorTBefore * (GS.PCCLookback / GetLuminance(colorTBefore, CS_BT709)); //fixed and guaranteed
    // colorTLessBlowG = TonemapVanilla_Internal(colorTLessBlowG);

    //pcc setup
    float colorTMax = max(colorT.x, max(colorT.y, colorT.z));
    float colorTY = GetLuminance(colorT, CS_BT709);

    // ucs
    colorT = UCSTo(colorT, CS_BT709);

    colorTLessBlow = UCSTo(colorTLessBlow, CS_BT709);
    colorTLessBlow.yz *= GS.PCCChrominanceBoost;

    // colorTLessBlowG = UCSTo(colorTLessBlowG, CS_BT709);
    // colorTLessBlowG.yz *= GS.PCCChrominanceBoost;

    // do
    float s = saturate(colorTY * colorTY);
    float sH = s * GS.PCCHue;
    float sC = s * GS.PCCChrominance;
    colorT = RestoreHueAndChrominanceUcs(colorT, colorTLessBlow, sH, sC, 1.f);

    //BRUH
    #if BRUH > 0
      colorU = UCSTo(colorU, CS_BT709);
      colorU.yz = colorT.yz;
      colorU = UCSFrom(colorU, CS_BT709);
      colorU = max(0, colorU); //clamp BT709
    #endif

    // colorT = RestoreHueAndChrominanceUcs(colorT, colorTLessBlowG, sH * min(GS.PCCGuaranteed, 0.25f), sC * GS.PCCGuaranteed, 1.f);
    colorT = UCSFrom(colorT, CS_BT709); //back to linear

    //clamp
    float colorTMaxAfter = max(colorT.x, max(colorT.y, colorT.z));
    // colorTMax = lerp(colorTMax, colorTMaxAfter, 0.8f); //reduce max channel feeling
    colorTMax = min(1, colorTMax);
    colorT *= colorTMax / colorTMaxAfter; //prevent clip
    colorT = max(0, colorT); //clamp BT709
  #endif

    // midgray match
    // colorU *= (0.5f / 0.18f); //TODO: remove this, it's causing too much labyrinth-ness
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Bloom_Comp_ColorU(inout float3 colorU, in float3 bloomBefore, in float3 bloomAfter, in float3 bloomColor) {
  // //mask
  // float3 bloomMask = bloomAfter - bloomBefore;
  // bloomMask = max(0, bloomMask);
  // bloomMask = TonemapVanilla_Inverse(bloomMask);
  // colorU += bloomMask;

  // bloomColor = TonemapVanilla_Inverse(bloomColor);
  bloomColor = RenoDX_Contrast(bloomColor, 1.5f, 0.103f) * 0.125f;

  //screen
  const float m = 100.f;
  // bloomColor *= 4.f;
  // bloomColor *= 1/7.f;
  bloomColor = clamp(bloomColor, 0, m);
  // bloomColor = EncodeRec709(bloomColor);
  colorU = bloomColor + colorU * max(0, 1.0 - bloomColor / m);
}

void Bloom_Comp(inout float3 colorT, inout float3 colorU, in Texture2D<float4> bloomTex, in SamplerState bloomTex_s, in float2 uv) {
  float3 colorTBefore = colorT;

  // original SDR bloom composite, but named
  float3 bloomColor = bloomTex.Sample(bloomTex_s, uv.xy).xyz /* * (GS.Bloom / 256.f) */;
  float3 bloomColorScaledUnclamped = bloomColor;
  float3 bloomColorScaled = saturate(bloomColorScaledUnclamped);

  //normal
  float3 bloomAdded = bloomColorScaled + colorT;
  float3 colorT1 = -colorT * bloomColorScaled + bloomAdded;
  // float3 colorT1 = bloomAdded;

  #if CUSTOM_SDR > 0
    colorT = colorT1;
    return;
  #endif

  #if 0/* CUSTOM_BLOOM_SATURATIONPRESERVE > 0 */
    less blownout
    bloomColorScaled *= 0.25f; //TODO: user settings
    bloomAdded = bloomColorScaled + colorT;
    float3 colorT2 = -colorT * bloomColorScaled + bloomAdded;

    //steal chroma from less blown out
    colorT1 = UCSTo(colorT1, CS_BT709);
    colorT2 = UCSTo(colorT2, CS_BT709);
    colorT1 = RestoreHueAndChrominanceUcs(colorT1, colorT2, 0.2f, 0.85f, 1.f);
    colorT1.x = lerp(colorT1.x, colorT2.x, 0.125f);
    colorT = UCSFrom(colorT1, CS_BT709);
  #else
    colorT = colorT1;
  #endif

  #if CUSTOM_LUT_MAXCHANNELCLAMPINPUT == 0
    colorT = saturate(colorT);
  #else
    colorT = max(0, colorT);
    colorT = ClampByMaxChannel(colorT, 1.f);
  #endif

  //HDR custom composite
  Bloom_Comp_ColorU(colorU, colorTBefore, colorT, bloomColorScaledUnclamped);
}
void Bloom_Comp_HDR(inout float3 colorT, inout float3 colorU, in Texture2D<float4> bloomTex, in SamplerState bloomTex_s, in float2 uv) {
  float3 bloom = bloomTex.Sample(bloomTex_s, uv.xy).xyz;
  const float m = 100.f;
  // bloom = ClampByMaxChannel(bloom, m);
  colorT = bloom + colorT * max(0, 1.0 - bloom / m);

  bloom *= 1.65;
  colorU = bloom + colorU * max(0, 1.0 - bloom / m);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void LensFlare_Comp(inout float3 colorT, inout float3 colorU, in Texture2D<float4> flareTex, in SamplerState flareTex_s, in float2 uv) {
  const float mult = (1 / 32768.f) * GS.LensFlare;

  float3 lens = flareTex.Sample(flareTex_s, uv).xyz * mult;

  //normal
  float3 colorT1 = colorT + lens;

  #if CUSTOM_SDR > 0
    colorT = saturate(colorT1);
    return;
  #endif

  #if 0
    //less blownout blend (kinda overkill)
    float3 colorT2 = colorT + (lens * 0.7f); //TODO: user settings
    colorT1 = UCSTo(colorT1, CS_BT709);
    colorT2 = UCSTo(colorT2, CS_BT709);
    colorT1 = RestoreHueAndChrominanceUcs(colorT1, colorT2, 0.3f, 0.5f, 1.f);
    // colorT1.x = lerp(colorT1.x, colorT2.x, 0.2f);
    colorT = UCSFrom(colorT1, CS_BT709);
  #else
    colorT = colorT1;
  #endif

  #if CUSTOM_LUT_MAXCHANNELCLAMPINPUT == 0
    colorT = saturate(colorT);
  #else
    colorT = max(0, colorT);
    colorT = ClampByMaxChannel(colorT, 1.f);
  #endif

  //HDR
  lens = TonemapVanilla_Inverse(lens);
  // lens = EncodeRec709(lens) * 4.f;
  // colorU += lens;
  const float m = 100.f;
  lens = clamp(lens, 0, m);
  lens = EncodeRec709(lens);
  colorU = lens + colorU * max(0, 1.0 - lens / m);

}
void LensFlare_Comp_HDR(inout float3 colorT, inout float3 colorU, in Texture2D<float4> flareTex, in SamplerState flareTex_s, in float2 uv) {
  float3 flare = flareTex.Sample(flareTex_s, uv).xyz * GS.LensFlare;
  // flare = ClampByMaxChannel(flare, 1.f);
  colorT += flare;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float LUTNeutralize_LumaStrength(float3 lutUcs, float3 neutralUcs, float strengthLuma) {
  #if CUSTOM_LUTBUILDER_NEUTRAL_LUMA == 0
    //high pass
    float sL = saturate(lutUcs.x);
    sL = smoothstep(0.36/* GS.LUTBuilderNeutralLumaHPStart */, 1, sL);
    sL = sqrt(sL);
    return strengthLuma * sL;
  #else
   // none
   return strengthLuma;
  #endif
}

float3 LUTNeutralize(float3 lutCoord, float3 lutColor, uint lutSize) {
  //skip
  #if CUSTOM_LUTBUILDER_NEUTRAL == 0 || CUSTOM_SDR > 0
    return lutColor;
  #endif

  //const
  const float strengthChromi = GS.LUTBuilderNeutralChrominance;
  const float strengthHue = GS.LUTBuilderNeutralHue;
  const float strengthLuma = GS.LUTBuilderNeutralLuma;

  //get neutral
  float3 neutral = float3(lutCoord / (lutSize - 1));
  if(strengthChromi == 1 && strengthHue == 1 && strengthLuma == 1) return neutral;

  //forced neutral   
//   #if CUSTOM_LUTBUILDER_NEUTRAL == 1
//     //get chroma
//     float lutColorChroma = GetChrominance(lutColor);
//     float neutralChroma = GetChrominance(neutral);
// 
//     //skip: lut is more staturated than neutral
//     if (lutColorChroma > neutralChroma) return lutColor;
// 
//     // strength scaled
//     float strengthScaled = saturate(GetLuminance(lutColor, CS_BT709));
// 
//     //get max channel
//     float lutMax = max(lutColor.x, max(lutColor.y, lutColor.z));
//     float neutralMax = max(neutral.x, max(neutral.y, neutral.z));
// 
//     //scale neutral to match lut tex's max channel, hopefully perserving luma
//     float3 neutralUnscaled = neutral; //save for later
//     neutral *= lutMax / neutralMax;
// 
//     //perchannel lerp to neutral
//     return lerp(lutColor, neutral, strength);
  #if CUSTOM_LUTBUILDER_NEUTRAL == 1
    float3 lutUcs = UCSTo(lutColor, CS_BT709);
    float3 neutralUcs = UCSTo(neutral, CS_BT709);
    
    float s = saturate(lutUcs.x);
    lutUcs = RestoreHueAndChrominanceUcs(lutUcs, neutralUcs, strengthHue * s, strengthChromi * s, 1.f);
    lutUcs.x = lerp(lutUcs.x, neutralUcs.x, LUTNeutralize_LumaStrength(lutUcs, neutralUcs, strengthLuma));
    lutColor = UCSFrom(lutUcs, CS_BT709);
  #elif CUSTOM_LUTBUILDER_NEUTRAL == 2
    float3 lutUcs = UCSTo(lutColor, CS_BT709);
    float3 neutralUcs = UCSTo(neutral, CS_BT709);
    lutUcs = RestoreHueAndChrominanceUcs(lutUcs, neutralUcs, strengthHue, strengthChromi, 1.f);
    lutUcs.x = lerp(lutUcs.x, neutralUcs.x, LUTNeutralize_LumaStrength(lutUcs, neutralUcs, strengthLuma));
    lutColor = UCSFrom(lutUcs, CS_BT709);
  #endif

  //clamp 1 for rest of color grading
  lutColor = ClampByMaxChannel(lutColor, 1.f);

  return lutColor;
}

//From RenoDX clshortfuse
float3 UpgradeToneMapBo3(
    float3 color_untonemapped,
    float3 color_tonemapped_graded,
    float3 color_tonemapped_neutral) {
    
  float ratio = 1.f;
  float y_untonemapped = GetLuminance(color_untonemapped, CS_BT709);
  // float y_tonemapped = Reinhard::ReinhardPiecewiseExtended(y_untonemapped, 100, 1, 0.18);
  float y_tonemapped = HermiteSpline::HermiteSplineLuminanceRolloff(y_untonemapped, 1, 200); //we need a natural but smooth rolloff
  float y_tonemapped_graded = GetLuminance(color_tonemapped_graded, CS_BT2020);
  if (y_untonemapped < y_tonemapped) {
    ratio = y_untonemapped / y_tonemapped;
  } else {
    float y_delta = y_untonemapped - y_tonemapped;
    y_delta = max(0, y_delta);
    const float y_new = y_tonemapped_graded + y_delta;
    const bool y_valid = (y_tonemapped_graded > 0);
    ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
  }
  float3 color_scaled = color_tonemapped_graded * ratio;

  #if CUSTOM_UPGRADE_HUE_CORRECTION > 0
    float3 color_scaled_ucs = UCSTo(color_scaled, CS_BT2020);
    float3 color_tonemapped_graded_ucs = UCSTo(color_tonemapped_graded, CS_BT2020);
    color_scaled_ucs = RestoreHueAndChrominanceUcs(color_scaled_ucs, color_tonemapped_graded_ucs, 1.f, 1.f, 1.f);
    color_scaled = UCSFrom(color_scaled_ucs, CS_BT2020);
  #endif

  #if CUSTOM_UPGRADE_DEBUG == 0
    return color_scaled;
  #elif CUSTOM_UPGRADE_DEBUG == 1
    return BT709_To_BT2020(color_untonemapped);
  #elif CUSTOM_UPGRADE_DEBUG == 2
    return BT709_To_BT2020(color_untonemapped) * (y_tonemapped / y_untonemapped);
  #elif CUSTOM_UPGRADE_DEBUG == 3
    return BT709_To_BT2020(color_tonemapped_neutral);
  #elif CUSTOM_UPGRADE_DEBUG == 4
    return color_tonemapped_graded;
  #elif CUSTOM_UPGRADE_DEBUG == 5
    if (DVS10 < 1/5.f) return color_scaled;
    else if (DVS10 < 2/5.f) return BT709_To_BT2020(color_untonemapped);
    else if (DVS10 < 3/5.f) return BT709_To_BT2020(color_untonemapped) * (y_tonemapped / y_untonemapped);
    else if (DVS10 < 4/5.f) return BT709_To_BT2020(DecodeRec709(color_tonemapped_neutral));
    else return color_tonemapped_graded;
  #endif
}

void LUT(inout float3 colorU, inout float3 colorT, in Texture3D lut, in SamplerState lut_s) {
  //BRUH
  #if BRUH > 0
    colorT = colorU;
    float y = GetLuminance(colorT, CS_BT709);
    if (y > 0) colorT *= TonemapVanilla_Internal(y).x / y;
  #endif
  

  //clamp
  #if CUSTOM_LUT_MAXCHANNELCLAMPINPUT > 0
    colorT = max(0, colorT);
    colorT = ClampByMaxChannel(colorT, 1.f);
  #else
    colorT = saturate(colorT);
  #endif

  //sample lut
  float3 colorN = colorT;
  colorT = colorT * 0.96875 + 0.015625;
  colorT = lut.Sample(lut_s, colorT).xyz;

  #if CUSTOM_SDR > 0
    return;
  #endif

  //mid gray change sampling
  const float mid = 0.5; //approx 0.18 in rec709
  colorU *= (GetLuminance(lut.Sample(lut_s, mid).xyz, CS_BT2020) / mid) * (0.5f / 0.18f);

  //lut resolve 0 (UpgradeToneMap crutch ahh ahh)
  colorU = UpgradeToneMapBo3(colorU, colorT, colorN);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float TonemapGetLumaForAA(float3 color) {
  float o1;
  o1 = GetLuminance(color, CS_BT2020);
  // o1 = Reinhard::ReinhardSimple(o1.x);
  o1 = NeuTwo::NeuTwo(o1, 1);
  // o1 = sqrt(o1.x);
  // o1 = saturate(o1);

  //orig encoding
  {
    float3 r0;
    r0.x = o1;
    
    r0.y = log2(r0.x);
    r0.y = 0.333333343 * r0.y;
    r0.y = exp2(r0.y);
    r0.z = cmp(0.00885645207 < r0.x);
    r0.x = r0.x * 7.7870369 + 0.137931034;
    r0.x = r0.z ? r0.y : r0.x;

    o1 = r0.x * 1.15999997 + -0.159999996;
  }

  return o1;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 TonemapHDRAndTradeIn(float3 colorU) {
  //tonemap
  #if CUSTOM_TONEMAP > 0 && CUSTOM_SDR == 0 /* && CUSTOM_UPGRADE_DEBUG <= 1 */
    #if CUSTOM_TONEMAP_SCALING == 0
      float l = GetLuminance(colorU, CS_BT2020); //luma
    #elif CUSTOM_TONEMAP_SCALING == 1
      float l = max(colorU.x, max(colorU.y, colorU.z)); //max channel
    #endif
    if (l > 0) { //safe
      float lT;
      #if CUSTOM_TONEMAP == 1
        lT = Reinhard::ReinhardPiecewiseExtended(l, HDR_MAXEXPECTED, HDR_PEAK, HDR_SHOULDERSTART);
      #elif CUSTOM_TONEMAP == 2
        lT = HermiteSpline::HermiteSplineLuminanceRolloff(l, HDR_PEAK, HDR_MAXEXPECTED);
      #endif
      colorU *= lT / l;
    }

    //clamp
    #if CUSTOM_TONEMAP_SCALING != 1 /* skip if max channel. */
      #if CUSTOM_TONEMAP_CLAMP == 1
        colorU = min(HDR_PEAK, colorU);
      #elif CUSTOM_TONEMAP_CLAMP == 2
        colorU = ClampByMaxChannel(colorU, HDR_PEAK);
      #endif
    #endif
  #endif

  //tradeoff intermediate scaling
  colorU = Trade_In_NoCS(colorU);

  return colorU;
}
void TonemapShader_Out(inout float3 o0, inout float3 colorT, float3 colorU) {
  #if CUSTOM_SR == 0 || CUSTOM_SDR > 0
    colorT = BT2020_To_BT709(colorT);
    colorT = max(0, colorT);
  #endif

  //case: SDR
  #if CUSTOM_SDR > 0
    o0 = colorT; //use colorT BT709
    #if CUSTOM_SR == 0
      colorT *= 32768.f; //scale up for normal AA luma calculation
    #endif
  #else
    o0 = colorU; //use colorU BT2020
  #endif
  
  //exposure & gamma correction
  #if GAMMA_CORRECTION_TYPE > 0
    o0 *= GS.GammaInfluence;
    o0 = GammaCorrection_Linear(o0);
    o0 *= GS.Exposure / GS.GammaInfluence;
  #else
    o0 = GammaCorrection_Linear(o0);
    o0 *= GS.Exposure;
  #endif

  //case: SDR (returns)
  #if CUSTOM_SDR > 0
    #if CUSTOM_SR == 0
      o0 = Trade_In_NoCS(o0); //do it here instead of in SMAA T2x shader
    #endif
    return;
  #endif

  //color grade
  #if CUSTOM_COLORGRADE > 0
    o0 = RenoDX_ColorGrade(
      o0,
      GS.CGContrast, GS.CGContrastMidGray / GamePaperWhiteNits,
      GS.CGHighlightsStrength, GS.CGHighlightsMidGray / GamePaperWhiteNits,
      GS.CGShadowsStrength, GS.CGShadowsMidGray / GamePaperWhiteNits,
      GS.CGSaturation,
      CS_BT2020
    );
  #endif

  //clamp against negative values for DLSS
  o0 = max(0, o0);

  //Do HDR Tonemap and Tradeoff encoding if no SR (else delay until after to give as much info possible to SR)
  #if CUSTOM_SR == 0
    o0 = TonemapHDRAndTradeIn(o0);
  #endif  
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "./Includes/RCAS.hlsl"
float3 FinalShader_Resolve(Texture2D<float4> tex, SamplerState tex_s, float2 uv) {
  float3 o;
  o = tex.Sample(tex_s, uv.xy).xyz;

  //scaling
  #if CUSTOM_RCAS == 1
    o = RCAS_BO3(Trade_Out_NoCS(o), tex, tex_s, uv, GS.RCAS, HDR_PEAK);
  #else
    o = Trade_Out_NoCS(o);
  #endif

  //HDR clamp BT2020, SDR clamp BT709
  o = max(0, o);

  //intermediate colorspace encode
  #if CUSTOM_SDR == 0
    o = BT2020_To_BT709(o);
  #else
    return o; //skip rest for SDR
  #endif


  //intermediate scaling
  o *= HDR_INTSCALING;

  //intermediate gamma encode
  o = GammaCorrection_IntermediateEncode(o);

  return o;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////