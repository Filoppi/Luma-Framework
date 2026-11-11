#pragma once

#include "motion_vector_patch.h"

// This game's slots for the motion vector patches of "motion_vector_patch.h", which describes the scheme. They are dgVoodoo's
// ("MotionVectorPatch::DgVoodoo"), as in Mass Effect 2007 (same wrapper).
namespace MotionVectorPatches
{
   // UE3 (Common.usf, CTAB of the game's shaders): translated view projection c0-c3, camera position c4,
   // PreViewTranslation c5, LocalToWorld c6-c9 (skinned: bones c6-c230, LocalToWorld c231-c234).
   using MotionVectorPatch::DgVoodoo::JITTER_SLOT;
   using MotionVectorPatch::DgVoodoo::OBJECT_ROW_OFFSET;
   using MotionVectorPatch::DgVoodoo::OBJECT_SLOT;
   using MotionVectorPatch::DgVoodoo::PREVIOUS_SLOTS;
   using MotionVectorPatch::DgVoodoo::TARGET_SLOT;
   // The first vc4 row of the view projection (c0): vertex shaders that never read it don't draw the scene's camera view, and are
   // refused
   constexpr uint32_t VIEW_PROJECTION_ROW = OBJECT_ROW_OFFSET;
   // FSR's masks (x reactive, y transparency & composition), written by the scene's alpha blended draws (their pixel shaders
   // patched with "PatchPixelShaderReactive")
   constexpr uint32_t REACTIVE_SLOT = TARGET_SLOT + 1;

   constexpr MotionVectorPatch::Layout LAYOUT = MotionVectorPatch::DgVoodoo::MakeLayout(VIEW_PROJECTION_ROW);
} // namespace MotionVectorPatches
