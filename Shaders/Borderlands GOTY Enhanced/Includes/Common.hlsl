// Borderlands GOTY Enhanced — game-local Common. Include this (instead of the shared "../Includes/Common.hlsl")
// from any game shader that needs the per-game LumaGameSettings: it defines LUMA_GAME_CB_STRUCTS via
// GameCBuffers.hlsl BEFORE the shared Settings.hlsl declares the LumaSettings cbuffer, so GameSettings is the
// real grade struct rather than the empty default.

// Define the game custom cbuffer structs.
#include "GameCBuffers.hlsl"
// Shared global common (pulls in the shared Settings.hlsl -> LumaSettings cbuffer with our GameSettings).
#include "../../Includes/Common.hlsl"
