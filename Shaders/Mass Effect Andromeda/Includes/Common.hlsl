// Include this instead of the shared "../Includes/Common.hlsl" from any shader that reads "LumaGameSettings". It defines
// "LUMA_GAME_CB_STRUCTS" through "GameCBuffers.hlsl" before the shared "Settings.hlsl" declares the "LumaSettings" cbuffer, so
// "GameSettings" is this game's struct rather than the empty default.

#include "GameCBuffers.hlsl"
// Keep this after "GameCBuffers.hlsl". The comment also stops clang-format from sorting the includes.
#include "../../Includes/CInfinityRolloff.hlsl"
#include "../../Includes/Common.hlsl"

// Kept low on purpose, as the movies are low bitrate and a higher peak exposes compression blocks in the highlights.
#define VIDEO_AUTO_HDR_PEAK_NITS 250.0

// Light "PumboAutoHDR" for SDR movies (linear in and out). "Boost" 0 puts the peak at sRGB white, where PumboAutoHDR is a no-op;
// 1 is the full "VIDEO_AUTO_HDR_PEAK_NITS". "PaperWhiteNits" is the white the movie lands at.
float3 VideoAutoHDR(float3 LinearColor, bool Enable, float Boost, float PaperWhiteNits)
{
   [branch] if (Enable)
   {
      LinearColor = PumboAutoHDR(LinearColor, lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(Boost)), PaperWhiteNits);
   }
   return LinearColor;
}

// The game's HDR10 output and the HDR fix on it, ported from RenoDX's Mass Effect: Andromeda mod (its "Vanilla+" tone mapper,
// https://github.com/clshortfuse/renodx). The tone curve lives in the game's 33^3 grade LUT (the tonemap pass, untouched): its
// PQ-encoded output goes through the present's 1D output LUT to scene-linear (1 = diffuse white), which the game sends to PQ at a
// fixed 100 nits. The fix maps that to the display: paper white, an optional gamma 2.2 emulation, and a C-infinity rolloff to the
// peak, then the UI at its own white.

// Luma's Display Mode SDR leaves the game's own HDR output (the game, not Luma, owns HDR on/off)
bool HDRFixEnabled()
{
   return LumaSettings.GameSettings.HDRFix != 0.0 && LumaSettings.DisplayMode == 1;
}

// The game's own output puts diffuse and UI white at 100 nits
float SceneWhiteNits()
{
   return HDRFixEnabled() ? max(LumaSettings.GamePaperWhiteNits, 1.0) : 100.0;
}

float UIWhiteNits()
{
   return HDRFixEnabled() ? max(LumaSettings.UIPaperWhiteNits, 1.0) : 100.0;
}

bool GammaCorrectionEnabled()
{
   return HDRFixEnabled() && LumaSettings.GameSettings.GammaCorrection != 0.0;
}

// The look of an SDR display that decodes the sRGB-encoded image with gamma 2.2, sign preserving
float3 EmulateGamma22(float3 color)
{
   return gamma_to_linear(linear_to_sRGB_gamma(color, GCT_MIRROR), GCT_MIRROR, 2.2);
}

// Scene-linear (1 = diffuse white) to the display, in paper white units. This is a C-infinity rolloff (identity below 0.18,
// asymptotic to the peak) on the max channel, which keeps the grade LUT's RGB ratios, as in the MGR:R and Bayonetta mods (with
// strength 1 the paper white stays within 1%). "Highlights Desaturation" blends toward the same rolloff per channel, which whitens
// what it compresses. Contrast and saturation come before the map, so it contains what they push up.
float3 DisplayMapScene(float3 color)
{
   if (!HDRFixEnabled())
      return color;
   if (GammaCorrectionEnabled())
   {
      color = EmulateGamma22(color);
   }
   [branch] if (LumaSettings.GameSettings.Contrast != 1.0)
   {
      const float mid_gray = 0.18;
      color = exp2(LumaSettings.GameSettings.Contrast * log2(max(color / mid_gray, 1e-30))) * mid_gray;
   }
   color = Saturation(color, LumaSettings.GameSettings.Saturation);
   // The peak can't go below paper white (the rolloff needs it above its 0.18 anchor)
   const float paper_white = SceneWhiteNits();
   const float peak = max(LumaSettings.PeakWhiteNits, paper_white) / paper_white;
   const float3 mapped = Tonemap::CInfinityRollOff::MaxChannel(color, peak, 0.18, 1.0);
   [branch] if (LumaSettings.GameSettings.HighlightDechroma > 0.0)
   {
      return lerp(mapped, Tonemap::CInfinityRollOff::Apply(color, peak, 0.18, 1.0), LumaSettings.GameSettings.HighlightDechroma);
   }
   return mapped;
}

