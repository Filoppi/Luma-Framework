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

#if CUSTOM_LUTBUILDER_COLORSPACE == 0
  const static uint lutbuilder_colorspace = CS_BT709;
#else
  const static uint lutbuilder_colorspace = CS_BT2020;
#endif

static struct TonemapStuffStruct {
  float3 colorU;
  float3 colorT;

  float3 colorTRaw; //baseline raw after tonemap, before bloom/lensflare
  float3 colorTRawLUTed; //baseline raw now LUTed, right before Upgrade()

  float3 colorTWithBloom; //raw + bloom
  float3 colorTWithLens; //raw + lensflare

  float3 LutMinimumOutput; //Lut results sampled at (0,0,0)
} TS;

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
//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/reinhard.hlsl
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

  //https://github.com/patriciogonzalezvivo/lygia/blob/main/color/tonemap/reinhardJodie.hlsl
  //https://web.archive.org/web/20210205114323/http://www.cmap.polytechnique.fr/~peyre/cours/x2005signal/hdr_photographic.pdf
  float3 Jodie(float3 x, float peak = 1.f, float perchannelInfluence = 1.f, uint cs = CS_BT709) {
     float3 perScaled = Reinhard::ReinhardSimple(x, peak);

     float y = GetLuminance(x, cs);
     float3 yScaled = x / (y + 1.0f);

     x = lerp(yScaled, perScaled, y * perchannelInfluence);
     x = min(x, peak);

     return x;
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
// Uchimura 2018, "Practical HDR and Wide Color Techniques in Gran Turismo SPORT"
// https://www.desmos.com/calculator/gslcdxvipg
// http://cdn2.gran-turismo.com/data/www/pdi_publications/PracticalHDRandWCGinGTS.pdf
#define GTTONEMAP_GENERATOR(T)                \
  T GTTonemap(T x,                            \
              float P = 1.f,                  \
              float a = 1.f,                  \
              float m = 0.22f,                \
              float l = 0.5f,                 \
              float c = 1.33f,                \
              float b = 0.f) {                \
    float l0 = ((P - m) * l) / a;             \
    float L0 = m - (m / a);                   \
    float L1 = m + (1.0f - m) / a;            \
                                              \
    T S0 = m + l0;                            \
    T S1 = m + a * l0;                        \
    T C2 = (a * P) / (P - S1);                \
    T CP = -C2 / P;                           \
                                              \
    T w0 = 1.0f - smoothstep(0.0f, m, x);     \
    T w2 = step(m + l0, x);                   \
    T w1 = 1.0f - w0 - w2;                    \
                                              \
    T T_ = m * pow(x / m, c) + b;             \
    T S_ = P - (P - S1) * exp(CP * (x - S0)); \
    T L_ = m + a * (x - m);                   \
                                              \
    return T_ * w0 + L_ * w1 + S_ * w2;       \
  }

GTTONEMAP_GENERATOR(float)
GTTONEMAP_GENERATOR(float3)
#undef GTTONEMAP_GENERATOR
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
  float3 HermiteSplinePerChannelRolloff(float3 input, float target_white = 1.f, float max_white = 20.f) {
    float target_white_log2 = log2(target_white);
    float max_white_log2 = log2(max_white);
    float3 scaled = float3(
        input.r == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.r), target_white_log2, max_white_log2)),
        input.g == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.g), target_white_log2, max_white_log2)),
        input.b == 0 ? 0 : exp2(HermiteSplineRolloff(log2(input.b), target_white_log2, max_white_log2)));
    return scaled;
  }

  float HermiteSplineLuminanceRolloff(float luminance, float target_white = 1.f, float max_white = 20.f) {
    if (luminance <= 0) return 0;
    return exp2(HermiteSplineRolloff(log2(luminance), log2(target_white), log2(max_white)));
  }
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//https://github.com/clshortfuse/renodx/blob/main/src/shaders/tonemap/neutwo.hlsl
namespace NeuTwo {
// f\left(x\right)=\frac{x}{\sqrt{xx+1}}
float NeuTwo(float x) {
  // also written as x * rhypot(x, 1.0)
  float numerator = x;
  float denominator_squared = mad(x, x, 1.0);
  return numerator * rsqrt(denominator_squared);
}

// f_{p}\left(x\right)=\frac{px}{\sqrt{xx+pp}}
float NeuTwo(float x, float peak) {
  // also written as x * rhypot(x, peak)
  float p = peak;

  float numerator = p * x;
  float denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}

// f_{c}\left(x\right)=\frac{cpx}{\sqrt{xx\cdot\left(cc-pp\right)+\left(cc\cdot pp\right)}}
float NeuTwo(float x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float xx = x * x;

  float numerator = c * p * x;
  float denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}

// f_{g}\left(x\right)=\frac{pgx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot gg\cdot\left(xx\cdot\left(cc-pp\right)+cc\cdot\left(pp-gg\right)\right)}}
float NeuTwo(float x, float peak, float clip, float gray) {
  float p = peak;
  float g = gray;
  float c = clip;

  float cc = c * c;
  float pp = p * p;
  float gg = g * g;
  float xx = x * x;
  float cc_minus_gg = cc - gg;

  float numerator = p * g * x * cc_minus_gg;
  float denominator_squared = cc_minus_gg * gg * (mad(xx, (cc - pp), cc * (pp - gg)));
  return numerator * rsqrt(denominator_squared);
}

// f_{o}\left(x\right)=\frac{pox\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(xx\cdot\left(ccoo-ppgg\right)+ccgg\cdot\left(pp-oo\right)\right)}}
float NeuTwo(float x, float peak, float clip, float gray_in, float gray_out) {
  float p = peak;
  float g = gray_in;
  float o = gray_out;

  float cc = clip * clip;
  float pp = peak * peak;
  float gg = g * g;
  float oo = o * o;
  float xx = x * x;

  float cc_minus_gg = cc - gg;

  float numerator = p * o * x * cc_minus_gg;

  float ccoo = cc * oo;
  float ppgg = pp * gg;
  float ccgg = cc * gg;

  float denominator_squared = cc_minus_gg * mad(xx, (ccoo - ppgg), ccgg * (pp - oo));

  return numerator * rsqrt(denominator_squared);
}

// // f_{m}\left(x\right)=\frac{qzx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(xx\cdot\left(cczz-qqgg\right)+ccgg\cdot\left(qq-zz\right)\right)}}+m
// float NeuTwo(float x, float peak, float clip, float gray_in, float gray_out, float minimum) {
//   float m = minimum;
//   float g = gray_in;
//   float z = gray_out - m;
//   float q = peak - m;
//   float c = clip;
// 
//   float cc = c * c;
//   float gg = g * g;
//   float cc_minus_gg = cc - gg;
// 
//   float numerator = q * z * x * cc_minus_gg;
// 
//   float xx = x * x;
//   float zz = z * z;
//   float qq = q * q;
// 
//   float cczz = cc * zz;
//   float qqgg = qq * gg;
//   float ccgg = cc * gg;
// 
//   float denominator_squared = cc_minus_gg * mad(xx, (cczz - qqgg), ccgg * (qq - zz));
// 
//   return mad(numerator, rsqrt(denominator_squared), m);
// }

