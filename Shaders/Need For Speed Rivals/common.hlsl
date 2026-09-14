#include "./Includes/Common.hlsl"

/////////////////////////////////////////////////////////////////////////////////////////
float3 ClampByMaxChannel(float3 x, float p) {
  float m = max(max(x.x, x.y), x.z);
  if (m > p) x *= p / m;
  return x;
}
bool FloatEqual(float a, float b, float epsilon) {
  return abs(a - b) < epsilon;
}
/////////////////////////////////////////////////////////////////////////////////////////
float3 sRGB_Encode(float3 x) { return linear_to_sRGB_gamma(x); }
float  sRGB_Encode(float  x) { return linear_to_sRGB_gamma1(x); }
float3 sRGB_Decode(float3 x) { return gamma_sRGB_to_linear(x); }
float  sRGB_Decode(float  x) { return gamma_sRGB_to_linear1(x); }
/////////////////////////////////////////////////////////////////////////////////////////
float Neutwo(float x) {
  float numerator = x;
  float denominator_squared = mad(x, x, 1.0);
  return numerator * rsqrt(denominator_squared);
}

float Neutwo(float x, float peak) {
  float p = peak;

  float numerator = p * x;
  float denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}
float3 Neutwo(float3 x, float peak) {
  float p = peak;

  float3 numerator = p * x;
  float3 denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}

float3 Neupow(float3 x, float peak, float power) {
  float3 p_over_x_pow_a = exp2(power * (log2(peak) - log2(x)));
  return peak * rcp(exp2(log2(1.0f + p_over_x_pow_a) * rcp(power)));
}
float Neupow(float x, float peak, float power) {
  //\frac{xp}{\left(x^{a}+p^{a}\right)^{\frac{1}{a}}}
  // return (x * peak) / pow(pow(x, power) + pow(peak, power), rcp(power));
  
  float p_over_x_pow_a = exp2(power * (log2(peak) - log2(x)));
  return peak * rcp(exp2(log2(1.0f + p_over_x_pow_a) * rcp(power)));
}
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
float3 Neutwo(float3 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float3 xx = x * x;

  float3 numerator = c * p * x;
  float3 denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}

float Neupow(float x, float peak, float power, float clip) {
  // return (clip * peak * x) / pow(pow(x, power) * (pow(clip, power) - pow(peak, power)) + (pow(clip, power) * pow(peak, power)), rcp(power));

  float log2_x = log2(x);
  float x_pow_a = exp2(power * log2_x);
  float k = exp2(-power * log2(peak)) - exp2(-power * log2(clip));
  return exp2(log2_x - log2(k * x_pow_a + 1.0f) * rcp(power));
}
float3 Neupow(float3 x, float peak, float power, float clip) {
  // return (clip * peak * x) / pow(pow(x, power) * (pow(clip, power) - pow(peak, power)) + (pow(clip, power) * pow(peak, power)), rcp(power));

  float3 log2_x = log2(x);
  float3 x_pow_a = exp2(power * log2_x);
  float3 k = exp2(-power * log2(peak)) - exp2(-power * log2(clip));
  return exp2(log2_x - log2(k * x_pow_a + 1.0f) * rcp(power));
}

/////////////////////////////////////////////////////////////////////////////////////////
/// Piecewise linear + exponential compression to a target value starting from a specified number.
/// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
#define EXPONENTIALROLLOFF_GENERATOR(T)                                                 \
  T ExponentialRollOff(T input, float rolloff_start = 0.20f, float output_max = 1.0f) { \
    T rolloff_size = output_max - rolloff_start;                                        \
    T overage = -max((T)0, input - rolloff_start);                                      \
    T rolloff_value = (T)1.0f - exp(overage / rolloff_size);                            \
    T new_overage = mad(rolloff_size, rolloff_value, overage);                          \
    return input + new_overage;                                                         \
  }

