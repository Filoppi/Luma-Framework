#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

/////////////////////////////////////////
// Granblue Fantasy Relink LUMA advanced settings
// (note that the defaults might be mirrored in c++, the shader values will be overridden anyway)
/////////////////////////////////////////

#if !defined(TONEMAP_AFTER_TAA)
#define TONEMAP_AFTER_TAA 1
#endif



#endif // SRC_GAME_SETTINGS_HLSL