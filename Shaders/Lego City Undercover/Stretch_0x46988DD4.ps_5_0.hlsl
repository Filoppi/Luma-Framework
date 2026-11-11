#include "../Includes/Common.hlsl"
#include "../Includes/Reinhard.hlsl"

SamplerState texture0_ss_s : register(s0);
Texture2D<float4> texture0 : register(t0);

// Used for copy, stretch and "upscaling"
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  o0.xyzw = texture0.Sample(texture0_ss_s, v1.xy).xyzw;

  // Tonemap to SDR to avoid UI elements being overly bright in HDR
  // These drew in gamma space
  if (LumaData.CustomData1)
  {
#if 1 // Note: this ends up tonemapping UI sprites a bit too, not just UI scene rendering composed elements
    float highlightsStrength = 2.0; // Avoids tonemapping elements that were already SDR much
    o0.xyz = gamma_to_linear(o0.xyz, GCT_POSITIVE);
    o0.xyz = Reinhard::ReinhardAdvanced(o0.xyz, 0.667, FLT_MAX, 1.0, highlightsStrength);
    o0.xyz = linear_to_gamma(o0.xyz, GCT_NONE);
    // UNORM emulation, just for safety
    o0.a = saturate(o0.a);
#else // Alternatively just clamp everything to SDR, it should be fine in the UI!
    o0.rgba = saturate(o0.rgba);
#endif
  }
}