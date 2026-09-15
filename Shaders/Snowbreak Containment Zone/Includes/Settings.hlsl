#ifndef SRC_GAME_SETTINGS_HLSL
#define SRC_GAME_SETTINGS_HLSL

// Include this after the global "Settings.hlsl" file

/////////////////////////////////////////
// Mafia III LUMA advanced settings
// (note that the defaults might be mirrored in c++, the shader values will be overridden anyway)
/////////////////////////////////////////

#ifndef VELOCITY_TEX_INDEX
#define VELOCITY_TEX_INDEX t2
#endif
#ifndef DEPTH_TEX_INDEX
#define DEPTH_TEX_INDEX t0
#endif
#ifndef GLOBAL_CB_INDEX
#define GLOBAL_CB_INDEX b1
#endif
#ifndef GLOBAL_CB_SIZE
#define GLOBAL_CB_SIZE 140
#endif
#ifndef JITTER_CB_INDEX
#define JITTER_CB_INDEX 122
#endif
#ifndef CLIP_TO_PREV_CLIP_CB_START_INDEX
#define CLIP_TO_PREV_CLIP_CB_START_INDEX 118
#endif

#endif // SRC_GAME_SETTINGS_HLSL