#include "./Includes/Common.hlsl"
#include "../Includes/Math.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "./Includes/ColorGrade.hlsl"
#include "./Includes/DrawBinary.hlsl"
#include "./Includes/ictcp_portable.hlsl"

#ifndef cmp
#define cmp -
#endif

// CUSTOM_HDRTONEMAPONSDR
#ifdef TONEMAP_COMPLEX // applicable only for FutureTone
  #if CUSTOM_SDR_1 == 0 // force CUSTOM_HDRTONEMAPONSDR off if HDR
    #ifdef CUSTOM_HDRTONEMAPONSDR
      #undef CUSTOM_HDRTONEMAPONSDR
      #define CUSTOM_HDRTONEMAPONSDR 0
    #endif
  #endif
  #if CUSTOM_HDRTONEMAPONSDR // override CUSTOM_SDR_1
    #ifdef CUSTOM_SDR_1
      #undef CUSTOM_SDR_1
      #define CUSTOM_SDR_1 0
    #endif
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
  return 0.0810000002 >= x\
    ? 0.222222224 * x\
    : pow(0.909918129 * (0.0989999995 + x), 2.22222233);\
}
DECODEREC709(float)
DECODEREC709(float2)
DECODEREC709(float3)
DECODEREC709(float4)
#undef DECODEREC709

#define ENCODEREC709(T)\
T EncodeRec709(T x) {\
  return 0.0179999992 >= x\
    ? 4.5 * x\
    : 1.09899998 * pow(x, 0.449999988) - 0.0989999995;\
}
ENCODEREC709(float)
ENCODEREC709(float2)
ENCODEREC709(float3)
ENCODEREC709(float4)
#undef ENCODEREC709

float EncodeSrgb(float x) {
  return linear_to_sRGB_gamma1(x, GCT_NONE);
}
float3 EncodeSrgb(float3 x) {
  return linear_to_sRGB_gamma(x, GCT_NONE);
}
float DecodeSrgb(float x) {
  return gamma_sRGB_to_linear1(x, GCT_NONE);
}
float3 DecodeSrgb(float3 x) {
  return gamma_sRGB_to_linear(x, GCT_NONE);
}

