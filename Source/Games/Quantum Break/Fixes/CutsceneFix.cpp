// Original mod implementation. No game assets or decompiled game source included.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "../../../External/reshade/deps/minhook/include/MinHook.h"
extern "C"
{
#include "MouseFix_Settings.h"
}

namespace
{
   using Function = void (*)(void*);
   using CameraFunction = void (*)(void*, void*, void*, void*);
   using TrackFunction = void (*)(void*, float);
   using EventFunction = void (*)(void*, int, float);
   BYTE* game;
   Function original_timeline, original_cut, refresh_lights;
   Function original_event_end, finish_camera_event;
   CameraFunction original_camera;
   TrackFunction original_track;
   EventFunction original_event_render;
   constexpr unsigned hook_count = 6;
   void* targets[hook_count] = {};
   bool created[hook_count] = {}, installed[hook_count] = {}, initialized, patched;
   std::atomic<bool> enabled{false};
   std::atomic<unsigned> calls{0}, cut_epoch{0}, last_light_epoch{0};
   std::atomic<unsigned> camera_corrections{0}, light_refreshes{0};
   thread_local BYTE* timeline_scope;
   SRWLOCK state_lock = SRWLOCK_INIT;
   struct Call
   {
      Call()
      {
         ++calls;
      }
      ~Call()
      {
         --calls;
      }
   };

   struct Patch
   {
      uint32_t rva;
      unsigned length;
      BYTE original[2], replacement[2];
   };
   const Patch patches[] = {
      {0x2dacb8, 1, {0x74, 0}, {0xeb, 0}},
      {0x2dbcc6, 1, {0x74, 0}, {0xeb, 0}},
      {0x101d98, 2, {0xff, 0xc8}, {0x66, 0x90}},
      {0x101deb, 1, {0xff, 0}, {0, 0}},
      {0x101e5d, 1, {0xff, 0}, {0, 0}}};
   const uint32_t contexts[] = {0x2dacb1, 0x2dbcbf, 0x101d91, 0x101de2, 0x101e50};
   const unsigned guard_lengths[] = {26, 26, 14, 14, 14};
   const BYTE guards[5][26] = {
      {0x80, 0xb9, 0x74, 0x07, 0, 0, 0, 0x74, 0x11, 0x48, 0x8b, 0x05, 0xff, 0x5c, 0xe9, 0, 0xc7, 0x80, 0x20, 0x02, 0, 0, 0x05, 0, 0, 0},
      {0x80, 0xb9, 0x74, 0x07, 0, 0, 0, 0x74, 0x11, 0x48, 0x8b, 0x05, 0xf1, 0x4c, 0xe9, 0, 0xc7, 0x80, 0x20, 0x02, 0, 0, 0x05, 0, 0, 0},
      {0x8b, 0x4b, 0x0c, 0x85, 0xc9, 0x7e, 0x04, 0xff, 0xc8, 0x03, 0xc1, 0x66, 0x0f, 0x6e},
      {0x8b, 0x4b, 0x0c, 0x85, 0xc9, 0x7e, 0x07, 0x8d, 0x50, 0xff, 0x03, 0xd1, 0xeb, 0x02},
      {0x85, 0xff, 0x75, 0x2f, 0x8b, 0x4b, 0x0c, 0x85, 0xc9, 0x7e, 0x08, 0x8d, 0x51, 0xff}};

