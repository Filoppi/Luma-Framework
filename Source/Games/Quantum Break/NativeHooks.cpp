#include "NativeHooks.hpp"
#include "NativeInputs.hpp"

#include "../../External/reshade/deps/minhook/include/MinHook.h"
#include "../../External/reshade/include/reshade.hpp"

#include <Windows.h> // IWYU pragma: keep -- Windows API umbrella header.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <intrin.h> // IWYU pragma: keep -- MSVC _ReturnAddress intrinsic.

namespace QuantumBreakUpscaling::NativeHooks
{
   namespace
   {
      enum class Status : uint8_t
      {
         NotInstalled,
         Installing,
         Installed,
         UnsupportedRenderer,
         HookFailed
      };
      using ResolveFunction = void(__fastcall*)(void*, const void*, void*, void*, void*);
      using SceneFunction = void(__fastcall*)(void*, const void*, void*, void*);
      using ViewUseFunction = void(__fastcall*)(const void*);
      using JitterViewFunction = void(__fastcall*)(void*, const void*, const double*);
      ResolveFunction original_resolves[2] = {};
      SceneFunction original_scene = nullptr;
      ViewUseFunction original_view_use = nullptr;
      JitterViewFunction original_jitter_view = nullptr;
      std::atomic<Status> temporal_status = Status::NotInstalled;
      std::atomic<Status> scene_status = Status::NotInstalled;
      std::atomic<Status> halton_status = Status::NotInstalled;
      NativeJitterState capture_state;
      uintptr_t renderer_base = 0u;
      // Bit 0 enables Halton; bits 1..8 hold its period. Upper bits invalidate sequences.
      constexpr uint64_t halton_enabled_bit = 1u;
      constexpr uint64_t halton_phases_mask = 0x1feu;
      constexpr uint64_t halton_config_mask = halton_enabled_bit | halton_phases_mask;
      constexpr uint64_t halton_generation_step = 0x200u;
      std::atomic<uint64_t> halton_config = halton_enabled_bit | (8u << 1u);
      std::atomic<bool> halton_reset_pending = false;
      std::atomic<bool> halton_faulted = false;
      thread_local HaltonPhaseState halton_phase;

      struct HaltonSample
      {
         uintptr_t view = 0u;
         float x = 0.f;
         float y = 0.f;
      };

      struct TemporalScope
      {
         NativeJitterSample jitter;
         NativeSceneCamera camera;
         uintptr_t view = 0u;
      };
      struct SceneScope
      {
         NativeSceneAssociation association;
         NativeSceneCamera camera;
         uintptr_t renderer = 0u;
         uintptr_t source_view = 0u;
         uintptr_t active_view = 0u;
         uintptr_t depth_resource = 0u;
         uint64_t depth_area = 0u;
         uintptr_t motion_resource = 0u;
         HaltonSample halton;
      };
      thread_local TemporalScope current_temporal;
      thread_local SceneScope current_scene;

      void ChangeHaltonConfig(uint64_t mask, uint64_t value, bool invalidate = false) noexcept
      {
         uint64_t previous = halton_config.load(std::memory_order_acquire);
         while (invalidate || (previous & mask) != value)
         {
            const uint64_t next = ((previous & halton_config_mask) & ~mask) | value |
                                  ((previous & ~halton_config_mask) + halton_generation_step);
            if (halton_config.compare_exchange_weak(previous, next, std::memory_order_acq_rel, std::memory_order_acquire)) return;
         }
      }

      bool CopyViewMatrices(const void* view, double* projection, double* inverse) noexcept
      {
         if (view == nullptr) return false;
         __try
         {
            // Read only within the owning native call; no saved pointer is dereferenced later.
            const auto* bytes = static_cast<const uint8_t*>(view);
            std::memcpy(projection, bytes + 0xd0u, sizeof(double) * 16u);
            std::memcpy(inverse, bytes + 0x150u, sizeof(double) * 16u);
            return true;
         }
         __except (EXCEPTION_EXECUTE_HANDLER)
         {
            return false;
         }
      }

      bool CopyRendererReject(const void* renderer, bool& rejected) noexcept
      {
         if (renderer == nullptr) return false;
         __try
         {
            rejected = *(static_cast<const uint8_t*>(renderer) + 0x476u) != 0u;
            return true;
         }
         __except (EXCEPTION_EXECUTE_HANDLER)
         {
            return false;
         }
      }

