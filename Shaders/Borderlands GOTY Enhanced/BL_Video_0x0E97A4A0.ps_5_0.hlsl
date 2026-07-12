// Borderlands GOTY Enhanced — video (YUV->RGB) pass. SDR clamp + light AutoHDR for HDR.
//
// The game's movie pass converts YUV to RGB and writes straight to the swapchain. The YUV->RGB matrix
// overshoots (>1) and produces small negatives; on the vanilla 8-bit UNORM backbuffer those were clamped
// for free, but Luma's fp16 scRGB swapchain keeps them — so we saturate() to restore the vanilla clamp.
// Then apply a LIGHT PumboAutoHDR so SDR movies gain a little highlight pop in HDR instead of sitting flat at
// paper white.
// Kept conservative (videos are low-bitrate, highlight compression artifacts blow up if pushed hard).

// clang-format off
#include "Includes/Common.hlsl" // game-local: pulls GameCBuffers (VideoAutoHDR* fields) BEFORE shared Settings
// clang-format on

// Light AutoHDR on videos (0 = off → flat SDR at paper white). Peak kept low on purpose.
#ifndef ENABLE_VIDEO_AUTO_HDR
#define ENABLE_VIDEO_AUTO_HDR 1
#endif
#ifndef VIDEO_AUTO_HDR_PEAK_NITS
#define VIDEO_AUTO_HDR_PEAK_NITS 250.0
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
   o0.rgb = saturate(o0.rgb); // restore the vanilla 8-bit backbuffer clamp (kills YUV overshoot + negatives)

   // Work in linear: AutoHDR (optional) and the paper-white pre-scale both belong in linear space. The
   // composition decodes gamma then multiplies by UIPaperWhite (linear), so the pre-scale must pre-compensate
   // that linear multiply BEFORE re-encoding — applying it in gamma space diverges for GamePaperWhite!=UIPaperWhite.
   float3 lin = gamma_to_linear(o0.rgb);
#if ENABLE_VIDEO_AUTO_HDR
   // Video is gamma-encoded SDR; expand highlights mildly for HDR. Runtime-gated (ImGui "Video AutoHDR"):
   // boost 0 = peak at paper white -> PumboAutoHDR no-ops (off); 1 = full VIDEO_AUTO_HDR_PEAK_NITS.
   if (LumaSettings.GameSettings.VideoAutoHDREnable > 0.5)
   {
      const float peakNits = lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
      lin = PumboAutoHDR(lin, peakNits, LumaSettings.GamePaperWhiteNits);
   }
#endif
#if UI_DRAW_TYPE >= 2
   // Match the tonemap pass (linear pre-scale, see Luma_BL_Tonemap.hlsl): land full-screen movies at the same
   // brightness as in-game after the composition's UIPaperWhite rescale, when UIPaperWhite != GamePaperWhite.
   lin *= LumaSettings.GamePaperWhiteNits / max(LumaSettings.UIPaperWhiteNits, 1.0);
#endif
   o0.rgb = linear_to_gamma(lin); // re-encode for the gamma post buffer
   return;
}
