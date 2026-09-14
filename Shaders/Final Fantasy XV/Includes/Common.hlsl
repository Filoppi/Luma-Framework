// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers
#ifndef FINAL_FANTASY_XV_COMMON_HLSL
#define FINAL_FANTASY_XV_COMMON_HLSL
// Define the game custom cbuffer structs
#include "GameCBuffers.hlsl"
// Global common
#include "../../Includes/Common.hlsl"

// #include "Settings.hlsl"

#define PEAK_NITS LumaSettings.PeakWhiteNits
#define GAME_NITS LumaSettings.GamePaperWhiteNits
#define UI_NITS LumaSettings.UIPaperWhiteNits
#define GAMMA_CORRECTION LumaSettings.GameSettings.GammaCorrection
#define EXPOSURE LumaSettings.GameSettings.Exposure
#define HIGHLIGHTS LumaSettings.GameSettings.Highlights
#define HIGHLIGHT_CONTRAST LumaSettings.GameSettings.HighlightContrast
#define SHADOWS LumaSettings.GameSettings.Shadows
#define SHADOW_CONTRAST LumaSettings.GameSettings.ShadowContrast
#define CONTRAST LumaSettings.GameSettings.Contrast
#define FLARE LumaSettings.GameSettings.Flare
#define GAMMA LumaSettings.GameSettings.Gamma
#define SATURATION LumaSettings.GameSettings.Saturation
#define DECHROMA LumaSettings.GameSettings.Dechroma
#define HIGHLIGHT_SATURATION LumaSettings.GameSettings.HighlightSaturation
#define HUE_EMULATION LumaSettings.GameSettings.HueEmulation
#define PURITY_EMULATION LumaSettings.GameSettings.PurityEmulation
#define BLOOM_TYPE LumaSettings.GameSettings.BloomType
#define BLOOM_STRENGTH LumaSettings.GameSettings.BloomStrength
#define SHARPNESS LumaSettings.GameSettings.Sharpness

#endif // FINAL_FANTASY_XV_COMMON_HLSL