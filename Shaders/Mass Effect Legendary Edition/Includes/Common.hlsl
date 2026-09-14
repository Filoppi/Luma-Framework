// Define game cbuffer types before shared Common declares LumaSettings.
// clang-format off
#include "GameCBuffers.hlsl"
#include "../../Includes/Common.hlsl"
// clang-format on

// Transport ratio R = UI Paper White / Game Paper White. Stage 1 divides by it before the native gamma HUD
// blends, so UI-authored content stays relative to the Game Paper White scale applied further down.
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
// log2/exp2. The floor differs per permutation and is not cosmetic. The ME1/ME2 analytic shaders 0xAAE8755A and
// 0xCC76075F feed mul_sat straight into log, so log2(0) drives their result to 0; every LUT permutation and the
// ME3 analytic 0x225A8330 clamp first, lifting black to 1e-4^(1/gamma), about 0.0117 at gamma 2.2. Both forms
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

// Native per-channel tone curve from the stage-1 decompiles, asymptotic to 1; the non-filmic and analytic HDR wraps invert it exactly to measure the max-channel compression.
float MELE_NativeToneCurve(float x)
{
   return 1.0 - exp2(-1.70000005 * x);
}

// Native radial-vignette floors, transcribed from the stage-1 decompiles and selected through TM_VIG_FLOOR. The
// shared tail rebuilds each white point as TM_VIG_FLOOR + 1, so these carry the tint too: ME1 nearly neutral,
// ME2 strongly blue.
static const float3 kMELE_ME1VignetteFloor = float3(0.0103630004, 5.75000013e-06, 0.0130924946);
static const float3 kMELE_ME2VignetteFloor = float3(0.0103630004, 5.75000013e-06, 0.163092494);
