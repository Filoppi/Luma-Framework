#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

// The resolution the game was likely developed at. It was a mix of 720p and 1080p, however some effects radius becomes too big if we simulate 720p.
static const float DevelopmentVerticalResolution = 1080.0;

/////////////////////////////////////////
// Deus Ex: Human Revolution (DE) LUMA advanced settings
// (note that the defaults might be mirrored in c++, the shader values will be overridden anyway)
/////////////////////////////////////////

#if !defined(ENABLE_LUMA)
#define ENABLE_LUMA 1
#endif

#if !defined(ENABLE_AUTO_HDR)
#define ENABLE_AUTO_HDR 1
#endif

#if !defined(ENABLE_HIGHLIGHTS_DESATURATION_TYPE)
#define ENABLE_HIGHLIGHTS_DESATURATION_TYPE 0
#endif

#ifndef XE_GTAO_GENERATE_NORMALS
#define XE_GTAO_GENERATE_NORMALS 0
#endif

#ifndef XE_GTAO_QUALITY
#define XE_GTAO_QUALITY 2
#endif

// This is the "DX:HR DC (Fixed)" lighting from the "Gold Filter Restoration" mod (DXHRDC-GFX, "LightingShader1"), which swaps the DC shader (0x4C48AF67) with the original DX:HR one (0x0AB7755C), as the DC one looked broken ("inverted").
// The lighting shaders have two hashes because one is for the original game (and for the Gold Filter Restoration mod) and one for the DC, they both run the original game's version.
// The resources bindings and cbuffers layouts are the same between the two versions.
// Compared to the DC version, the original version:
//  -uses a half Lambert (wrapped) N dot L, instead of a hard (clamped) one
//  -saturates the distance falloff, instead of only clamping it to >= 0 (it could go beyond 1)
// With this on, the affect lighting shaders compile to the exact same instructions as the original game did. With it off, it's functionally equivalent to the DC shader (for both hashes).
#ifndef FIX_DC_LIGHTING
#define FIX_DC_LIGHTING ENABLE_LUMA
#endif

#endif // SRC_GAME_SETTINGS_HLSL