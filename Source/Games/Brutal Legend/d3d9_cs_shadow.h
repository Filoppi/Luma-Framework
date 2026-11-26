// d3d9_cs_shadow.h - Get shadows for the D3D9 command-stream thread.
// Includes only d3d9_cs_tables.h, d3d9_cs_hook.h and d3d9_cs_queue.h. No game names, no addresses.
// Lookups, fills and Set-side updates run on the owner thread (mode != 0). Invalidations and surface-desc
// forgetting may run on any thread.
// The caches hold no reference: a bound render target or depth-stencil is kept alive by the device, a level
// surface by its texture. A dropped entry (texture Release-to-0, address reuse via Create,
// Reset, mode change) is the only way one goes stale.
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "d3d9_cs_tables.h"
#include "d3d9_cs_hook.h"
#include "d3d9_cs_queue.h"

static_assert(sizeof(void*) == 4, "d3d9_cs_shadow.h packs surface pointers into a 4-byte callback payload (Win32 only)");

// Dev value d3d9.cs_shadow (0 = off: lookups miss and nothing is filled).
inline std::atomic<int> g_cs_shadow{1};

// 1 between a successful BeginStateBlock and EndStateBlock: Set calls then change no device state, so no shadow update.
inline std::atomic<int> g_cs_shadow_recording{0};

struct CsShadowKey {
   void* texture;
   uint32_t face;   // 0 for 2D textures
   uint32_t level;
   bool operator==(const CsShadowKey& o) const = default;
};

struct CsShadowKeyHash {
   size_t operator()(const CsShadowKey& k) const noexcept {
      size_t h = std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(k.texture));
      const uint64_t packed = (static_cast<uint64_t>(k.face) << 32) | k.level;
      h ^= std::hash<uint64_t>{}(packed) + 0x9e3779b9u + (h << 6) + (h >> 2);
      return h;
   }
};

// Guards every cache in this file. Lookups take it shared; fills, Set updates and invalidations take it exclusive.
inline SRWLOCK g_cs_shadow_lock = SRWLOCK_INIT;

// Heap-allocated and never freed, so no static destruction order issues at process exit.
inline std::unordered_map<CsShadowKey, void*, CsShadowKeyHash>& CsShadowMap() {
   static auto* map = new std::unordered_map<CsShadowKey, void*, CsShadowKeyHash>();
   return *map;
}

// Surface pointer -> immutable D3DSURFACE_DESC, filled by the first blocking GetDesc.
inline std::unordered_map<void*, D3DSURFACE_DESC>& CsDescMap() {
   static auto* map = new std::unordered_map<void*, D3DSURFACE_DESC>();
   return *map;
}

// Device value/interface shadows: viewport, render targets 0..3 and the depth-stencil. Owner writes, any thread clears.
struct CsDeviceShadow {
   bool vp_valid = false;
   D3DVIEWPORT9 vp{};
   bool rt_valid[4] = {};
   void* rt[4] = {};
   bool ds_valid = false;
   void* ds = nullptr;
};
inline CsDeviceShadow g_cs_devstate;


inline void* CsShadowPtr(DWORD v) {
   return reinterpret_cast<void*>(static_cast<uintptr_t>(v));
}

// Decodes the key and the out-argument index from the raw argument block.
//   Texture::GetSurfaceLevel(this, Level, ppSurface)                 -> args[0]=this, args[1]=level, args[2]=pp
//   CubeTexture::GetCubeMapSurface(this, FaceType, Level, ppSurface) -> args[0]=this, args[1]=face,  args[2]=level, args[3]=pp
// Returns false for any other method, so the caller falls through to the real call.
inline bool CsShadowParse(const CsSlot* s, const DWORD* args, CsShadowKey& key, uint8_t& pp_arg) {
   if (!s || !s->method || !args)
      return false;

   const char* name = s->method->name;
   switch (s->method->iface) {
   case CsI_Texture:
      if (std::strcmp(name, "GetSurfaceLevel") != 0)
         return false;
      key.texture = CsShadowPtr(args[0]);
      key.face = 0;
      key.level = static_cast<uint32_t>(args[1]);
      pp_arg = 2;
      return true;
   case CsI_CubeTexture:
      if (std::strcmp(name, "GetCubeMapSurface") != 0)
         return false;
      key.texture = CsShadowPtr(args[0]);
      key.face = static_cast<uint32_t>(args[1]);
      key.level = static_cast<uint32_t>(args[2]);
      pp_arg = 3;
      return true;
   default:
      return false;
   }
}

