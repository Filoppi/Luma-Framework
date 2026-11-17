#ifndef HMA_FLARE_HLSL
#define HMA_FLARE_HLSL

// Adapted from Pretty Post Process (https://github.com/EscapeEntertainment/PrettyPostProcess @ 8bc14ff9).
// Copyright (c) 2021-2022 Escape Entertainment & Froyok. BSD-3-Clause-Clear, full text in Shaders/Hitman Absolution/LICENSE_PrettyPostProcess.txt
//
// Source map:
//   FisheyeUV            <- PrettyPostProcess.ush
//   Downsample13         <- Downsample.usf (13-tap weights/taps)
//   Ghosts / Halo        <- Ghosts.usf / Halo.usf (used by the flare Ghosts and Halo passes)
//   Glare                <- Glare.usf (VS+GS math moved into the VS)
//   SquareUV             <- aspect-square UV repeated across upstream passes
//   DiscMask             <- UE PostProcessCommon.ush

// Shared constant buffer for all HMA flare passes. Layout must match HMAFlareCBData on the C++ side (35 float4, 560 bytes).
cbuffer HMAFlareCB : register(b0)
{
  float4 OutSize;          // xy = output RT size (px), zw = 1/xy
  float4 InSize;           // xy = t0 size (px), zw = 1/xy
  float4 GhostColors[8];   // rgba
  float4 GhostScales[8];   // x = scale
  float4 GhostParams;      // x Intensity, y ChromaShift, z Compression, w unused
  float4 HaloParams;       // x Width, y Mask, z Compression, w Intensity (already * HaloChainWeight)
  float4 HaloParams2;      // x ChromaShift, yzw unused
  float4 ThresholdParams;  // x Threshold (exposed units), y Knee (0..1), zw unused
  float4 GlareParams;      // x GlareIntensity (upstream 0.02 * user mult), y GlareDivider (60), zw unused
  float4 GlareTint;        // rgba (1,1,1,1)
  float4 GlareScales;      // per blade (1,1,1,1)
  float4 GlareTile;        // xy TileCount (mip1 size/2, integer), zw BufferSize (mip1 size)
  float4 MixParams;        // x FlareIntensity (user), y GlareValid (0/1), z FadeValid (0/1), w unused
  float4 FlareTint;        // rgb (1,0.85,0.7), a unused
};

// s0 = linear clamp, s1 = linear border (border color 0). Created on the C++ side.
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

// Aspect-square UV. aspect = screen width / height.
float2 SquareUV(float2 uv, float aspect)
{
  if (aspect > 1.0)
    return float2(uv.x, (uv.y - 0.5) / aspect + 0.5);
  if (aspect < 1.0)
    return float2((uv.x - 0.5) * aspect + 0.5, uv.y);
  return uv;
}

// Fisheye warp from PrettyPostProcess.ush (verbatim math).
float2 FisheyeUV(float2 UV, float Compression, float Zoom)
{
  float2 NegPosUV = (2.0f * UV - 1.0f);

  float Scale = Compression * atan(1.0f / Compression);
  float RadiusDistance = length(NegPosUV) * Scale;
  float RadiusDirection = Compression * tan(RadiusDistance / Compression) * Zoom;
  float Phi = atan2(NegPosUV.y, NegPosUV.x);

  float2 NewUV = float2(RadiusDirection * cos(Phi) + 1.0,
                        RadiusDirection * sin(Phi) + 1.0);
  NewUV = NewUV / 2.0;

  return NewUV;
}

// Radial falloff mask. p is in [-1,1] screen space (sp = uv * 2 - 1). UE PostProcessCommon.ush formula.
float DiscMask(float2 p)
{
  float x = saturate(1.0 - dot(p, p));
  return x * x;
}

// 13-tap downsample from Downsample.usf. px = texel size of the SOURCE texture times 0.5 (upstream InPixelSize).
float3 Downsample13(Texture2D<float4> t, SamplerState s, float2 uv, float2 px)
{
  static const float2 Coords[13] =
  {
    float2(-1.0f, 1.0f), float2(1.0f, 1.0f),
    float2(-1.0f, -1.0f), float2(1.0f, -1.0f),

    float2(-2.0f, 2.0f), float2(0.0f, 2.0f), float2(2.0f, 2.0f),
    float2(-2.0f, 0.0f), float2(0.0f, 0.0f), float2(2.0f, 0.0f),
    float2(-2.0f, -2.0f), float2(0.0f, -2.0f), float2(2.0f, -2.0f)
  };

  static const float Weights[13] =
  {
    // 4 samples: (1 / 4) * 0.5
    0.125f, 0.125f,
    0.125f, 0.125f,

    // 9 samples: (1 / 9) * 0.5
    0.0555555f, 0.0555555f, 0.0555555f,
    0.0555555f, 0.0555555f, 0.0555555f,
    0.0555555f, 0.0555555f, 0.0555555f
  };

  float3 OutColor = float3(0.0f, 0.0f, 0.0f);
  [unroll]
  for (int i = 0; i < 13; i++)
  {
    float2 CurrentUV = uv + Coords[i] * px;
    OutColor += Weights[i] * t.Sample(s, CurrentUV).rgb;
  }
  return OutColor;
}

// True when all components are finite (no NaN / inf). Use to guard outputs.
bool IsFinite3(float3 v)
{
  return all(isfinite(v));
}

#endif // HMA_FLARE_HLSL