   bool write_patch(const Patch& patch, bool active)
   {
      DWORD protection, ignored;
      BYTE* site = game + patch.rva;
      if (!VirtualProtect(site, patch.length, PAGE_EXECUTE_READWRITE, &protection)) return false;
      const BYTE* bytes = active ? patch.replacement : patch.original;
      if (patch.length == 2)
      {
         USHORT word;
         std::memcpy(&word, bytes, 2);
         *reinterpret_cast<volatile USHORT*>(site) = word;
      }
      else
         *reinterpret_cast<volatile BYTE*>(site) = bytes[0];
      FlushInstructionCache(GetCurrentProcess(), site, patch.length);
      VirtualProtect(site, patch.length, protection, &ignored);
      return true;
   }
   bool set_patches(bool active)
   {
      if (patched == active) return true;
      for (const auto& p : patches)
         if (std::memcmp(game + p.rva, active ? p.original : p.replacement, p.length)) return false;
      unsigned i = 0;
      for (; i < 5; i++)
         if (!write_patch(patches[i], active))
         {
            while (i > 0)
            {
               --i;
               write_patch(patches[i], !active);
            }
            return false;
         }
      patched = active;
      return true;
   }
   bool choose_camera_time(float previous, float current, float requested, float boundary, float& chosen)
   {
      chosen = requested;
      if (!std::isfinite(previous) || !std::isfinite(current) || !std::isfinite(requested) || !std::isfinite(boundary)) return false;
      if (previous < boundary && boundary <= current && requested < boundary && current - previous <= 2.0f)
      {
         chosen = current;
         return true;
      }
      return false;
   }
   void timeline_render(void* self)
   {
      Call call;
      BYTE* previous = timeline_scope;
      timeline_scope = static_cast<BYTE*>(self);
      original_timeline(self);
      timeline_scope = previous;
   }
   void track_render(void* self, float requested)
   {
      Call call;
      float chosen = requested;
      if (enabled.load(std::memory_order_acquire) && timeline_scope && timeline_scope[0x774] && *reinterpret_cast<int*>(timeline_scope + 0x1d8) == 1)
      {
         auto* track = static_cast<BYTE*>(self);
         auto** events = *reinterpret_cast<BYTE***>(track);
         const uint64_t count = *reinterpret_cast<uint64_t*>(track + 8);
         const float previous = *reinterpret_cast<float*>(timeline_scope + 0x618);
         const float current = *reinterpret_cast<float*>(timeline_scope + 0x614);
         if (events && count <= 4096)
            for (uint64_t i = 0; i < count; i++)
            {
               BYTE* event = events[i];
               if (!event || *reinterpret_cast<BYTE**>(event) != game + 0xaadbd8) continue;
               auto* settings = *reinterpret_cast<BYTE**>(event + 0x20);
               if (!settings || *reinterpret_cast<int*>(settings + 0x1d8) > 0) continue;
               const float boundary = static_cast<float>(*reinterpret_cast<int*>(event + 8));
               if (choose_camera_time(previous, current, requested, boundary, chosen))
               {
                  ++camera_corrections;
                  break;
               }
            }
      }
      original_track(self, chosen);
   }
   void camera_cut(void* self)
   {
      Call call;
      ++cut_epoch;
      original_cut(self);
   }
   void camera_update(void* self, void* arg2, void* arg3, void* arg4)
   {
      Call call;
      original_camera(self, arg2, arg3, arg4);
      const unsigned epoch = cut_epoch.load();
      if (last_light_epoch.exchange(epoch) == epoch || !enabled.load(std::memory_order_acquire)) return;
      auto* manager = *reinterpret_cast<BYTE**>(game + 0xf27410);
      if (manager)
      {
         refresh_lights(manager);
         ++light_refreshes;
      }
   }
   BYTE* camera_event_timeline(BYTE* event)
   {
      if (!event || *reinterpret_cast<BYTE**>(event) != game + 0xaadbd8) return nullptr;
      auto* track = *reinterpret_cast<BYTE**>(event + 0x28);
      return track ? *reinterpret_cast<BYTE**>(track + 0x40) : nullptr;
   }
   bool native_camera_bounds(BYTE* event, BYTE* timeline)
   {
      if (!timeline || !timeline[0x774]) return true;
      const int frames = *reinterpret_cast<int*>(timeline + 0x624);
      const int start = *reinterpret_cast<int*>(event + 8);
      const int duration = *reinterpret_cast<int*>(event + 0xc);
      return frames > 0 && start >= 0 && duration > 0 &&
             static_cast<int64_t>(start) + duration >= frames;
   }
   void camera_event_render(void* self, int mode, float requested)
   {
      Call call;
      auto* event = static_cast<BYTE*>(self);
      if (enabled.load(std::memory_order_acquire) && event &&
          *reinterpret_cast<BYTE**>(event) == game + 0xaadbd8 && std::isfinite(requested))
      {
         BYTE* timeline = camera_event_timeline(event);
         const int duration = *reinterpret_cast<int*>(event + 0xc);
         if (duration > 0 && native_camera_bounds(event, timeline))
         {
            const int64_t last = static_cast<int64_t>(*reinterpret_cast<int*>(event + 8)) + duration - 1;
            if (last >= 0 && last < 0x1000000)
            {
               // The three extended render comparisons are only needed between
               // shots in full cinematics. Gameplay animations and final shots
               // keep the engine's original begin/update/end boundary instead.
               if (mode == 0)
               {
                  if (requested < static_cast<float>(last)) return;
                  requested += 1.0f;
               }
               else if ((mode == 1 || mode == 2) && requested >= static_cast<float>(last))
                  return;
            }
         }
      }
      original_event_render(self, mode, requested);
   }
   bool camera_release_cannot_wait(BYTE* event)
   {
      BYTE* timeline = camera_event_timeline(event);
      // Noncinematic gameplay cameras retain their complete native lifecycle.
      if (!timeline || !timeline[0x774]) return false;
      const int state = *reinterpret_cast<int*>(timeline + 0x1d8);
      // Stopped timelines do not reach the render pass that consumes pending ends.
      if (state == 0 || state == 3) return true;
      const int frames = *reinterpret_cast<int*>(timeline + 0x624);
      const int start = *reinterpret_cast<int*>(event + 8);
      const int duration = *reinterpret_cast<int*>(event + 0xc);
      // The native clock ends at frames-1. Extending this event's render range to
      // frames must not also postpone the final camera's return to player control.
      return frames > 0 && start >= 0 && start < frames && duration > 0 &&
             static_cast<int64_t>(start) + duration >= frames;
   }
   void camera_event_end(void* self)
   {
      Call call;
      original_event_end(self);
      auto* event = static_cast<BYTE*>(self);
      if (enabled.load(std::memory_order_acquire) && event && event[0xe1] &&
          camera_release_cannot_wait(event))
         finish_camera_event(self);
      // Internal shots keep their deferred end: the native render pass releases
      // the outgoing camera before it starts the incoming camera in the same pass.
   }
   void remove_hooks()
   {
      for (unsigned i = 0; i < hook_count; i++)
         if (installed[i])
         {
            MH_DisableHook(targets[i]);
            installed[i] = false;
         }
      const ULONGLONG until = GetTickCount64() + 1000;
      while (calls.load() && GetTickCount64() < until)
         Sleep(1);
      // Retain trampolines if a call is still using them. The containing add-on is pinned.
      if (!calls.load())
         for (unsigned i = 0; i < hook_count; i++)
            if (created[i])
            {
               MH_RemoveHook(targets[i]);
               created[i] = false;
            }
      // MinHook is shared with the mouse input hooks; never uninitialize it here.
   }
} // namespace