// The scene with the UI over it, to HDR10. "ui" is linear with its white at 1, "cb2" is the present's "cbData[2]" (.y gates the UI,
// .z scales its alpha). This is the game's composite: the UI is premultiplied and the scene is weighted by the square of its
// inverse coverage.
float4 OutputHDR10(float3 scene, float scene_alpha, float4 ui, float4 cb2, float2 uv)
{
   float3 nits = max(DisplayMapScene(scene) * SceneWhiteNits(), 0.0);
   if (cb2.y > 0.0)
   {
      const float t = max(0.0, 1.0 - ui.a * cb2.z);
      nits = nits * (t * t) + ui.rgb * UIWhiteNits();
      scene_alpha = scene_alpha * (t * t) + 1.0 - (t * t);
   }
   float3 bt2020_nits = BT709_To_BT2020(nits);
   // The scene is within the peak already, so this only clamps a UI brighter than it
   if (HDRFixEnabled())
   {
      bt2020_nits = min(bt2020_nits, LumaSettings.PeakWhiteNits);
   }
   float3 pq = Linear_to_PQ(max(bt2020_nits, 0.0) / HDR10_MaxWhiteNits);
   if (HDRFixEnabled() && LumaSettings.GameSettings.Dithering != 0.0)
   {
      ApplyDithering(pq, uv, true, 1.0, 10u, LumaSettings.FrameIndex, true);
   }
   return float4(pq, scene_alpha);
}

// The game's SDR output (8 bit sRGB, in-game HDR off): only the dithering is added, at one 8 bit code
float4 OutputSDR(float3 srgb, float alpha, float2 uv)
{
   srgb = saturate(srgb);
   if (LumaSettings.GameSettings.Dithering != 0.0)
   {
      ApplyDithering(srgb, uv, true, 1.0, 8u, LumaSettings.FrameIndex, true);
   }
   return float4(srgb, alpha);
}

// The vanilla upscale front-ends of the presents (Resolution Scale below 100%), cb0[0] = (source size, 1 / source size). They
// return the raw resampled texel, decoded by the caller.

// Keys cubic convolution with a = -1 (not Catmull-Rom's -0.5): interpolating, and its weights sum to 1, so the vanilla shader's
// division by their sum is left out
float4 KeysCubicWeights(float t)
{
   const float t2 = t * t;
   const float t3 = t2 * t;
   return float4(-t3 + 2.0 * t2 - t, t3 - 2.0 * t2 + 1.0, -t3 + t2 + t, t3 - t2);
}

// 16 taps, separable
float3 SampleKeysCubic(Texture2D<float4> tex, SamplerState smp, float2 uv, float4 size)
{
   const float2 f = frac(uv * size.xy - 0.5);
   const float2 base = uv - (1.0 + f) * size.zw;
   const float4 xs = base.x + float4(0.0, 1.0, 2.0, 3.0) * size.z;
   const float4 ys = base.y + float4(0.0, 1.0, 2.0, 3.0) * size.w;
   const float4 wx = KeysCubicWeights(f.x);
   const float4 wy = KeysCubicWeights(f.y);
   float3 result = 0.0;
   [unroll] for (int j = 0; j < 4; ++j)
   {
      result += wy[j] * (wx.x * tex.SampleLevel(smp, float2(xs.x, ys[j]), 0).rgb + wx.y * tex.SampleLevel(smp, float2(xs.y, ys[j]), 0).rgb +
                         wx.z * tex.SampleLevel(smp, float2(xs.z, ys[j]), 0).rgb + wx.w * tex.SampleLevel(smp, float2(xs.w, ys[j]), 0).rgb);
   }
   return max(result, 0.0);
}

// 4 taps, vertical only: the sampler's bilinear filter resamples x
float3 SampleKeysCubicY(Texture2D<float4> tex, SamplerState smp, float2 uv, float4 size)
{
   const float f = frac(uv.y * size.y - 0.5);
   const float base = uv.y - (1.0 + f) * size.w;
   const float4 w = KeysCubicWeights(f);
   const float3 result = w.x * tex.SampleLevel(smp, float2(uv.x, base), 0).rgb + w.y * tex.SampleLevel(smp, float2(uv.x, base + size.w), 0).rgb +
                         w.z * tex.SampleLevel(smp, float2(uv.x, base + 2.0 * size.w), 0).rgb + w.w * tex.SampleLevel(smp, float2(uv.x, base + 3.0 * size.w), 0).rgb;
   return max(result, 0.0);
}

// Uniform cubic B-spline in 4 bilinear taps (each pair of taps folded into one offset fetch, so the sampler must be bilinear).
// The pair offsets are mirrored against the canonical placement, as in the vanilla shader: unbiased but blurrier, kept to match it.
float4 SampleBSpline(Texture2D<float4> tex, SamplerState smp, float2 uv, float4 size)
{
   const float2 f = frac(uv * size.xy - 0.5);
   const float2 f2 = f * f;
   const float2 f3 = f2 * f;
   const float2 w_m1 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0;
   const float2 w_0 = 2.0 / 3.0 - f2 + 0.5 * f3;
   const float2 w_1 = 1.0 / 6.0 + 0.5 * f + 0.5 * f2 - 0.5 * f3;
   const float2 w_2 = f3 / 6.0;
   const float2 g_0 = w_m1 + w_0;
   const float2 uv_a = uv + (1.0 + f - w_0 / g_0) * size.zw;
   const float2 uv_b = uv - (1.0 - f + w_2 / (w_1 + w_2)) * size.zw;
   const float4 column_b = lerp(tex.SampleLevel(smp, uv_b, 0), tex.SampleLevel(smp, float2(uv_b.x, uv_a.y), 0), g_0.y);
   const float4 column_a = lerp(tex.SampleLevel(smp, float2(uv_a.x, uv_b.y), 0), tex.SampleLevel(smp, uv_a, 0), g_0.y);
   return max(lerp(column_b, column_a, g_0.x), 0.0);
}
