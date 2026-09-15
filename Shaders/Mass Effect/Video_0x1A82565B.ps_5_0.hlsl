// Bink movie pass (YUV->RGB: planes Y=t0 U=t1 V=t2, matrix rows PsConstants[8..10], constant [11].x, alpha [11].w)
// straight onto the canvas, never through the scene passes. Restores the vanilla clamp, then a LIGHT PumboAutoHDR.

// clang-format off
#include "Includes/Common.hlsl"       // game-local: pulls GameCBuffers (LumaGameSettings VideoAutoHDR* fields) + shared Common
#include "Includes/GameBindings.hlsl" // b3/b4, the dgVoodoo masks, ApplyDgvMask
// clang-format on

// Light AutoHDR on movies. Peak kept low: Bink is low-bitrate and pushing peak amplifies block artifacts.
// PumboAutoHDR self-noops in SDR (peak == paper white), so no display branch is needed.
#ifndef ENABLE_VIDEO_AUTO_HDR
#define ENABLE_VIDEO_AUTO_HDR 1
#endif
#ifndef VIDEO_AUTO_HDR_PEAK_NITS
#define VIDEO_AUTO_HDR_PEAK_NITS 250.0
#endif

Texture2D<float4> t0 : register(t0); // Y plane
Texture2D<float4> t1 : register(t1); // U plane
Texture2D<float4> t2 : register(t2); // V plane

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);
SamplerState s2_s : register(s2);

// Rows named per pass (GameBindings rule): the same cb4 rows are DoF/grade parameters in the post chain.
#define BinkRowR     PsConstants[8]
#define BinkRowG     PsConstants[9]
#define BinkRowB     PsConstants[10]
#define BinkConstant PsConstants[11] // .x = the constant input the matrix's fourth column multiplies, .w = output alpha

// 13 interpolators, all declared in order - linkage is by REGISTER (see Luma_ME1_Tonemap.hlsl). Only TEXCOORD0 (v5.xy,
// the movie UV) is read.
void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0,
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   // YUV plane fetch + dgVoodoo mask (verbatim; all three planes share v5.xy).
   float4 yuv1;
   yuv1.x = ApplyDgvMask(t0.Sample(s0_s, v5.xy), DgvMaskT0, DgvFillT0).x;
   yuv1.y = ApplyDgvMask(t1.Sample(s1_s, v5.xy), DgvMaskT1, DgvFillT1).x;
   yuv1.z = ApplyDgvMask(t2.Sample(s2_s, v5.xy), DgvMaskT2, DgvFillT2).x;
   yuv1.w = BinkConstant.x;

   // YUV -> RGB, matrix from the constants (verbatim dp4 rows).
   float3 rgb;
   rgb.r = dot(BinkRowR, yuv1);
   rgb.g = dot(BinkRowG, yuv1);
   rgb.b = dot(BinkRowB, yuv1);

   // Restore the vanilla clamp: vanilla got it free from the 8-bit UNORM canvas, Luma's fp16 canvas clips nothing.
   rgb = saturate(rgb);

   float3 lin = gamma_to_linear(rgb);
#if ENABLE_VIDEO_AUTO_HDR
   if (LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
   {
      // boost 0 = peak at sRGB white (80 nits) -> PumboAutoHDR no-ops (off); 1 = full VIDEO_AUTO_HDR_PEAK_NITS.
      const float peakNits = lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
      lin = PumboAutoHDR(lin, peakNits, LumaSettings.GamePaperWhiteNits);
   }
#endif

   // Same pre-scale as the scene passes, so movies land at gameplay brightness. The canvas is a gamma-space buffer;
   // the composition decodes it at present.
   o0.rgb = linear_to_gamma(PreScaleForUIPaperWhite(lin));
   o0.w = BinkConstant.w; // vanilla alpha (constant)
}
