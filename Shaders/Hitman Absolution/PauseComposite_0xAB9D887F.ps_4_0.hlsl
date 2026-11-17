// Luma: Hitman Absolution pause-menu composite pixel shader 0xAB9D887F.
// Faithful translation of the vanilla DXBC (ps_4_0). Vanilla body (TONEMAP_TYPE 0) only change: the sqrt input is clamped to >= 0.
// Blurred HDR inputs carry small negatives; sqrt of them was NaN (black blocks in the pause menu).
// TONEMAP_TYPE >= 1: the scene stays linear and goes through the shared HMA output path (same as FinalPost).

#include "Includes/Common.hlsl"
#include "Includes/HMAOutput.hlsl"

cbuffer cb4 : register(b4)
{
  float4 cb4[115];
}

Texture2D<float4> t0 : register(t0);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t4 : register(t4);
#if TONEMAP_TYPE >= 1
// Modern flare is bound at t5 by C++; null/0-size when unavailable.
Texture2D<float4> t5 : register(t5);
SamplerState s5 : register(s5);
#endif

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
SamplerState s2 : register(s2);
SamplerState s3 : register(s3);
SamplerState s4 : register(s4);

float4 main(float4 v0 : SV_POSITION, float4 v1 : TEXCOORD0, float3 v2 : COLOR0, float3 v3 : COLOR1) : SV_TARGET
{
#if TONEMAP_TYPE < 1
  float4 o0;
  float4 r0, r1, r2, r3;

  r0 = t1.Sample(s1, v1.xy);
  r0.xy = r0.xy + cb4[114].y;
  r0.xy = r0.xy + float2(-0.5, -0.5);
  r0.xy = r0.xy * r0.w;
  r0.z = saturate(r0.w * cb4[114].z);
  r0.w = saturate(r0.w * cb4[114].x);
  r0.xy = r0.xy * cb4[113].w;
  r0.xy = r0.xy * 2.0 + v1.xy;
  r1 = t4.Sample(s4, r0.xy);
  r2 = t2.Sample(s2, r0.xy);
  r3 = t0.Sample(s0, v1.xy);
  r1.xyz = r1.xyz - r3.xyz;
  r0.xyz = r0.z * r1.xyz + r3.xyz;
  r1.xyz = cb4[113].xyz - 1.0;
  r1.xyz = r0.w * r1.xyz + 1.0;
  r0.xyz = r0.xyz * r1.xyz;
  r0.xyz = sqrt(max(r0.xyz, 0.0));
  r0.xyz = r0.xyz * 2.0 - 1.0;
  r1.xyz = 1.0 - abs(r0.xyz);
  r0.xyz = saturate(r0.xyz);
  r3 = t3.Sample(s3, v1.zw);
  r0.w = saturate(-r3.x * cb4[98].w + 1.0);
  r3.xyz = r0.w * v2.xyz;
  r0.xyz = r3.xyz * r1.xyz + r0.xyz;
  r0.xyz = r0.xyz + v3.xyz;
  r1.xyz = 1.0 - r2.xyz;
  o0.xyz = r0.xyz * r1.xyz + r2.xyz;
  o0.w = 0.0;
  return o0;
#else
  // Scene (distortion, blur blend, tint) kept linear, same math as vanilla lines 29-44.
  float4 r0, r1, r2, r3;
  r0 = t1.Sample(s1, v1.xy);
  r0.xy = r0.xy + cb4[114].y;
  r0.xy = r0.xy + float2(-0.5, -0.5);
  r0.xy = r0.xy * r0.w;
  r0.z = saturate(r0.w * cb4[114].z);
  r0.w = saturate(r0.w * cb4[114].x);
  r0.xy = r0.xy * cb4[113].w;
  r0.xy = r0.xy * 2.0 + v1.xy;
  float2 overlayUV = r0.xy;
  r1 = t4.Sample(s4, overlayUV);
  r2 = t2.Sample(s2, overlayUV);
  r3 = t0.Sample(s0, v1.xy);
  r1.xyz = r1.xyz - r3.xyz;
  r0.xyz = r0.z * r1.xyz + r3.xyz;
  r1.xyz = cb4[113].xyz - 1.0;
  r1.xyz = r0.w * r1.xyz + 1.0;
  float3 sceneLin = r0.xyz * r1.xyz;

  // Grain mask as in FinalPost, noise from t3 (vanilla pause reads t3 at v1.zw).
  float grainIntensity = cb4[98].w;
  float grainMaskVanilla = saturate(1.0 - t3.Sample(s3, v1.zw).x * grainIntensity);
  float grainMaskMean = saturate(1.0 - 0.5 * grainIntensity);
  float grainMask = grainMaskMean + (grainMaskVanilla - grainMaskMean) * LumaSettings.GameSettings.FilmGrainStrength;
  float3 vig = lerp(0.5, v2, LumaSettings.GameSettings.VignetteStrength);
  float3 grain = grainMask * vig;

  float3 c = sqrt(max(sceneLin, 0.0));
  float3 cS = min(c, 1.0);
  float3 excess = c - cS;
  float gradientFade = smoothstep(0.0, 0.25, max(cS.r, max(cS.g, cS.b)));
  float3 x = max(GrainLevels(cS, grain, v3 * LumaSettings.GameSettings.TopGradientStrength * gradientFade), 0.0) + excess;

  float4 o0;
  float3 lin = HMA_GammaToLinear(x);
  uint flareWidth, flareHeight;
  t5.GetDimensions(flareWidth, flareHeight);
  bool newFlareValid = LumaSettings.GameSettings.LumaBloom != 0 && LumaSettings.GameSettings.ModernLensFlares != 0 && flareWidth > 0;
  if (newFlareValid)
    lin = HMA_AddFlareHeadroom(lin, max(t5.SampleLevel(s5, overlayUV, 0).rgb, 0.0));
  else
  {
    // Keep the legacy overlay when the modern flare is unavailable.
    float3 ov = saturate(r2.xyz);
    x = x * (1.0 - ov) + ov;
    lin = HMA_GammaToLinear(x);
  }
  o0.xyz = HMA_DisplayOutput(lin);
  o0.w = 0.0;
  return o0;
#endif
}