// Worker-side callback: AddRef through the hooked vtable (CsDispatch runs it directly on the worker).
// Payload is the 4-byte surface pointer copied into the ring.
inline void CsShadowAddRefRun(const void* data) {
   void* surf = CsShadowPtr(*static_cast<const DWORD*>(data));
   auto add_ref = reinterpret_cast<ULONG(__stdcall*)(void*)>((*static_cast<void***>(surf))[1]);
   add_ref(surf);
}

// Async AddRef for a surface handed out from a shadow. Owner thread.
inline void CsShadowEnqueueAddRef(void* surf) {
   const DWORD payload = static_cast<DWORD>(reinterpret_cast<uintptr_t>(surf));
   CsEnqueueCallback(&CsShadowAddRefRun, &payload, sizeof(payload));
}

// Hit path: returns true and sets *pp, hr = D3D_OK and enqueues the AddRef. False = miss (caller does the blocking call).
// Miss also when g_cs_shadow == 0.
inline bool CsShadowGetSurface(const CsSlot* s, const DWORD* args, DWORD& hr) {
   if (g_cs_shadow.load(std::memory_order_relaxed) == 0)
      return false;

   CsShadowKey key{};
   uint8_t pp_arg = 0;
   if (!CsShadowParse(s, args, key, pp_arg))
      return false;

   void** out = reinterpret_cast<void**>(static_cast<uintptr_t>(args[pp_arg]));
   if (!out)
      return false;

   void* surf = nullptr;
   bool found = false;
   AcquireSRWLockShared(&g_cs_shadow_lock);
   auto& map = CsShadowMap();
   auto it = map.find(key);
   if (it != map.end()) {
      surf = it->second;
      found = true;
   }
   ReleaseSRWLockShared(&g_cs_shadow_lock);

   if (!found) {
      return false;
   }

   *out = surf;
   hr = static_cast<DWORD>(D3D_OK);
   CsShadowEnqueueAddRef(surf);
   return true;
}

// Surface GetDesc hit: copies the cached desc to args[1]. Owner thread.
inline bool CsShadowGetDesc(const DWORD* args, DWORD& hr) {
   auto* out = reinterpret_cast<D3DSURFACE_DESC*>(static_cast<uintptr_t>(args[1]));
   if (!out)
      return false;

   void* surf = CsShadowPtr(args[0]);
   bool found = false;
   AcquireSRWLockShared(&g_cs_shadow_lock);
   auto& map = CsDescMap();
   auto it = map.find(surf);
   if (it != map.end()) {
      *out = it->second;
      found = true;
   }
   ReleaseSRWLockShared(&g_cs_shadow_lock);

   if (!found) {
      return false;
   }
   hr = static_cast<DWORD>(D3D_OK);
   return true;
}

// GetViewport hit: copies the shadow to args[1]. Owner thread.
inline bool CsShadowGetViewport(const DWORD* args, DWORD& hr) {
   auto* out = reinterpret_cast<D3DVIEWPORT9*>(static_cast<uintptr_t>(args[1]));
   if (!out)
      return false;

   bool found = false;
   AcquireSRWLockShared(&g_cs_shadow_lock);
   if (g_cs_devstate.vp_valid) {
      *out = g_cs_devstate.vp;
      found = true;
   }
   ReleaseSRWLockShared(&g_cs_shadow_lock);

   if (!found) {
      return false;
   }
   hr = static_cast<DWORD>(D3D_OK);
   return true;
}

