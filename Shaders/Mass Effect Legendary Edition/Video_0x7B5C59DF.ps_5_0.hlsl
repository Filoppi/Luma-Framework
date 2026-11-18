// Trilogy-wide Bink Y'CbCr-to-R'G'B' pass with optional HDR highlight expansion, matrix transcribed from 0x7B5C59DF.
// The native 8-bit clamp is restored before decoding: fp16 targets do not clamp the out-of-range R'G'B' it produces.
//
// Intermediate draws stay gamma for stage 2. Direct swapchain draws follow the topology fixed in OnInit: linear scRGB
// on native HDR (POST_PROCESS_SPACE_TYPE 1, whose fp16 swapchain is linear and has no composition decode after it),
// gamma on native SDR (Core's composition decodes the whole frame). Bypassing stage 2 makes the linear path the other
// pass that applies Game Paper White itself. C++ reports per draw whether the target is a back buffer.

// clang-format off
#include "Includes/Common.hlsl"   // Defines game settings; keep first.
#include "../Includes/Color.hlsl" // Gamma transfer helpers and reference white.
// clang-format on

// Keep the peak conservative because stronger expansion exposes Bink compression. Relative to UI white,
// boost 0, 0.5, and 1 target 1x, 2.0625x, and 3.125x before scene/display peak containment.
#ifndef ENABLE_VIDEO_AUTO_HDR
#define ENABLE_VIDEO_AUTO_HDR 1
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
   float4 yscale; // Y' (luma) scale
   float4 consts; // .w = alpha
}

void main(
    float2 v0 : TEXCOORD0,
    out float4 o0 : SV_Target0)
{
   float y = tex0.Sample(tex0Sampler_s, v0.xy).x;
   float cr = tex1.Sample(tex1Sampler_s, v0.xy).x;
   float cb = tex2.Sample(tex2Sampler_s, v0.xy).x;
   float3 rgb = cr * crc.xyz + y * yscale.xyz + cb * cbc.xyz + adj.xyz;
   o0.w = consts.w; // Preserve alpha for in-world video surfaces.

   const bool hdr = LumaSettings.DisplayMode == 1;
   const bool onSwapchain = LumaSettings.GameSettings.VideoOnSwapchain > 0.5;

   // The native 8-bit clamp, restored in every mode (see the header).
   const float3 clamped = saturate(rgb);

   const bool emitLinear = onSwapchain && POST_PROCESS_SPACE_TYPE == 1;
#if ENABLE_VIDEO_AUTO_HDR
   const bool autoHDR = hdr && LumaSettings.GameSettings.VideoAutoHDREnable > 0.5; // Exact HDR mode only.
#else
   const bool autoHDR = false;
#endif

   // Both selectors are cbuffer-uniform, so this branch is free. On a gamma target, skipping the decode/re-encode
   // round trip keeps the output exact, without its transcendental error.
   [branch] if (!autoHDR && !emitLinear)
   {
      o0.xyz = clamped;
   }
   else
   {
      float3 lin = gamma_to_linear(clamped, GCT_MIRROR);

#if ENABLE_VIDEO_AUTO_HDR
      // Linear 1 lands at UI Paper White once the transport ratio and Game Paper White are applied.
      lin = VideoAutoHDR(lin, autoHDR, LumaSettings.GameSettings.VideoAutoHDRBoost, LumaSettings.UIPaperWhiteNits);
#endif

      // Linear output carries the transport ratio and Game Paper White like stage 2, so video white lands at UI Paper White.
      const float directScale = MELE_GetUIPaperWhiteRelativeToGame() * MELE_GetGamePaperWhiteScale();
      o0.xyz = emitLinear ? (lin * directScale) : linear_to_gamma(lin, GCT_MIRROR);
   }
}
