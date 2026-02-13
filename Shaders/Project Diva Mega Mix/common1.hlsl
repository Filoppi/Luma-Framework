#include "./Includes/Common.hlsl"
#include "../Includes/Math.hlsl"
#include "../Includes/Color.hlsl"
#include "../Includes/Tonemap.hlsl"
#include "../Includes/Reinhard.hlsl"
#include "./Includes/PerChannelCorrect.hlsl"
#include "./Includes/ColorGrade.hlsl"
#include "./Includes/DrawBinary.hlsl"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool CheckBlack(in float v) {
  return v == 0.f;
}
bool CheckBlack(in float2 v) {
  const float e = 0.f;
  return v.x == e && v.y == e;
}
bool CheckBlack(in float3 v) {
  const float e = 0.f;
  return v.x == e && v.y == e && v.z == e;
}
bool CheckBlack(in float4 v) {
  const float e = 0.f;
  return v.x == e && v.y == e && v.z == e && v.w == e;
}

bool CheckWhite(in float v) {
  return v == 1.f;
}
bool CheckWhite(in float2 v) {
  const float e = 1.f;
  return v.x == e && v.y == e;
}
bool CheckWhite(in float3 v) {
  const float e = 1.f;
  return v.x == e && v.y == e && v.z == e;
}
bool CheckWhite(in float4 v) {
  const float e = 1.f;
  return v.x == e && v.y == e && v.z == e && v.w == e;
}
float HUDBrightness(float x) {
  return x;
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
float3 Gamma_CorrectionLinear(float3 x) {
  //gamma correction
  #if GAMMA_CORRECTION_TYPE == 0
   if (DefaultGamma != 2.2f) {
      x = linear_to_gamma(x, GCT_POSITIVE, 2.2);
      x = gamma_to_linear(x, GCT_POSITIVE);
   }
  #else 
   x = linear_to_sRGB_gamma(x, GCT_POSITIVE);
   x = gamma_to_linear(x, GCT_POSITIVE);
  #endif

  return x;
}

float3 Gamma_IntermediateEncode(float3 x) {
  #if GAMMA_CORRECTION_TYPE == 0
    float g = DefaultGamma;
  #else 
    float g = DefaultGamma + 0.2;
  #endif
  x = linear_to_gamma(x, GCT_MIRROR, g);

  return x;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float Sum(float3 x) { return x.x + x.y + x.z; }

float3 Tonemap_SaveSprites_UpgradeSpritesOnly(float3 sprites) {
  //Debug: force 0
  #if CUSTOM_BGSPRITES_FORCE > 0
    sprites = 0;
  #endif

  //gamma decode
  sprites = gamma_to_linear(sprites, GCT_POSITIVE, 2.2); //requires GCT_POSITIVE

  const float maxIn = GS.UpscaleBGSpritesMax;
//   #if DEVELOPMENT > 0
//    if (max3(sprites) > maxIn) colorT, colorU = 10000.f;
//   #endif
  sprites = min(maxIn - 0.00001f, sprites);
  float y0 = GetLuminance(sprites);
  if (y0 > maxIn) sprites *= maxIn / y0 /* min(maxIn, y0) */;
  sprites = Reinhard::inverse::ReinhardScalable(sprites, maxIn, 0, GS.UpscaleBGSpritesExp, 0.18f);

  //gamma encode
  sprites = linear_to_gamma(sprites, GCT_POSITIVE, 2.2); //max(0)

  return sprites;
}

void Tonemap_SaveSprites(in float3 sprites, in float alpha, inout float3 colorT, inout float3 colorU) {
  //Debug: force 0
  #if CUSTOM_BGSPRITES_FORCE > 0
    sprites = 0;
  #endif

  //colorT (ez)
  colorT += sprites * alpha;

  //////////////////////////////////////////////////////////////////

  //colorU
#if CUSTOM_UPSCALE_BGSPRITES > 0
  if (alpha > 0) sprites = Tonemap_SaveSprites_UpgradeSpritesOnly(sprites);
#endif
  colorU += sprites * alpha;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef TONEMAP_COMPLEX
float3 Tonemap_Complex(float3 colorT, float4 v3, bool isLookBack = true) {
  float4 r0, r1;
  r0.xyz = colorT;

  r0.y = dot(r0.xyz, float3(0.300000012, 0.589999974, 0.109999999)); //YUV/YCbCr: Y
  r0.xz = r0.xz + -r0.y; //UV
  r1.x = v3.y * r0.y; //exposure on Y
  float backUpY = r1.x;
  r1.xy = g_textures_2_.SampleLevel(g_samplers_2__s, float2(r1.x, 0), 0).yx;
      // Neutral LUT https://www.desmos.com/calculator/u3bhz0bn62
      // r1.x = Saturation
      // r1.y = Luma

  // blowout reduction
  #if CUSTOM_LUT_BLOWOUT_REDUCTION > 0
    if (isLookBack)
    {
      // //lerp to min
      // const float m = 0.1;
      // if (r1.x < m) r1.x = lerp(r1.x, m, GS.LUTBlowoutReduction);
    
      // look back
      float yLB = backUpY * GS.LUTBlowoutReductionLookBack;
      yLB = max(0.0430528375734, yLB);
      float2 m = g_textures_2_.SampleLevel(g_samplers_2__s, float2(yLB, 0), 0).yx;
      /* if (r1.x < m.x)  */r1.x = lerp(r1.x, m.x, GS.LUTBlowoutReduction);
    
      // //pull back
      // const float p = 0.0469667318982;
      // if (backUpY > p)
      // {
      //    float n = lerp(backUpY, p, GS.LUTBlowoutReduction);
      //    float2 m = g_textures_2_.SampleLevel(g_samplers_2__s, float2(n, 0), 0).yx;
      //    if (n < m.x) r1.x = lerp(r1.x, m.x, GS.LUTBlowoutReduction);
      // }
    }
  #endif

  r0.y = v3.x * r1.x; //saturation color grading
  r1.xz = r0.y * r0.xz;
  r0.xz = r0.y * r0.xz + r1.y;
  r0.y = dot(r1.xyz, float3(-0.508475006, 1, -0.186441004)); //YUV to G
  r0.xyz = r0.xyz * g_tone_scale.xyz + g_tone_offset.xyz; //post multiply and addition color grading
  r0.xyz = /* saturate */(r0.xyz);

  return r0.xyz;
}
// REQUIRES colorU linear!
void Tonemap_ResolveComplexWithExposure(inout float3 colorT, inout float3 colorU, float4 v3) {
  //tonemap
  colorT = Tonemap_Complex(colorT, v3);
  #if CUSTOM_TEST_SDR
    colorU = colorT; return;
  #endif

  //exposure
  float midGray = GetLuminance(Tonemap_Complex(0.46f, v3, false)); //TODO: simplify with only Y?
  midGray = gamma_to_linear1(midGray, GCT_POSITIVE, 2.2);
  colorU *= midGray / 0.18f;
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
float3 Tonemap_Do(in float3 colorU, in float3 colorT, in float2 uv, in Texture2D<float4> texColor, bool isIVT = false) {
  
   #if CUSTOM_TEST_SDR
      return colorT;
   #endif

  // gamma decode
  // colorU = gamma_to_linear(colorU, GCT_POSITIVE, 2.2); //done outside
  //  colorT = gamma_sRGB_to_linear(colorT, GCT_POSITIVE);
  colorT = gamma_to_linear(colorT, GCT_POSITIVE, 2.2);

   
   // GS.SaturateStrength
   #if CUSTOM_PCBLOWOUT == 0
      const float colorTMax = 1;
   #elif CUSTOM_PCBLOWOUT > 0
      const float colorTMax = GS.PCBlowoutEnd;
   #endif
   if (GS.PCBlowoutStrength > 0)
   {
      const float colorTShoulderStart = GS.PCBlowoutStart;

      #if CUSTOM_PCBLOWOUT == 0
         float3 colorTS = min(1, colorT);
      #elif CUSTOM_PCBLOWOUT == 1
         float3 colorTS = Reinhard::ReinhardPiecewiseExtended(colorT, 100, colorTMax, colorTShoulderStart);
      #elif CUSTOM_PCBLOWOUT == 2
         float3 colorTS = ExponentialRollOff(colorT, colorTShoulderStart, colorTMax);
      #endif
      
      colorT = lerp(colorT, colorTS, GS.PCBlowoutStrength);
   }

  // inverse tonemap (for toon)
#if CUSTOM_UPSCALE_TOON > 0
   if (isIVT) {
    colorT = min(GS.UpscaleToonMax - 0.00001f, colorT);

    float y = GetLuminance(colorT);
    float yDes = y;
    yDes = Reinhard::inverse::ReinhardScalable(yDes, GS.UpscaleToonMax, 0, GS.UpscaleToonExp, 0.18);
    yDes = max(0.f, yDes);
  
    colorU *= safeDivision(yDes, y, 0);
  }
#endif

   float y0 = GetLuminance(colorU, CS_BT709);
   float y1 = Reinhard::ReinhardSimple(y0, colorTMax); y1 = max(1e-6, y1);
   float3 colorN = colorU * (y1 / y0);
   colorN = min(colorN, 1);

   //Upgrade
   colorT = UpgradeToneMap(colorU, colorN, colorT, GS.LUT, 0);

   //gamme encode
   colorT = linear_to_gamma(colorT, GCT_POSITIVE, 2.2); 
  //  colorT = linear_to_sRGB_gamma(colorT, GCT_POSITIVE); 

   return colorT;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