// GetRenderTarget(this, index, pp) when depth is false; GetDepthStencilSurface(this, pp) when depth is true.
// Hit: a NULL shadow answers D3DERR_NOTFOUND like D3D9; otherwise *pp = surface and an async AddRef is enqueued.
inline bool CsShadowGetTarget(bool depth, const DWORD* args, DWORD& hr) {
   const uint32_t index = depth ? 0u : static_cast<uint32_t>(args[1]);
   if (!depth && index >= 4u)
      return false;

   auto* out = reinterpret_cast<void**>(static_cast<uintptr_t>(args[depth ? 1 : 2]));
   if (!out)
      return false;

   void* surf = nullptr;
   bool valid = false;
   AcquireSRWLockShared(&g_cs_shadow_lock);
   if (depth) {
      valid = g_cs_devstate.ds_valid;
      surf = g_cs_devstate.ds;
   } else {
      valid = g_cs_devstate.rt_valid[index];
      surf = g_cs_devstate.rt[index];
   }
   ReleaseSRWLockShared(&g_cs_shadow_lock);

   if (!valid) {
      return false;
   }

   *out = surf;
   if (!surf) {
      hr = static_cast<DWORD>(D3DERR_NOTFOUND);
      return true;
   }
   hr = static_cast<DWORD>(D3D_OK);
   CsShadowEnqueueAddRef(surf);
   return true;
}

// Answers Get calls from the device shadows. Owner thread, mode != 0. True = answered with hr set.
inline bool CsShadowGetDevice(const CsSlot* s, const DWORD* args, DWORD& hr) {
   if (g_cs_shadow.load(std::memory_order_relaxed) == 0 || !s || !s->method || !args)
      return false;

   const CsMethod* m = s->method;
   if (m->iface == CsI_Surface)
      return std::strcmp(m->name, "GetDesc") == 0 && CsShadowGetDesc(args, hr);
   if (m->iface != CsI_Device)
      return false;
   if (std::strcmp(m->name, "GetViewport") == 0)
      return CsShadowGetViewport(args, hr);
   if (std::strcmp(m->name, "GetRenderTarget") == 0)
      return CsShadowGetTarget(false, args, hr);
   if (std::strcmp(m->name, "GetDepthStencilSurface") == 0)
      return CsShadowGetTarget(true, args, hr);
   return false;
}

// After a successful blocking miss: cache *pp under the key. Caller's hr must have been D3D_OK.
// No extra reference is taken (see file header).
inline void CsShadowFill(const CsSlot* s, const DWORD* args) {
   if (g_cs_shadow.load(std::memory_order_relaxed) == 0)
      return;

   CsShadowKey key{};
   uint8_t pp_arg = 0;
   if (!CsShadowParse(s, args, key, pp_arg))
      return;

   void** out = reinterpret_cast<void**>(static_cast<uintptr_t>(args[pp_arg]));
   if (!out)
      return;
   void* surf = *out;
   if (!surf)
      return;

   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   CsShadowMap().insert_or_assign(key, surf);
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);

}

// After a blocking call completed on the owner thread: caches the device shadow it answered (viewport, RT, DS, surface desc).
// Only successful results are cached; a NULL render target or depth-stencil is cached from D3DERR_NOTFOUND.
inline void CsShadowFillDevice(const CsSlot* s, const DWORD* args, DWORD hr) {
   if (g_cs_shadow.load(std::memory_order_relaxed) == 0 || !s || !s->method || !args)
      return;

   const CsMethod* m = s->method;
   const bool ok = static_cast<LONG>(hr) >= 0;
   const bool notfound = hr == static_cast<DWORD>(D3DERR_NOTFOUND);

   if (m->iface == CsI_Surface) {
      if (!ok || std::strcmp(m->name, "GetDesc") != 0)
         return;
      const auto* desc = reinterpret_cast<const D3DSURFACE_DESC*>(static_cast<uintptr_t>(args[1]));
      if (!desc)
         return;
      const D3DSURFACE_DESC copy = *desc;
      void* surf = CsShadowPtr(args[0]);
      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      CsDescMap().insert_or_assign(surf, copy);
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
      return;
   }

   if (m->iface != CsI_Device)
      return;

   if (std::strcmp(m->name, "GetViewport") == 0) {
      const auto* vp = reinterpret_cast<const D3DVIEWPORT9*>(static_cast<uintptr_t>(args[1]));
      if (!ok || !vp)
         return;
      const D3DVIEWPORT9 copy = *vp;
      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      g_cs_devstate.vp = copy;
      g_cs_devstate.vp_valid = true;
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
      return;
   }

   if (std::strcmp(m->name, "GetRenderTarget") == 0) {
      const uint32_t index = static_cast<uint32_t>(args[1]);
      auto* pp = reinterpret_cast<void**>(static_cast<uintptr_t>(args[2]));
      if (index >= 4u || !pp || (!ok && !notfound))
         return;
      void* surf = notfound ? nullptr : *pp;
      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      g_cs_devstate.rt[index] = surf;
      g_cs_devstate.rt_valid[index] = true;
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
      return;
   }

   if (std::strcmp(m->name, "GetDepthStencilSurface") == 0) {
      auto* pp = reinterpret_cast<void**>(static_cast<uintptr_t>(args[1]));
      if (!pp || (!ok && !notfound))
         return;
      void* surf = notfound ? nullptr : *pp;
      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      g_cs_devstate.ds = surf;
      g_cs_devstate.ds_valid = true;
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
   }
}

