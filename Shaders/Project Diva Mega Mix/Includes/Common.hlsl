// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers

// Define the game custom cbuffer structs
#include "GameCBuffers.hlsl"
// Global common
#include "../../Includes/Common.hlsl"
// Game specific settings
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings

/*

#include "./Includes/Common.hlsl"
if (!GS.IsHud) discard;

#include "./common1.hlsl"

*/

#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING  GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits

#define SR_ENABLE !(LumaSettings.SRType > 1)
// #define SR_ENABLE !(LumaSettings.SRType < 0)

