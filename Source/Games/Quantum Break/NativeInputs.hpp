#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace QuantumBreakUpscaling
{
   struct NativeJitterSample
   {
      float x = 0.f;
      float y = 0.f;
      uint64_t generation = 0u;
      uint32_t thread = 0u;
      uint32_t path = 0u;
      bool live = false;
      bool valid = false;
   };

   class NativeJitterState
   {
   public:
      uint64_t Token() const noexcept
      {
         return state_.load(std::memory_order_acquire);
      }
      static bool IsActive(uint64_t token) noexcept
      {
         return (token & active_bit) != 0u;
      }
      static bool IsPreferred(uint64_t token) noexcept
      {
         return (token & preferred_bit) != 0u;
      }
      void SetActive(bool active) noexcept
      {
         Change(active_bit, active);
      }
      void SetPreferred(bool preferred) noexcept
      {
         Change(preferred_bit, preferred);
      }
      void Reset() noexcept
      {
         Change(active_bit, false, true);
      }

   private:
      static constexpr uint64_t active_bit = 1u;
      static constexpr uint64_t preferred_bit = 2u;
      static constexpr uint64_t flags_mask = active_bit | preferred_bit;
      static constexpr uint64_t generation_step = flags_mask + 1u;
      // Flags and generation publish together; re-enable cannot revive a pre-toggle scope.
      std::atomic<uint64_t> state_ = generation_step | preferred_bit;

      void Change(uint64_t bit, bool enabled, bool force_generation = false) noexcept
      {
         uint64_t previous = Token();
         while (force_generation || ((previous & bit) != 0u) != enabled)
         {
            const uint64_t flags = enabled ? (previous | bit) : (previous & ~bit);
            const uint64_t next = (flags & flags_mask) | ((previous & ~flags_mask) + generation_step);
            if (state_.compare_exchange_weak(previous, next, std::memory_order_acq_rel, std::memory_order_acquire)) return;
         }
      }
   };

   template <typename T>
   class NativeScopedValue
   {
   public:
      explicit NativeScopedValue(T& value) noexcept : value_(value), previous_(value)
      {
         value_ = {};
      }
      ~NativeScopedValue() noexcept
      {
         value_ = previous_;
      }
      NativeScopedValue(const NativeScopedValue&) = delete;
      NativeScopedValue& operator=(const NativeScopedValue&) = delete;

   private:
      T& value_;
      T previous_;
   };

   inline bool DecodeNativeJitter(double projection_x, double projection_y, float& x, float& y) noexcept
   {
      // Preserve double-to-float rounding before projection-to-UV scaling.
      x = static_cast<float>(projection_x) * 0.5f;
      y = static_cast<float>(projection_y) * -0.5f;
      return std::isfinite(x) && std::isfinite(y);
   }

   constexpr float NativeHaltonSequence(uint32_t index, uint32_t base) noexcept
   {
      ++index; // Skip the biased all-zero radical inverse, matching Core's SR::HaltonSequence.
      float result = 0.f;
      const float inverse_base = 1.f / static_cast<float>(base);
      float fraction = inverse_base;
      while (index != 0u)
      {
         result += static_cast<float>(index % base) * fraction;
         index /= base;
         fraction *= inverse_base;
      }
      return result - 0.5f;
   }

   struct HaltonPhaseState
   {
      uintptr_t renderer = 0u;
      uint64_t config = 0u;
      uint32_t frame = 0u;
      uint32_t width = 0u;
      uint32_t height = 0u;
      uint32_t index = 0u;
      bool initialized = false;

      bool Update(uintptr_t current_renderer, uint64_t current_config, uint32_t current_frame,
         uint32_t current_width, uint32_t current_height, uint32_t phases, bool history_valid = true) noexcept
      {
         const uint32_t elapsed = current_frame - frame;
         const bool reset = !initialized || renderer != current_renderer || config != current_config ||
                            width != current_width || height != current_height || elapsed > 1u || !history_valid;
         if (reset)
         {
            index = 0u;
         }
         else if (elapsed == 1u)
         {
            index = (index + 1u) % phases;
         }
         renderer = current_renderer;
         config = current_config;
         frame = current_frame;
         width = current_width;
         height = current_height;
         initialized = true;
         return reset;
      }
   };

   inline bool NativeCameraValueMatches(float native_value, float reference_value, float absolute_tolerance) noexcept
   {
      if (!std::isfinite(native_value) || !std::isfinite(reference_value)) return false;
      const float scale = (std::max)(std::fabs(native_value), std::fabs(reference_value));
      return std::fabs(native_value - reference_value) <= (std::max)(absolute_tolerance, scale * 1e-5f);
   }

   struct NativeSceneCamera
   {
      float near_plane = 0.f;
      float vertical_fov = 0.f;
      bool valid = false;
   };

   inline bool DecodeNativeSceneCamera(const std::array<double, 16>& projection, const std::array<double, 16>& inverse,
      NativeSceneCamera& camera) noexcept
   {
      camera = {};
      std::array<float, 16> p;
      std::array<float, 16> m;
      bool finite = true;
      for (size_t i = 0u; i < projection.size(); ++i)
      {
         p[i] = static_cast<float>(projection[i]);
         m[i] = static_cast<float>(inverse[i]);
         finite = finite && std::isfinite(p[i]) && std::isfinite(m[i]);
      }
      const float sx = std::fabs(p[0]);
      const float sy = std::fabs(p[5]);
      // Reject nonfinite or non-perspective input before deriving camera parameters.
      if (!finite || sx <= 1e-6f || sy <= 1e-6f || m[14] == 0.f || std::fabs(p[15]) > 1e-6f || std::fabs(p[11]) <= 1e-6f) return false;

      // Match View::use's float operation order at clip z=0.
      const float inverse_near =
         ((m[7] * 0.f + m[3] * 0.f) + m[11] * 0.f + m[15]) / ((m[6] * 0.f + m[2] * 0.f) + m[10] * 0.f + m[14]);
      camera.near_plane = 1.f / inverse_near;
      camera.vertical_fov = 2.f * std::atan(1.f / sy);
      // All selected parameters must also remain finite and within the verified perspective range.
      camera.valid = std::isfinite(inverse_near) && inverse_near > 0.f && std::isfinite(camera.near_plane) && camera.near_plane > 0.f && camera.near_plane < 1000.f && std::isfinite(camera.vertical_fov) && camera.vertical_fov >= 0.1f && camera.vertical_fov < 3.13f;
      return camera.valid;
   }

   struct NativeSceneAssociation
   {
      uint64_t token = 0u;
      uintptr_t view = 0u; // Numeric identity only, never borrowed for later dereferencing.
      uint32_t thread = 0u;
      bool live = false;
      bool renderer_rejected = false;
   };

   enum class NativeInputFallback : uint8_t
   {
      None,
      HooksUnavailable,
      NativeDisabled,
      NoTemporalScope,
      StaleTemporalScope,
      UnverifiedPath,
      NoSceneScope,
      StaleSceneScope,
      OtherView,
      InvalidCamera,
      RendererRejected,
      MissingSceneWork,
      DepthMismatch,
      MotionMismatch,
      CBufferBindingGuard,
      TemporalShaderUnavailable,
      ResourceLayoutMismatch,
      Count
   };

   inline const char* NativeInputFallbackText(NativeInputFallback reason) noexcept
   {
      // Keep these labels in the same order as NativeInputFallback.
      constexpr const char* names[] = {"none",
         "hooks_unavailable",
         "native_disabled",
         "no_temporal_scope",
         "stale_temporal_scope",
         "unverified_path",
         "no_scene_scope",
         "stale_scene_scope",
         "other_view",
         "invalid_camera",
         "renderer_rejected",
         "missing_scene_work",
         "depth_identity_mismatch",
         "motion_identity_mismatch",
         "cbuffer_binding_guard",
         "temporal_shader_unavailable",
         "resource_layout_mismatch"};
      return names[static_cast<size_t>(reason)];
   }

   inline NativeInputFallback EvaluateNativeInputs(const NativeSceneAssociation& scene,
      const NativeSceneCamera& scene_camera,
      const NativeJitterSample& temporal_jitter,
      const NativeSceneCamera& temporal_camera, uintptr_t temporal_view,
      uint64_t token, uint32_t thread, bool hooks_installed,
      bool scene_work, bool depth_matches, bool motion_matches) noexcept
   {
      // Both scopes must be live now on this exact thread; no previous-frame values are used.
      if (!hooks_installed) return NativeInputFallback::HooksUnavailable;
      if (!NativeJitterState::IsActive(token) || !NativeJitterState::IsPreferred(token)) return NativeInputFallback::NativeDisabled;
      if (!temporal_jitter.live) return NativeInputFallback::NoTemporalScope;
      if (temporal_jitter.thread != thread || temporal_jitter.generation != token) return NativeInputFallback::StaleTemporalScope;
      if (temporal_jitter.path != 0u) return NativeInputFallback::UnverifiedPath;
      if (!scene.live) return NativeInputFallback::NoSceneScope;
      if (scene.token != token || scene.thread != thread) return NativeInputFallback::StaleSceneScope;
      if (scene.view == 0u || temporal_view != scene.view) return NativeInputFallback::OtherView;
      if (!scene_camera.valid || !temporal_camera.valid || !temporal_jitter.valid ||
          !std::isfinite(temporal_jitter.x) || !std::isfinite(temporal_jitter.y) ||
          !NativeCameraValueMatches(scene_camera.near_plane, temporal_camera.near_plane, 1e-6f) ||
          !NativeCameraValueMatches(scene_camera.vertical_fov, temporal_camera.vertical_fov, 1e-6f))
      {
         return NativeInputFallback::InvalidCamera;
      }
      if (scene.renderer_rejected) return NativeInputFallback::RendererRejected;
      if (!scene_work) return NativeInputFallback::MissingSceneWork;
      if (!depth_matches) return NativeInputFallback::DepthMismatch;
      if (!motion_matches) return NativeInputFallback::MotionMismatch;
      return NativeInputFallback::None;
   }

   inline NativeInputFallback GuardNativeInputBindings(NativeInputFallback reason, bool frame_binding_valid, bool ssaa_binding_valid) noexcept
   {
      // Binding metadata never overrides a native lifetime/view/resource rejection.
      if (reason != NativeInputFallback::None) return reason;
      // Metadata-only guard until the historical missing-camera rejection is covered.
      if (!frame_binding_valid || !ssaa_binding_valid) return NativeInputFallback::CBufferBindingGuard;
      return NativeInputFallback::None;
   }

   enum class TemporalInputAction : uint8_t
   {
      NativeSR,
      NativeTAA,
      Skip
   };

   inline TemporalInputAction SelectTemporalInputAction(NativeInputFallback reason, bool frame_binding_valid, bool ssaa_binding_valid) noexcept
   {
      // Missing native capture alone is never authority to cancel the game's resolve.
      // Preserve the historical missing-both-buffer rejection without reading either buffer.
      if (!frame_binding_valid && !ssaa_binding_valid) return TemporalInputAction::Skip;
      if (GuardNativeInputBindings(reason, frame_binding_valid, ssaa_binding_valid) != NativeInputFallback::None) return TemporalInputAction::NativeTAA;
      return TemporalInputAction::NativeSR;
   }

   inline bool CanUseTemporalSRShader(bool enabled, uintptr_t original, uintptr_t replacement, uintptr_t bound) noexcept
   {
      // A cached replacement handle or a custom vertex shader does not prove the PS is active.
      return enabled && original != 0u && replacement != 0u && replacement != original && bound == replacement;
   }

   struct SRRenderSize
   {
      uint32_t width = 0u;
      uint32_t height = 0u;

      bool operator==(const SRRenderSize&) const = default;
   };

   enum class SRDepthSource : uint8_t
   {
      Unavailable,
      Cached,
      Temporal
   };

   inline SRDepthSource ChooseSRDepthSource(SRRenderSize color, SRRenderSize motion, SRRenderSize cached, SRRenderSize temporal) noexcept
   {
      // No subrectangle mapping is implemented: color, depth, and low-resolution MVs must agree.
      if (color.width == 0u || color.height == 0u || color != motion) return SRDepthSource::Unavailable;
      if (cached == color) return SRDepthSource::Cached;
      if (temporal == color) return SRDepthSource::Temporal;
      return SRDepthSource::Unavailable;
   }

   inline bool ConsumeSRResetRequest(std::atomic<bool>& requested, bool resources_changed) noexcept
   {
      // Consume even when resources changed; a later concurrent request stays set for the next draw.
      return requested.exchange(false, std::memory_order_acq_rel) || resources_changed;
   }
} // namespace QuantumBreakUpscaling