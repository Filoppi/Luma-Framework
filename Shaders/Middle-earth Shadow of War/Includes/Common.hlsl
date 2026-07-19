// Always include this instead of the global "Common.hlsl" if you made any changes to the game shaders/cbuffers

// Define the game custom cbuffer structs
#include "GameCBuffers.hlsl"
// Global common
#include "../../Includes/Common.hlsl"
// Game specific settings
#include "Settings.hlsl"

#define GS LumaSettings.GameSettings

float4 sRGB_Encode(float4 x) {return x < 0.0031308f ? x * 12.92f : 1.055f * pow(x, 1.f / 2.4f) - 0.055f;}
float3 sRGB_Encode(float3 x) {return x < 0.0031308f ? x * 12.92f : 1.055f * pow(x, 1.f / 2.4f) - 0.055f;}
float  sRGB_Encode(float  x) {return x < 0.0031308f ? x * 12.92f : 1.055f * pow(x, 1.f / 2.4f) - 0.055f;}
float4 sRGB_Decode(float4 x) {return x <= 0.04045f ? x / 12.92f : pow((x + 0.055f) / 1.055f, 2.4f);}
float3 sRGB_Decode(float3 x) {return x <= 0.04045f ? x / 12.92f : pow((x + 0.055f) / 1.055f, 2.4f);}
float  sRGB_Decode(float  x) {return x <= 0.04045f ? x / 12.92f : pow((x + 0.055f) / 1.055f, 2.4f);}

#define HDR_ENABLED LumaSettings.DisplayMode == 1
#define HDR_PEAK PeakWhiteNits / GamePaperWhiteNits
#define HDR_INTSCALING GamePaperWhiteNits / UIPaperWhiteNits
#define HDR_SHOULDERSTART GS.TonemapperRolloffStart / GamePaperWhiteNits
#define HDR_MAXEXPECTED GS.TonemapperMaxExpected / GamePaperWhiteNits