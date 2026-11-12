// Define game cbuffer types before shared Common declares LumaSettings.
// clang-format off
#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
// clang-format on

// Game-local: MELE's output tail and Bink shader use these, and the shared "Settings.hlsl" does not have them.
#ifndef VIDEO_AUTO_HDR_PEAK_NITS
// Kept low on purpose: movies (e.g. Bink) are low bitrate, a higher peak exposes compression blocks in the highlights
#define VIDEO_AUTO_HDR_PEAK_NITS 250.0
#endif

// Light "PumboAutoHDR" for SDR movies (linear in and out). "Boost" 0 puts the peak at sRGB white, where PumboAutoHDR is a no-op;
// 1 is the full VIDEO_AUTO_HDR_PEAK_NITS. "PaperWhiteNits" is the white the movie lands at.
float3 VideoAutoHDR(float3 LinearColor, bool Enable, float Boost, float PaperWhiteNits)
{
   [branch] if (Enable)
   {
      LinearColor = PumboAutoHDR(LinearColor, lerp(sRGB_WhiteLevelNits, VIDEO_AUTO_HDR_PEAK_NITS, saturate(Boost)), PaperWhiteNits);
   }
   return LinearColor;
}

// Anti-banding dither of one output quantizer step: the 8 bit code in SDR, the 10 bit BT.2020 PQ code in HDR.
// "gammaSpace": whether "color" is gamma encoded (else linear). "paperWhiteNits": the nits the display composition maps 1 to,
// which places the PQ code steps (HDR only, floored at 1).
void ApplyOutputDithering(inout float3 color, float2 uv, bool gammaSpace, bool hdr, float paperWhiteNits)
{
   if (hdr)
   {
      const float pqScale = max(paperWhiteNits, 1.0) / HDR10_MaxWhiteNits;
      const float3 linearColor = gammaSpace ? gamma_to_linear(color, GCT_MIRROR) : color;
      float3 pq = Linear_to_PQ(BT709_To_BT2020(linearColor * pqScale), GCT_MIRROR);
      ApplyDithering(pq, uv, true, 1.0, 10u, LumaSettings.FrameIndex, true);
      color = BT2020_To_BT709(PQ_to_Linear(pq, GCT_MIRROR)) / pqScale;
      if (gammaSpace)
      {
         color = linear_to_gamma(color, GCT_MIRROR);
      }
   }
   else
   {
      ApplyDithering(color, uv, gammaSpace, 1.0, 8u, LumaSettings.FrameIndex, true);
   }
}

// Transport ratio R = UI Paper White / Game Paper White. Stage 1 divides the scene by it before the native gamma HUD
// blends and stage 2 multiplies it back, so the scene is restored and HUD white lands at UI Paper White.
float MELE_GetUIPaperWhiteRelativeToGame()
{
   return LumaSettings.UIPaperWhiteNits / max(LumaSettings.GamePaperWhiteNits, 1.0);
}

// Absolute scRGB scale G = Game Paper White / 80. Under EARLY_DISPLAY_ENCODING 1 the game owns it, so exactly two
// passes apply it: stage 2 and direct-to-swapchain Bink. Core's Display Composition divides it back out under the
// same define, so the two agree whether or not that pass runs.
float MELE_GetGamePaperWhiteScale()
{
   return LumaSettings.GamePaperWhiteNits / sRGB_WhiteLevelNits;
}

// Nothing contains the frame after stage 1, by design: the display's per-channel clip preserves more highlight
// detail than any luminance, max-channel, or desaturation correction, which move all three channels at once.

// Native SDR gamma curve shared by the stage-1 grade chains: scale, optional black floor, then pow(1/gamma) as
// log2/exp2. The floor differs per permutation and is not cosmetic. The ME1LE/ME2LE analytic shaders 0xAAE8755A and
// 0xCC76075F feed mul_sat straight into log, so log2(0) drives their result to 0; every LUT permutation and the
// ME3LE analytic 0x225A8330 clamp first, lifting black to 1e-4^(1/gamma), about 0.0152 at gamma 2.2. Both forms
// come from the dumped bytecode, so clampFloor stays faithful per entry point.
float3 MELE_NativeGammaCurve(float3 c, float3 scale, float invGamma, bool clampFloor)
{
   c = saturate(scale * c);
   if (clampFloor)
   {
      c = max(float3(9.99999975e-05, 9.99999975e-05, 9.99999975e-05), c);
   }
   c = log2(c);
   c = invGamma * c;
   return exp2(c);
}

// Native per-channel tone curve from the stage-1 decompiles, asymptotic to 1: F(x) = 1 - exp2(-a * x). It stays the
// exact native branch below the HDR continuation pivot, and it also supplies the domain the ME2LE staged filmic fit
// probes in. The bodies' register-level transcription of the same curve keeps its literal.
static const float kMELE_NativeToneCurveRate = 1.70000005;
float MELE_NativeToneCurve(float x)
{
   return 1.0 - exp2(-kMELE_NativeToneCurveRate * x);
}

// Native radial-vignette floors, transcribed from the stage-1 decompiles and selected through TM_VIG_FLOOR. The
// shared tail rebuilds each white point as TM_VIG_FLOOR + 1, so these carry the tint too: ME1LE nearly neutral,
// ME2LE strongly blue.
static const float3 kMELE_ME1LEVignetteFloor = float3(0.0103630004, 5.75000013e-06, 0.0130924946);
static const float3 kMELE_ME2LEVignetteFloor = float3(0.0103630004, 5.75000013e-06, 0.163092494);

// Finite guards for the stage-1 HDR reconstruction, chosen per value: a light quantity has no meaning below zero,
// while an artist dial (a shadow lift, a luminance weight, an overlay offset, and the signed differences they produce)
// has a free sign, and the non-negative form would reject it as corrupt.
//
// Ordered comparisons against constants, not the IsNaN_Strict/IsInfinite_Strict bit tests: NaN fails both (DXBC ge/le
// are ordered), +INF fails the upper bound, -INF and negatives the lower, so the rejected sets are identical, at 2
// instructions per channel instead of about 7 (the same trade Luma_BL2TPS_Tonemap.hlsl measured). Keep any negation
// OUTSIDE the conjunction: (x < lo || x > hi) is false for NaN and would accept it.
//
// COMPUTED VALUES ONLY. Without IEEE strictness (/Gis) fxc treats a raw constant-buffer read as finite and deletes
// these comparisons on it (MELE_IsFinite(SceneMidTones.xyz) was measured to vanish from the analytic listings), while
// a product, sum or quotient can overflow and keeps its comparison. Guard raw cbuffer values with the bit tests.
bool MELE_IsFinite(float x)
{
   return abs(x) <= FLT_MAX;
}
bool MELE_IsFinite(float3 v)
{
   return all(abs(v) <= FLT_MAX);
}
bool MELE_IsFiniteNonNegative(float x)
{
   return x >= 0.0 && x <= FLT_MAX;
}
bool MELE_IsFiniteNonNegative(float3 v)
{
   return all(v >= 0.0) && all(v <= FLT_MAX);
}
// A divisor or exponent that must be finite and strictly positive. A macro, not a function: fxc
// reassociates unrelated math around an inlined call (0x8C8E8CA2's exponential curve moved), never around the bare
// comparison, so the listings stay identical.
#define MELE_IS_FINITE_POSITIVE(x) ((x) > 0.0 && (x) <= FLT_MAX)
