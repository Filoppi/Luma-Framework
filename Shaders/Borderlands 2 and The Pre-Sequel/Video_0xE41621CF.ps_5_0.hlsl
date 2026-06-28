// Borderlands 2 — Bink intro/cutscene video (YUV->RGB) pass. SDR clamp + light AutoHDR for HDR.
//
// BL2's pre-rendered movies (2K/Gearbox/Unreal boot logos, story FMVs) decode to 3 YUV planes (Y=t0, U=t1,
// V=t2) and a fullscreen quad converts them to RGB straight onto the swapchain — bypassing the scene tonemap
// (0xD00AA2A7). On Luma's fp16 scRGB swapchain that SDR output would sit flat at paper white. So: restore the
// vanilla clamp, then apply a LIGHT PumboAutoHDR so movies gain a little highlight pop in HDR.
//
// Body transcribed VERBATIM from the live dgVoodoo->ps_5_0 disasm of 0xE41621CF (maps the DX9 video shader
// 0x33244F80: tor/tog/tob/consts c0..c3 -> cb4[8..11], same cb4[N+8] mapping as the tonemap). The cb3 and/or
// pairs are dgVoodoo's texture-format bit emulation (mask+set), kept exactly via asuint/asfloat.
// NOTE: dgVoodoo dropped the SM3 `saturate(o)` (the vanilla 8-bit UNORM backbuffer clamped for free); the fp16
// swapchain does not, so we re-add it before the AutoHDR (kills YUV overshoot + negatives).

#include "../Includes/Common.hlsl"

// Light AutoHDR on videos (0 = off -> flat SDR at paper white). Peak kept low on purpose (Bink is low-bitrate;
// pushing peak amplifies block/compression artifacts in highlights). PumboAutoHDR self-noops in SDR (peak==paper).
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
   float4 r0, r1;

   // --- YUV plane fetch + dgVoodoo format-emulation mask (verbatim) ---
   r0 = t0.Sample(s0_s, v5.xy);
   r0 = asfloat((asuint(r0) & asuint(cb3[44])) | asuint(cb3[45]));
   r1 = t1.Sample(s1_s, v5.xy);
   r1 = asfloat((asuint(r1) & asuint(cb3[46])) | asuint(cb3[47]));
   r0.y = r1.x;
   r1 = t2.Sample(s2_s, v5.xy);
   r1 = asfloat((asuint(r1) & asuint(cb3[48])) | asuint(cb3[49]));
   r0.z = r1.x;
   r0.w = cb4[11].x;

   // --- YUV -> RGB matrix (verbatim: c0/c1/c2 = tor/tog/tob -> cb4[8..10], consts -> cb4[11]) ---
   o0.x = dot(cb4[8], r0);
   o0.y = dot(cb4[9], r0);
   o0.z = dot(cb4[10], r0);
   o0.w = cb4[11].w; // vanilla alpha

   // --- restore vanilla 8-bit clamp, then light AutoHDR ---
   o0.rgb = saturate(o0.rgb);
   float3 lin = gamma_to_linear(o0.rgb);
#if ENABLE_VIDEO_AUTO_HDR
   lin = PumboAutoHDR(lin, VIDEO_AUTO_HDR_PEAK_NITS, LumaSettings.GamePaperWhiteNits);
#endif
#if UI_DRAW_TYPE >= 2
   // Match the tonemap's linear pre-scale: land movies at the same brightness as in-game after the
   // composition's UIPaperWhite rescale (when UIPaperWhite != GamePaperWhite).
   lin *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif
   o0.rgb = linear_to_gamma(lin); // re-encode for the gamma post buffer the composition expects
}
