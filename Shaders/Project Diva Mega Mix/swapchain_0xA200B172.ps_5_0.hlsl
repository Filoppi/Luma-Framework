// ---- Created with 3Dmigoto v1.3.16 on Sun Aug 31 23:28:36 2025

cbuffer Quad : register(b0)
{
  float4 g_texcoord_modifier : packoffset(c0);
  float4 g_texel_size : packoffset(c1);
  float4 g_color : packoffset(c2);
  float4 g_texture_lod : packoffset(c3);
}

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);
Texture2D<float4> UIOutputTex: register(t1);

// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

//CUSTOM_SDR defs
#if CUSTOM_SDR == 1
  #ifdef CUSTOM_GAMMACORRECT22
    #undef CUSTOM_GAMMACORRECT22
    #define CUSTOM_GAMMACORRECT22 0
  #endif
  #ifdef CUSTOM_FAKEBT2020
    #undef CUSTOM_FAKEBT2020
    #define CUSTOM_FAKEBT2020 0
  #endif
  #ifdef CUSTOM_COLORGRADE_SATORDER
    #undef CUSTOM_COLORGRADE_SATORDER
    #define CUSTOM_COLORGRADE_SATORDER 0
  #endif
#endif

//in game final draw to tex
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0 = g_texture.SampleLevel(g_sampler_s, v1.xy, g_texture_lod.x).xyzw;

  // o0.xyz = (4000/203.f);
  // o0.xyz = linear_to_sRGB_gamma(o0.xyz, GCT_NONE);
  // return;

  float3 x = o0.xyz;
  x = max(0, x);

  #if CUSTOM_UITRANSPARENCY > 0
  {
    float3 colorWithUi = UIOutputTex.SampleLevel(g_sampler_s, v1.xy, g_texture_lod.x).xyz;
    colorWithUi = max(0, colorWithUi);
    float3 uiOnly = colorWithUi - x;
    x += uiOnly * GS.UITransparency;
  }
  #endif

  #if CUSTOM_PROGRESSBAR > 0
  {
    const float3 c = 1/* float3(0/255.f, 255/255.f, 157/255.f) */;

    #if CUSTOM_PROGRESSBAR == 1
      const bool r = v1.y < 0.0035f;
    #elif CUSTOM_PROGRESSBAR == 2
      const bool r = 1-v1.y < 0.0035f;
    #endif

    if (GS.ProgressBarRatio >= 0 && r) {
      if (v1.x <= GS.ProgressBarRatio) { //played
        x = lerp(x, 0.8, 0.5f);
      } else if (v1.x > GS.ProgressBarRatio && v1.x <= GS.ProgressBarRatio + 0.0025f) { //curr
        x = lerp(x, 1, 0.9f);
      } else { //future
        x = lerp(x, 0, 0.5f);
      }

      // x = lerp(x, 0, 0.25f);
      // const float t = 0.2f;
      // if (v1.x <= GS.ProgressBarRatio) { //trail
      //   x = lerp(x, c, InverseLerp(GS.ProgressBarRatio - t, GS.ProgressBarRatio, v1.x) * 0.2f);
      // } else if (v1.x > GS.ProgressBarRatio && v1.x <= GS.ProgressBarRatio + 0.0025f) { //curr
      //   x = lerp(x, 1, 0.99f);
      // }
    }
  }
  #endif

  //legacy Tonemap Identify
  #if CUSTOM_TONEMAP_IDENTIFY > 0
    o0.xyz = DrawBinary(TonemapInfo::GetIndexOnlyIfDrawn(GS.TonemapInfo), o0.xyz, v1.xy);
  #endif

  //intermediate decode
  x = gamma_sRGB_to_linear(x);
  x /= HDR_INTSCALING;
  
  #if CUSTOM_HDTVREC709_1 == 0
    //noop
  #else
    x = EncodeRec709(x);
    // x = pow(x, 2.4);
    x = gamma_sRGB_to_linear(x);
  #endif

  //Gamma Correction & Mode / Fake BT2020 / Saturation (bruh moment)
  const float3 xBack = UCSTo(x, CS_BT709);
  const float gcScale = GamePaperWhiteNits / GS.GammaCorrection22PaperWhite;
  #if CUSTOM_GAMMACORRECT22 == 0 && CUSTOM_FAKEBT2020 == 0
    #if CUSTOM_COLORGRADE_SATORDER == 2
      x = UCSTo(x, CS_BT709);
      x.yz *= GS.CGSaturation;
      x = UCSFrom(x, CS_BT2020);
    #else
      x = BT709_To_BT2020(x);
    #endif
  #elif CUSTOM_GAMMACORRECT22 == 1 && CUSTOM_FAKEBT2020 == 0
    x *= gcScale;

    float3 x709 = x;
    
    bool3 below1 = x < 1;
    x = linear_to_sRGB_gamma(x, GCT_NONE);
    x = gamma_to_linear(x, GCT_NONE, 2.2);
    x = below1 ? x : x709; //low pass piecewise

    #if CUSTOM_GAMMA_CORRECTION_MODE == 0 && CUSTOM_COLORGRADE_SATORDER != 2
      x = BT709_To_BT2020(x);
    #elif CUSTOM_GAMMA_CORRECTION_MODE == 0 && CUSTOM_COLORGRADE_SATORDER == 2
      x = UCSTo(x, CS_BT709);
      x.yz *= GS.CGSaturation;
      x = UCSFrom(x, CS_BT2020);
    #elif CUSTOM_GAMMA_CORRECTION_MODE == 1 && CUSTOM_COLORGRADE_SATORDER != 2
      x = UCSTo(x, CS_BT709);
      x = RestoreHueAndChrominanceUcs(x, xBack, 1, GS.GammaPerceptualChrominanceCorrect, 0);
      x = UCSFrom(x, CS_BT2020);
    #elif CUSTOM_GAMMA_CORRECTION_MODE == 1 && CUSTOM_COLORGRADE_SATORDER == 2
      x = UCSTo(x, CS_BT709);
      x = RestoreHueAndChrominanceUcs(x, xBack, 1, GS.GammaPerceptualChrominanceCorrect, 0);
      x.yz *= GS.CGSaturation;
      x = UCSFrom(x, CS_BT2020);
    #else
      #error Invalid CUSTOM_GAMMA_CORRECTION_MODE & CUSTOM_COLORGRADE_SATORDER configuration
    #endif

    x /= gcScale;
  #elif CUSTOM_GAMMACORRECT22 == 0 && CUSTOM_FAKEBT2020 == 1
    // x *= gcScale;

    float3 x709 = x;
    float3 x2020 = x;

    x2020 = linear_to_sRGB_gamma(x2020, GCT_NONE);
    x2020 = BT709_To_BT2020(x2020);
    x2020 = gamma_sRGB_to_linear(x2020, GCT_NONE);

    x709 = UCSTo(x709, CS_BT709);
    x2020 = UCSTo(x2020, CS_BT2020);
    x = RestoreHueAndChrominanceUcs(x709, x2020, 0, GS.FakeBT2020Chroma, 1);
    float ls = saturate(sqrt(x709.x)); //low pass
    ls = lerp(1, ls, GS.FakeBT2020Luma); //strength
    x.x = lerp(x2020.x, x709.x, ls);
    #if CUSTOM_COLORGRADE_SATORDER == 2
      x.yz *= GS.CGSaturation;
    #endif
    x = UCSFrom(x, CS_BT2020);

    // x /= gcScale;
  #elif CUSTOM_GAMMACORRECT22 == 1 && CUSTOM_FAKEBT2020 == 1
    x *= gcScale;

    float3 x709 = x;
    float3 x2020 = x;

    bool3 below1 = x709 < 1;
    x709 = linear_to_sRGB_gamma(x709, GCT_NONE);
    x709 = gamma_to_linear(x709, GCT_NONE, 2.2);
    x709 = below1 ? x709 : x; //low pass piecewise

    x2020 = linear_to_sRGB_gamma(x2020, GCT_NONE);
    x2020 = BT709_To_BT2020(x2020);
    x2020 = gamma_to_linear(x2020, GCT_NONE, 2.2);

    x709 = UCSTo(x709, CS_BT709);
    x2020 = UCSTo(x2020, CS_BT2020);
    x = RestoreHueAndChrominanceUcs(x709, x2020, 0, GS.FakeBT2020Chroma, 1);
    float ls = saturate(sqrt(x709.x)); //low pass
    ls = lerp(1, ls, GS.FakeBT2020Luma); //strength
    x.x = lerp(x2020.x, x709.x, ls); //low pass
    #if CUSTOM_GAMMA_CORRECTION_MODE == 1
      float s = x.x; //low pass
      // s *= 1.25;
      s = 1 - s;
      s = saturate(s);
      s *= GS.GammaPerceptualChrominanceCorrect;

      x = RestoreHueAndChrominanceUcs(x, xBack, 1, s, 0);
    #endif
    #if CUSTOM_COLORGRADE_SATORDER == 2
      x.yz *= GS.CGSaturation;
    #endif
    x = UCSFrom(x, CS_BT2020);

    x /= gcScale;
  #else
    #error Invalid CUSTOM_GAMMACORRECT22 & CUSTOM_FAKEBT2020 configuration
  #endif

  // x = UCSFrom(xBack, CS_BT2020);

  //clamp BT2020
  x = max(0, x);

  // clamp peak
  {
    #if CUSTOM_TESTSDR == 0
      const float p = HDR_PEAK;
    #else
      const float p = 1;
    #endif

    #if CUSTOM_CLAMP_PEAK == 1
      x = min(x, p);
    #elif CUSTOM_CLAMP_PEAK == 2
      x = ClampByMaxChannel(x, p);
    #elif CUSTOM_CLAMP_PEAK == 3
      x = ExponentialRollOff(x, p, p * 1.1f);
    #endif
  }

  // to scRGB
  //  x *= GamePaperWhiteNits / 80.f;
  x *= HDR_INTSCALING;
  x = BT2020_To_BT709(x);
  x = linear_to_sRGB_gamma(x, GCT_MIRROR);

  //out
  o0.xyz = x;

  return;
}