      bool CopyJitterTransform(const double* input, const void* source, double* transform,
         uint32_t& frame, uint32_t& width, uint32_t& height, bool& history_valid) noexcept
      {
         if (input == nullptr) return false;
         __try
         {
            const auto* base = reinterpret_cast<const uint8_t*>(renderer_base);
            if (base[0x4748a1u] == 0u) return false; // No jittered scene view when native SSAA is off.
            const auto* size = reinterpret_cast<const int32_t*>(base + 0x49ae38u);
            if (size[0] <= 0 || size[1] <= 0) return false;
            frame = *reinterpret_cast<const uint32_t*>(base + 0x49adf4u);
            width = static_cast<uint32_t>(size[0]);
            height = static_cast<uint32_t>(size[1]);
            history_valid = *(static_cast<const uint8_t*>(source) + 0x4b0u) != 0u;
            std::memcpy(transform, input, sizeof(double) * 16u);
            return true;
         }
         __except (EXCEPTION_EXECUTE_HANDLER)
         {
            return false;
         }
      }

      void __fastcall CaptureJitterView(void* output, const void* source, const double* native_transform)
      {
         const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
         auto& scene = current_scene;
         const uint64_t token = capture_state.Token();
         const uint64_t config = halton_config.load(std::memory_order_acquire);
         const bool enabled = (config & halton_enabled_bit) != 0u && NativeJitterState::IsActive(token) &&
                              NativeJitterState::IsPreferred(token) && !halton_faulted.load(std::memory_order_acquire);
         const bool current = caller == renderer_base + 0xffd70u && scene.association.live &&
                              scene.association.thread == GetCurrentThreadId() && scene.association.token == token &&
                              !scene.association.renderer_rejected && source != nullptr && output != nullptr &&
                              scene.source_view == reinterpret_cast<uintptr_t>(source);
         const auto use_native = [&]()
         {
            if (current)
            {
               scene.halton = {};
               if (halton_phase.initialized)
               {
                  halton_phase.initialized = false;
                  halton_reset_pending.store(true, std::memory_order_release);
               }
            }
            original_jitter_view(output, source, native_transform);
         };
         if (!enabled || !current || halton_status.load(std::memory_order_acquire) != Status::Installed)
         {
            use_native();
            return;
         }

         alignas(16) std::array<double, 16> transform;
         std::array<double, 16> projection;
         std::array<double, 16> inverse;
         HaltonSample sample;
         uint32_t frame = 0u;
         uint32_t width = 0u;
         uint32_t height = 0u;
         bool history_valid = false;
         if (!(CopyJitterTransform(native_transform, source, transform.data(), frame, width, height, history_valid) &&
                CopyViewMatrices(static_cast<const uint8_t*>(source) + 0x10u, projection.data(), inverse.data())))
         {
            use_native();
            return;
         }
         // This call accepts identity + XY translation, not an arbitrary camera transformation.
         for (size_t i = 0u; i < transform.size(); ++i)
         {
            if (!std::isfinite(transform[i]) || (i != 12u && i != 13u && transform[i] != (i % 5u == 0u ? 1.0 : 0.0)))
            {
               use_native();
               return;
            }
         }
         NativeSceneCamera camera;
         if (!DecodeNativeSceneCamera(projection, inverse, camera))
         {
            use_native();
            return;
         }
         const uint32_t phases = static_cast<uint32_t>((config & halton_phases_mask) >> 1u);
         if (halton_phase.Update(scene.renderer, config, frame, width, height, phases, history_valid))
         {
            halton_reset_pending.store(true, std::memory_order_release);
         }
         const uint32_t index = halton_phase.index;
         // Top-left pixel jitter becomes clip translation with the opposite Y sign.
         transform[12] = static_cast<double>((2.f * NativeHaltonSequence(index, 2u)) / static_cast<float>(width));
         transform[13] = static_cast<double>(-(2.f * NativeHaltonSequence(index, 3u)) / static_cast<float>(height));
         DecodeNativeJitter(projection[8] + projection[11] * transform[12],
            projection[9] + projection[11] * transform[13], sample.x, sample.y);
         sample.view = reinterpret_cast<uintptr_t>(output) + 0x10u;
         scene.halton = sample;
         // Let the renderer derive projection/inverse/providers and history from this local value.
         original_jitter_view(output, source, transform.data());
      }