/// Piecewise linear + exponential compression to a target value starting from a specified number.
/// https://www.ea.com/frostbite/news/high-dynamic-range-color-grading-and-display-in-frostbite
#define EXPONENTIALROLLOFF_CLIP_GENERATOR(T)                                         \
  T ExponentialRollOff(T input, float rolloff_start, float output_max, float clip) { \
    T rolloff_size = output_max - rolloff_start;                                     \
    T overage = -max((T)0, input - rolloff_start);                                   \
    T clip_size = rolloff_start - clip;                                              \
    T rolloff_value = (T)1.0f - exp(overage / rolloff_size);                         \
    T clip_value = (T)1.0f - exp(clip_size / rolloff_size);                          \
    T new_overage = mad(rolloff_size, rolloff_value / clip_value, overage);          \
    return input + new_overage;                                                      \
  }

EXPONENTIALROLLOFF_GENERATOR(float)
EXPONENTIALROLLOFF_GENERATOR(float3)
EXPONENTIALROLLOFF_CLIP_GENERATOR(float)
EXPONENTIALROLLOFF_CLIP_GENERATOR(float3)
#undef EXPONENTIALROLLOFF_GENERATOR
#undef EXPONENTIALROLLOFF_CLIP_GENERATOR
/////////////////////////////////////////////////////////////////////////////////////////
// #include "../Includes/JzAzBz.hlsl"
#include "./Includes/ictcp_portable.hlsl"
float3 UCS_Encode(float3 x) {
  // return JzAzBz::rgbToJzazbz(x, CS_BT709);
  return renodx::color::ictcp::Encode(x, CS_BT709);
}
float3 UCS_Decode(float3 x) {
  // return JzAzBz::jzazbzToRgb(x, CS_BT709);
  return renodx::color::ictcp::Decode(x, CS_BT709);
}
// From Luma
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
    targetChrominanceRatio = clamp(targetChrominanceRatio, minChromaRatio, 99999999);
    targetUcs.yz *= lerp(1.0, targetChrominanceRatio, chrominanceStrength);
  }

  return targetUcs;
}

float3 RestoreHueAndChrominanceUcs(float3 targetUcs, float3 sourceUcs, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  return RestoreHueAndChrominanceUcsInternal(targetUcs, sourceUcs, length(targetUcs.yz), hueStrength, chrominanceStrength, minChromaRatio);
}
/////////////////////////////////////////////////////////////////////////////////////////
// #define COMMON_TONEMAP 1
#ifdef COMMON_TONEMAP
#ifndef LUT_SIZE
#define LUT_SIZE 32u
#endif
#ifndef LUT_3D
#define LUT_3D 1
#endif
#include "../Includes/ColorGradingLUT.hlsl"

struct TonemapInfo {
  float3 x; //main color
  float3 sdr; //main color sdr
  float p; //gamma corrected peak
};
static TonemapInfo tmi = { float3(0,0,0), float3(0,0,0), 0.0f };

void T_Color(float3 x) {
  tmi.x = max(0, x);
  tmi.x = x;

  // Peak (Corrected Gamma 2.0)
  tmi.p = HDR_PEAK;
  tmi.p *= tmi.p;
  tmi.p = RenderIntermediatePass_Encode(tmi.p);
}

void T_Vignette(float2 v2) {
  #if TONEMAP_VIGNETTE == 0
    return;
  #endif

  float2 r0;

  r0.xy = v2.xy * vignetteScaleAndOffset.xy + vignetteScaleAndOffset.zw;
  r0.x = max(0, 1 - dot(r0.xy, r0.xy));
  r0.x = pow(r0.x, vignetteParams.z);

  tmi.x *= r0.x;
}

