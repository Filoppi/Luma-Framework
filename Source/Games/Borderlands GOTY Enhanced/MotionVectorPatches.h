#pragma once

#include <utility>

#include "motion_vector_patch.h"

// Motion vectors for this game, which renders none, by patching whole DXBC containers
// ("motion_vector_patch.h"; this file holds the game's slots and registers).
// Vertex shaders run twice: the second run reads the previous frame's per-draw constants (UE3's b0 $Globals: LocalToWorld, material
// parameters; b1 VSOffsetConstants: ViewProjectionMatrix, CameraPosition; b3 VSBoneConstants: the bone palette) from other slots, and
// only its SV_Position is kept, as an extra output. The first run's SV_Position is also copied to an extra output, as outputs can't be
// read back. Pixel shaders write the UV space delta from the current to the previous position to an extra target.
// Tested offline on every cached shader.
namespace MotionVectorPatches
{
   // The camera buffer every projecting vertex shader declares: the jitter buffer's declaration goes after it. A shader without it
   // (shadow depth with its own ProjectionMatrix, screen quads) is refused.
   constexpr uint32_t object_slot = 1;
   // Per-draw constant buffers (b0 object, b1 camera, b3 bones), and the slots of the previous frame's copies the second run reads
   constexpr std::pair<uint32_t, uint32_t> previous_slots[] = {{0, 10}, {object_slot, 8}, {3, 11}};
   // The jitter buffer. c0.xy: the upscaler's projection jitter in NDC, added to SV_Position after its unjittered copy, so motion
   // vectors never contain it.
   constexpr uint32_t jitter_slot = 9;
   // The second run reads t0-t15 at t64-t79 (no game vertex shader reads a resource)
   constexpr uint32_t resource_slots = 16;
   constexpr uint32_t previous_resources_slot = 64;
   // Past every register the game uses (vertex outputs end at o10, pixel inputs at v10), within SM5's 32
   constexpr uint32_t current_position_register = 30;
   constexpr uint32_t previous_position_register = 31;
   // Past the game's targets (almost every pixel shader writes o0, the uber post o1; only the unused HBAO+ pixel shader writes o0-o7)
   constexpr uint32_t target_slot = 4;
   // FSR's masks (x reactive, y transparency & composition), written by the scene's alpha blended draws (their pixel
   // shaders patched with "PatchPixelShaderReactive")
   constexpr uint32_t reactive_slot = 5;

   // For the patches ("motion_vector_patch.h")
   constexpr MotionVectorPatch::Layout layout = {object_slot, previous_slots, jitter_slot, resource_slots, previous_resources_slot,
      current_position_register, previous_position_register, target_slot};
} // namespace MotionVectorPatches