      void VerifyAppliedHalton(NativeJitterSample& jitter)
      {
         const auto& expected = current_scene.halton;
         if (expected.view == 0u || expected.view != current_temporal.view || !jitter.valid) return;
         const bool matches = NativeCameraValueMatches(jitter.x, expected.x, 1e-9f) &&
                              NativeCameraValueMatches(jitter.y, expected.y, 1e-9f);
         if (!matches)
         {
            // Do not keep altering projections if the supported call stopped producing our input.
            jitter.valid = false;
            if (!halton_faulted.exchange(true, std::memory_order_acq_rel))
            {
               reshade::log::message(reshade::log::level::warning, "QB Halton: applied projection mismatch; further overrides disabled, native jitter retained next scene.");
            }
         }
      }

      template <size_t Path>
      void __fastcall CaptureResolve(void* self, const void* view, void* output, void* color, void* depth)
      {
         NativeScopedValue<TemporalScope> scope(current_temporal);
         auto& sample = current_temporal.jitter;
         sample.live = true;
         sample.path = static_cast<uint32_t>(Path);
         sample.thread = GetCurrentThreadId();
         sample.generation = capture_state.Token();
         current_temporal.view = reinterpret_cast<uintptr_t>(view);
         // Only the verified inner temporal path decodes current camera and jitter.
         if (Path == 0u && NativeJitterState::IsActive(sample.generation) &&
             NativeJitterState::IsPreferred(sample.generation) &&
             temporal_status.load(std::memory_order_acquire) == Status::Installed)
         {
            std::array<double, 16> projection;
            std::array<double, 16> inverse;
            if (CopyViewMatrices(view, projection.data(), inverse.data()))
            {
               sample.valid = DecodeNativeJitter(projection[8], projection[9], sample.x, sample.y);
               DecodeNativeSceneCamera(projection, inverse, current_temporal.camera);
               VerifyAppliedHalton(sample);
            }
         }
         // Path 1 hides its enclosing sample; nested path 0 restores it only until caller return.
         original_resolves[Path](self, view, output, color, depth);
      }

      void __fastcall CaptureScene(void* self, const void* view, void* arg3, void* arg4)
      {
         NativeScopedValue<SceneScope> scope(current_scene);
         if (scene_status.load(std::memory_order_acquire) == Status::Installed)
         {
            auto& a = current_scene.association;
            a.live = true;
            a.token = capture_state.Token();
            a.thread = GetCurrentThreadId();
            current_scene.renderer = reinterpret_cast<uintptr_t>(self);
            current_scene.source_view = reinterpret_cast<uintptr_t>(view);
            bool rejected = true;
            a.renderer_rejected = !CopyRendererReject(self, rejected) || rejected;
         }
         original_scene(self, view, arg3, arg4);
      }

      void __fastcall CaptureViewUse(const void* view)
      {
         const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
         original_view_use(view);
         auto& scene = current_scene;
         if (!scene.association.live) return;
         const uintptr_t internal = view == nullptr ? 0u : reinterpret_cast<uintptr_t>(view) + 0x10u;
         scene.active_view = internal;
         if (caller != renderer_base + 0x100a1bu && caller != renderer_base + 0x100b82u) return;
         const uint64_t token = capture_state.Token();
         if (scene.association.token != token || scene.association.view != internal)
         {
            // A new upload cannot revalidate depth/MV from before a toggle or different main view.
            scene.depth_resource = scene.depth_area = scene.motion_resource = 0u;
         }
         scene.association.view = internal;
         scene.association.token = token; // Refresh at the actual upload, never at draw selection.
         bool rejected = true;
         scene.association.renderer_rejected =
            !CopyRendererReject(reinterpret_cast<const void*>(scene.renderer), rejected) || rejected;
         scene.camera = {};
         if (NativeJitterState::IsActive(token) && NativeJitterState::IsPreferred(token))
         {
            std::array<double, 16> projection;
            std::array<double, 16> inverse;
            if (CopyViewMatrices(reinterpret_cast<const void*>(internal), projection.data(), inverse.data()))
            {
               DecodeNativeSceneCamera(projection, inverse, scene.camera);
            }
         }
      }

