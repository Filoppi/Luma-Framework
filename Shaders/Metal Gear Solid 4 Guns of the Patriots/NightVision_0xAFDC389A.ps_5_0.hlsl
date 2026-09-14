#include "../Includes/Common.hlsl"

#ifndef ENABLE_HIGH_QUALITY_NIGHT_VISION
#define ENABLE_HIGH_QUALITY_NIGHT_VISION 1
#endif

Texture2D<float4> brightnessTexture : register(t2);
// 256x1
Texture2D<float4> paletteTexture : register(t1);
Texture2D<float4> sceneTexture : register(t0);

// Bilinear clamp
SamplerState s2_s : register(s2);
// Nearest neighbor
SamplerState s1_s : register(s1);
// Bilinear wrap
SamplerState s0_s : register(s0);

cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  out float4 o0 : SV_TARGET0)
{
  float2 invResolution = cb0[8].xy;

  float3 sceneColor = sceneTexture.Sample(s0_s, v2.xy).xyz;

#if 1 // Luma: fix BT.601 luminance and remove saturate // TODO: calc luminance in linear!
  float luminance = GetLuminance(sceneColor);
#else
  float luminance = saturate(dot(sceneColor, float3(0.300000012,0.589999974,0.109999999)));
#endif
  float2 paletteUV;
  float minLuminanceOffset = cb0[0].x; // Usually 0
  paletteUV.x = minLuminanceOffset + luminance;
  paletteUV.y = 0.5;
#if 1 // Luma: fix 8 bit quantization from point sampler, and restore clipped colors
  float LUTSize = 256;
  float LUTScale = (LUTSize - 1.0) / LUTSize;
  float LUTBias  = 0.5 / LUTSize;
  float paletteXMax = max(paletteUV.x, 1.0); // TODO: implement "ShouldForceSDR" (barely matters)
  paletteUV = paletteUV * LUTScale + LUTBias;
  float4 palettedColor = paletteTexture.Sample(s0_s, paletteUV.xy).xyzw;
  palettedColor.rgb *= paletteXMax;
#else
  float4 palettedColor = paletteTexture.Sample(s1_s, paletteUV.xy).xyzw;
#endif
  
  // Grain tint
  float2 grainUV = invResolution * v0.xy;
  // "Quantization" grid
#if 1 // Luma: fix grain stretching in UW
  grainUV = float2(180.0 * (invResolution.y / invResolution.x), 180.0) * grainUV;
#else
  grainUV = float2(320,180) * grainUV;
#endif
  float3 grainTint = brightnessTexture.Sample(s2_s, grainUV).xyz;
#if ENABLE_HIGH_QUALITY_NIGHT_VISION // This makes night vision smoother (and more stable), as such, increase the grid strength to compensate
  grainTint = max(lerp(grainTint, 1.0, -1.0), 0.0); // Move it away from neutral
#endif

  o0.xyz = palettedColor.xyz * grainTint;
  o0.w = palettedColor.w;
}