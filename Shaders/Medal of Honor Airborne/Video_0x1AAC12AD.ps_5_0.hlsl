// Medal of Honor: Airborne — Bink movie pass (YUV->RGB, VS 0x4A36949B). SDR clamp + light AutoHDR for HDR.
//
// The game's pre-rendered movies (EA/Danger Close logos, mission FMVs) decode to three YUV planes (Y=t0, U=t1,
// V=t2) and one fullscreen quad converts them to RGB straight onto the canvas, NEVER touching the scene passes
// (UberPostProcessBlend 0xB9548800 / FGammaCorrection 0x52B868E0). So on Luma's HDR path a movie would sit flat
// at paper white while everything else has highlights. Fix: restore the vanilla clamp, then a LIGHT
// PumboAutoHDR. Same shape as the sibling BL2/TPS port (Video_0xE41621CF), which runs the same wrapper.
//
// ONE PERMUTATION, audited rather than assumed: the constant fingerprint (1.164383 / 1.596027 / 2.017232 /
// -0.391762 / -0.812968) has exactly one hit across the merged shader dumps, and a raw byte scan for
// 1.164383f over every dumped CSO agrees — including the handful the disassembler had skipped. No BT.709 and no
// full-range BT.601 coefficients exist anywhere in the dump, so there is no second colour-matrix variant. The
// hash also never appears in a gameplay frame, so it is movie-exclusive and not a generic textured quad.
//
// Body transcribed from the dgVoodoo->ps_5_0 disasm of 0x1AAC12AD. The cb3 and/or pairs are dgVoodoo's texture
// format bit emulation (mask+set), kept exactly via asuint/asfloat.
// TWO details differ from BL2 and both matter: the tint/alpha come from the VERTEX COLOUR (v2), not a cbuffer
// row, and the vanilla `saturate` sits on only ONE of the two gamma branches (see below).

#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaGameSettings VideoAutoHDR* fields) + shared Common

// Light AutoHDR on movies (0 = off -> flat SDR at paper white). Peak kept low on purpose: Bink is low-bitrate and
// pushing peak amplifies block artifacts. PumboAutoHDR self-noops in SDR (peak == paper white), so no display branch.
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

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

// Full 13-entry interpolator layout, declared in order even where unread: linkage is by REGISTER (see
// Luma_MOHA_Tonemap.hlsl). Only COLOR0 (v2, centroid: tint + alpha) and TEXCOORD0 (v5.xy, the movie UV) are read.
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
   float4 r0;

   // --- YUV plane fetch + dgVoodoo format-emulation mask (verbatim; all three planes share v5.xy) ---
   float3 yuv;
   r0 = t0.Sample(s0_s, v5.xy);
   r0 = asfloat((asuint(r0) & asuint(cb3[44])) | asuint(cb3[45]));
   yuv.x = r0.x - 0.0625; // Y, limited range (16/256)
   r0 = t1.Sample(s1_s, v5.xy);
   r0 = asfloat((asuint(r0) & asuint(cb3[46])) | asuint(cb3[47]));
   yuv.y = r0.x - 0.5; // U
   r0 = t2.Sample(s2_s, v5.xy);
   r0 = asfloat((asuint(r0) & asuint(cb3[48])) | asuint(cb3[49]));
   yuv.z = r0.x - 0.5; // V

   // --- YUV -> RGB, BT.601 limited range (verbatim literals; green is the only 3-term row) ---
   float3 rgb;
   rgb.r = dot(float2(1.164383, 1.596027), yuv.xz);
   rgb.g = dot(float3(1.164383, -0.391762, -0.812968), yuv.xyz);
   rgb.b = dot(float2(1.164383, 2.017232), yuv.xy);

   rgb *= v2.xyz; // tint from the vertex colour (movie fades ride on this)

   // --- gamma branch, verbatim: the pow is bypassed when the exponent is EXACTLY 1 ---
   const float gammaExp = cb4[8].x;
   rgb = (gammaExp == 1.0) ? rgb : pow(saturate(rgb), gammaExp);

   // Restore the vanilla clamp. Vanilla got it free from an 8-bit UNORM canvas and the original only saturates inside
   // the pow branch, so the ROP clipped YUV overshoot on the bypass branch. Luma's fp16 canvas clips nothing.
   rgb = saturate(rgb);

   float3 lin = gamma_to_linear(rgb);
#if ENABLE_VIDEO_AUTO_HDR
   if (LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
   {
      // boost 0 = peak at paper white -> PumboAutoHDR no-ops (off); 1 = full VIDEO_AUTO_HDR_PEAK_NITS.
      const float peakNits = lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
      lin = PumboAutoHDR(lin, peakNits, LumaSettings.GamePaperWhiteNits);
   }
#endif
#if UI_DRAW_TYPE >= 2
   // Match the scene passes' pre-scale (Luma_MOHA_Tonemap.hlsl) so movies land at gameplay brightness after
   // composition rescales by UIPaperWhite. Guarded the same way: a zero GamePaperWhiteNits scales the movie to black.
   if (LumaSettings.GamePaperWhiteNits > 0.0)
      lin *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif

   o0.rgb = linear_to_gamma(lin); // the canvas is a gamma-space buffer; the composition decodes it at present
   o0.w = v2.w;                   // vanilla alpha (vertex colour)
}
