#include "./Includes/Common.hlsl"
#include "../Includes/Math.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "./Includes/PerChannelCorrect.hlsl"
#include "./Includes/ColorGrade.hlsl"
#include "./Includes/DrawBinary.hlsl"
#include "./Includes/ictcp_portable.hlsl"

// #define CUSTOM_ALTSAT 0

//CUSTOM_SDR defs
#if CUSTOM_SDR == 1
  #ifdef CUSTOM_TESTSDR
    #undef CUSTOM_TESTSDR
    #define CUSTOM_TESTSDR 1
  #endif
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CheckCustom(float x, float target, float leniency) {
  if (leniency == 0.f) return all(x == target);
  return all(abs(x - target) <= leniency);
}
bool CheckCustom(float2 x, float2 target, float leniency) {
  if (leniency == 0.f) return all(x == target);
  return all(abs(x - target) <= leniency);
}
bool CheckCustom(float3 x, float3 target, float leniency) {
  if (leniency == 0.f) return all(x == target);
  return all(abs(x - target) <= leniency);
}
bool CheckCustom(float4 x, float4 target, float leniency) {
  if (leniency == 0.f) return all(x == target);
  return all(abs(x - target) <= leniency);
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
DECODEREC709(float2)
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
ENCODEREC709(float2)
ENCODEREC709(float3)
ENCODEREC709(float4)
#undef ENCODEREC709

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//From RenoDX
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
  float3 HermiteSplinePerChannelRolloff(float3 input, float target_white = 1.f, float max_white = 20.f) {
    float target_white_log2 = log2(target_white);
    float max_white_log2 = log2(max_white);
    float3 scaled = float3(
        input.r == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.r), target_white_log2, max_white_log2)),
        input.g == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.g), target_white_log2, max_white_log2)),
        input.b == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.b), target_white_log2, max_white_log2)));
    return scaled;
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/neutwo.hlsl
namespace NeuTwo {
  // f\left(x\right)=\frac{x}{\sqrt{xx+1}}
  float Neutwo(float x) {
    // also written as x * rhypot(x, 1.0)
    float numerator = x;
    float denominator_squared = mad(x, x, 1.0);
    return numerator * rsqrt(denominator_squared);
  }

  // f_{p}\left(x\right)=\frac{px}{\sqrt{xx+pp}}
  float Neutwo(float x, float peak) {
    // also written as x * rhypot(x, peak)
    float p = peak;

    float numerator = p * x;
    float denominator_squared = mad(x, x, p * p);
    return numerator * rsqrt(denominator_squared);
  }

  // f_{c}\left(x\right)=\frac{cpx}{\sqrt{xx\cdot\left(cc-pp\right)+\left(cc\cdot pp\right)}}
  float Neutwo(float x, float peak, float clip) {
    float p = peak;
    float c = clip;
    float cc = c * c;
    float pp = p * p;
    float xx = x * x;

    float numerator = c * p * x;
    float denominator_squared = mad(xx, (cc - pp), cc * pp);

    return numerator * rsqrt(denominator_squared);
  }

  float3 PerChannel(float3 color) {
    return float3(Neutwo(color.r),
                  Neutwo(color.g),
                  Neutwo(color.b));
  }

  float3 PerChannel(float3 color, float3 peak) {
    return float3(Neutwo(color.r, peak.r),
                  Neutwo(color.g, peak.g),
                  Neutwo(color.b, peak.b));
  }

  float3 PerChannel(float3 color, float3 peak, float3 clip) {
    return float3(Neutwo(color.r, peak.r, clip.r),
                  Neutwo(color.g, peak.g, clip.g),
                  Neutwo(color.b, peak.b, clip.b));
  }


  namespace inverse {
    // f_{i}\left(x\right)=\frac{x}{\sqrt{-xx+1}}
    float Neutwo(float x) {
      float numerator = x;
      float denominator_squared = mad(-x, x, 1.0);
      return numerator * rsqrt(denominator_squared);
    }

    // f_{pi}\left(x\right)=\frac{px}{\sqrt{-xx+pp}}
    float Neutwo(float x, float peak) {
      float p = peak;

      float numerator = p * x;
      float denominator_squared = mad(-x, x, p * p);
      return numerator * rsqrt(denominator_squared);
    }

