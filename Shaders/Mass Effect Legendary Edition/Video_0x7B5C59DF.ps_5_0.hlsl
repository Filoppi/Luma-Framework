// Trilogy-wide Bink YUV-to-RGB pass with optional HDR highlight expansion. The YUV matrix is transcribed from
// 0x7B5C59DF; the native 8-bit clamp is restored before decoding because fp16 targets do not clamp YUV overshoot.
//
// Intermediate draws stay gamma for stage 2. Direct swapchain draws emit linear scRGB once stage 2 has
// established a linear frame, and otherwise stay gamma for the final composition decode; C++ supplies both
// states. Bypassing stage 2 makes the linear path the other pass that applies Game Paper White itself.

// clang-format off
#include "Includes/Common.hlsl"   // Defines game settings; keep first.
#include "../Includes/Color.hlsl" // Gamma transfer helpers and reference white.
// clang-format on

// Keep the peak conservative because stronger expansion exposes Bink compression. Relative to UI white,
// boost 0, 0.5, and 1 target 1x, 2.0625x, and 3.125x before scene/display peak containment.
#ifndef ENABLE_VIDEO_AUTO_HDR
#define ENABLE_VIDEO_AUTO_HDR 1
#endif
#ifndef VIDEO_AUTO_HDR_PEAK_NITS
#define VIDEO_AUTO_HDR_PEAK_NITS 250.0
#endif

Texture2D<float4> tex0 : register(t0); // Y plane
Texture2D<float4> tex1 : register(t1); // Cr plane
Texture2D<float4> tex2 : register(t2); // Cb plane

SamplerState tex0Sampler_s : register(s0);
SamplerState tex1Sampler_s : register(s1);
SamplerState tex2Sampler_s : register(s2);

cbuffer _Globals : register(b0)
{
   float4 crc;    // Cr coefficients
   float4 cbc;    // Cb coefficients
   float4 adj;    // chroma re-centering offset
   float4 yscale; // Y (luma) scale
   float4 consts; // .w = alpha
}

void main(
    float2 v0 : TEXCOORD0,
    out float4 o0 : SV_Target0)
{
   // YUV-to-RGB matrix from 0x7B5C59DF.
   float y = tex0.Sample(tex0Sampler_s, v0.xy).x;
   float cr = tex1.Sample(tex1Sampler_s, v0.xy).x;
   float cb = tex2.Sample(tex2Sampler_s, v0.xy).x;
   float3 rgb = cr * crc.xyz + y * yscale.xyz + cb * cbc.xyz + adj.xyz;
   o0.w = consts.w; // Preserve alpha for in-world video surfaces.

   const bool hdr = LumaSettings.DisplayMode == 1;
   const bool on_swapchain = LumaSettings.GameSettings.VideoOnSwapchain > 0.5;
   const bool swapchain_gamma_encoded = LumaData.GameData.SwapchainGammaEncoded > 0.5;

   // Restore the native clamp in every mode. Whether the decode is observable depends on the target.
   const float3 clamped = saturate(rgb);

   // Intermediate video and a native-SDR swapchain stay gamma for the remaining final decode.
   const bool emit_linear = on_swapchain && !swapchain_gamma_encoded;
#if ENABLE_VIDEO_AUTO_HDR
   const bool auto_hdr = hdr && LumaSettings.GameSettings.VideoAutoHDREnable > 0.5; // Exact HDR mode only.
#else
   const bool auto_hdr = false;
#endif

   // Both selectors are cbuffer-uniform, so this branch is free. Skipping the decode/re-encode round trip on a
   // gamma target is an exact identity and avoids the transcendental error the two paths would otherwise differ by.
   [branch] if (!auto_hdr && !emit_linear)
   {
      o0.xyz = clamped;
   }
   else
   {
      // Decode exactly once; the target below decides how it is re-encoded.
      float3 lin = gamma_to_linear(clamped, GCT_MIRROR);

#if ENABLE_VIDEO_AUTO_HDR
      [branch] if (auto_hdr)
      {
         // Linear 1 lands at UI Paper White after relative transport and final composition.
         const float peakNits = lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(LumaSettings.GameSettings.VideoAutoHDRBoost));
         lin = PumboAutoHDR(lin, peakNits, LumaSettings.UIPaperWhiteNits);
      }
#endif

      // The linear branch writes the swapchain directly, so it carries both the transport ratio and Game Paper
      // White, exactly as stage 2 does; video white then lands at UI Paper White.
      const float directScale = MELE_GetUIPaperWhiteRelativeToGame() * MELE_GetGamePaperWhiteScale();
      o0.xyz = emit_linear ? (lin * directScale) : linear_to_gamma(lin, GCT_MIRROR);
   }
}