  float3 PerChannel(float3 color) {
    return float3(NeuTwo(color.r),
                  NeuTwo(color.g),
                  NeuTwo(color.b));
  }

  float3 PerChannel(float3 color, float3 peak) {
    return float3(NeuTwo(color.r, peak.r),
                  NeuTwo(color.g, peak.g),
                  NeuTwo(color.b, peak.b));
  }

  float3 PerChannel(float3 color, float3 peak, float3 clip) {
    return float3(NeuTwo(color.r, peak.r, clip.r),
                  NeuTwo(color.g, peak.g, clip.g),
                  NeuTwo(color.b, peak.b, clip.b));
  }

  float3 PerChannel(float3 color, float3 peak, float3 clip, float3 gray) {
    return float3(NeuTwo(color.r, peak.r, clip.r, gray.r),
                  NeuTwo(color.g, peak.g, clip.g, gray.g),
                  NeuTwo(color.b, peak.b, clip.b, gray.b));
  }

  float3 PerChannel(float3 color, float3 peak, float3 clip, float3 gray_in, float3 gray_out) {
    return float3(NeuTwo(color.r, peak.r, clip.r, gray_in.r, gray_out.r),
                  NeuTwo(color.g, peak.g, clip.g, gray_in.g, gray_out.g),
                  NeuTwo(color.b, peak.b, clip.b, gray_in.b, gray_out.b));
  }


  namespace inverse {
    // f_{i}\left(x\right)=\frac{x}{\sqrt{-xx+1}}
    float NeuTwo(float x) {
      float numerator = x;
      float denominator_squared = mad(-x, x, 1.0);
      return numerator * rsqrt(denominator_squared);
    }

    float3 PerChannel(float3 color) {
       return float3(NeuTwo(color.r),
                     NeuTwo(color.g),
                     NeuTwo(color.b));
    }

    // f_{pi}\left(x\right)=\frac{px}{\sqrt{-xx+pp}}
    float NeuTwo(float x, float peak) {
      float p = peak;

      float numerator = p * x;
      float denominator_squared = mad(-x, x, p * p);
      return numerator * rsqrt(denominator_squared);
    }

    float3 PerChannel(float3 color, float3 peak) {
       return float3(NeuTwo(color.r, peak.r),
                     NeuTwo(color.g, peak.g),
                     NeuTwo(color.b, peak.b));
    }

    // f_{ci}\left(x\right)=\frac{cpx}{\sqrt{-xx\cdot\left(cc-pp\right)+\left(cc\cdot pp\right)}}
    float NeuTwo(float x, float peak, float clip) {
      float p = peak;
      float c = clip;
      float cc = c * c;
      float pp = p * p;
      float xx = x * x;

      float numerator = c * p * x;
      float denominator_squared = mad(-xx, (cc - pp), cc * pp);

      return numerator * rsqrt(denominator_squared);
    }

    float3 PerChannel(float3 color, float3 peak, float3 clip) {
       return float3(NeuTwo(color.r, peak.r, clip.r),
                     NeuTwo(color.g, peak.g, clip.g),
                     NeuTwo(color.b, peak.b, clip.b));
    }

    // f_{gi}\left(x\right)=\frac{pgx\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot gg\cdot\left(-xx\cdot\left(cc-pp\right)+cc\cdot\left(pp-gg\right)\right)}}
    float NeuTwo(float x, float peak, float clip, float gray) {
      float p = peak;
      float g = gray;
      float c = clip;

      float cc = c * c;
      float pp = p * p;
      float gg = g * g;
      float xx = x * x;
      float cc_minus_gg = cc - gg;

      float numerator = p * g * x * cc_minus_gg;
      float denominator_squared = cc_minus_gg * gg * (mad(-xx, (cc - pp), cc * (pp - gg)));
      return numerator * rsqrt(denominator_squared);
    }