    // f_{ci}\left(x\right)=\frac{cpx}{\sqrt{-xx\cdot\left(cc-pp\right)+\left(cc\cdot pp\right)}}
    float Neutwo(float x, float peak, float clip) {
      float p = peak;
      float c = clip;
      float cc = c * c;
      float pp = p * p;
      float xx = x * x;

      float numerator = c * p * x;
      float denominator_squared = mad(-xx, (cc - pp), cc * pp);

      return numerator * rsqrt(denominator_squared);
    }
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 UCSTo(float3 x, uint cs) {
  // #if CUSTOM_UCS_TYPE == 0
    // return JzAzBz::rgbToJzazbz(x, cs);
  // #elif CUSTOM_UCS_TYPE == 1
  //   return Oklab::rgb_to_oklab(x, cs);
  // #elif CUSTOM_UCS_TYPE == 2
    return renodx::color::ictcp::To(x, cs);
  // #endif
}
float3 UCSFrom(float3 x, uint cs) {
  // #if CUSTOM_UCS_TYPE == 0
    // return JzAzBz::jzazbzToRgb(x, cs);
  // #elif CUSTOM_UCS_TYPE == 1
  //   return Oklab::oklab_to_rgb(x, cs);
  // #elif CUSTOM_UCS_TYPE == 2
    return renodx::color::ictcp::From(x, cs);
  // #endif
}

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
float3 ClampByMaxChannel(float3 x, float peak) {
  float m = max(x.x, max(x.y, x.z));
  if (m > peak) x *= m > 0 ? peak / m : 0;
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float Sum(float3 x) { return x.x + x.y + x.z; }
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Emulate luminance loss/clipping from LDR per-channel tonemap on high single channel colors.
//
// Takes in raw/no-blowout linear color, do per-channel tonemap, then do inverse luminance tonemap.
// That gives a luminance ratio to reduce HDR luminance upgraded color (i.e. from UpgradeToneMap()).
// This means single channel highlights must try harder to be bright.
//
// color_upgraded: Luminance upgraded Color to apply emulation.
// color_untonemapped: Color WITHOUT per-channel blowout.
// peak: The peak of the LDR tonemap curve. (Prob best 1.0 - 1.5)
// makeup: Simple multiplier after inverse luminance to compensate reduction. (prob best around 1.3)
// strength: Global strength of the effect. (prob best 0.25 - 0.35)
// cs: Color space for luminance.
// return: color_upgraded adjusted by the emulated luminance reduction.
float3 PerChannelTonemapLuminanceReductionEmulatation(float3 color_upgraded, float3 color_untonemapped, float peak = 1.0f, float makeup = 1.35f, float strength = 0.25f, uint cs = CS_BT709) {
  //compress perchannel
  color_untonemapped = NeuTwo::PerChannel(color_untonemapped, peak);
  color_untonemapped = min(color_untonemapped, peak); //clip

  //inverse luminance
  float y = GetLuminance(color_untonemapped, cs);
  float y1 = NeuTwo::inverse::Neutwo(y, peak);
  y1 *= makeup; //makeup

  //ratio
  float y2 = GetLuminance(color_upgraded, CS_BT709);
  float ratio = y1 / y2;
  ratio = lerp(1, ratio, saturate(y2 * 2)); //high pass
  ratio = lerp(1, ratio, strength); //global
  
  //apply
  return color_upgraded * ratio;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 Tonemap_SaveSprites_UpgradeSpritesOnly(float3 sprites) {
  #if CUSTOM_TESTSDR == 1
    return saturate(sprites);
  #endif

  //gamma decode
  sprites = gamma_sRGB_to_linear(sprites, GCT_POSITIVE); //requires GCT_POSITIVE

  const float maxIn = GS.UpscaleBGSpritesMax;
  sprites = min(maxIn - 0.00001f, sprites);
  float y0 = GetLuminance(sprites);
  if (y0 > maxIn) sprites *= maxIn / y0; //y clamp
  sprites = min(sprites, maxIn); //per channel clamp
  sprites = Reinhard::inverse::ReinhardScalable(sprites, maxIn, 0, GS.UpscaleBGSpritesExp, 0.18f);

  //gamma encode
  sprites = linear_to_sRGB_gamma(sprites, GCT_POSITIVE); //max(0)

  return sprites;
}

void Tonemap_SaveSprites(in float3 sprites, in float alpha, inout float3 colorT, inout float3 colorU) {
  //colorT (ez)
  colorT += sprites * alpha;

  #if CUSTOM_TESTSDR == 1
    return;
  #endif

  //////////////////////////////////////////////////////////////////

  //colorU
#if CUSTOM_UPSCALE_BGSPRITES > 0
  if (alpha > 0) sprites = Tonemap_SaveSprites_UpgradeSpritesOnly(sprites);
#endif
  colorU += sprites * alpha;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef TONEMAP_COMPLEX
static struct ExposureBracket {
  float3 c0;
  float3 cn1;
  float3 cn2;
  float3 cn3;
  float3 cn4;
} EXPOSURE_BRACKET;

// float3 Tonemap_Complex_ToCbYCr(float3 color) {
//   float4 r0;
//   r0.xyz = color;
//   r0.y = dot(r0.xyz, float3(0.300000012, 0.589999974, 0.109999999)); //YUV/Y'CbCr
//   r0.xz = r0.xz + -r0.y; //UV
//   return r0.xyz;
// }
// float3 Tonemap_Complex_FromCbYCr(float3 color) {
//   float4 r0;
//   r0.xyz = color; // (dR, Y, dB)
//   float g = dot(r0.xyz, float3(-0.508475006, 1, -0.186441004)); //g (from dR, Y, dB)
//   r0.xz = r0.xz + r0.y; //r & b
//   r0.y = g;
//   return r0.xyz;
// }
float3 Tonemap_Complex(float3 colorT, float4 v3, bool isLookBack = true, bool isExtend = true) {
  /*
    r0.y = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
    r0.xz = r0.xz + -r0.yy;
    r1.x = v3.y * r0.y; //exposure
    r1.y = 0;
    r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, r1.xy, 0).yx; //strip tonemap, are they input to output luminance sampling instead of calculating?
    r0.y = v3.x * r1.x; //sat
    r1.xz = r0.yy * r0.xz;
    r0.xz = r0.yy * r0.xz + r1.yy;
    r0.y = dot(r1.xyz, float3(-0.508475006,1,-0.186441004));
    r0.xyz = r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz; //this barely changes from 0, idk cases.
    r0.xyz = saturate(r0.xyz);
  */

  #if CUSTOM_TESTSDR == 1
    isLookBack = false; //force no
  #endif

  // colorT *= colorT;
  // colorT *= DVS7; //Doing exposure here doesn't hue and sat shift, unlike v3.y
  // colorT = sqrt(colorT);

  float4 r0, r1;
  r0.xyz = colorT;
  float3 colorTBak = r0.xyz;

  r0.y = dot(r0.xyz, float3(0.300000012, 0.589999974, 0.109999999)); //Y'CbCr (partial, close to BT601 coeffs)
  r0.xz = r0.xz + -r0.y; //UV
  r1.x = v3.y * r0.y; // exposure on Y
  // r1.x *= DVS1; //debug: exposure multiplier
  // return sqrt(r1.x); //debug: linearized luminance
  float backUpY = r1.x;
  // float moddedY;
  // {
  //   float3 x = Tonemap_Complex_FromCbYCr(float3(r0.x, r1.x, r0.z));
  //   x = pow(x, 1/2.2);
  //   return x; //debug: exposure applied but not tonemapped
  //   // x = pow(x, 2.2);
  //   // moddedY = GetLuminance(x, CS_BT709);
  //   // return moddedY / v3.y; //debug: modded luminance before tonemap
  // }

//   #if CUSTOM_LUT_BLOWOUT_REDUCTION > 0
//   if (isLookBack)
//   {
//      //  if (r1.x > 1) return float3(1, 0, 1); //debug: highlight lut clippings
//     //  return sqrt(r1.x); //debug: return LUT input
// 
//      // rolloff luminance so LUT doesnt clip
//      const float c = DVS6;
//      r1.x = ExponentialRollOff(r1.x, 0.9, 1.001);
//   }
//   #endif

  r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, float2(r1.x, 0), 0).yx;
    // Maybe Neutral LUT https://www.desmos.com/calculator/u3bhz0bn62
    // r1.x = Saturation (Rolls off to 0 way before 1) //TODO: some has too fast drop off in saturation, causing chroma clipping sensation. fix by replacing lut?
    // r1.y = SDR Tonemapped Luma (Rolls off to 1 as it approaches 1)
    // r1.x *= DVS2;
    // r1.y *= DVS3;

//   //extend //TODO: remove, artefacts when out of bounds
//   if (isExtend)
//   {
//     float ex = backUpY * 5; //fudge midgray
//       //1.2 ~= 450nits
//       //1.4 ~= 500nits
//       //1.6 ~= 600nits
//       //2.0 ~= 800nits
//     
//     float y_untonemapped = NeuTwo::Neutwo(ex, 1.4f);
//     y_untonemapped = min(y_untonemapped, 1.4f);
//     float y_tonemapped = NeuTwo::Neutwo(ex, 1.0);
//     y_tonemapped = min(y_tonemapped, 1.0f);
//     float y_tonemapped_graded = r1.y;
// 
//     float ratio = 1.f;
//     float ratioToU = y_untonemapped / y_tonemapped_graded;
//     if (y_untonemapped < y_tonemapped) {
//       ratio = y_untonemapped / y_tonemapped;
//     } else {
//       float y_delta = y_untonemapped - y_tonemapped;
//       y_delta = max(0, y_delta);  // Cleans up NaN
//       const float y_new = y_tonemapped_graded + y_delta;
// 
//       const bool y_valid = (y_tonemapped_graded > 0);
//       ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
//     }
// 
//     // r1.y *= ratio;
//     // r1.y = ex; //debug: replace
//   }

  #if CUSTOM_TESTSDR == 0 && CUSTOM_SDR == 0
    r1.xy *= GS.LUTScalingAndMakeUp;
  #endif

  // blowout reduction
  if (isLookBack)
  {
    float newSat = r1.x;

    //Gaussian, soft-max biased towards higher saturation
    #if CUSTOM_LUT_BLOWOUT_GAUSSIAN > 0
    {
      float yLB = backUpY;
      const float lutStep = (1.0f / 512.f) * GS.LUTGaussianBlurStep;
      const float softMaxStr = GS.LUTGaussianBlurBias; //higher = stronger bias toward peak sat
      float blurredSat = 0, totalWeight = 0;
      [unroll]
      for (int k = -4; k <= 2; k++) { //biased towards lower luminance
        float ySample = max(0.0430528375734, yLB + k * lutStep); //neutral LUT peak
        float sat = g_textures_2_.SampleLevel(g_samplers_2__s, float2(ySample, 0), 0).y; //sat channel
        float w = exp(-0.5f * (k * k)) * exp(sat * softMaxStr); //Gaussian * soft-max bias
        blurredSat = mad(sat, w, blurredSat);
        totalWeight += w;
      }
      float m = blurredSat / totalWeight;
      newSat = max(newSat, m); //clamp chrominance loss
    }
    #endif

    //look back
    #if CUSTOM_LUT_BLOWOUT_REDUCTION > 0
    {
      float yLB = backUpY * GS.LUTBlowoutReductionLookBack;
      yLB = max(0.0430528375734, yLB); //neutral LUT peak
      float2 m = g_textures_2_.SampleLevel(g_samplers_2__s, float2(yLB, 0), 0).yx;
      m = max(newSat, m); //clamp chrominance loss
      newSat = lerp(newSat, m.x, GS.LUTBlowoutReduction);
    }
    #endif

    //high pass (else, shadows may change luminance)
    #if CUSTOM_LUT_BLOWOUT_REDUCTION > 0 || CUSTOM_LUT_BLOWOUT_GAUSSIAN > 0
    {
      float hp = backUpY;
      hp *= 8;
      hp = pow(hp, 2.5f);
      // return hp; //debug
      hp = saturate(hp);
      r1.x = lerp(r1.x, newSat, hp);
    }
    #endif
  }

  r0.y = v3.x * r1.x; //editor saturation slider, usually 1
  r1.xz = r0.y * r0.xz;
  r0.xz = r0.y * r0.xz + r1.y; //r & b channel
  r0.y = dot(r1.xyz, float3(-0.508475006, 1, -0.186441004)); //g channel (recovered)
  //(there is a mismatch from coeffs, creating de/sat from exposure changes?)

  r0.xyz = r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz; //gamma color grade gain-offset slope

  // r0.xyz = saturate(r0.xyz); //per channel blowout

  return r0.xyz;
}
float Tonemap_Complex_GetExposure(float mgg, float mg, float4 v3) {
  float3 x = Tonemap_Complex(mgg, v3, false, false);
  //  x = pow(x, 2.2);
  x *= x;
  // x = gamma_sRGB_to_linear1(x, GCT_POSITIVE);
  float y = GetLuminance(x, CS_BT709);
  return y / mg;
}
// REQUIRES colorU linear!
void Tonemap_ResolveComplexWithExposure(inout float3 colorT, inout float3 colorU, float4 v3) {
  float3 colorTBak = colorT;

  #if CUSTOM_TESTSDR == 1
    colorT = Tonemap_Complex(colorT, v3, false, false);
    return;
  #endif

  //tonemap
  colorT = Tonemap_Complex(colorT, v3, true, false);

  // //extend luminance
  // {
  //   float3 colorTEx = colorTBak;
  //   colorTEx = Tonemap_Complex(colorTEx, v3, false, true);
  //   float colorTExY = GetLuminance(colorTEx, CS_BT709);
  //   float colorTY = GetLuminance(colorT, CS_BT709);
  //   float extendRatio = safeDivision(colorTExY, colorTY, 1);
  //   colorT *= extendRatio;
  //   // colorT = colorTEx; //debug: replaced
  // }

  // exposure
  colorU *= Tonemap_Complex_GetExposure(0.46, 0.18, v3); //best for neutral
  // colorU *= Tonemap_Complex_GetExposure(0.63, 0.36, v3);
  // colorU *= Tonemap_Complex_GetExposure(0.795, 0.6036, v3); //best for troll
}
#endif
#ifdef TONEMAP_FADE
float3 Tonemap_DoFade(float3 x) {
  float4 r0, r1, r2, r3, r4;
  r0 = float4(x, 1);

  r1.x = cmp(0 < g_fade_color.w);
  r1.yzw = g_fade_color.xyz + -r0.xyz;
  r1.yzw = g_fade_color.www * r1.yzw + r0.xyz;
  r2.xy = cmp(g_tone_scale.ww == float2(0,2));
  r3.xyz = g_fade_color.xyz + r0.xyz;
  r4.xyz = g_fade_color.xyz * r0.xyz;
  r2.yzw = r2.yyy ? r3.xyz : r4.xyz;
  r1.yzw = r2.xxx ? r1.yzw : r2.yzw;
  r0.xyz = r1.xxx ? r1.yzw : r0.xyz;

  return r0.xyz;
}
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float GammaPeakCorrect(float x) {
  #if CUSTOM_HDTVREC709_1 == 1
    x = linear_to_sRGB_gamma1(x, GCT_NONE);
    x = DecodeRec709(x);
  #endif

  return x;
}

// REQUIRES colorU linear!
// isIVT is constexpr type beat
float3 Tonemap_Do(in float3 colorU, in float3 colorT, in float2 uv, in Texture2D<float4> texColor, /* float4 g_tone_offset, */ const bool isIVT = false) {
  // return colorT; //debug
  
  #if CUSTOM_TESTSDR == 1
    return saturate(colorT);
  #endif

  #if CUSTOM_UPSCALE_TOON == 0
    if (isIVT) return saturate(colorT);
  #endif

  //CUSTOM_UPSCALE_TOON);
  // "Forced SDR", "Off / Treat as Complex", "On", "On (Ignore Customization Menu)"
  #if CUSTOM_UPSCALE_TOON == 0
    const bool isIVT1 = false; //DEFAULT CASE! THIS SHOULD NOT BE USED.
  #elif CUSTOM_UPSCALE_TOON == 1
    const bool isIVT1 = false;
  #elif CUSTOM_UPSCALE_TOON >= 2
    const bool isIVT1 = isIVT; //see t10 for Customization
  #endif

  // gamma decode
  colorT = max(0, colorT);
  // colorT = pow(colorT, 2.2);
  colorT = gamma_sRGB_to_linear(colorT, GCT_NONE);
  // colorT = pow(colorT, 1/g_tone_offset.w); //g_tone_offset.w is the game's gamma?!?! but there's no benefit to using it now.
  
  // colorU = pow(colorU, 1 / 2.2);
  // colorU = gamma_sRGB_to_linear(colorU, GCT_NONE);

  //y
  float colorUy;
  float colorNy;
  float colorTy;
  {
    colorTy = GetLuminance(colorT, CS_BT709);

    if (!isIVT1)
    {
      colorUy = GetLuminance(colorU, CS_BT709);

      const float mx = 1/* 500.f / 203.f */;
      // colorNy = NeuTwo::Neutwo(colorUy, 1/* , 13.5 */); //neutral rolloff
      colorNy = HermiteSpline::HermiteSplineLuminanceRolloff(colorUy, mx, 90/* 1000 */); // neutral rolloff
      colorNy = min(colorNy, mx);
      // colorNy = colorUy; //no rolloff
    }
  }
  
  //CUSTOM_PCBLOWOUT per-channel blowout emulation
  // #if CUSTOM_PCBLOWOUT == 0
  //   const float colorTMax = 1;
  //   float3 colorTS = min(colorT, colorTMax);
  // #elif CUSTOM_PCBLOWOUT == 1
  //   const float colorTShoulderStart = GS.PCBlowoutStart;
  //   const float colorTMax = GS.PCBlowoutEnd;
  //   float3 colorTS = Reinhard::ReinhardPiecewiseExtended(colorT, 100, colorTMax, colorTShoulderStart);
  // #elif CUSTOM_PCBLOWOUT == 2
  //   const float colorTShoulderStart = GS.PCBlowoutStart;
  //   const float colorTMax = GS.PCBlowoutEnd;
  //   float3 colorTS = ExponentialRollOff(colorT, colorTShoulderStart, colorTMax);
  // #elif CUSTOM_PCBLOWOUT == 3
  //   float3 colorTS = NeuTwo::PerChannel(colorT, GS.PCBlowoutEnd, GS.PCBlowoutStart); //start is replaced with clip
  //   colorTS = min(colorTS, GS.PCBlowoutEnd); //clamp clippings
  // #endif

  // // neutralize LUT luminance
  // float neutralizeRatio;
  // {
  //   neutralizeRatio = colorNy / colorTy;
  //   neutralizeRatio = lerp(neutralizeRatio, 1, saturate(pow(1 - colorNy, 16.0 * GS.LUTNeutralize))); //highpass
  //   neutralizeRatio = lerp(1, neutralizeRatio, GS.LUTNeutralize); //strength
  //   
  //   colorT *= neutralizeRatio;
  //   colorTy *= neutralizeRatio;
  // }

//   Per Channel Blowout
//   if (DVS6 == 0)
//   {  
//     // NeuTwo is most goated
//     float3 colorTS = NeuTwo::PerChannel(colorT, GS.PCBlowoutEnd, GS.PCBlowoutStart); //start is replaced with clip
//     colorTS = min(colorTS, GS.PCBlowoutEnd); //clamp clippings
// 
//     // colorT = UCSTo(colorT, CS_BT709);
//     // colorTS = UCSTo(colorTS, CS_BT709);
//     // colorT = RestoreHueAndChrominanceUcs(colorT, colorTS, 1/* GS.PCBlowoutHue */, 1/* GS.PCBlowoutChrom */);
//     // // colorT.x = colorTS.x; //not needed
//     // colorT = UCSFrom(colorT, CS_BT709);
//     // colorT = max(0, colorT);
// 
//     colorTS *= colorTy / GetLuminance(colorTS, CS_BT709);
//     colorT = colorTS;
//   }

  if (isIVT1) {
    // inverse tonemap, skips colorU
    float colorTyBack = colorTy;
    colorTy = min(GS.UpscaleToonMax - 0.00001f, colorTy); //avoid floating point issues near max

    if (colorTyBack > 0) {
      float yDes = colorTy;
      yDes = Reinhard::inverse::ReinhardScalable(yDes, GS.UpscaleToonMax, 0, GS.UpscaleToonExp, 0.18);
      yDes = clamp(yDes, 0, 100.f);
      colorT *= yDes / colorTyBack;
    }
  } else {
    //Upgrade
    {
      float y_untonemapped = colorUy;
      float y_tonemapped = colorNy;
      float y_tonemapped_graded = colorTy;

      float ratio = 1.f;
      float ratioToU = y_untonemapped / y_tonemapped_graded;
      if (y_untonemapped < y_tonemapped) {
        ratio = y_untonemapped / y_tonemapped;
      } else {
        float y_delta = y_untonemapped - y_tonemapped;
        y_delta = max(0, y_delta);  // Cleans up NaN
        const float y_new = y_tonemapped_graded + y_delta;

        const bool y_valid = (y_tonemapped_graded > 0);
        ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
      }

      //apply ratio
      float3 color_scaled = colorT * ratio;
      float color_scaled_y = y_tonemapped_graded * ratio;

      //hue correct //TODO: remove or fix. This causes rare issues when colors are max BT709 primaries.
      // float3 color_scaled_ucs = UCSTo(color_scaled, CS_BT2020);
      // float3 color_tonemapped_graded_ucs = UCSTo(colorT, CS_BT2020);
      // color_scaled_ucs = RestoreHueAndChrominanceUcs(color_scaled_ucs, color_tonemapped_graded_ucs, 1.f, 0.f, 1.f);
      // color_scaled = UCSFrom(color_scaled_ucs, CS_BT2020);

      float3 color_scaled_bak = color_scaled;

      //Per Channel Blowout (gradual pass)
      {
        float3 colorTS = color_scaled;
        float y = color_scaled_y;
        if (y > 0) { //safe
          const float p = GS.PCBlowoutLumaEnd; //peak
          // float y1 = HermiteSpline::HermiteSplineLuminanceRolloff(y, /* 12 */ DVS8, 10000 * DVS7); //extended peak for smoother gradients.
          float y1 = NeuTwo::Neutwo(y, p/* , DVS7 */); //extended peak for smoother gradients.
          y1 = min(y1, p); //clamp to peak
          colorTS *= y1 / y;

          const float end = GS.PCBlowoutPerChannelEnd;
          colorTS = NeuTwo::PerChannel(colorTS, end, GS.PCBlowoutPerChannelClip); //per channel rolloff
          colorTS = min(colorTS, end); //clamp clip
          // colorTS = colorTS == DVS4 ? float3(1,0,1) : colorTS; //debug clipped visualize

          //set: chroma & hue only
          colorTS *= y / GetLuminance(colorTS, CS_BT709);
          color_scaled = colorTS;

          //set: via UCS
          // color_scaled = UCSTo(color_scaled, CS_BT709);
          // colorTS = UCSTo(colorTS, CS_BT709);
          // color_scaled = RestoreHueAndChrominanceUcs(color_scaled, colorTS, 0.95/* GS.PCBlowoutHue */, 0.8/* GS.PCBlowoutChrom */);
          // if (DVS6 == 0) color_scaled.x = colorTS.x;
          // color_scaled = UCSFrom(color_scaled, CS_BT709);
          // color_scaled = max(0, color_scaled);
        }
      }

      //Per Channel Blowout (agressive near peak pass)
      {
        float3 colorTS = color_scaled;
        float y = color_scaled_y;
        if (y > 0) { //safe
          const float p = GS.PCBlowoutPerChannel2ndEnd;
          float y1 = NeuTwo::Neutwo(y, p); 
          y1 = min(y1, p);
          colorTS *= y1 / y;

          const float end = p;
          colorTS = ExponentialRollOff(colorTS, end * GS.PCBlowoutPerChannel2ndStartRatio, end);
          colorTS = min(colorTS, end);

          //set: chroma & hue only
          colorTS *= y / GetLuminance(colorTS, CS_BT709);
          color_scaled = colorTS;

          //set: via UCS
          // color_scaled = UCSTo(color_scaled, CS_BT709);
          // colorTS = UCSTo(colorTS, CS_BT709);
          // color_scaled = RestoreHueAndChrominanceUcs(color_scaled, colorTS, 0.95/* GS.PCBlowoutHue */, 0.8/* GS.PCBlowoutChrom */);
          // if (DVS6 == 0) color_scaled.x = colorTS.x;
          // color_scaled = UCSFrom(color_scaled, CS_BT709);
          // color_scaled = max(0, color_scaled);
        }
      }

      //perchannel luminance reduction
      #if CUSTOM_PERCHANNELLUMAEMULATE > 0
        color_scaled = PerChannelTonemapLuminanceReductionEmulatation(
          color_scaled, color_scaled_bak, 
          1, 
          1.35, 
          GS.PerChannelLuminanceReductionEmulateStrength,
          CS_BT709
        );
      #endif

      //debug
      #if CUSTOM_UPGRADE_DEBUG == 0
         colorT = color_scaled;
      #elif CUSTOM_UPGRADE_DEBUG == 1
        colorT = colorU;
      #elif CUSTOM_UPGRADE_DEBUG == 2
        colorT = colorU * (y_tonemapped / GetLuminance(colorU, CS_BT709));
        colorT = max(0, colorT);
        colorT = linear_to_sRGB_gamma(colorT);
        return colorT;
      #elif CUSTOM_UPGRADE_DEBUG == 3
        colorT = colorT;
        colorT = max(0, colorT);
        colorT = linear_to_sRGB_gamma(colorT);
        return colorT;
      #endif
    }
  }
  //(colorT is the main output starting here!)

  //LUT decrease makeup
  #ifdef TONEMAP_COMPLEX
  {
    float e = rcp(GS.LUTScalingAndMakeUp);
    e *= e;
    colorT *= e;
  }
  #endif

  //ColorGrade
  #if CUSTOM_COLORGRADE == 1
    colorT = RenoDX_ColorGrade(
      colorT, 
      GS.CGContrast, GS.CGContrastMidGray / GamePaperWhiteNits,
      GS.CGHighlightsStrength, GS.CGHighlightsMidGray / GamePaperWhiteNits,
      GS.CGShadowsStrength, GS.CGShadowsMidGray / GamePaperWhiteNits,
      1,
      CS_BT709,
      true
    );
  #endif

  // //Gamma Correction
  // colorT = GammaCorrection_Linear(colorT);

  //HDR tonemap
  #if CUSTOM_TONEMAP_SCALING == 0
    float l = GetLuminance(colorT, CS_BT709); //luma
  #elif CUSTOM_TONEMAP_SCALING == 1
    float l = max(colorT.x, max(colorT.y, colorT.z)); //max channel
  #endif
  if (l > 0) { //safe
    float p = GS.TonemapperPeakCached;
    float m = GS.TonemapperMaxExpectedCached;

    float lT;
    lT = HermiteSpline::HermiteSplineLuminanceRolloff(l, p, m);
    colorT *= lT / l;

    // colorT = HermiteSpline::HermiteSplinePerChannelRolloff(colorT, p, m); //TODO: remove. not possible, hues required luminance rolloff first, else blowout is too strong.

    // clamp peak
    #if CUSTOM_TONEMAP_CLAMP == 1
      colorT = min(colorT, p);
    #elif CUSTOM_TONEMAP_CLAMP == 2
      colorT = ClampByMaxChannel(colorT, p);
    #endif

    // colorT = p; //test peak 
    // colorT = -0.00001f; //test 0
    // colorT = 1; //test 1
   } else colorT = 0; //safe

  //gamme encode
  colorT = max(0, colorT);
  // colorT = pow(colorT, g_tone_offset.w);
  colorT = linear_to_sRGB_gamma(colorT);

  return colorT;
}

void Tonemap_Out(inout float4 o0) {
  float3 x = o0.xyz;

  o0.xyz = x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
