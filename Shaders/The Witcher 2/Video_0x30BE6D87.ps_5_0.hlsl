// The Witcher 2 — pre-rendered video (USM, YUV->RGB) pass. SDR clamp + light AutoHDR for HDR (BL2 pattern).
//
// Plays the menu background / intro / loading movies: three YUV planes (Y=t0 2048x1024, U/V=t1/t2 1024x512,
// value in .w of the dgVoodoo r8g8b8a8 plane views) converted BT.601 limited-range to RGB by a fullscreen
// quad alpha-blended straight onto the fp16 scene canvas. Menu/loading frames run NO tonemap, so on Luma's
// scRGB output this SDR video would sit flat at paper white: restore the vanilla 8-bit clamp, then apply a
// LIGHT PumboAutoHDR for a little highlight pop in HDR.
//
// Body transcribed verbatim from the dgVoodoo->ps_5_0 disasm of 0x30BE6D87 (VS 0xFC80B21A): a border fade
// (saturate(700*x) edge ramps; content is 1280x720 inside the pow2 planes -> crop at 0.625/0.703125),
// BT.601 constants baked in code (Y-16/255 then *1.164; V*1.596/0.813; U*0.392/2.017), tint/alpha cb4[10],
// additive bias cb4[11]. The cb3 and/or pairs are dgVoodoo's texture-format bit emulation, kept verbatim.
// NOTE: dgVoodoo dropped the SM3 `saturate(o)` (vanilla's 8-bit UNORM target clamped for free); the fp16
// canvas does not, so we re-add it before the AutoHDR (kills YUV overshoot + negatives).

#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (LumaGameSettings VideoAutoHDR* fields) + shared Common

// Light AutoHDR on videos (0 = off -> flat SDR at paper white). Peak kept low on purpose (the movies are
// low-bitrate; pushing peak amplifies block/compression artifacts in highlights). PumboAutoHDR self-noops
// in SDR (peak==paper).
#ifndef ENABLE_VIDEO_AUTO_HDR
#define ENABLE_VIDEO_AUTO_HDR 1
#endif
#ifndef VIDEO_AUTO_HDR_PEAK_NITS
#define VIDEO_AUTO_HDR_PEAK_NITS 250.0
#endif

Texture2D<float4> t0 : register(t0); // Y plane (value in .w)
Texture2D<float4> t1 : register(t1); // U plane (value in .w)
Texture2D<float4> t2 : register(t2); // V plane (value in .w)

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

void main(
    float4 v0 : SV_POSITION0,
    float4 v1 : TEXCOORD8,
    float4 v2 : COLOR0,
    float4 v3 : COLOR1,
    float4 v4 : TEXCOORD9,
    float4 v5 : TEXCOORD0, // movie UV (only .xy used)
    float4 v6 : TEXCOORD1,
    float4 v7 : TEXCOORD2,
    float4 v8 : TEXCOORD3,
    float4 v9 : TEXCOORD4,
    float4 v10 : TEXCOORD5,
    float4 v11 : TEXCOORD6,
    float4 v12 : TEXCOORD7,
    out float4 o0 : SV_TARGET0)
{
   // --- border fade / content crop (verbatim): full weight inside the 1280x720 content, hard ramp at edges ---
   float fade = saturate(700.0 * (0.703125 - v5.y)) * saturate(700.0 * (0.625 - v5.x)) * saturate(700.0 * v5.x) * saturate(700.0 * v5.y);
   float3 rgbScale = fade * cb4[10].xyz;

   // --- YUV plane fetch + dgVoodoo format-emulation mask (verbatim; plane value lives in .w) ---
   float4 rV = t2.Sample(s2_s, v5.xy);
   rV = asfloat((asuint(rV) & asuint(cb3[48])) | asuint(cb3[49]));
   float V = rV.w - 0.501961;
   float4 rU = t1.Sample(s1_s, v5.xy);
   rU = asfloat((asuint(rU) & asuint(cb3[46])) | asuint(cb3[47]));
   float U = rU.w - 0.501961;
   float4 rY = t0.Sample(s0_s, v5.xy);
   rY = asfloat((asuint(rY) & asuint(cb3[44])) | asuint(cb3[45]));
   float Y = (rY.w - 0.062745) * 1.164;

   // --- BT.601 limited-range -> RGB (verbatim constants) ---
   float3 rgb;
   rgb.x = Y + 1.596 * V;
   rgb.y = Y - 0.392 * U - 0.813 * V;
   rgb.z = Y + 2.017 * U;

   o0.xyz = rgbScale * rgb + cb4[11].xyz;
   o0.w = cb4[10].w + cb4[11].w; // vanilla alpha (src-alpha blend onto the canvas)

   // --- restore vanilla 8-bit clamp, then light AutoHDR ---
   o0.rgb = saturate(o0.rgb);
   float3 lin = gamma_to_linear(o0.rgb);
#if ENABLE_VIDEO_AUTO_HDR
   if (LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
   {
      // boost 0 = peak at paper white -> PumboAutoHDR no-ops (off); 1 = full VIDEO_AUTO_HDR_PEAK_NITS.
      const float peakNits = lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
      lin = PumboAutoHDR(lin, peakNits, LumaSettings.GamePaperWhiteNits);
   }
#endif
#if UI_DRAW_TYPE >= 2
   // Match the final grade's linear pre-scale: land movies at the same brightness as in-game after the
   // composition's UIPaperWhite rescale (when UIPaperWhite != GamePaperWhite).
   lin *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif
   o0.rgb = linear_to_gamma(lin); // re-encode for the gamma post buffer the composition expects
}