extern "C" int QBMF_GetCutsceneEnabled()
{
   return enabled.load() ? 1 : 0;
}
extern "C" int QBMF_SetCutsceneEnabled(int value)
{
   AcquireSRWLockExclusive(&state_lock);
   if (!initialized)
   {
      ReleaseSRWLockExclusive(&state_lock);
      return 0;
   }
   const bool previous = enabled.exchange(false);
   const bool success = set_patches(value != 0);
   enabled.store(success ? value != 0 : previous, std::memory_order_release);
   ReleaseSRWLockExclusive(&state_lock);
   if (success) QBMF_SaveCutscenePreference(value);
   return success ? 1 : 0;
}
extern "C" void QBMF_GetCutsceneCounters(unsigned* out)
{
   if (out)
   {
      out[0] = camera_corrections.load();
      out[1] = light_refreshes.load();
   }
}
extern "C" int QBCF_Initialize()
{
   if (initialized) return 1;
   game = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
   auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game);
   if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x100000) return 0;
   auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(game + dos->e_lfanew);
   if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage < 0x11709c8) return 0;
   HMODULE renderer = GetModuleHandleW(L"renderer_x64_f.dll");
   if (!renderer) return 0;
   for (unsigned i = 0; i < 5; i++)
      if (std::memcmp(game + contexts[i], guards[i], guard_lengths[i])) return 0;
   targets[0] = game + 0x2e0410;
   targets[1] = game + 0x2e51c0;
   targets[2] = game + 0x46baa0;
   targets[3] = reinterpret_cast<void*>(GetProcAddress(renderer, "?makeCameraCut@VectorBlurWrapper@rend@@QEAAXXZ"));
   targets[4] = game + 0x101c00;
   targets[5] = game + 0x101d10;
   if (targets[3] != reinterpret_cast<BYTE*>(renderer) + 0x2883d0) return 0;
   refresh_lights = reinterpret_cast<Function>(game + 0x116c40);
   finish_camera_event = reinterpret_cast<Function>(game + 0x101b00);
   const BYTE timeline_guard[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x30, 0x83, 0xb9, 0xd8, 0x01, 0x00};
   const BYTE track_guard[] = {0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30, 0x33, 0xf6, 0x0f, 0x29, 0x74, 0x24, 0x20, 0x0f, 0x28, 0xf1, 0x4c};
   const BYTE camera_guard[] = {0x48, 0x83, 0xec, 0x68, 0x48, 0xc7, 0x44, 0x24, 0x20, 0xfe, 0xff, 0xff, 0xff, 0x48, 0x8b, 0x05};
   const BYTE light_guard[] = {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48};
   const BYTE event_end_guard[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x70, 0xc6, 0x81, 0xe1, 0, 0, 0, 1};
   const BYTE event_finish_guard[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x59, 0x28};
   const BYTE event_render_guard[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xec, 0x80, 0, 0, 0};
   if (std::memcmp(targets[0], timeline_guard, sizeof timeline_guard) || std::memcmp(targets[1], track_guard, sizeof track_guard) || std::memcmp(targets[2], camera_guard, sizeof camera_guard) || std::memcmp(game + 0x116c40, light_guard, sizeof light_guard) || std::memcmp(targets[4], event_end_guard, sizeof event_end_guard) || std::memcmp(game + 0x101b00, event_finish_guard, sizeof event_finish_guard) || std::memcmp(targets[5], event_render_guard, sizeof event_render_guard) || *reinterpret_cast<BYTE**>(game + 0xaadbd8 + 0x10) != game + 0x101c00 || *reinterpret_cast<BYTE**>(game + 0xaadbd8 + 0x80) != game + 0x101d10) return 0;
   const MH_STATUS status = MH_Initialize();
   if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) return 0;
   void* callbacks[] = {reinterpret_cast<void*>(&timeline_render), reinterpret_cast<void*>(&track_render), reinterpret_cast<void*>(&camera_update), reinterpret_cast<void*>(&camera_cut), reinterpret_cast<void*>(&camera_event_end), reinterpret_cast<void*>(&camera_event_render)};
   void** originals[] = {reinterpret_cast<void**>(&original_timeline), reinterpret_cast<void**>(&original_track), reinterpret_cast<void**>(&original_camera), reinterpret_cast<void**>(&original_cut), reinterpret_cast<void**>(&original_event_end), reinterpret_cast<void**>(&original_event_render)};
   for (unsigned i = 0; i < hook_count; i++)
   {
      if (MH_CreateHook(targets[i], callbacks[i], originals[i]) != MH_OK)
      {
         remove_hooks();
         return 0;
      }
      created[i] = true;
   }
   HMODULE pinned;
   if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&track_render), &pinned))
   {
      remove_hooks();
      return 0;
   }
   for (unsigned i = 0; i < hook_count; i++)
   {
      if (MH_EnableHook(targets[i]) != MH_OK)
      {
         remove_hooks();
         return 0;
      }
      installed[i] = true;
   }
   initialized = true;
   return 1;
}
extern "C" void QBCF_Shutdown()
{
   AcquireSRWLockExclusive(&state_lock);
   enabled = false;
   if (patched) set_patches(false);
   initialized = false;
   ReleaseSRWLockExclusive(&state_lock);
   remove_hooks();
}
