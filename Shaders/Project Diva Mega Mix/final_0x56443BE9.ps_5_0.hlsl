// ---- Created with 3Dmigoto v1.3.16 on Sat Aug 09 22:01:31 2025

SamplerState g_sampler_s : register(s0);
Texture2D<float4> g_texture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

float3 Saturation(float3 x, float s) {
  x = UCSTo(x, CS_BT709);
  x.yz *= s;
  x.yz *= 1.085f;
  x = UCSFrom(x, CS_BT709);
  x = max(0, x); //clamp cs
  return x;
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = g_texture.Sample(g_sampler_s, v1.xy).xyzw;

  #if CUSTOM_TESTSDR == 1
    o0 = saturate(o0);
    return;
  #endif

  float3 x = o0.xyz;
  x = max(0, x);

  // decode from tonemap
  x = gamma_sRGB_to_linear(x);
  
  //Saturation
  #if CUSTOM_COLORGRADE_SATORDER == 1 || CUSTOM_ALTSAT == 1
    float s = 1;
    #if CUSTOM_ALTSAT == 0
      s *= GS.CGSaturation;
    #endif
    #if CUSTOM_ALTSAT == 1
      x = 1.1f;
    #endif
    x = Saturation(x, GS.CGSaturation);
  #endif

  //debug
  // x = 203/203.f;
  // x = v1.x;

  //intermediate scaling
  x *= HDR_INTSCALING;

  //intermediate encode
  x = linear_to_sRGB_gamma(x);

  o0.xyz = x;
  return;
}