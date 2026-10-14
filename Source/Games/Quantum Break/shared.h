#pragma once

#define GAME_QUANTUM_BREAK 1

// SR feature defines come from UseLumaNGX/UseLumaFSR in the project, not a second source here.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../../../Shaders/Quantum Break/Includes/GameCBuffers.hlsl"
#include "../../Core/core.hpp"