      bool TemporalLayoutMatches(HMODULE module)
      {
         const auto* base = reinterpret_cast<const uint8_t*>(module);
         const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
         if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return false;
         const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
         if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || pe->FileHeader.TimeDateStamp != 0x5808affau || pe->OptionalHeader.SizeOfImage != 0x768000u) return false;
         constexpr std::array<float, 8> table = {0.25f, -0.75f, -0.25f, 0.75f, 0.75f, 0.25f, -0.75f, -0.25f};
         // Verify entry points and float matrix conversions before installing either hook.
         constexpr uint8_t path_0[] = {0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
         constexpr uint8_t path_1[] = {0x48, 0x8b, 0xc4, 0x4c, 0x89, 0x48, 0x20, 0x4c, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10};
         constexpr uint8_t projection_getter[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda, 0x48, 0x8d, 0x91, 0xd0, 0x00, 0x00, 0x00};
         constexpr uint8_t inverse_getter[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda, 0x48, 0x8d, 0x91, 0x50, 0x01, 0x00, 0x00};
         constexpr uint8_t table_reader[] = {0x48, 0x8d, 0x0d, 0x74, 0x4c, 0x37, 0x00, 0x0f, 0x28, 0xce, 0xf3, 0x0f, 0x59, 0x0c, 0xc1};
         return std::memcmp(base + 0x474830u, table.data(), sizeof(table)) == 0 &&
                std::memcmp(base + 0x10bcb0u, path_0, sizeof(path_0)) == 0 &&
                std::memcmp(base + 0x10cd30u, path_1, sizeof(path_1)) == 0 &&
                std::memcmp(base + 0x18cc0u, projection_getter, sizeof(projection_getter)) == 0 &&
                std::memcmp(base + 0x18620u, inverse_getter, sizeof(inverse_getter)) == 0 &&
                std::memcmp(base + 0xffbb5u, table_reader, sizeof(table_reader)) == 0;
      }

      bool SceneLayoutMatches(HMODULE module)
      {
         const auto* base = reinterpret_cast<const uint8_t*>(module);
         constexpr uint8_t scene[] = {0x48, 0x8b, 0xc4, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xa8, 0x38, 0xf8, 0xff, 0xff};
         constexpr uint8_t use[] = {0x40, 0x53, 0x48, 0x81, 0xec, 0x90, 0x00, 0x00, 0x00, 0x48, 0xc7, 0x44, 0x24, 0x40, 0xfe, 0xff, 0xff, 0xff};
         constexpr uint8_t call_a[] = {0x49, 0x8b, 0xce, 0xe8, 0xe5, 0x2a, 0x05, 0x00};
         constexpr uint8_t call_b[] = {0x49, 0x8b, 0xce, 0xe8, 0x7e, 0x29, 0x05, 0x00};
         constexpr uint8_t temporal[] = {0x49, 0x8d, 0x54, 0x24, 0x10, 0x48, 0x89, 0x5c, 0x24, 0x20};
         constexpr uint8_t reject[] = {0x80, 0xb9, 0x76, 0x04, 0x00, 0x00, 0x00, 0x0f, 0x85, 0xf2, 0x3a, 0x00, 0x00};
         constexpr uint8_t projection_provider[] = {0x48, 0x8b, 0x93, 0x60, 0x05, 0x00, 0x00, 0x8b, 0x8b, 0x68, 0x05, 0x00, 0x00};
         return std::memcmp(base + 0xff980u, scene, sizeof(scene)) == 0 &&
                std::memcmp(base + 0x153500u, use, sizeof(use)) == 0 &&
                std::memcmp(base + 0x100a13u, call_a, sizeof(call_a)) == 0 &&
                std::memcmp(base + 0x100b7au, call_b, sizeof(call_b)) == 0 &&
                std::memcmp(base + 0x102311u, temporal, sizeof(temporal)) == 0 &&
                std::memcmp(base + 0x100179u, reject, sizeof(reject)) == 0 &&
                std::memcmp(base + 0x15353bu, projection_provider, sizeof(projection_provider)) == 0;
      }