#define INTERMEDIATE_GAMMA 1 // do sRGB, treat as if correct/matching, letting SDR mismatch by itself
float EncodeIntermediate(float x) {
  #if INTERMEDIATE_GAMMA == 0
    return pow(x, 1/2.2);
  #else
    return EncodeSrgb(x);
  #endif
}
float3 EncodeIntermediate(float3 x) {
  #if INTERMEDIATE_GAMMA == 0
    return pow(x, 1/2.2);
  #else
    return EncodeSrgb(x);
  #endif
}
float DecodeIntermediate(float x) {
  #if INTERMEDIATE_GAMMA == 0
    return pow(x, 2.2);
  #else
    return DecodeSrgb(x);
  #endif
}
float3 DecodeIntermediate(float3 x) {
  #if INTERMEDIATE_GAMMA == 0
    return pow(x, 2.2);
  #else
    return DecodeSrgb(x);
  #endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// rect is top left (x,y), bottom right (x,y)
float3 DrawRect(float2 uv, float4 rect, float3 color, float3 rectColor) 
{
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
	if (r == 0) return color;
	return rectColor;
}

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
float3 PerChannelTonemapLuminanceReductionEmulation(float3 color_upgraded, float3 color_untonemapped, float peak = 1.0f, float makeup = 1.35f, float strength = 0.25f, uint cs = CS_BT709) {
  //compress perchannel
  color_untonemapped = NeuTwo::PerChannel(color_untonemapped, peak);

  //luminance
  float y = GetLuminance(color_untonemapped, cs);
  if (y <= 0) return color_upgraded; //safe

  //inverse luminance
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
float3 BloomThreshold(float3 x, float3 threshold) {
  float3 csum = x;
  float3 csumBack = x;

  // apply
  csum -= threshold;
  csum = max(0, csum);

  // correct
#if CUSTOM_BLOOM_THRESHOLD_1 > 0
  float csumY = GetLuminance(csum);

  #if CUSTOM_BLOOM_THRESHOLD_1 == 1
    csumBack -= 0.955; // good fudge TODO: if g_color.xyz != 1.1, make dynamic
    csumBack = max(0, SetChrominance(csumBack, 1.088)); // makeup
  #elif CUSTOM_BLOOM_THRESHOLD_1 == 2
    // dumb curve
    // float anchor = 0.18;
    // csumBack *= anchor;
    // float3 upper = pow(csumBack, 2.4);
    // float3 lower = pow(csumBack, 3.66);
    // csumBack = lerp(lower, upper, saturate(csumBack));
    // csumBack /= anchor;
    csumBack = pow(csumBack, 2.4);

    // hue shift
    float p = 40000 / 203.f;
    csumBack = csumBack / ((csumBack / p) + 1); // reinhard for hue shift
    csumBack = max(0, SetChrominance(csumBack, 1.055)); // makeup
  #endif

  // y correct
  csum = csumBack * safeDivision(csumY, GetLuminance(csumBack), 0);
#endif

  return csum;
}

float3 Tonemap_BloomSample(Texture2D<float4> t, SamplerState s, float2 uv) {
    // uint w;
    // uint h;
    // t.GetDimensions(w, h);
    // float2 pixSize = 1.f / uint2(w, h);

    float3 x = 0;

    // x += t.Sample(s, uv + (pixSize * int2(-1,  0))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 1,  0))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 0,  0))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 0, -1))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 0, 1))).xyz;
    // x *= (1.f/5.f) * GS.BloomStrength;

    // x += t.Sample(s, uv + (pixSize * int2(-1, -1))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2(-1,  0))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2(-1,  1))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 0, -1))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 0,  0))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 0,  1))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 1, -1))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 1,  0))).xyz;
    // x += t.Sample(s, uv + (pixSize * int2( 1,  1))).xyz;
    // x *= (1.f/9.f) * GS.BloomStrength;

    x = t.Sample(s, uv).xyz /* * GS.BloomStrength */;

    return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float3 Tonemap_SaveSprites_UpgradeSpritesOnly(float3 sprites) {
  #if CUSTOM_SDR_1 == 1 || CUSTOM_UPSCALE_BGSPRITES == 0
    return saturate(sprites);
  #endif

  //gamma decode
  sprites = DecodeIntermediate(max(0, sprites));

  const float maxIn = GS.UpscaleBGSpritesMax;
  sprites = min(maxIn - 0.00001f, sprites);
  float y0 = GetLuminance(sprites);
  if (y0 > maxIn) sprites *= maxIn / y0; //y clamp
  sprites = min(sprites, maxIn); //per channel clamp
  sprites = Reinhard::inverse::ReinhardScalable(sprites, maxIn, 0, GS.UpscaleBGSpritesExp, 0.18f);

  //gamma encode
  sprites = EncodeIntermediate(max(0, sprites));

  return sprites;
}

void Tonemap_SaveSprites(in float3 sprites, in float alpha, inout float3 colorT, inout float3 colorU) {
  //colorT (ez)
  colorT += sprites * alpha;

  #if CUSTOM_SDR_1 == 1
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
Texture2D<float> g_textures_lut_biased : register(t11); // orig uses up to 7 + optional 10 (depth)
float3 Tonemap_Complex(float3 colorT, float4 v3, bool isLookBack = true, bool isExtend = true) { // TODO: del isExtend
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

  #if CUSTOM_SDR_1 == 1 || CUSTOM_LUT_BLOWOUT_GAUSSIAN == 0
    isLookBack = false; //force no
  #endif

  float4 r0, r1;
  r0.xyz = colorT;
  float3 colorTBak = r0.xyz;

  r0.y = dot(r0.xyz, float3(0.300000012, 0.589999974, 0.109999999)); //Y'CbCr (partial, close to BT601 coeffs)
  r0.xz = r0.xz + -r0.y; // UV
  r1.x = v3.y * r0.y; // exposure on Y
  float backUpY = r1.x;

#if 1 // used cached biased LUT
  // orig LUT
  r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, float2(r1.x, 0), 0).yx; // dumb decomp swizzle!

  // biased LUT
  if (isLookBack) {
    float lutInput = backUpY * (512./LUT_CACHE_OUTPUT_SIZE); // encode
    r1.x = g_textures_lut_biased.SampleLevel(g_samplers_2__s, float2(lutInput, 0), 0).x;
    r1.y *= 0.995f;

    // debug: overshoot (but at this point, it should blow out white anyway)
#if TEST
    if (lutInput > 1.0f) return float3(5, 0, 0);
#endif
  }
#else // calculate per pixel (TODO: del)
  /*
    Maybe Neutral LUT https://www.desmos.com/calculator/u3bhz0bn62
    r1.x = Saturation (Rolls off to 0 way before 1)
    r1.y = SDR Tonemapped Luma (Rolls off to 1 as it approaches 1)
  */
  r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, float2(r1.x, 0), 0).yx; // dumb decomp swizzle!

  // "headroom"
  float satBeforeHeadroom = r1.x;
  if (isLookBack) r1.xy *= float2(0.9975f, 0.995f);

  // blowout reduction
  if (isLookBack)
  {
    float newSat = r1.x;

    // Gaussian, soft-max biased towards higher saturation
    {
      float y = backUpY;
      float satOrig = r1.x;

      #if CUSTOM_LUT_BLOWOUT_GAUSSIAN_STOPS == 0 // sampling step size
        const float lutStep = (1.0f / 512.f) * GS.LUTGaussianBlurStep;
      #else
        const float lutStep = (1.0f / 512.f) * GS.LUTGaussianBlurStep * (HDR_STOPS * 0.5f + 0.5f); 
      #endif 
      const float softMaxStr = GS.LUTGaussianBlurBias; // higher = stronger bias toward peak sat

      float blurredSat = 0, totalWeight = 0;
      [unroll] for (int k = -4; k <= 2; k++) { // biased towards lower luminance (more negative index)
        float ySample = max(0.0430528375734, k * lutStep + y); // neutral LUT peak
        float sat = g_textures_2_.SampleLevel(g_samplers_2__s, float2(ySample, 0), 0).y; // sat channel
        if (sat > satOrig)
        {
          float w = exp(-0.5f * (k * k)) * exp(sat * softMaxStr); // gaussian * soft-max bias
          blurredSat = mad(sat, w, blurredSat);
          totalWeight += w;
        }
      }

      float m = safeDivision(blurredSat / totalWeight, 0); // avg
      newSat = max(newSat, m); //clamp chrominance loss
    }

    // high pass (else, shadows may change luminance)
    {
      float hp = backUpY;
      hp *= 8;
      hp = pow(hp, 2.5f);
      hp = saturate(hp);
      r1.x = lerp(r1.x, newSat, hp);
    }
  }
#endif

  r0.y = v3.x * r1.x; // per-shot PV defined saturation, usually 1
  r1.xz = r0.y * r0.xz;
  r0.xz = r0.y * r0.xz + r1.y; // r & b channel
  r0.y = dot(r1.xyz, float3(-0.508475006, 1, -0.186441004)); // g channel (recovered)

  r0.xyz = r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz; // gamma color grade gain-offset slope

  return r0.xyz;
}
float Tonemap_Complex_GetExposure(float mgg, float mg, float4 v3) {
  float3 x = Tonemap_Complex(mgg, v3, false, false);
  x = DecodeIntermediate(max(0, x)); // required safety
  float y = GetLuminance(x, CS_BT709);
  return safeDivision(y, mg, 0);
}
// REQUIRES colorU linear!
void Tonemap_ResolveComplexWithExposure(inout float3 colorT, inout float3 colorU, float4 v3) {
  float3 colorTBak = colorT;

  #if CUSTOM_SDR_1 == 1
    colorT = Tonemap_Complex(colorT, v3, false, false);
    return;
  #else
    colorT = Tonemap_Complex(colorT, v3, true, false); //tonemap
    colorU *= Tonemap_Complex_GetExposure(0.46, 0.18, v3); //exposure
  #endif
  return;
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
// REQUIRES colorU linear!
// isIVT is constexpr type beat
float3 Tonemap_Do(in float3 colorU, in float3 colorT, in float2 uv, in Texture2D<float4> texColor, /* float4 g_tone_offset, */ const bool isIVT = false) {
  // return colorT; //debug
  // return EncodeIntermediate(colorU);
  
  #if CUSTOM_SDR_1 == 1
    return saturate(colorT);
  #endif

  #if CUSTOM_UPSCALE_TOON == 0
    if (isIVT) return saturate(colorT);
  #endif

  // CUSTOM_UPSCALE_TOON ("Forced SDR", "Off / Treat as Complex", "On", "On (Ignore Customization Menu)")
  #if CUSTOM_UPSCALE_TOON == 0
    const bool isIVT1 = false; //DEFAULT CASE! THIS SHOULD NOT BE USED.
  #elif CUSTOM_UPSCALE_TOON == 1
    const bool isIVT1 = false;
  #elif CUSTOM_UPSCALE_TOON >= 2
    const bool isIVT1 = isIVT; //see t10 for Customization
  #endif

  // gamma decode
  colorT = DecodeIntermediate(max(0, colorT));

  //y
  float colorUy;
  float colorNy;
  float colorTy;
  {
    colorTy = GetLuminance(colorT, CS_BT709);

    if (!isIVT1)
    {
      colorUy = GetLuminance(colorU, CS_BT709);
      // colorNy = HermiteSpline::HermiteSplineLuminanceRolloff(colorUy, 1, 90); 
      colorNy = colorUy / (colorUy + 1); // reinhard (like 99% same as Hermite)
    }
  }

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
      if (y_untonemapped < y_tonemapped) {
        ratio = y_untonemapped / y_tonemapped;
      } else {
        float y_delta = y_untonemapped - y_tonemapped;
        y_delta = max(0, y_delta);  // Cleans up NaN
        const float y_new = y_tonemapped_graded + y_delta;
        ratio = y_tonemapped_graded > 0 ? (y_new / y_tonemapped_graded) : 0;
      }

      //apply ratio
      float3 color_scaled = colorT * ratio;
      float color_scaled_y = y_tonemapped_graded * ratio;

      //backup
      float3 color_scaled_bak = color_scaled;

      // TODO: it blows since luminance is gamma encoded & orig perchannel is saturate().
      // hard to do hue shift without ruining instantly and too much PerChannelTonemapLuminanceReduction will make white too OP.

      //Per Channel Blowout (gradual)
      {
        float3 colorTS = color_scaled;
        float y = color_scaled_y;

        const float p = 2.016f /* GS.PCBlowoutLumaEnd */; //peak
        float y1 = NeuTwo::Neutwo(y, p); //extended peak for smoother gradients.
        colorTS *= safeDivision(y1, y, 0);

        const float end = 2.64f /* GS.PCBlowoutPerChannelEnd */;
        colorTS = NeuTwo::PerChannel(colorTS, end, 3.918f /* GS.PCBlowoutPerChannelClip */); //per channel rolloff
        colorTS = min(colorTS, end); //clamp clip

        #if CUSTOM_PCC_QUALITY == 0
          //set: chroma & hue only
          colorTS *= safeDivision(y, GetLuminance(colorTS, CS_BT709), 0);
          color_scaled = colorTS;
        #else
          //set: via UCS
          colorTS *= safeDivision(y, GetLuminance(colorTS, CS_BT709), 1); //luma normalization
          color_scaled = UCSTo(color_scaled, CS_BT709);
          colorTS = UCSTo(colorTS, CS_BT709);
          color_scaled = RestoreHueAndChrominanceUcs(color_scaled, colorTS, 0.87, 0.87, 0.1f);
          color_scaled = UCSFrom(color_scaled, CS_BT709);
          color_scaled = max(0, color_scaled);
        #endif
      }

      //Per Channel Blowout (aggressive near peak)
      {
        float3 colorTS = color_scaled;
        float y = color_scaled_y;

        const float p = 2.517f/* GS.PCBlowoutPerChannel2ndEnd */;
        float y1 = NeuTwo::Neutwo(y, p); 
        colorTS *= safeDivision(y1, y, 0);

        const float end = p;
        colorTS = ExponentialRollOff(colorTS, end * 0.93f /* GS.PCBlowoutPerChannel2ndStartRatio */, end);

        #if CUSTOM_PCC_QUALITY == 0
          //set: chroma & hue only
          colorTS *= safeDivision(y, GetLuminance(colorTS, CS_BT709), 0);
          color_scaled = colorTS;
        #else
          //set: via UCS
          colorTS *= safeDivision(y, GetLuminance(colorTS, CS_BT709), 1); //luma normalization
          color_scaled = UCSTo(color_scaled, CS_BT709);
          colorTS = UCSTo(colorTS, CS_BT709);
          color_scaled = RestoreHueAndChrominanceUcs(color_scaled, colorTS, 0.87, 0.87, 0.1f);
          color_scaled = UCSFrom(color_scaled, CS_BT709);
          color_scaled = max(0, color_scaled);
        #endif
      }

      // PerChannelTonemapLuminanceReductionEmulation
      #if CUSTOM_PERCHANNELLUMAEMULATE > 0 && CUSTOM_HDRTONEMAPONSDR == 0
        color_scaled = PerChannelTonemapLuminanceReductionEmulation(
          color_scaled, color_scaled_bak, 
          1, 
          1.35, 
          GS.PerChannelLuminanceReductionEmulateStrength,
          CS_BT709
        );
      #endif

      // save in post ahh sat boost to better match highlights
      // color_scaled = CorrectPerChannelTonemapHiglightsDesaturation(color_scaled, DVS2, DVS1, CS_BT709);
      {
        float peakBrightness = 1.287;
        float invSat = 0.790;

        float sourceChrominance = GetChrominance(color_scaled);

        if (color_scaled_y < 100000.000 && sourceChrominance > 0.000001) { // TODO: SetChrominance() is weak, and needs safety from high values
          float maxBrightness = max3(color_scaled);
          float midBrightness = GetMidValue(color_scaled); 
          float minBrightness = min3(color_scaled);
          float brightnessRatio = saturate(maxBrightness / peakBrightness);
          brightnessRatio = lerp(brightnessRatio, sqrt(brightnessRatio), sqrt(saturate(InverseLerp(minBrightness, maxBrightness, midBrightness)))); // TODO: saturate() is not present in global code, which causes errors for white and near black

          float chrominancePow = lerp(1.0, 1 / invSat, brightnessRatio);
          float targetChrominance = sourceChrominance > 1.0 ? pow(sourceChrominance, chrominancePow) : (1.0 - pow(1.0 - sourceChrominance, chrominancePow));
          float chrominanceRatio = safeDivision(targetChrominance, sourceChrominance, 1);

          color_scaled = RestoreLuminance(SetChrominance(color_scaled, chrominanceRatio), color_scaled, true, CS_BT709);
        }
      }

      //debug
      #if CUSTOM_UPGRADE_DEBUG == 0
        colorT = color_scaled;
      #elif CUSTOM_UPGRADE_DEBUG == 1
        colorT = colorU;
      #elif CUSTOM_UPGRADE_DEBUG == 2
        colorT = colorU * (y_tonemapped / GetLuminance(colorU, CS_BT709));
        colorT = max(0, colorT);
        colorT = EncodeIntermediate(colorT);
        return colorT;
      #elif CUSTOM_UPGRADE_DEBUG == 3
        colorT = colorT;
        colorT = max(0, colorT);
        colorT = EncodeIntermediate(colorT);
        return colorT;
      #endif
    }
  }
  //(colorT is the main output starting here!)

  // LUT decrease makeup
  #ifdef TONEMAP_COMPLEX
  {
    float e = rcp(0.995f);
    e *= e;
    colorT *= e;
  }
  #endif

  // ColorGrade
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

  // HDR tonemap
  float p = GS.TonemapperPeakCached;
  float m = GS.TonemapperMaxExpectedCached;

  #if CUSTOM_HDRTONEMAPONSDR == 1 // force SDR
    p = 1;
    m = 36;
  #endif

  #if CUSTOM_TONEMAP_SCALING == 0
    float l = GetLuminance(colorT, CS_BT709); //luma
  #elif CUSTOM_TONEMAP_SCALING == 1
    float l = max(colorT.x, max(colorT.y, colorT.z)); //max channel
  #endif
  float lT = HermiteSpline::HermiteSplineLuminanceRolloff(l, p, m); //REQUIRED HQ HermiteSpline!
  colorT *= safeDivision(lT, l, 0);

  // clamp peak
  #if CUSTOM_TONEMAP_CLAMP == 1
    colorT = min(colorT, p);
  #elif CUSTOM_TONEMAP_CLAMP == 2
    colorT = ClampByMaxChannel(colorT, p);
  #endif

  // gamme encode
  colorT = EncodeIntermediate(max(0, colorT));

  return colorT;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
