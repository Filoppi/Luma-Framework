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

#endif // SRC_GAME_SETTINGS_HLSL