// Vanilla is Reinhard 1.6231 peak
void T_Rolloff() {
  float3 x = tmi.x;

  // x = Neupow(x, tmi.p, GS.WhiteClip); //BRUHHH Doing this will later lose so much luminance for single channel color.
  // x = Neutwo(x, tmi.p * GS.WhiteClip);
  // x = min(x, tmi.p);

//   // HDR guided perchannel blowout and hues
//   if (DVS1)
//   {
//     // float3 hdr = x / (x / (tmi.p * 1.6231) + 1);
//     // hdr = min(hdr, tmi.p);
//     float3 hdr = Neutwo(x, tmi.p * 1.6231);
// 
//     // float3 sdr = tmi.x;
//     // sdr = sdr / (sdr / 1.6231 + 1);
//     // sdr = Neupow(sdr, 1, 1.6231, 16);
// 
//     hdr = UCS_Encode(hdr);
//     // sdr = UCS_Encode(sdr);
//     x = UCS_Encode(x);
// 
//     float s = 0.6 * (1 - GS.HighlightSat) + 0.6;
//     s = saturate(s);
//     x = RestoreHueAndChrominanceUcs(x, hdr, s, s * 0.2, 0);
//     // x = RestoreHueAndChrominanceUcs(x, sdr, 0.3125, 0, 0);
//     // x = RestoreHueAndChrominanceUcs(x, sdr, 0, DVS2, 0);
// 
//     // float hdrC = GetChrominance(hdr);
//     // float sdrC = GetChrominance(sdr);
//     // float xC = GetChrominance(x);
// 
//     // x = SetChrominance(x, safeDivision(lerp(xC, hdrC, DVS2), xC, 1));
// 
//     // x = CorrectPerChannelTonemapHiglightsDesaturation(x, 30000 / 203.f, DVS2, CS_BT709);
// 
//     x = UCS_Decode(x);
//     x = max(0, x);
//     // x = min(tmi.p, x);
//   }

  // // SDR perchannel to run parallel
  // tmi.sdr = tmi.x * 0.8; //reduce exposure
  // tmi.sdr = tmi.sdr / (tmi.sdr / 1.6231 + 1);

  tmi.x = x;
}

void T_Gamma() {
  // tmi.x = sqrt(tmi.x);
  // tmi.sdr = sqrt(tmi.sdr);
}

