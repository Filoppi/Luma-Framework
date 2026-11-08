// Bink movie pass: converts Y'CbCr (YTex, CrCbTex) to R'G'B' straight into the swapchain. The matrix overshoots above 1 and gives
// small negatives, which vanilla's 8-bit UNORM backbuffer clamped and Luma's fp16 one keeps: saturate() restores the clamp. In HDR,
// an optional light PumboAutoHDR adds highlights on top, kept conservative because the low-bitrate movies' compression artifacts
// blow up when pushed.

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (VideoAutoHDR* fields) BEFORE shared Settings
// clang-format on

// 0 compiles the AutoHDR out (flat SDR at paper white).
#ifndef ENABLE_VIDEO_AUTO_HDR
#define ENABLE_VIDEO_AUTO_HDR 1
#endif

cbuffer _Globals : register(b0)
{
   float4 cmatrix[4] : packoffset(c0);
   float4 alpha_mult : packoffset(c4);
   float4 hdr : packoffset(c5);
   float4 ctcp : packoffset(c6);
}

SamplerState YTexSampler_s : register(s0);
SamplerState CrCbTexSampler_s : register(s1);
Texture2D<float4> YTex : register(t0);
Texture2D<float4> CrCbTex : register(t1);

void main(
    float2 v0 : TEXCOORD0,
    float4 v1 : SV_Position0,
    out float4 o0 : SV_Target0)
{
   float4 r0, r1;

   r0.x = YTex.Sample(YTexSampler_s, v0.xy).x;
   r0.yz = CrCbTex.Sample(CrCbTexSampler_s, v0.xy).xy;
   r1.xyz = cmatrix[0].xyz * r0.yyy;
   r0.xyw = r0.xxx * cmatrix[3].xyz + r1.xyz;
   r0.xyz = r0.zzz * cmatrix[1].xyz + r0.xyw;
   r0.xyz = cmatrix[2].xyz + r0.xyz;
   r0.w = 1;
   o0.xyzw = alpha_mult.xyzw * r0.xyzw;
   o0.rgb = saturate(o0.rgb); // The vanilla UNORM clamp (see the header)

   // AutoHDR and the paper white pre-scale both belong in linear: the composition decodes gamma and then multiplies by UIPaperWhite,
   // so the pre-scale must compensate that multiply before the re-encode (in gamma it diverges when GamePaperWhite != UIPaperWhite).
   float3 lin = gamma_to_linear(o0.rgb);
#if ENABLE_VIDEO_AUTO_HDR
   [branch] if (LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
   {
      // Boost 0 puts the peak at sRGB white, where PumboAutoHDR is a no-op. The 250 nit peak is kept low on purpose: a higher one exposes
      // the low bitrate movies' compression blocks in the highlights.
      lin = PumboAutoHDR(lin, lerp(sRGB_WhiteLevelNits, 250.0, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost)), LumaSettings.GamePaperWhiteNits);
   }
#endif
   // Full-screen movies land at the in-game brightness after the composition's UIPaperWhite rescale
   lin *= GetUIPaperWhitePreScale(LumaSettings.GamePaperWhiteNits, LumaSettings.UIPaperWhiteNits);
   o0.rgb = linear_to_gamma(lin); // Gamma, as the tonemap pass stores it
   return;
}