// Owner thread, mode != 0, before a Set* call is enqueued or run. Updates the device shadow to the state the call produces.
// Only SetViewport, SetRenderTarget and SetDepthStencilSurface are shadowed. A call that may fail (invalid viewport, NULL RT0)
// marks its shadow invalid instead of guessing.
inline void CsShadowNoteSet(const CsSlot* s, const DWORD* args) {
   const char* name = s->method->name;

   if (std::strcmp(name, "SetViewport") == 0) {
      const auto* vp = reinterpret_cast<const D3DVIEWPORT9*>(static_cast<uintptr_t>(args[1]));
      const bool valid = vp && vp->MinZ >= 0.f && vp->MinZ <= vp->MaxZ && vp->MaxZ <= 1.f;
      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      g_cs_devstate.vp_valid = valid;
      if (valid)
         g_cs_devstate.vp = *vp;
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
      return;
   }

   if (std::strcmp(name, "SetRenderTarget") == 0) {
      const uint32_t index = static_cast<uint32_t>(args[1]);
      void* surf = CsShadowPtr(args[2]);
      if (index >= 4u)
         return;

      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      if (index == 0) {
         if (!surf) {
            // D3D9 rejects a NULL RT0; the state is unknown here, so both shadows go invalid.
            g_cs_devstate.rt_valid[0] = false;
            g_cs_devstate.vp_valid = false;
         } else {
            g_cs_devstate.rt[0] = surf;
            g_cs_devstate.rt_valid[0] = true;
            // Setting RT0 resets the viewport to the full target. Needs the surface size from the desc cache.
            auto it = CsDescMap().find(surf);
            if (it != CsDescMap().end()) {
               g_cs_devstate.vp.X = 0;
               g_cs_devstate.vp.Y = 0;
               g_cs_devstate.vp.Width = it->second.Width;
               g_cs_devstate.vp.Height = it->second.Height;
               g_cs_devstate.vp.MinZ = 0.0f;
               g_cs_devstate.vp.MaxZ = 1.0f;
               g_cs_devstate.vp_valid = true;
            } else {
               g_cs_devstate.vp_valid = false;
            }
         }
      } else {
         g_cs_devstate.rt[index] = surf;
         g_cs_devstate.rt_valid[index] = true;
      }
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
      return;
   }

   if (std::strcmp(name, "SetDepthStencilSurface") == 0) {
      AcquireSRWLockExclusive(&g_cs_shadow_lock);
      g_cs_devstate.ds = CsShadowPtr(args[1]);
      g_cs_devstate.ds_valid = true;
      ReleaseSRWLockExclusive(&g_cs_shadow_lock);
   }
}

// Removes every entry whose texture key, surface value or desc key equals object. Any thread.
inline void CsShadowForget(void* object) {
   if (!object)
      return;

   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   auto& map = CsShadowMap();
   for (auto it = map.begin(); it != map.end();) {
      if (it->first.texture == object || it->second == object) {
         it = map.erase(it);
      } else {
         ++it;
      }
   }
   CsDescMap().erase(object);
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);
}

// Drops only the desc entry of a surface that is being released. Any thread.
inline void CsShadowForgetDesc(void* surface) {
   if (!surface)
      return;
   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   CsDescMap().erase(surface);
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);
}

// After any successful Create*: address reuse, so drop anything keyed or valued by that address. Any thread.
inline void CsShadowOnCreate(void* object) {
   CsShadowForget(object);
}

