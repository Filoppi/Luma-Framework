// Borderlands 2 / The Pre-Sequel — game-local Common: defines LumaGameSettings before the shared Settings.hlsl cbuffer.

// Define the game custom cbuffer structs.
#include "GameCBuffers.hlsl"
// Shared global common (pulls in the shared Settings.hlsl -> LumaSettings cbuffer with our GameSettings).
#include "../../Includes/Common.hlsl"
