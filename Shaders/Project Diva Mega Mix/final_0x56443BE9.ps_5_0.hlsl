// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 09 22:01:31 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"



void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw;
  float3 x = o0.xyz;
  x = max(0, x);

  // decode from tonemap
  x = gamma_to_linear(x, GCT_NONE, 2.2);

  // exposure
  x *= GS.Exposure;

  //color grade
  #if CUSTOM_COLORGRADE == 1
  x = RenoDX_ColorGrade(
      x, 
      GS.CGContrast, GS.CGContrastMidGray / GamePaperWhiteNits,
      GS.CGHighlightsStrength, GS.CGHighlightsMidGray / GamePaperWhiteNits,
      GS.CGShadowsStrength, GS.CGShadowsMidGray / GamePaperWhiteNits,
      GS.CGSaturation,
      CS_BT709,
      true
    );
  #endif

  //intemediate scaling
  x *= HDR_INTSCALING;

  //enocde for ui
  x = linear_to_gamma(x, GCT_NONE, 2.2);

  o0.xyz = x;
  return;
}