#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
// This include is needed to allow reading shader types from c++.
#include "../../../Source/Core/includes/shader_types.h"
#endif

// Mirrors c++ name spaces.
namespace CB
{
// The HDR fix's switches (HDRFix and GammaCorrection: DEVELOPMENT only toggles, always on otherwise), the movie state, the
// dithering, the grade sliders and Hide UI, read by the replaced presents (HDR and SDR). The brightnesses are Core's
// LumaSettings peak/paper/UI white; the effect sliders patch the tonemap's own constants instead (see "WrapTonemapEffects").
struct LumaGameSettings
{
   float HDRFix;             // 0 = the game's own output (1D LUT x 100 nits to PQ), 1 = Luma's
   float GammaCorrection;    // 0/1: emulate a gamma 2.2 SDR display (sRGB encode, 2.2 decode) on the scene and the UI
   float VideoActive;        // Runtime: the FMV decode (PS 0x7ED07F45) drew into the UI layer this frame
   float VideoAutoHDREnable; // 0/1: AutoHDR on the FMVs
   float VideoAutoHDRBoost;  // 0..1: its highlight expansion, 0 = none (peak at sRGB white), 1 = VIDEO_AUTO_HDR_PEAK_NITS
   float Dithering;          // 0/1: one output code of noise against banding (10 bit PQ in HDR, 8 bit sRGB in SDR)
   float Contrast;           // 1 = vanilla: around mid gray, before the display map (HDR fix only)
   float Saturation;         // 1 = vanilla: after the display map (HDR fix only)
   float HighlightDechroma;  // 0 = off: the max channel display map, 1 = per channel (HDR fix only)
   float HideUI;             // 0/1: the gameplay present leaves out the UI layer
};

// Game specific cbuffer (instance/pass) data.
struct LumaGameData
{
   float Dummy; // hlsl doesn't support empty structs
};
} // namespace CB

#endif // LUMA_GAME_CB_STRUCTS
