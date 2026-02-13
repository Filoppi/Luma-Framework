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

float3 GammaDecode(float3 x) {
  x = gamma_sRGB_to_linear(x, GCT_NONE);

  //gamma correction
  #if GAMMA_CORRECTION_TYPE > 0 && CUSTOM_HDTVREC709 > 0
    x = EncodeRec709(x);
    x = gamma_to_linear(x, GCT_NONE);
  #elif GAMMA_CORRECTION_TYPE > 0 && CUSTOM_HDTVREC709 == 0
    x = linear_to_sRGB_gamma(x, GCT_NONE);
    x = gamma_to_linear(x, GCT_NONE);
  #elif GAMMA_CORRECTION_TYPE == 0 && CUSTOM_HDTVREC709 > 0
    x = EncodeRec709(x);
    x = gamma_sRGB_to_linear(x, GCT_NONE);
  #elif GAMMA_CORRECTION_TYPE == 0 && CUSTOM_HDTVREC709 == 0
    // x = linear_to_sRGB_gamma(x, GCT_NONE);
    // x = gamma_sRGB_to_linear(x, GCT_NONE);
  #endif

  return x;
}

float3 HDRTonemap(float3 x) {
  //skip tonemap if FMV is active
  #if CUSTOM_UPSCALE_MOV >= 2
    if (TonemapInfo::GetIsFMV(GS.TonemapInfo)) return x;
  #endif

  // skip, if nothing to tonemap
  #if CUSTOM_TONEMAP_TRYIGNOREUI > 0
    if (!TonemapInfo::GetDrawnTonemap(GS.TonemapInfo)) return x;
  #endif


  float shoulderStart = HDR_SHOULDERSTART;
    #if CUSTOM_TONEMAP_PERCHANNEL == 0
      float l = GetLuminance(x, CS_BT2020);
      float lT;
      #if CUSTOM_TONEMAP == 1
        lT = Reinhard::ReinhardPiecewiseExtended(l, 100, HDR_PEAK, HDR_SHOULDERSTART);
      #elif CUSTOM_TONEMAP == 2
        lT = ExponentialRollOff(l, HDR_SHOULDERSTART, HDR_PEAK);
      #endif

      x *= safeDivision(lT, l, 0);
    #else
      #if CUSTOM_TONEMAP == 1
        x = Reinhard::ReinhardPiecewiseExtended(x, 100, HDR_PEAK, HDR_SHOULDERSTART);
      #elif CUSTOM_TONEMAP == 2
        x = ExponentialRollOff(x, HDR_SHOULDERSTART, HDR_PEAK);
      #endif
    #endif
  return x;
}

//in game final draw to tex
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
//   float4 color = g_texture.SampleLevel(g_sampler_s, v1.xy, g_texture_lod.x).xyzw;
//   float4 uiAndColor = UIOutputTex.SampleLevel(g_sampler_s, v1.xy, g_texture_lod.x).xyzw;
// 
//   // bool isUI = color.x != uiAndColor.x || color.y != uiAndColor.y || color.z != uiAndColor.z;
//   // if (isUI) o0 = float4(saturate(uiAndColor.xyz), color.w);
//   // else o0 = color;
// 
//   float3 ui = uiAndColor.xyz - color.xyz;
//   // ui = saturate(ui);
//   ui *= DVS1;
//   o0 = float4(color.xyz + ui.xyz, color.w);

  o0 = g_texture.SampleLevel(g_sampler_s, v1.xy, g_texture_lod.x).xyzw;

  float3 x = o0.xyz;
  x = max(0, x);

  //reverse intemediate scaling
  float exp = HDR_INTSCALING;
  if (exp != 1.0f) {
    x = gamma_to_linear(x, GCT_NONE, 2.2);
    x /= HDR_INTSCALING;
    x = linear_to_gamma(x, GCT_NONE, 2.2);
  }

  // decode from tonemap
  #if CUSTOM_FAKEBT2020 > 0
    float3 x2020 = GammaDecode(BT709_To_BT2020(x));
    float3 x709 = GammaDecode(x);
    x = RestoreHueAndChrominance(x2020, BT709_To_BT2020(x709), 1, GS.FakeBT2020ChromaCorrect, 0, FLT_MAX, GS.FakeBT2020LumaCorrect, CS_BT2020);
  #else
    x =  BT709_To_BT2020(GammaDecode(x));
  #endif

  // //Fake BT2020 highlights sat boost
  // #if CUSTOM_FAKEBT2020 > 0
  //   x = CorrectPerChannelTonemapHiglightsDesaturationFixed(x, 10, 0.750, CS_BT2020);
  // #endif

  //color grade
  #if CUSTOM_COLORGRADE == 2
    x = RenoDX_ColorGrade(
      x, 
      GS.CGContrast, GS.CGContrastMidGray / GamePaperWhiteNits,
      GS.CGHighlightsStrength, GS.CGHighlightsMidGray / GamePaperWhiteNits,
      GS.CGShadowsStrength, GS.CGShadowsMidGray / GamePaperWhiteNits,
      GS.CGSaturation,
      CS_BT2020
    );
  #endif

  // y tonemap
  x = HDRTonemap(x);

  //clamp cs
  x = max(0, x);

  // clamp peak
  float p = HDR_PEAK;
  #if CUSTOM_TONEMAP_CLAMP == 1
    x = min(p, x);
  #elif CUSTOM_TONEMAP_CLAMP == 2
    x = ClampByMaxChannel(x, p);
  #endif

  //intermediate scaling
  x *= HDR_INTSCALING;

  //replacement for DisplayComposition
  #if SWAPCHAIN_SKIPALL > 0
    //to scRGB
    x *= LumaSettings.UIPaperWhiteNits / sRGB_WhiteLevelNits;
  #endif

  //to intemediate cs
  x = BT2020_To_BT709(x);

  //intermediate encode
  #if SWAPCHAIN_SKIPALL == 0
    #if GAMMA_CORRECTION_TYPE == 0
      x = linear_to_sRGB_gamma(x, GCT_MIRROR);
    #else 
      x = linear_to_gamma(x, GCT_MIRROR);
    #endif

    //rec709 encode
    #if CUSTOM_HDTVREC709 > 0
      x = sign(x) * DecodeRec709(abs(x));
      x = linear_to_sRGB_gamma(x, GCT_MIRROR);
    #endif

    // x = linear_to_gamma(x, GCT_MIRROR);
  #endif

  //out
  o0.xyz = x;

  //legacy debug
  #if CUSTOM_TONEMAP_IDENTIFY > 0
    o0.xyz = DrawBinary(TonemapInfo::GetIndexOnlyIfDrawn(GS.TonemapInfo), o0.xyz, v1.xy);
  #endif

  return;
}