    float3 PerChannel(float3 color, float3 peak, float3 clip, float3 gray) {
       return float3(NeuTwo(color.r, peak.r, clip.r, gray.r),
                     NeuTwo(color.g, peak.g, clip.g, gray.g),
                     NeuTwo(color.b, peak.b, clip.b, gray.b));
    }

    // f_{oi}\left(x\right)=\frac{pox\left(cc-gg\right)}{\sqrt{\left(cc-gg\right)\cdot\left(-xx\cdot\left(ccoo-ppgg\right)+ccgg\cdot\left(pp-oo\right)\right)}}
    float NeuTwo(float x, float peak, float clip, float gray_in, float gray_out) {
      float p = peak;
      float g = gray_in;
      float o = gray_out;

      float cc = clip * clip;
      float pp = peak * peak;
      float gg = g * g;
      float oo = o * o;
      float xx = x * x;

      float cc_minus_gg = cc - gg;

      float numerator = p * o * x * cc_minus_gg;

      float ccoo = cc * oo;
      float ppgg = pp * gg;
      float ccgg = cc * gg;

      float denominator_squared = cc_minus_gg * mad(-xx, (ccoo - ppgg), ccgg * (pp - oo));

      return numerator * rsqrt(denominator_squared);
    }

    float3 PerChannel(float3 color, float3 peak, float3 clip, float3 gray_in, float3 gray_out) {
       return float3(NeuTwo(color.r, peak.r, clip.r, gray_in.r, gray_out.r),
                     NeuTwo(color.g, peak.g, clip.g, gray_in.g, gray_out.g),
                     NeuTwo(color.b, peak.b, clip.b, gray_in.b, gray_out.b));
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
float3 ClampByMaxChannel(float3 x, float peak) {
  float m = max(x.x, max(x.y, x.z));
  if (m > peak) x *= peak / m;
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
  #if CUSTOM_LUTBUILDER_COLORSPACE == 1
    x = BT709_To_BT2020(x);
  #endif
  x = Trade_In_NoCS(x);
  return x;
}
float3 Trade_Out(float3 x) {
  #if CUSTOM_LUTBUILDER_COLORSPACE == 1
    x = BT2020_To_BT709(x);
  #endif
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
float3 GammaCorrection_Linear(float3 x, uint cs) {
  #if CUSTOM_SDR > 0
    return x;
  #endif

  #if CUSTOM_LUTBUILDER_COLORSPACE == 0
    #ifdef CUSTOM_GAMMA_CORRECTION_MODE
      #undef CUSTOM_GAMMA_CORRECTION_MODE
      #define CUSTOM_GAMMA_CORRECTION_MODE 0
    #endif
  #endif

  #if CUSTOM_GAMMA_CORRECTION_MODE == 1
    float3 xBak = UCSTo(x, cs);
  #endif

  //gamma correction
  #if GAMMA_CORRECTION_TYPE > 0 && CUSTOM_HDTVREC709 > 0
    x = EncodeRec709(x);
    x = pow(x, DefaultGamma);
  #elif GAMMA_CORRECTION_TYPE > 0 && CUSTOM_HDTVREC709 == 0
    x = linear_to_sRGB_gamma(x, GCT_POSITIVE);
    x = pow(x, DefaultGamma);
  #elif GAMMA_CORRECTION_TYPE == 0 && CUSTOM_HDTVREC709 > 0
    x = EncodeRec709(x);
    x = gamma_sRGB_to_linear(x, GCT_POSITIVE);
  #elif GAMMA_CORRECTION_TYPE == 0 && CUSTOM_HDTVREC709 == 0
    // x = linear_to_sRGB_gamma(x, GCT_POSITIVE);
    // x = gamma_sRGB_to_linear(x, GCT_POSITIVE);
  #endif

  #if CUSTOM_GAMMA_CORRECTION_MODE == 1 //TODO: too expensive?
    x = UCSTo(x, cs);
    x = RestoreHueAndChrominanceUcs(x, xBak, 1, GS_GammaPerceptualChrominanceCorrect, 0);
    x = UCSFrom(x, cs);
    x = max(x, 0);
  #endif

  return x;
}

float3 GammaCorrection_IntermediateEncode(float3 x) {
  #if CUSTOM_LUTBUILDER_COLORSPACE == 0
    const uint gct = GCT_NONE;
  #else
    const uint gct = GCT_MIRROR;
  #endif

  //gamma encode
  #if GAMMA_CORRECTION_TYPE == 0 || CUSTOM_SDR > 0
    x = linear_to_sRGB_gamma(x, gct);
  #else
    x = linear_to_gamma(x, gct);
  #endif

  //additional rec709 encode
  #if CUSTOM_HDTVREC709 > 0
    #if CUSTOM_LUTBUILDER_COLORSPACE == 0
      x = DecodeRec709(x);
    #else
      x = sign(x) * DecodeRec709(abs(x));
    #endif

    x = linear_to_sRGB_gamma(x, gct);
  #endif

  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// //AI ahh moment
// static const float K_A  =  0.0727029592f;
// static const float K_B  =  0.598205984f;
// static const float K_C  =  0.0669102818f;
// 
// float ForwardPoly(float t)
// {
//     float p = 7.71294689f;
//     p = p * t - 19.3115273f;
//     p = p * t + 14.2751675f;
//     p = p * t - 2.49004531f;
//     p = p * t + 0.87808305f;
//     return p * t - K_C;
// }
// 
// float ForwardPolyDeriv(float t)
// {
//     float dp = 5.0f * 7.71294689f;
//     dp = dp * t - 4.0f * 19.3115273f;
//     dp = dp * t + 3.0f * 14.2751675f;
//     dp = dp * t - 2.0f * 2.49004531f;
//     dp = dp * t + 0.87808305f;
//     return dp;
// }
// 
// float InvertPoly(float y)
// {
//     static const float F0 = -K_C;
//     static const float F1 =  0.99715394f;
//     float t = saturate((y - F0) / (F1 - F0));
// 
//     [unroll]
//     for (int i = 0; i < 5; ++i)
//     {
//         float f  = ForwardPoly(t) - y;
//         float df = ForwardPolyDeriv(t);
//         t -= f / (df + 1e-10f);
//         t  = saturate(t);   // clamped — gracefully clips at forward ceiling
//     }
//     return t;
// }

float3 TonemapVanilla_Inverse(float3 y)
{
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
float3 TonemapVanilla_Internal(float3 x, float blackFloor = -1) { //https://www.desmos.com/calculator/1hmlnb6z1m
  float3 r0, r1;

  #if CUSTOM_SDRTONEMAP > 0
    // x += 0.05/203.;
    // x *= DVS9;
    x = pow(x, 1/2.35);
    x = gamma_sRGB_to_linear(x);
    
    #if CUSTOM_SDRTONEMAP == 1
      x = NeuTwo::PerChannel(x, 3.);
    #elif CUSTOM_SDRTONEMAP == 2
      // if (DVS7 > 0) x = ExponentialRollOff(x, DVS6, DVS7);
      x = Reinhard::ReinhardSimple(x, 3.);
    #endif
      // if (DVS7 > 0) x = Reinhard::ReinhardSimple(x, DVS7);
      // x = GTTonemap(x, DVS7, DVS6, 0.18, 0, 1, 0);

    // x *= 1.125;

    {
      float y = max3(x);
      float y1 = NeuTwo::NeuTwo(y, 1);
      float ratio = safeDivision(y1, y, 1);
      x *= ratio;
    }
    x = EncodeRec709(x);
    // x = pow(x, 1/2.2);

    return x;
  #endif

  if (blackFloor < 0) blackFloor = GS_BlackFloorSDRTonemap;
  r0 = x;
  r0 += 0.00872999988 * blackFloor;
  r0 = log2(r0);
  r0 = saturate(r0 * 0.0727029592 + 0.598205984);
  // const float contrast = ;
  // r0 = saturate(r0 * (0.0727029592 * contrast) + (0.598205984 - 0.17986 * (1.0 - contrast)));
  r1 = r0 * 7.71294689 + -19.3115273;
  r1 = r1 * r0 + 14.2751675;
  r1 = r1 * r0 + -2.49004531;
  r1 = r1 * r0 + 0.87808305;
  r0 = r1 * r0 + -0.0669102818;
  r0 = saturate(r0);

  return r0.xyz;
}
void TonemapVanilla() {
  //SDR Tonemap
  float3 colorTBefore = TS.colorT;
  TS.colorT = TonemapVanilla_Internal(TS.colorT);

  #if CUSTOM_SDR > 0
    return;
  #endif

  //SDR perchannel luminance emulate colorU
  #if CUSTOM_PERCHANNELLUMAEMULATE > 0
  {
    const float p = GS_PerChannelLuminanceReductionEmulatePeak;
    const float makeup = GS_PerChannelLuminanceReductionEmulateMakeup;
    const float strength = GS_PerChannelLuminanceReductionEmulateStrength;
    const uint cs = CS_BT709;

    float3 colorUBak = TS.colorU;
    float colorUBakY = GetLuminance(colorUBak, cs);

    //down perchannel
    TS.colorU = NeuTwo::PerChannel(TS.colorU, p);
    TS.colorU = min(TS.colorU, p); //clip

    //up luminance
    float y = GetLuminance(TS.colorU, cs);
    y = min(y, p * 0.999); //clip
    float y1 = NeuTwo::inverse::NeuTwo(y, p);
    y1 *= makeup; //makeup

    //ratio
    float ratio = y1 / colorUBakY;
    ratio = lerp(1, ratio, saturate(/* sqrt */(colorUBakY))); //high pass
    ratio = lerp(1, ratio, strength); //global

    //apply
    colorUBak *= ratio;
    TS.colorU = colorUBak;
  }
  #endif

  // Per Channel Correct
  #if CUSTOM_PCC > 0 && CUSTOM_SDRTONEMAP == 0
    // pcc setup
    TS.colorT = DecodeRec709(TS.colorT);
    float colorTMax = max(TS.colorT.x, max(TS.colorT.y, TS.colorT.z));
    float colorTY = GetLuminance(TS.colorT, CS_BT709);

    // 2nd perchannel
    float3 colorTLessBlow = colorTBefore;
    {
      // float3 x = colorTBefore;
      // x = pow(x, 1/2.35);
      // x = gamma_sRGB_to_linear(x);
      // x = NeuTwo::PerChannel(x, GS_PCCPeak);
      // colorTLessBlow = x;

      // https://www.desmos.com/calculator/lk9bpn4wkz
      float3 l = DecodeRec709(TonemapVanilla_Internal(colorTBefore, 1)); //TODO: no other way? we have to recalc w/o black floor lower, else piecewise will not merge
      float3 u = colorTLessBlow + 0.019988025;
      colorTLessBlow = colorTLessBlow < 0.1103529061 ? l : u;
      #if CUSTOM_PCC_ROLLOFF == 0
        colorTLessBlow = Reinhard::ReinhardPiecewise(colorTLessBlow, HDR_PEAK, 0.1103529061);
      #elif CUSTOM_PCC_ROLLOFF == 1
         colorTLessBlow = NeuTwo::PerChannel(colorTLessBlow, HDR_PEAK);
      #endif
    }
    float colorTLessBlowY = GetLuminance(colorTLessBlow, CS_BT709);

    // ucs
    TS.colorT = UCSTo(TS.colorT, CS_BT709);
    colorTLessBlow = UCSTo(colorTLessBlow, CS_BT709);

    // do
    TS.colorT = RestoreHueAndChrominanceUcs(TS.colorT, colorTLessBlow, GS_PCCHue, GS_PCCChrominance, 1.f);
    TS.colorT = UCSFrom(TS.colorT, CS_BT709); //back to linear
    TS.colorT = max(0, TS.colorT); //clamp BT709

    // clamp
    float colorTMaxAfter = max(TS.colorT.x, max(TS.colorT.y, TS.colorT.z));
    // colorTMax = lerp(colorTMax, colorTMaxAfter, 0.8f); //reduce max channel feeling
    colorTMax = min(1, colorTMax);
    TS.colorT *= colorTMaxAfter > 0 ? colorTMax / colorTMaxAfter : 1; //prevent clip

    // reencode
    TS.colorT = EncodeRec709(TS.colorT);
  #endif

  //save this result as baseline
  TS.colorTRaw = TS.colorT;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float LUTNeutralize_LumaStrength(float3 lutUcs, float3 neutralUcs, float strengthLuma) {
  #if CUSTOM_LUTBUILDER_NEUTRAL_LUMA == 0
    //high pass
    float sL = saturate(lutUcs.x);
    sL = smoothstep(0.36/* GS_LUTBuilderNeutralLumaHPStart */, 1, sL);
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
  const float strengthChromi = GS_LUTBuilderNeutralChrominance;
  const float strengthHue = GS_LUTBuilderNeutralHue;
  const float strengthLuma = GS_LUTBuilderNeutralLuma;

  //get neutral
  float3 neutral = float3(lutCoord / (lutSize - 1));
  if(strengthChromi == 1 && strengthHue == 1 && strengthLuma == 1) return neutral;

  //forced neutral   
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
    lutUcs = RestoreHueAndChrominanceUcs(lutUcs, neutralUcs, strengthHue, strengthChromi, 1 - strengthChromi);
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
    
  //ratio setup
  float ratio = 1.f;

  //y setups
  float y_untonemapped = GetLuminance(color_untonemapped, CS_BT709);

  // float y_tonemapped = NeuTwo::NeuTwo(y_untonemapped, 1);
  #if CUSTOM_SDRTONEMAP > 0
    float3 x = color_untonemapped;
    x *= DVS9;
    // x = pow(x, 1/2.35);
    // x = gamma_sRGB_to_linear(x);

    #if CUSTOM_SDRTONEMAP == 1
      x = NeuTwo::PerChannel(x, 3.);
    #elif CUSTOM_SDRTONEMAP == 2
      // if (DVS7 > 0) x = ExponentialRollOff(x, DVS6, DVS7);
      x = Reinhard::ReinhardSimple(x, 3.);
    #endif
      // if (DVS7 > 0) x = Reinhard::ReinhardSimple(x, DVS7);
      // x = GTTonemap(x, DVS7, DVS6, 0.18, 0, 1, 0);

    // x *= 1.125;

    {
      float y = max3(x);
      float y1 = NeuTwo::NeuTwo(y, 1);
      float ratio = safeDivision(y1, y, 1);
      x *= ratio;
    }
    // x = EncodeRec709(x);
    float y_tonemapped = GetLuminance(x, CS_BT709); 
  #else
    float y_tonemapped = HermiteSpline::HermiteSplineLuminanceRolloff(y_untonemapped, 1, 200);
  #endif

  float y_tonemapped_graded = GetLuminance(color_tonemapped_graded, lutbuilder_colorspace);

  //luma upgrade
  if (y_untonemapped < y_tonemapped) {
    ratio = y_untonemapped / y_tonemapped;
  } else {
    float y_delta = y_untonemapped - y_tonemapped;
    y_delta = max(0, y_delta);
    const float y_new = y_tonemapped_graded + y_delta;
    const bool y_valid = (y_tonemapped_graded > 0);
    ratio = y_valid ? (y_new / y_tonemapped_graded) : 0;
  }

  //apply ratio
  float3 color_scaled = color_tonemapped_graded * ratio;
  // color_scaled = max(0, color_scaled); //clean

  //hue correct //TODO: remove, meh
  // float3 color_scaled_ucs = UCSTo(color_scaled, CS_BT2020);
  // float3 color_tonemapped_graded_ucs = UCSTo(color_tonemapped_graded, CS_BT2020);
  // color_scaled_ucs = RestoreHueAndChrominanceUcs(color_scaled_ucs, color_tonemapped_graded_ucs, 1.f, 0.f, 1.f);
  // color_scaled = UCSFrom(color_scaled_ucs, CS_BT2020);

  //debug output //TODO: CUSTOM_LUTBUILDER_COLORSPACE
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

float3 LUT_Sample(float3 x, Texture3D lut, SamplerState lut_s) {
  x = x * 0.96875 + 0.015625; //x * (31/32) + (0.5/32)
  x = lut.Sample(lut_s, x).xyz;
  return x;
}

void LUT_WarmMinimum(Texture3D lut) {
  TS.LutMinimumOutput = lut.Load(int4(0, 0, 0, 0)).xyz;
}

static float BloomAndLensHDRHeadRoom = 0.6f;
void LUT(Texture3D lut, SamplerState lut_s) {  
  #if CUSTOM_SDR == 0
    TS.colorT = ClampByMaxChannel(TS.colorT, 1.f); 
  #endif

  //sample lut
  float3 colorN = TS.colorT;
  TS.colorT = LUT_Sample(TS.colorT, lut, lut_s);
  #if CUSTOM_SDR == 0
    TS.colorTRawLUTed = LUT_Sample(TS.colorTRaw * BloomAndLensHDRHeadRoom, lut, lut_s);
  #endif

  //new minimum
  #if CUSTOM_BLACKFLOOR_LUT > 0
  {
    const float minStrength = GS_BlackFloorLUT;
    #if CUSTOM_BLACKFLOOR_LUT == 1
      float y = GetLuminance(TS.colorT, lutbuilder_colorspace);
      if (y > 0) {
        float3 minimum3 = TS.LutMinimumOutput;
        float minimum = max(minimum3.x, max(minimum3.y, minimum3.z));
        float y1 = InverseLerp(minimum, 1, y);
        y1 = lerp(y, y1, minStrength);
        TS.colorT *= y1 / y;
        TS.colorT = max(0, TS.colorT);
      }
    #elif CUSTOM_BLACKFLOOR_LUT == 2
      float3 minimum3 = TS.LutMinimumOutput;
      float3 colorTnew = float3(InverseLerp(minimum3.x, 1, TS.colorT.x),
                                InverseLerp(minimum3.y, 1, TS.colorT.y),
                                InverseLerp(minimum3.z, 1, TS.colorT.z));
      TS.colorT = lerp(TS.colorT, colorTnew, minStrength);
    #endif
    TS.colorT = max(0, TS.colorT);
  }
  #endif

  #if CUSTOM_SDR > 0
    return;
  #endif

  //mid gray change sampling
  const float mid = 0.5; //approx 0.18 in rec709
  TS.colorU *= (GetLuminance(lut.Sample(lut_s, mid).xyz, lutbuilder_colorspace) / mid) * (0.5f / 0.18f);

  //lut resolve 0 (UpgradeToneMap crutch ahh ahh)
  TS.colorU = UpgradeToneMapBo3(TS.colorU, TS.colorT, colorN);
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

void Bloom_Comp_SDR(in Texture2D<float4> bloomTex, in SamplerState bloomTex_s, in float2 uv) {
  float3 colorTBefore = TS.colorT;

  // original SDR bloom composite, but named
  float3 bloomColor = bloomTex.Sample(bloomTex_s, uv.xy).xyz;
  float3 bloomColorScaledUnclamped = bloomColor;
  float3 bloomColorScaled = saturate(bloomColorScaledUnclamped);

  //normal (Bloom MUST BE COMP this way, else vanilla hues are lost)
  float3 colorTForBloom = TS.colorT;
  #if CUSTOM_SDR == 0
    colorTForBloom *= BloomAndLensHDRHeadRoom; //give headroom
  #endif
  float3 bloomAdded = bloomColorScaled + colorTForBloom;
  TS.colorT = -colorTForBloom * bloomColorScaled + bloomAdded;
  // TS.colorT = max(0, TS.colorT); //clean
  TS.colorTWithBloom = TS.colorT;

  #if CUSTOM_SDR > 0
    return;
  #endif

  //swap back to baseline
  TS.colorT = TS.colorTRaw;

  // //colorU parallel mimic
  // #if CUSTOM_SDR == 0 && CUSTOM_BLOOM_COMP == 0 
  //   Bloom_Comp_ColorU(TS.colorU, colorTBefore, TS.colorT, bloomColorScaledUnclamped);
  // #endif
}
void Bloom_Comp_HDR(Texture2D<float4> bloomTex, Texture3D<float4> lutTex, SamplerState samp, float2 uv) {
  //SDR
  #if CUSTOM_SDR > 0
     return;
  #endif

  //separate
  TS.colorTWithBloom = LUT_Sample(TS.colorTWithBloom, lutTex, samp);
  float3 colorOnlyBloom = TS.colorTWithBloom - TS.colorTRawLUTed;
  colorOnlyBloom = max(0, colorOnlyBloom);
  {
    float y = GetLuminance(colorOnlyBloom, lutbuilder_colorspace);
    // float y = max(colorOnlyBloom.x, max(colorOnlyBloom.y, colorOnlyBloom.z));
    if (y <= 0) return;

    float y1 = y;
    y1 /= BloomAndLensHDRHeadRoom;
    y1 = min(y1, 2.0f);
    float c = 2.01f;
    y1 = NeuTwo::inverse::NeuTwo(y1, c); //helps restores highlights.
    y1 *= BloomAndLensHDRHeadRoom;
    #if CUSTOM_BLOOM_TONEMAP == 0
    #elif CUSTOM_BLOOM_TONEMAP == 1
      y1 *= 0.725f;
    #endif
    y1 *= 0.926f;
    y1 *= GS_Bloom;
    colorOnlyBloom *= y1 / y;
  }
  TS.colorU += colorOnlyBloom;

//   //HDR
//   float3 bloomColor = bloomTex.Sample(samp, uv.xy).xyz * GS_Bloom;
//   // bloomColor = NeuTwo::PerChannel(bloomColor, 1.0f); //per-channel blowout
//   {
//     float y = GetLuminance(bloomColor, CS_BT709);
//     float y1 = RenoDX_Shadows(y, 1.4f, 0.18f);
//     float ratio = y1 / y;
//     bloomColor *= ratio;
//   }
//   bloomColor = LUT_Sample(bloomColor, lutTex, samp);
// 
//   //min
//   float3 minimum = TS.LutMinimumOutput;
//   bloomColor = float3(InverseLerp(minimum.x, 1, bloomColor.x),
//                       InverseLerp(minimum.y, 1, bloomColor.y),
//                       InverseLerp(minimum.z, 1, bloomColor.z));
// 
//   //boost
//   float y = GetLuminance(bloomColor, lutbuilder_colorspace);
//   if (y > 0) {
//     float y1 = NeuTwo::inverse::NeuTwo(y, 1.0f);
//     y1 = DecodeRec709(y1) * 8;
//     y1 = RenoDX_Shadows(y, 0.6f, 0.46f);
//     float ratio = y1 / y;
//     bloomColor *= ratio;
//   }
//   //screen
//   const float m = 100.f;
//   bloomColor = clamp(bloomColor, 0, m);
//   TS.colorU = bloomColor + TS.colorU * max(0, 1.0 - bloomColor / m);
// 
//   // // bloomColor = DecodeRec709(bloomColor) * 12;
//   // bloomColor *= 2.2;
//   // TS.colorU += bloomColor;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void LensFlare_Comp_SDR(Texture2D<float4> flareTex, SamplerState flareTex_s, float2 uv) {
  const float mult = (1 / 32768.f);
  float3 lens = flareTex.Sample(flareTex_s, uv).xyz * mult;


  float3 colorT1 = TS.colorT; 
  #if CUSTOM_SDR == 0
    colorT1 *= BloomAndLensHDRHeadRoom;
  #endif
  colorT1 += lens;
  TS.colorT = saturate(colorT1);
  TS.colorTWithLens = TS.colorT;

  //SDR
  #if CUSTOM_SDR > 0
    return;
  #endif

  //swap back to baseline
  TS.colorT = TS.colorTRaw;

  // //colorU parallel mimic
  // #if CUSTOM_SDR == 0 && CUSTOM_LENSFLARE_COMP == 0
  //   lens = TonemapVanilla_Inverse(lens);
  //   // lens = EncodeRec709(lens) * 2.2;
  //   lens *= 2.f;
  //   TS.colorU += lens;
  //   // const float m = 100.f;
  //   // lens = clamp(lens, 0, m);
  //   // lens = EncodeRec709(lens);
  //   // colorU = lens + colorU * max(0, 1.0 - lens / m);
  // #endif
}
void LensFlare_Comp_HDR(Texture2D<float4> flareTex, Texture3D<float4> lutTex, SamplerState samp, float2 uv) {
  //SDR
  #if CUSTOM_SDR > 0
     return;
  #endif

  //separate
  TS.colorTWithLens = LUT_Sample(TS.colorTWithLens, lutTex, samp);
  float3 colorOnlyLens = TS.colorTWithLens - TS.colorTRawLUTed;
  colorOnlyLens = max(0, colorOnlyLens);
//   {
//     float y = GetLuminance(colorOnlyLens, lutbuilder_colorspace);
//     if (y <= 0) return;
// 
//     float y1 = NeuTwo::inverse::NeuTwo(y, 1.0f);
//     y1 *= 2.f; //makeup
//     colorOnlyLens *= y1 / y;
//   }
  colorOnlyLens *= /* (1/BloomAndLensHDRHeadRoom) * */ 0.67f * GS_LensFlare;
  TS.colorU += colorOnlyLens;

  // HDR
  float3 lens = flareTex.Sample(samp, uv).xyz * (1.f / 32768.f) * GS_LensFlare;
  const float p = 1.f;
  {
    float y = GetLuminance(lens, lutbuilder_colorspace);
    if (y <= 0) return;

    // perchannel blowout
    float3 lensB = lens;
    lensB *= 4.f;
    lensB = ExponentialRollOff(lensB, 0/* .18f */, p);
    // lensB = NeuTwo::PerChannel(lensB, p);
    lensB = min(lensB, p);
    // lensB *= y / GetLuminance(lensB, lutbuilder_colorspace);
    lens = lensB;
  }
  lens = LUT_Sample(lens, lutTex, samp);

  //min
  float3 minimum = TS.LutMinimumOutput;
  lens = float3(InverseLerp(minimum.x, 1, lens.x),
                InverseLerp(minimum.y, 1, lens.y),
                InverseLerp(minimum.z, 1, lens.z));
  lens = max(0, lens);

  //boost
  float y = GetLuminance(lens, lutbuilder_colorspace);
  if (y > 0) {
    float y1 = y;
    y1 = min(y1, p * 0.99f); //clamp from floating point explode
    y1 = NeuTwo::inverse::NeuTwo(y1, p);

    y1 *= 1.46f;
    y1 = pow(y1, 1.6f);
    y1 *= 0.46f * 0.67f;
    y1 *= GS_LensFlare;

    float ratio = y1 / y;
    lens *= ratio;
  }

  TS.colorU += lens;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float TonemapGetLumaForAA(float3 color) {
  float o1;
  o1 = GetLuminance(color, lutbuilder_colorspace);
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
      float l = GetLuminance(colorU, lutbuilder_colorspace); //luma
    #elif CUSTOM_TONEMAP_SCALING == 1
      float l = max(colorU.x, max(colorU.y, colorU.z)); //max channel
    #endif
    if (l > 0) { //safe
      float lT;
      #if CUSTOM_TONEMAP == 1
        lT = Reinhard::ReinhardPiecewiseExtended(l, HDR_MAXEXPECTED, HDR_PEAK, HDR_SHOULDERSTART);
      #elif CUSTOM_TONEMAP == 2
        // float me = DVS2 == 0 ? HDR_MAXEXPECTED : 100000 / GamePaperWhiteNits;
        lT = HermiteSpline::HermiteSplineLuminanceRolloff(l, HDR_PEAK, HDR_MAXEXPECTED);
      #endif
      colorU *= lT / l;
    }

    //clamp
    #if CUSTOM_TONEMAP_SCALING != 1 /* skip if max channel. */
      #if CUSTOM_TONEMAP_CLAMP == 1
        colorU = min(colorU, HDR_PEAK);
      #elif CUSTOM_TONEMAP_CLAMP == 2
        colorU = ClampByMaxChannel(colorU, HDR_PEAK);
      #endif
    #endif
  #endif

  //tradeoff intermediate scaling
  colorU = Trade_In_NoCS(colorU);

  return colorU;
}
void TonemapShader_Out(inout float3 o0) {
  uint cs = lutbuilder_colorspace;
  
  //case: SDR
  #if CUSTOM_SDR > 0
    o0 = TS.colorT; //use colorT BT709
    #if CUSTOM_SR == 0
      TS.colorT *= 32768.f; //scale up for normal AA luma calculation
    #endif
  #else
    o0 = TS.colorU; //use colorU BT2020
  #endif

  //SDR (returns)
  #if CUSTOM_SDR > 0
    return;
  #endif

  //exposure & gamma correction
  #if GAMMA_CORRECTION_TYPE > 0
    o0 *= GS_GammaInfluence;
    o0 = GammaCorrection_Linear(o0, cs);
    o0 *= GS_Exposure / GS_GammaInfluence;
  #else
    o0 = GammaCorrection_Linear(o0, cs);
    o0 *= GS_Exposure;
  #endif

  //color grade
  #if CUSTOM_COLORGRADE > 0
    o0 = RenoDX_ColorGrade(
      o0,
      GS_CGContrast, GS_CGContrastMidGray / GamePaperWhiteNits,
      GS_CGHighlightsStrength, GS_CGHighlightsMidGray / GamePaperWhiteNits,
      GS_CGShadowsStrength, GS_CGShadowsMidGray / GamePaperWhiteNits,
      GS_CGSaturation,
      cs
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
float TonemapShader_Alpha(float o) {
  // o = 1-o; //invert
  // o *= o; //gamma
  return o;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "./Includes/RCAS.hlsl"
float3 FinalShader_Resolve(Texture2D<float4> tex, SamplerState tex_s, float2 uv) {
  float3 o;
  o = tex.Sample(tex_s, uv.xy).xyz; //scale back from 15-bit FP used for bloom/lens composite

  //scaling
  #if CUSTOM_RCAS == 1
    #if CUSTOM_SR == 0
      o = RCAS_BO3(o, tex, tex_s, uv, GS_RCAS, HDR_PEAK, true);
    #else
      o = RCAS_BO3(o, tex, tex_s, uv, GS_RCAS, HDR_PEAK, GS_IsDLAA > 0);
    #endif
  #else
    #if CUSTOM_SR == 0
      o = Trade_Out_NoCS(o);
    #else
      if (GS_IsDLAA > 0) o = Trade_Out_NoCS(o);
    #endif
  #endif

  //HDR clamp BT2020, SDR clamp BT709
  o = max(0, o);

  //clamp peak
  #if CUSTOM_FINAL_CLAMP == 1
    o = min(o, HDR_PEAK);
  #elif CUSTOM_FINAL_CLAMP == 2
    o = ClampByMaxChannel(o, HDR_PEAK);
  #endif

  //skip rest for SDR
  #if CUSTOM_SDR > 0
    return o;
  #endif

  //intermediate colorspace encode
  #if CUSTOM_LUTBUILDER_COLORSPACE == 0
    //noop
  #else
    o = BT2020_To_BT709(o);
  #endif

  //intermediate scaling
  o *= HDR_INTSCALING;

  //intermediate gamma encode
  o = GammaCorrection_IntermediateEncode(o);

  return o;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////