// On texture Release-to-0: same removal. Any thread.
inline void CsShadowOnRelease(void* object) {
   CsShadowForget(object);
}

// State-block proxy snapshot API (D3DSBT_ALL). A proxy snapshots the viewport at create and at Capture; its Apply
// restores that viewport (invalid snapshot = invalid shadow). Render targets and depth-stencil are never touched.
struct CsShadowViewportSnap {
   bool valid = false;
   D3DVIEWPORT9 vp{};
};

inline CsShadowViewportSnap CsShadowSnapshotViewport() {
   CsShadowViewportSnap s;
   AcquireSRWLockShared(&g_cs_shadow_lock);
   s.valid = g_cs_devstate.vp_valid;
   if (s.valid)
      s.vp = g_cs_devstate.vp;
   ReleaseSRWLockShared(&g_cs_shadow_lock);
   return s;
}

// Apply of a D3DSBT_ALL proxy, owner thread.
inline void CsShadowRestoreViewport(const CsShadowViewportSnap& s) {
   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   g_cs_devstate.vp_valid = s.valid;
   if (s.valid)
      g_cs_devstate.vp = s.vp;
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);
}

// Apply of a D3DSBT_ALL proxy from a foreign thread: the viewport is unknown afterwards.
inline void CsShadowInvalidateViewport() {
   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   g_cs_devstate.vp_valid = false;
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);
}

// Clears the device shadows. Any thread.
inline void CsShadowInvalidateDevice() {
   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   g_cs_devstate = CsDeviceShadow{};
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);
}

// Set*, Reset, BeginStateBlock, EndStateBlock (device) and StateBlock::Apply: the calls that change state the shadows cover.
inline bool CsShadowIsStateChange(const CsMethod* m) {
   const char* n = m->name;
   if (m->iface == CsI_StateBlock)
      return std::strcmp(n, "Apply") == 0;
   if (m->iface != CsI_Device)
      return false;
   return std::strncmp(n, "Set", 3) == 0 || std::strcmp(n, "Reset") == 0 ||
          std::strcmp(n, "BeginStateBlock") == 0 || std::strcmp(n, "EndStateBlock") == 0;
}

// Every hooked call on a non-worker, non-Present thread, before any other handling.
//   on_owner: the caller is the owner thread. threading: the active mode is not 0.
// Mode 0 keeps no shadows (every mode switch drops them), so nothing is done there.
// Foreign state changes invalidate the device shadows before the call goes to the mailbox.
// Owner state changes: Set* updates the shadow (skipped while recording), Begin/End/Apply invalidate.
inline void CsShadowPreCall(const CsSlot* s, const DWORD* args, bool on_owner, bool threading) {
   if (!threading || !s || !s->method || !args)
      return;

   const CsMethod* m = s->method;
   if (m->iface == CsI_Surface)
      return;

   if (!CsShadowIsStateChange(m))
      return;

   if (!on_owner) {
      CsShadowInvalidateDevice();
      return;
   }

   if (std::strncmp(m->name, "Set", 3) == 0) {
      if (g_cs_shadow_recording.load(std::memory_order_relaxed) == 0)
         CsShadowNoteSet(s, args);
      return;
   }

   CsShadowInvalidateDevice();
}

// After any hooked call completed, any thread, any mode. Tracks state-block recording (needed in mode 0 too).
inline void CsShadowFinish(const CsSlot* s, DWORD hr) {
   if (!s || !s->method || s->method->iface != CsI_Device)
      return;
   const char* n = s->method->name;
   if (std::strcmp(n, "BeginStateBlock") == 0) {
      if (static_cast<LONG>(hr) >= 0)
         g_cs_shadow_recording.store(1, std::memory_order_relaxed);
   } else if (std::strcmp(n, "EndStateBlock") == 0) {
      g_cs_shadow_recording.store(0, std::memory_order_relaxed);
   }
}

// Caller has drained the queue. Called before Reset and on mode change.
inline void CsShadowDropAll() {
   AcquireSRWLockExclusive(&g_cs_shadow_lock);
   CsShadowMap().clear();
   CsDescMap().clear();
   g_cs_devstate = CsDeviceShadow{};
   ReleaseSRWLockExclusive(&g_cs_shadow_lock);
}
