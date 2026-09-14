// The Witcher 2 glow screen blend (dgVoodoo -> ps_5_0, hash 0x12931281). This is the pass that gives candles
// and torches their halo: it screen-blends the engine's blurred, THRESHOLDLESS copy of the scene onto the
// scene itself, weighted toward dark pixels by the blend's own shape.
// Vanilla: o0 = 1 - (1 - sat(glow)) * (1 - sat(scene)) — a classic screen blend that SATURATES both inputs,
// hard-clipping the whole frame to 0-1. It runs right after the tonemap every frame, so it was the pass
// eating the Luma HDR range.
// Replacement: bit-exact vanilla math on the saturated values (SDR look preserved), then re-add the
// clipped HDR excess on top (identity for any input <= 1). Screen blend of a source at/above 1 saturates
// toward 1 in vanilla anyway, so the excess re-add keeps the blend monotonic and hue-stable.

// Slot mapping is disasm-confirmed, and it is the opposite way round from the sibling light-shaft blend
// (0xCB06362E): here t0 is the GLOW and t1 is the SCENE. t0 is clamped by cb4[60], whose half-texel inset and
// 0..0.5 extent describe the glow buffer — a half-res texture holding its image in a quarter of itself
// (at 3840x2160 output: a 1920x1080 buffer with the image at 960x540) — while t1 is clamped by
// cb4[61], the full 0..1 rect of the scene.
// This pass also forwards the GLOW's alpha to its output, and that alpha is live data (mean 2.3),
// so anything that replaces the glow colour here has to leave t0's alpha alone.
#include "Includes/Common.hlsl" // game-local: LumaSettings (DisplayMode gates the HDR-only excess restore)

Texture2D<float4> t0 : register(t0); // engine glow
Texture2D<float4> t1 : register(t1); // scene (fp16, gamma-space; carries Luma HDR range > 1)

SamplerState s0_s : register(s0);
SamplerState s1_s : register(s1);

cbuffer cb3 : register(b3)
{
   float4 cb3[77];
}
cbuffer cb4 : register(b4)
{
   float4 cb4[236];
}

// dgVoodoo texture-format fixup masks (see Luma_TW2_Tonemap.hlsl) — transcribed verbatim.
float4 DgVoodooTexFixup(float4 color, float4 mask_and, float4 mask_or)
{
   return asfloat((asuint(color) & asuint(mask_and)) | asuint(mask_or));
}

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
   // Vanilla UV clamp rects (cb4[60] glow, cb4[61] scene: .xy min, .zw max)
   float2 uv0 = min(cb4[60].zw, max(v5.xy, cb4[60].xy));
   float4 glow = DgVoodooTexFixup(t0.Sample(s0_s, uv0), cb3[44], cb3[45]);
   float2 uv1 = min(cb4[61].zw, max(v6.xy, cb4[61].xy));
   float4 scene = DgVoodooTexFixup(t1.Sample(s1_s, uv1), cb3[46], cb3[47]);

   // User Bloom Intensity (1 = vanilla, 0 = no halo): scale the engine's glow where it enters the blend, so
   // everything downstream — the screen blend, and the HDR excess restore below, which derives from this same
   // value — follows. Only the glow COLOR is scaled: .a is forwarded to o0.w as live engine data (
   // mean 2.3), and the screen blend is not additive, so at 0 the scene is left exactly as it arrived rather
   // than brightened. Applies in SDR too: this weakens a vanilla effect, it does not shape HDR.
   glow.rgb *= LumaSettings.GameSettings.BloomIntensity;

   float3 glowSat = saturate(glow.rgb);
   float3 sceneSat = saturate(scene.rgb);
   float3 blended = 1.0 - (1.0 - glowSat) * (1.0 - sceneSat); // vanilla screen blend

#if TONEMAP_TYPE == 1
   // Re-add the range the vanilla saturates clipped (0 for any SDR input -> bit-exact vanilla).
   // HDR display path only: vanilla saturates BOTH inputs here, so its output is provably <= 1 and the SDR
   // path must see exactly that. Not redundant with the composition's SDR clamp — this pass runs BEFORE the
   // final grade, whose saturated-luma tint weights and per-channel gamma pow react non-linearly to an
   // above-1 input, so the excess would move graded values that end up BELOW 1 too. "== 1" also keeps the
   // dev "SDR on HDR" mode honest (composition only clamps for DisplayMode 0).
   [branch] if (LumaSettings.DisplayMode == 1)
       blended += max(glow.rgb - glowSat, 0.0) + max(scene.rgb - sceneSat, 0.0);
#endif

   o0 = float4(blended, glow.a); // vanilla alpha: raw t0 sample .a
}