void T_LUT() { //and Gamma
  #if TONEMAP_LUT == 0
    tmi.x = sqrt(tmi.x);
    return;
  #endif

  // tmi.x *= tmi.x; //Gamma Decode
  float3 x = tmi.x;

  // Setup
  float3 colorU = x;
  float colorUY = /* max3(colorU) */ GetLuminance(colorU);
  // float colorNY = colorUY / (colorUY / 0.96 + 1);
  // float colorNY = GetLuminance(colorU / (colorU / DVS1/* 0.96 */ + 1));
  float colorNY = Neupow(colorUY, 0.7, 0.77);

  x *= safeDivision(colorNY, colorUY, 0);
  // x = ClampByMaxChannel(x, 1); //TODO: how does this cause error?!?!?

  // // HDR blowout
  // {
  //   float3 hdr = colorU;
  //   hdr = hdr / (hdr / tmi.p + 1);
  //   hdr *= safeDivision(colorNY, GetLuminance(hdr), 0);
  //   hdr = UCS_Encode(hdr);
  //   x = UCS_Encode(x);
  //   x = RestoreHueAndChrominanceUcs(x, hdr, DVS1, DVS2, 0);
  //   x = UCS_Decode(x);
  //   x = max(0, x);
  // }

  float3 colorN = x;

  // HDR LUT
  x = sqrt(x); // Gamma Encode
    // x = SampleLUT(colorGradingTexture, colorGradingTextureSampler_s, x, 32, true); //tehtrahedral to reduce rgba8 banding
      x = x * 0.96875 + 0.015625; //32x
      x = colorGradingTexture.Sample(colorGradingTextureSampler_s, x).xyz;
  x *= x; //Gamma Decode

  // // SDR LUT guided perchannel blowout and hues
  // if (DVS1)
  // {
  //   float3 sdr = tmi.sdr;
  //   sdr = sqrt(sdr); // Gamma Encode
  //     sdr = sdr * 0.96875 + 0.015625; //32x
  //     sdr = colorGradingTexture.Sample(colorGradingTextureSampler_s, sdr).xyz;
  //   sdr *= sdr; //Gamma Decode
  //   
  //   x = UCS_Encode(x);
  //   sdr = UCS_Encode(sdr);
  //   float hs = 0.76 * (1-GS.HighlightSat);
  //   x = RestoreHueAndChrominanceUcs(x, sdr, 0.6, hs, 0);
  //   x.yz *= 1.04;
  //   x = UCS_Decode(x);
  // }

  // 2 SDR LUT guided perchannel blowout and hues
  // if (DVS2)
  {
    // // SDR perchannel to run parallel
    // tmi.sdr = tmi.x * 0.8; //reduce exposure
    // tmi.sdr = tmi.sdr / (tmi.sdr / 1.6231 + 1);

    float3 sdr0 = tmi.x;
    sdr0 *= 0.78;
    sdr0 = sdr0 / (sdr0 / 1.6231 + 1);
    sdr0 = sqrt(sdr0); // Gamma Encode
      sdr0 = sdr0 * 0.96875 + 0.015625; //32x
      sdr0 = colorGradingTexture.Sample(colorGradingTextureSampler_s, sdr0).xyz;
    sdr0 *= sdr0; //Gamma Decode
    sdr0 = UCS_Encode(sdr0);

    float3 sdr1 = tmi.x;
    { //perchannel
      sdr1 = sdr1 / (sdr1 / (tmi.p /* * 1.6231 */) + 1);
      // sdr1 = Neupow(sdr1, tmi.p, 1);
    }
    { //y
      float y = GetLuminance(sdr1);
      float y1 = Neupow(y, 0.55, tmi.p /* * 1.6231 */, 1);
      y1 = min(y1, 1);
      sdr1 *= safeDivision(y1, y, 0);
    }
    sdr1 = sqrt(sdr1); // Gamma Encode
      // sdr1 = sdr1 * 0.96875 + 0.015625; //32x
      // sdr1 = colorGradingTexture.Sample(colorGradingTextureSampler_s, sdr1).xyz;
      sdr1 = SampleLUT(colorGradingTexture, colorGradingTextureSampler_s, sdr1, 32, true); //tehtrahedral to reduce rgba8 banding
    sdr1 *= sdr1; //Gamma Decode
    sdr1 = UCS_Encode(sdr1);

//     float3 sdr2 = tmi.x;
//     sdr2 *= 0.8;
//     sdr2 = Neutwo(sdr2, tmi.p * 1.6231);
//     sdr2 = sqrt(sdr2); // Gamma Encode
//       sdr2 = sdr2 * 0.96875 + 0.015625; //32x
//       sdr2 = colorGradingTexture.Sample(colorGradingTextureSampler_s, sdr2).xyz;
//     sdr2 *= sdr2; //Gamma Decode
//     sdr2 = UCS_Encode(sdr2);
    
    x = UCS_Encode(x);
    float hs = 0.6 * (1-GS.HighlightSat) + 0.6;
    hs = saturate(hs);
    x = RestoreHueAndChrominanceUcs(x, sdr0, hs * 0.5, hs * 0.1, 0.5);
    x = RestoreHueAndChrominanceUcs(x, sdr1, hs, hs, 0.0);
    // x = RestoreHueAndChrominanceUcs(x, sdr2, 0.125, hs, 0);
    // x.yz *= 1.02;
    x = UCS_Decode(x);
  }

  // x = CorrectPerChannelTonemapHiglightsDesaturation(x, 1, 1.125, CS_BT709);
  // x = max(0, x);

  x = RestorePostProcess(colorU, colorN, x, 0, true);
  x = max(0, x);
  // x = min(tmi.p, x);

  // // float hs1 = 0.4 * (1-GS.HighlightSat) + 0.4;
  // x = CorrectPerChannelTonemapHiglightsDesaturation(x, tmi.p, 0.9, CS_BT709);
  // x = max(0, x);

  // float black = colorGradingTexture.Load(float4(0, 0, 0, 0));
  // x = black;

  // x = CorrectPerChannelTonemapHiglightsDesaturation(x, 800 / 203.f, DVS2, CS_BT709);
  // x = max(0, x);

//   // for Chroma
//   float3 x0;
//   {
//     float3 colorU = x;
//     float colorUY = /* max3(x) */ GetLuminance(x);
//     float colorNY = Neupow(colorUY, GS.LUTPeak, /* tmi.p, */ 1.5);
// 
//     x *= safeDivision(colorNY, colorUY, 0);
//     // x = ClampByMaxChannel(x, 1); //TODO: how does this cause error?!?!?
//     float3 colorN = x;
// 
//     x = sqrt(x); //Gamma Encode
//     x = x * 0.96875 + 0.015625; //32x
//     x = colorGradingTexture.Sample(colorGradingTextureSampler_s, x).xyz;
//     x *= x; //Gamma Decode
// 
//     x = RestorePostProcess(colorU, colorN, x, 0.3125, true);
//     x = max(0, x);
//     x0 = x;
//   }
// 
//   // for Luma
//   float3 x1;
//   x = tmi.x;
//   {
//     float3 colorU = x;
//     float colorUY = /* max3(x) */ GetLuminance(x);
//     float colorNY = Neupow(colorUY, 1, 1.5);
// 
//     x *= safeDivision(colorNY, colorUY, 0);
//     float3 colorN = x;
// 
//     x = sqrt(x); //Gamma Encode
//     x = x * 0.96875 + 0.015625; //32x
//     x = colorGradingTexture.Sample(colorGradingTextureSampler_s, x).xyz;
//     x *= x; //Gamma Decode
// 
//     x = RestorePostProcess(colorU, colorN, x, 0.3125, true);
//     x = max(0, x);
//     x1 = x;
//   }
// 
//   x = x0 * safeDivision(GetLuminance(x1), GetLuminance(x0), 0);
//   x = min(tmi.p, x);

  tmi.x = x;
  tmi.x = sqrt(tmi.x); //Gamma Encode
}