      // Failure before activation can remove hooks. Partial activation must retain trampolines.
      bool CreatePair(void* const* targets, void* const* hooks, void** originals)
      {
         size_t created = 0u;
         for (; created < 2u; ++created)
         {
            if (MH_CreateHook(targets[created], hooks[created], &originals[created]) != MH_OK)
            {
               break;
            }
         }
         if (created == 2u) return true;
         for (size_t i = 0u; i < created; ++i)
         {
            MH_RemoveHook(targets[i]);
         }
         return false;
      }

      void RemovePair(void* const* targets)
      {
         for (size_t i = 0u; i < 2u; ++i)
         {
            MH_RemoveHook(targets[i]);
         }
      }

      bool QueuePair(void* const* targets)
      {
         return MH_QueueEnableHook(targets[0]) == MH_OK && MH_QueueEnableHook(targets[1]) == MH_OK;
      }

      void InstallTemporal(HMODULE module)
      {
         Status expected = Status::NotInstalled;
         if (!temporal_status.compare_exchange_strong(expected, Status::Installing, std::memory_order_acq_rel)) return;
         if (!TemporalLayoutMatches(module))
         {
            temporal_status.store(Status::UnsupportedRenderer, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native jitter: renderer layout mismatch; SR unavailable, game TAA retained.");
            return;
         }
         const auto base = reinterpret_cast<uintptr_t>(module);
         void* targets[] = {reinterpret_cast<void*>(base + 0x10bcb0u), reinterpret_cast<void*>(base + 0x10cd30u)};
         void* hooks[] = {reinterpret_cast<void*>(&CaptureResolve<0>), reinterpret_cast<void*>(&CaptureResolve<1>)};
         void* originals[2] = {};
         const MH_STATUS initialized = MH_Initialize();
         if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
         {
            temporal_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native jitter: MinHook initialization failed; SR unavailable, game TAA retained.");
            return;
         }
         if (!CreatePair(targets, hooks, originals))
         {
            temporal_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native jitter: hook creation failed; SR unavailable, game TAA retained.");
            return;
         }
         original_resolves[0] = reinterpret_cast<ResolveFunction>(originals[0]);
         original_resolves[1] = reinterpret_cast<ResolveFunction>(originals[1]);
         HMODULE pinned = nullptr;
         constexpr DWORD pin_flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN;
         if (!GetModuleHandleExW(pin_flags, reinterpret_cast<LPCWSTR>(&CaptureResolve<0>), &pinned) ||
             !GetModuleHandleExW(pin_flags, reinterpret_cast<LPCWSTR>(module), &pinned))
         {
            RemovePair(targets);
            temporal_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native jitter: module pinning failed; SR unavailable, game TAA retained.");
            return;
         }
         if (!QueuePair(targets))
         {
            RemovePair(targets);
            temporal_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native jitter: queued enable failed; SR unavailable, game TAA retained.");
            return;
         }
         if (MH_ApplyQueued() != MH_OK)
         {
            temporal_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(
               reshade::log::level::warning,
               "QB native jitter: activation failed; game TAA retained, pinned trampolines kept until exit.");
            return;
         }
         temporal_status.store(Status::Installed, std::memory_order_release);
         reshade::log::message(reshade::log::level::info, "QB native jitter: installed; path 0 current jitter/camera captured in live scope; other resolves use game TAA without readback.");
      }

      void InstallScene(HMODULE module)
      {
         Status expected = Status::NotInstalled;
         if (!scene_status.compare_exchange_strong(expected, Status::Installing, std::memory_order_acq_rel)) return;
         if (!SceneLayoutMatches(module))
         {
            scene_status.store(Status::UnsupportedRenderer, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native inputs: scene fingerprint mismatch; SR unavailable, game TAA retained.");
            return;
         }
         const auto base = reinterpret_cast<uintptr_t>(module);
         void* targets[] = {reinterpret_cast<void*>(base + 0xff980u), reinterpret_cast<void*>(base + 0x153500u)};
         void* hooks[] = {reinterpret_cast<void*>(&CaptureScene), reinterpret_cast<void*>(&CaptureViewUse)};
         void* originals[2] = {};
         if (!CreatePair(targets, hooks, originals))
         {
            scene_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native inputs: scene hook creation failed; SR unavailable, game TAA retained.");
            return;
         }
         original_scene = reinterpret_cast<SceneFunction>(originals[0]);
         original_view_use = reinterpret_cast<ViewUseFunction>(originals[1]);
         renderer_base = base; // Temporal hooks already pin the addon and renderer.
         if (!QueuePair(targets))
         {
            RemovePair(targets);
            scene_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB native inputs: scene queued enable failed; SR unavailable, game TAA retained.");
            return;
         }
         if (MH_ApplyQueued() != MH_OK)
         {
            scene_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(
               reshade::log::level::warning,
               "QB native inputs: scene activation failed; pinned trampolines retained, game TAA active.");
            return;
         }
         scene_status.store(Status::Installed, std::memory_order_release);
         reshade::log::message(reshade::log::level::info, "QB native inputs: scene hooks installed; camera/jitter primary without CB0/CB1 byte capture on verified live scenes.");
      }

      void InstallHalton(HMODULE module)
      {
         Status expected = Status::NotInstalled;
         if (!halton_status.compare_exchange_strong(expected, Status::Installing, std::memory_order_acq_rel)) return;
         const auto* image = reinterpret_cast<const uint8_t*>(module);
         constexpr uint8_t build_view[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57};
         constexpr uint8_t build_call[] = {0x4c, 0x8d, 0x44, 0x24, 0x50, 0x48, 0x8b, 0xd6, 0x48, 0x8d, 0x4d, 0xe0, 0xe8, 0x90, 0x27, 0x05, 0x00};
         constexpr uint8_t frame_increment[] = {0xff, 0x05, 0xbc, 0x2e, 0x3a, 0x00};
         constexpr uint8_t history_copy[] = {0x48, 0x8b, 0x42, 0x10, 0x48, 0x89, 0x41, 0x10, 0x48, 0x8b, 0x42, 0x18, 0x48, 0x89, 0x41, 0x18};
         const bool matches = std::memcmp(image + 0x152500u, build_view, sizeof(build_view)) == 0 &&
                              std::memcmp(image + 0xffd5fu, build_call, sizeof(build_call)) == 0 &&
                              std::memcmp(image + 0xf7f32u, frame_increment, sizeof(frame_increment)) == 0 &&
                              std::memcmp(image + 0x10b3e0u, history_copy, sizeof(history_copy)) == 0;
         if (!matches)
         {
            halton_status.store(Status::UnsupportedRenderer, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB Halton: producer layout unsupported; native jitter retained.");
            return;
         }
         void* target = reinterpret_cast<void*>(renderer_base + 0x152500u);
         void* original = nullptr;
         if (MH_CreateHook(target, reinterpret_cast<void*>(&CaptureJitterView), &original) != MH_OK)
         {
            halton_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB Halton: producer hook creation failed; native jitter retained.");
            return;
         }
         original_jitter_view = reinterpret_cast<JitterViewFunction>(original);
         // Scene prerequisites already pinned both modules. Publish the trampoline before activation.
         if (MH_QueueEnableHook(target) != MH_OK)
         {
            MH_RemoveHook(target);
            halton_status.store(Status::HookFailed, std::memory_order_release);
            return;
         }
         if (MH_ApplyQueued() != MH_OK)
         {
            halton_status.store(Status::HookFailed, std::memory_order_release);
            reshade::log::message(reshade::log::level::warning, "QB Halton: activation failed; pass-through trampoline retained until exit.");
            return;
         }
         halton_status.store(Status::Installed, std::memory_order_release);
         reshade::log::message(reshade::log::level::info, "QB Halton: producer hook installed; enabled for upscaling; native table and phase indices untouched.");
      }

      bool IsCurrentScene() noexcept
      {
         const auto& a = current_scene.association;
         const auto token = capture_state.Token();
         // Resource attribution is valid only while this exact main view remains active.
         return scene_status.load(std::memory_order_acquire) == Status::Installed && a.live && a.thread == GetCurrentThreadId() && a.token == token && a.view != 0u && current_scene.active_view == a.view;
      }
   } // namespace

   void TryInstall()
   {
      const auto temporal = temporal_status.load(std::memory_order_acquire);
      const auto scene = scene_status.load(std::memory_order_acquire);
      const bool pending = temporal == Status::NotInstalled ||
                           (temporal == Status::Installed && scene == Status::NotInstalled) ||
                           (scene == Status::Installed && halton_status.load(std::memory_order_acquire) == Status::NotInstalled);
      if (!pending) return; // Installation attempts are process-lifetime decisions.
      const auto module = GetModuleHandleW(L"renderer_x64_f.dll");
      if (module == nullptr) return;
      if (temporal_status.load(std::memory_order_acquire) == Status::NotInstalled)
      {
         InstallTemporal(module);
      }
      if (temporal_status.load(std::memory_order_acquire) == Status::Installed &&
          scene_status.load(std::memory_order_acquire) == Status::NotInstalled)
      {
         InstallScene(module);
      }
      if (scene_status.load(std::memory_order_acquire) == Status::Installed)
      {
         InstallHalton(module);
      }
   }

   void SetActive(bool active) noexcept
   {
      capture_state.SetActive(active);
   }
   void Reset() noexcept
   {
      capture_state.Reset();
      ChangeHaltonConfig(0u, 0u, true);
   }

   void SetHaltonPhases(uint32_t phases) noexcept
   {
      ChangeHaltonConfig(halton_phases_mask, static_cast<uint64_t>(std::clamp(phases, 8u, 128u)) << 1u);
   }

   bool ConsumeHaltonReset() noexcept
   {
      return halton_reset_pending.exchange(false, std::memory_order_acq_rel);
   }

   void RecordDepth(uintptr_t resource, uint32_t width, uint32_t height) noexcept
   {
      if (!IsCurrentScene() || resource == 0u)
      {
         return;
      }
      const uint64_t area = static_cast<uint64_t>(width) * height;
      if (area != 0u && (current_scene.depth_resource == 0u || area > current_scene.depth_area))
      {
         current_scene.depth_resource = resource;
         current_scene.depth_area = area;
      }
   }

   void RecordMotion(uintptr_t resource) noexcept
   {
      if (IsCurrentScene())
      {
         current_scene.motion_resource = resource;
      }
   }

   NativeInputFallback CopyCurrentInputs(NativeSceneCamera& camera, float& x, float& y, uintptr_t depth, uintptr_t motion)
   {
      const auto& scene = current_scene;
      const auto token = capture_state.Token();
      const bool installed = scene_status.load(std::memory_order_acquire) == Status::Installed &&
                             temporal_status.load(std::memory_order_acquire) == Status::Installed;
      const auto reason = EvaluateNativeInputs(scene.association, scene.camera, current_temporal.jitter, current_temporal.camera,
         current_temporal.view, token, GetCurrentThreadId(), installed,
         scene.active_view == scene.association.view, depth != 0u && scene.depth_resource == depth,
         motion != 0u && scene.motion_resource == motion);
      if (reason != NativeInputFallback::None) return reason;
      camera = current_temporal.camera;
      x = current_temporal.jitter.x;
      y = current_temporal.jitter.y;
      return reason;
   }

#if DEVELOPMENT || TEST
   bool IsHaltonEnabled() noexcept
   {
      return (halton_config.load(std::memory_order_acquire) & halton_enabled_bit) != 0u;
   }

   void SetHaltonEnabled(bool enabled) noexcept
   {
      ChangeHaltonConfig(halton_enabled_bit, enabled ? halton_enabled_bit : 0u);
      if (enabled) halton_faulted.store(false, std::memory_order_release);
      reshade::log::message(reshade::log::level::info, enabled ? "QB Halton: MODE requested=Halton." : "QB Halton: MODE requested=Native (debug comparison).");
   }

   const char* StatusText() noexcept
   {
      switch (temporal_status.load(std::memory_order_acquire))
      {
      case Status::Installing:
         return "Installing native SSAA hooks";
      case Status::Installed:
         return scene_status.load(std::memory_order_acquire) == Status::Installed ? "Installed (path 0 native inputs)" : "Scene inputs unavailable; game TAA";
      case Status::UnsupportedRenderer:
         return "Unsupported renderer; game TAA";
      case Status::HookFailed:
         return "Hook failed; game TAA";
      default:
         return "Not installed";
      }
   }
   bool IsPreferred() noexcept
   {
      return NativeJitterState::IsPreferred(capture_state.Token());
   }
   void SetPreferred(bool preferred) noexcept
   {
      capture_state.SetPreferred(preferred);
   }
#endif
} // namespace QuantumBreakUpscaling::NativeHooks
