#pragma once

#include "NativeInputs.hpp"

namespace QuantumBreakUpscaling::NativeHooks
{
   // Renderer offsets, trampolines, TLS scopes, and installation status are private to the CPP.
   void TryInstall();
   void SetActive(bool active) noexcept;
   void Reset() noexcept;
   void SetHaltonPhases(uint32_t phases) noexcept;
   bool ConsumeHaltonReset() noexcept;
   void RecordDepth(uintptr_t resource, uint32_t width, uint32_t height) noexcept;
   void RecordMotion(uintptr_t resource) noexcept;
   // Returns current owned inputs only; validate the exact resource selected for the backend.
   NativeInputFallback CopyCurrentInputs(NativeSceneCamera& camera, float& x, float& y, uintptr_t depth, uintptr_t motion);

#if DEVELOPMENT || TEST
   bool IsHaltonEnabled() noexcept;
   void SetHaltonEnabled(bool enabled) noexcept;
   const char* StatusText() noexcept;
   bool IsPreferred() noexcept;
   void SetPreferred(bool preferred) noexcept;
#endif
} // namespace QuantumBreakUpscaling::NativeHooks