void T_FilmGrain(float2 v2) {
  #if TONEMAP_FILMGRAIN == 0
    return;
  #endif

  float4 r0;
  float2 r1;

  r0.w = filmGrainTexture.Sample(filmGrainTextureSampler_s, v2.xy * filmGrainTextureScaleAndOffset.xy + filmGrainTextureScaleAndOffset.zw).x;
  r0.xyz = (-0.5 + r0.w) * filmGrainColorScale.xyz;

  tmi.x += r0.xyz;
  tmi.x = max(0, tmi.x); //needed!
  // tmi.x = min(tmi.p, tmi.x);
}

float4 T_Out() {

  float3 x = tmi.x;
  x *= x;

  // x = CorrectPerChannelTonemapHiglightsDesaturation(x, 1000 / 203.f, 0.9, CS_BT709);
  // x = max(0, x);

  float xY = GetLuminance(x);
  float xY1 = xY;
  xY1 = Neupow(xY, tmi.p, GS.WhiteClip * 2);
  x *= safeDivision(xY1, xY, 0);
  // x = min(tmi.p, x);

  // {
  //   float3 x0 = x;
  //   float3 x1 = Neupow(x, tmi.p, GS.WhiteClip * 2.2);
  //   // float3 x1 = ExponentialRollOff(x, GS.WhiteClip * 0.18, tmi.p);
  //   x0 = UCS_Encode(x0);
  //   x0.yz *= 1.06;
  //   x1 = UCS_Encode(x1);
  //   float hs = 0.3 * GS.HighlightSat;
  //   x = RestoreHueAndChrominanceUcs(x1, x0, hs, hs, 1);
  //   x = UCS_Decode(x);
  //   x = max(0, x);
  //   // x = min(tmi.p, x);
  //   x = ClampByMaxChannel(x, tmi.p);
  // }

  float l = GetLuminance(tmi.x);
  l *= l;
  l = l / (l + 1);
  l = saturate(l);

  l = sqrt(l);
  x = sqrt(x); //Gamma Encode

  return float4(x, l);
}

#endif