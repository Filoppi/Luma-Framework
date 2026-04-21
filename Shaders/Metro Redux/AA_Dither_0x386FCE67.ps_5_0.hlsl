#define FXAA_HLSL_5 1
#if 1 // Optional: force max quality (otherwise it falls back on the default)
#define FXAA_QUALITY__PRESET 39
#endif

#include "../Includes/ColorGradingLUT.hlsl"
#include "../Includes/Common.hlsl"
#include "../Includes/FXAA.hlsl"


// Workaround: Common.hlsl defines FLT_* and PI macros that collide with RenoDX math constants.
#undef FLT_MIN
#undef FLT_MAX
#undef FLT_NAN
#undef FLT_QNAN_POS
#undef FLT_QNAN_NEG
#undef FLT_SNAN_POS
#undef FLT_SNAN_NEG
#undef FLT_EPSILON
#undef FLT10_MAX
#undef FLT11_MAX
#undef FLT16_MAX
#undef PI

#include "./RenoDX/renodx.hlsl"

// Enables FXAA (Luma's improved implementation)
#ifndef ENABLE_FXAA
#define ENABLE_FXAA 1
#endif
// Not needed in HDR
#ifndef ENABLE_DITHER
#define ENABLE_DITHER 0
#endif

cbuffer cb_screen : register(b2)
{
  float4 rtdim : packoffset(c0);
  float4 depth_xform : packoffset(c1);
  float4 envmap_color : packoffset(c2);
  float4 sph_r[3] : packoffset(c3);
  float4 sph_g[3] : packoffset(c6);
  float4 sph_b[3] : packoffset(c9);
}

cbuffer cb_misc_1 : register(b4)
{
  float4 eye_position : packoffset(c0);
  float4 timers : packoffset(c1);
  float4 clipplanes[6] : packoffset(c2);
}

SamplerState s_clamp_bi_s : register(s6);
Texture2D<float4> t_backbuffer : register(t0); // Originally called "t_ssao" but it's not SSAO!

void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float2 uv = v0.xy * rtdim.xy;

  if (any(uv > 1.0))
  {
    o0 = 0;
    return;
  }

#if ENABLE_FXAA
  FxaaTex tex;
  tex.tex = t_backbuffer; // We store the perceptually encoded luminance in the alpha channel!
  tex.smpl = s_clamp_bi_s;
  FxaaFloat2 fxaaQualityRcpFrame = LumaSettings.SwapchainInvSize;
  FxaaFloat fxaaQualitySubpix = FxaaFloat(0.75);
#if FXAA_QUALITY__PRESET >= 39
  FxaaFloat fxaaQualityEdgeThreshold = FxaaFloat(0.125); // Increase default quality
  FxaaFloat fxaaQualityEdgeThresholdMin = FxaaFloat(0.0312); // Increase default quality
#else
  FxaaFloat fxaaQualityEdgeThreshold = FxaaFloat(0.166);
  FxaaFloat fxaaQualityEdgeThresholdMin = FxaaFloat(0.0833);
#endif

  // The 0 params are console exclusive
  o0.xyzw = FxaaPixelShader(
    uv,
    0.0,
    tex,
    tex,
    tex,
    fxaaQualityRcpFrame,
    0.0,
    0.0,
    0.0,
    fxaaQualitySubpix,
    fxaaQualityEdgeThreshold,
    fxaaQualityEdgeThresholdMin).xyzw;
#else
  o0.xyzw = t_backbuffer.SampleLevel(s_clamp_bi_s, uv, 0).xyzw;
#endif

 //o0.xyz = gamma_sRGB_to_linear(o0.xyz);

#if ENABLE_DITHER // Applies dither in gamma space
  float2 r0;
  r0.xy = v0.xy * rtdim.xy + timers.ww;
  r0.x = dot(r0.xy, float2(12.9898005,78.2330017));
  r0.x = sin(r0.x);
  r0.x = 43758.5469 * r0.x;
  r0.x = frac(r0.x);
  r0.x = -0.5 + r0.x;
  float temporalDither = r0.x * 0.0166664999;
   
  o0.xyz = pow(abs(o0.xyz), 1.0 / 2.2) * Sign_Fast(o0.xyz); // Luma: improved encoding to gamma space (it was just sqr/sqrt)
  o0.xyz += temporalDither;
  o0.xyz = pow(abs(o0.xyz), 2.2) * Sign_Fast(o0.xyz);
#endif
  float3 untonemapped_bt2020 = BT709_To_BT2020(o0.xyz);
  untonemapped_bt2020 = gamma_sRGB_to_linear(untonemapped_bt2020);

  float peak = LumaSettings.PeakWhiteNits / sRGB_WhiteLevelNits;
  float diffuse_white = LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;

  peak = peak / diffuse_white;

  // bool gamma_correction = true;
  // if (gamma_correction) {
  //     peak = gamma_to_linear(peak, 0, 2.0);
  //     peak = linear_to_gamma(peak, 0, 2.2);
  // }

  float3 tonemapped_bt2020 = renodx::tonemap::neutwo::PerChannel(untonemapped_bt2020, peak);
  float3 tonemapped_bt709 = BT2020_To_BT709(tonemapped_bt2020);

  //tonemapped_bt709 = linear_to_sRGB_gamma(tonemapped_bt709);

// #if UI_DRAW_TYPE == 2 // Scale by the inverse of the relative UI brightness so we can draw the UI at brightness 1x and then multiply it back to its intended range
//   ColorGradingLUTTransferFunctionInOutCorrected(tonemapped_bt709.rgb, VANILLA_ENCODING_TYPE, GAMMA_CORRECTION_TYPE, true);
//   tonemapped_bt709.rgb *= (LumaSettings.GamePaperWhiteNits / LumaSettings.UIPaperWhiteNits);
//   ColorGradingLUTTransferFunctionInOutCorrected(tonemapped_bt709.rgb, GAMMA_CORRECTION_TYPE, VANILLA_ENCODING_TYPE, true);
// #endif

  // if (gamma_correction) {
  // // Game originally encodes with 2.0, so recreate mismatch
  // tonemapped_bt709 = linear_to_gamma(tonemapped_bt709, 0, 2.0);
  // tonemapped_bt709 = gamma_to_linear(tonemapped_bt709, 0, 2.2);
  // }
  tonemapped_bt709 = linear_to_sRGB_gamma(tonemapped_bt709);
  o0.xyz = tonemapped_bt709;

}