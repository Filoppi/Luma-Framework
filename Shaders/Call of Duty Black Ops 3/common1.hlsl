#define LUT_3D 1
// #define LUT_SIZE 32u
// #define LUT_MAX 32u
// #define LUT_OUT_MULTIPLIER (1/32768.)

#include "./Includes/Common.hlsl"
#include "../Includes/Math.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "./Includes/PerChannelCorrect.hlsl"
#include "./Includes/ColorGrade.hlsl"


#define TRADE_SCALE HDR_PEAK * 0.25 /*  Still have no clue why 0.3 is good fudge factor. */

#define cmp -

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

float ComputeReinhardSmoothClampScale(float3 untonemapped, float rolloff_start = 0.5f, float output_max = 1.f,
                                      float white_clip = 100.f)
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
float3 ClampByMaxChannel(float3 x, float peak) {
  float m = max(x.x, max(x.y, x.z));
  if (m > peak) x *= peak / m;
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 CorrectPerChannelTonemapHiglightsDesaturationBo3(float3 color, float peakBrightness, float desaturationExponent = 2.0, float highlightsOnly = 2, uint colorSpace = CS_DEFAULT)
{    
  float sourceChrominance = GetChrominance(color);

  float maxBrightness = max3(color); 
  float midBrightness = GetMidValue(color);
	float minBrightness = min3(color);
	float brightnessRatio = saturate(maxBrightness / peakBrightness);

  brightnessRatio = lerp(brightnessRatio, sqrt(brightnessRatio), sqrt(saturate(InverseLerp(minBrightness, maxBrightness, midBrightness))));
  brightnessRatio = pow(brightnessRatio, highlightsOnly); // skewed towards highlights only

  float chrominancePow = lerp(1.0, 1.0 / desaturationExponent, brightnessRatio);
  
  float targetChrominance = sourceChrominance > 1.0 ? pow(sourceChrominance, chrominancePow) : (1.0 - pow(1.0 - sourceChrominance, chrominancePow));
  float chrominanceRatio = safeDivision(targetChrominance, sourceChrominance, 1);

  return RestoreLuminance(SetChrominance(color, chrominanceRatio), color, true, colorSpace);
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
// float3 TonemapVanilla_InternalInv(float3 x, float3 t) {

// }
float3 TonemapVanilla_Internal(float3 x) { //https://www.desmos.com/calculator/1hmlnb6z1m
  float3 r0, r1;
  r0 = x;

  r0 += 0.00872999988 * GS.SDRTonemapFloorRaiseScale;
  r0 = log2(r0);
  r0 = saturate(r0 * 0.0727029592 + 0.598205984); 
  r1 = r0 * 7.71294689 + -19.3115273;
  r1 = r1 * r0 + 14.2751675;
  r1 = r1 * r0 + -2.49004531;
  r1 = r1 * r0 + 0.87808305;

  // r0 = saturate(r1 * r0 + -0.0669102818);
  r0 =  max(0, r1 * r0 + -0.0669102818);
  #if CUSTOM_SDR > 0
    r0 = min(1, r0);
  #endif

  return r0.xyz;
}
void TonemapVanilla(inout float3 colorT, inout float3 colorU) {
  //SDR Tonemap
  colorT = TonemapVanilla_Internal(colorT);

  #if CUSTOM_SDR > 0
    return;
  #endif

  //mid gray exposure
  colorU *= (0.5 / 0.18);

  // Per Channel Correct
  #if CUSTOM_PCC > 0
    colorT *= colorT; //approx linearize
    colorT = ApplyPerChannelCorrectionHighlightOnly(colorU, colorT, GS.PCCStrength, GS.PCCHighlightsOnly, CS_BT709, 1, 1, 1, 0.65);
    colorT = sqrt(colorT); //revert approx linearize
  #endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void Bloom_Comp_ColorU(inout float3 colorU, in float3 bloomBefore, in float3 bloomAfter, in float3 bloomColor) {
  //mask
  float3 bloomMask = bloomAfter - bloomBefore;
  bloomMask *= 0.5625; //dodge

  // color
  float3 bloomColor1 = bloomColor;
  bloomColor1 -= bloomMask;
  bloomColor1 = max(0, bloomColor1);

  // float l = GetLuminance(bloomColor1); //TODO: This is fake inverse tonemap.
  // l = RenoDX_Contrast(bloomColor1 * 3.5f, 1.65f) / 3.f;
  // l = Reinhard::inverse::ReinhardScalable(l, 2.0, 0, 0.18, 0.18);
  // bloomColor = RestoreLuminance(bloomColor, l);

  bloomColor = RenoDX_Contrast(bloomColor1 * 3.5f, 1.65f) / 3.f;

  //add
  colorU += bloomColor;
}

void Bloom_Comp(inout float3 colorT, inout float3 colorU, in Texture2D<float4> bloomTex, in SamplerState bloomTex_s, in float2 uv) {
  float3 colorTBefore = colorT;

  //original SDR bloom composite, but named
  float3 bloomColor = bloomTex.Sample(bloomTex_s, uv.xy).xyz * (GS.Bloom * GS.PreExposure);
  float3 bloomColorScaled = saturate(bloomColor / 255);

  float3 bloomAdded = bloomColorScaled + colorT;
  colorT = -colorT * bloomColorScaled + bloomAdded;

  #if CUSTOM_SDR > 0
    return;
  #endif

  //HDR custom composite
  Bloom_Comp_ColorU(colorU, colorTBefore, colorT, bloomColorScaled);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 LUTNeutralize(float3 lutCoord, float3 lutColor, uint lutSize, float strength) {
  //get neutral
  float3 neutral = float3(lutCoord / (lutSize - 1));
  
  //get chroma
  float3 lutOklch = Oklab::linear_srgb_to_oklch(lutColor);
  float3 neutralOklch = Oklab::linear_srgb_to_oklch(neutral);
  float3 resultOklch = lutOklch;

  //skip: lut is more staturated than neutral
  if (lutOklch.y > neutralOklch.y) return lutColor;

  //strength scaled
  strength = saturate(GetLuminance(lutColor) * strength);

#if 1 //mode: lerp to neutral color
  //get max channel
  float lutMax = max(lutColor.x, max(lutColor.y, lutColor.z));
  float neutralMax = max(neutral.x, max(neutral.y, neutral.z));

  //scale neutral to match lut tex's max channel, hopefully perserving luma
  float3 neutralUnscaled = neutral; //save for later
  neutral *= lutMax / neutralMax;

  //perchannel lerp to neutral
  return lerp(lutColor, neutral, strength);
#elif 1 //mode: enforce saturation 
  resultOklch.y = lerp(lutOklch.y, neutralOklch.y, strength);

  float3 result = Oklab::oklch_to_linear_srgb(resultOklch);
  return result;
#endif
}

//From RenoDX clshortfuse
float3 UpgradeToneMapBo3(
    float3 color_untonemapped,
    float3 color_tonemapped,
    float3 color_tonemapped_graded,
    float post_process_strength = 1.f,
    float auto_correction = 0.f,
    uint colorSpace = CS_DEFAULT ) {
      
  float ratio = 1.f;

  float y_untonemapped = GetLuminance(color_untonemapped, colorSpace);
  float y_tonemapped = GetLuminance(color_tonemapped, colorSpace);
  float y_tonemapped_graded = GetLuminance(color_tonemapped_graded, colorSpace);

  if (y_untonemapped < y_tonemapped) {
    // If substracting (user contrast or paperwhite) scale down instead
    // Should only apply on mismatched HDR
    ratio = y_untonemapped / y_tonemapped;
  } else {
    float y_delta = y_untonemapped - y_tonemapped;
    y_delta = max(0, y_delta);  // Cleans up NaN
    const float y_new = y_tonemapped_graded + y_delta;

    const bool y_valid = (y_tonemapped_graded > 0);  // Cleans up NaN and ignore black
    ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
  }
  float auto_correct_ratio = lerp(1.f, ratio, saturate(y_untonemapped));
  ratio = lerp(ratio, auto_correct_ratio, auto_correction);

  float3 color_scaled = color_tonemapped_graded * ratio;
  // Match hue
  color_scaled = RestoreHueAndChrominance(color_scaled, color_tonemapped_graded, 1.0, 0.0, 0.0, FLT_MAX, 0.0, colorSpace);
  return lerp(color_untonemapped, color_scaled, post_process_strength);
}

// float3 LUTSamplingThing(in float3 colorU, in float3 colorT, in Texture3D/* <float3> */ lut, in SamplerState lut_s) {
//   colorU /= (0.5 / 0.18);

//   LUTExtrapolationData lutData = DefaultLUTExtrapolationData();
//   lutData.inputColor = linear_to_sRGB_gamma(colorU);
//   lutData.vanillaInputColor = colorT;

//   LUTExtrapolationSettings lutSettings = DefaultLUTExtrapolationSettings();
//   lutSettings.lutSize = 32u;
//   lutSettings.samplingQuality = 2u;

//   lutSettings.inputLinear = false;
//   lutSettings.lutInputLinear = false;
//   lutSettings.lutOutputLinear = true;
//   lutSettings.outputLinear = true;

//   lutSettings.transferFunctionIn = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;
//   lutSettings.transferFunctionOut = LUT_EXTRAPOLATION_TRANSFER_FUNCTION_SRGB;

//   lutSettings.neutralLUTRestorationAmount = 0.0;
//   lutSettings.vanillaLUTRestorationAmount = GS.LUT;

//   lutSettings.enableExtrapolation = true;
//   lutSettings.extrapolationQuality = 2;
//   lutSettings.fixExtrapolationInvalidColors = true;
//   lutSettings.backwardsAmount = 0.2;

//   lutSettings.whiteLevelNits = Rec709_WhiteLevelNits;
//   lutSettings.inputTonemapToPeakWhiteNits = 0;
//   lutSettings.clampedLUTRestorationAmount = 0;

//   float3 lutOutputColor = SampleLUTWithExtrapolation(lut, lut_s, lutData, lutSettings);
//   // lutOutputColor = linear_to_sRGB_gamma(lutOutputColor/* , GCT_MIRROR */);
//   return lutOutputColor;
// }

void LUT(inout float3 colorU, inout float3 colorT, in Texture3D/* <float3> */ lut, in SamplerState lut_s) {
  //sample lut
  colorT = max(0, colorT);
  float3 colorTBefore = colorT;
  colorT = colorT * 0.96875 + 0.015625;
  colorT = lut.Sample(lut_s, colorT).xyz;

  #if CUSTOM_SDR > 0
    return;
  #endif

  // //dual lut
  // // #if CUSTOM_DUALLUT > 0
  // float3 dual = lut.Sample(lut_s, (colorTBefore * 0.96875 + 0.015625) * 0.01).xyz / 0.01;
  // colorT = lerp(colorT, dual, saturate(pow(GetLuminance(colorT, CS_BT2020), 1.4)));
  // // #endif

  //tradeoff intermediate scaling color space
  colorU = BT709_To_BT2020(colorU);

  //mid gray exposure sampling
  const float mid = 0.5;
  colorU *= (GetLuminance(lut.Sample(lut_s, mid).xyz, CS_BT2020) / mid);

  //lut resolve 0 (UpgradeToneMap crutch ahh ahh)
  colorU = UpgradeToneMapBo3(colorU, Reinhard::ReinhardSimple(colorU), colorT, GS.LUT, 0, CS_BT2020); 

  //lut resolve 1
  // colorU = LUTSamplingThing(colorU * 2, colorTS, lut, lut_s);
  // colorT = Reinhard::ReinhardSimple(colorU);
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 TonemapHDRAndTradeIn(float3 colorU) {
  //tonemap
  #if CUSTOM_TONEMAP > 0 && CUSTOM_SDR == 0
    #if CUSTOM_TONEMAP_PERCHANNEL == 0
      float l = GetLuminance(colorU, CS_BT2020);
      float lT;
      #if CUSTOM_TONEMAP == 1
        lT = Reinhard::ReinhardPiecewiseExtended(l, 100, HDR_PEAK, HDR_SHOULDERSTART);
      #elif CUSTOM_TONEMAP == 2
        lT = ExponentialRollOff(l, HDR_SHOULDERSTART, HDR_PEAK);
      #endif

      colorU = colorU * (lT / l);
    #else
      #if CUSTOM_TONEMAP == 1
        colorU = Reinhard::ReinhardPiecewiseExtended(colorU, 100, HDR_PEAK, HDR_SHOULDERSTART);
      #elif CUSTOM_TONEMAP == 2
        colorU = ExponentialRollOff(colorU, HDR_SHOULDERSTART, HDR_PEAK);
      #endif
    #endif

    //clamp
    #if CUSTOM_TONEMAP_CLAMP == 1
      colorU = min(HDR_PEAK, colorU);
    #elif CUSTOM_TONEMAP_CLAMP == 2
      colorU = ClampByMaxChannel(colorU, HDR_PEAK);
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
    o0 *= GS.GammaInfluence / GS.PreExposure;
    o0 = GammaCorrection_Linear(o0);
    o0 *= GS.Exposure / GS.GammaInfluence;
  #else
    o0 /= GS.PreExposure;
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

  //intermediate colorspace encode
  o = max(0, o);
  #if CUSTOM_SDR == 0
    o = BT2020_To_BT709(o);
  #endif

  //intermediate scaling
  o *= HDR_INTSCALING;

  //intermediate gamma encode
  o = GammaCorrection_IntermediateEncode(o);

  return o;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////