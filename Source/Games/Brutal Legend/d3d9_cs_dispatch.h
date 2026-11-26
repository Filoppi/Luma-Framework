#pragma once

// Brutal Legend: CsDispatch (direct and threaded modes) and the device vtable install glue for the D3D9 command stream.
// Thunks (d3d9_cs_hook.h) call CsDispatch(slot, args). Mode 0 calls the original entry and adopts returned
// interface pointers. Modes 1/2 run calls on the CsQueue worker (d3d9_cs_queue.h).

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

#include "d3d9_cs_tables.h"
#include "d3d9_cs_hook.h"
#include "d3d9_cs_queue.h"
#include "d3d9_cs_staging.h"
#include "d3d9_cs_shadow.h"

// Thread that made the last device Present. 0 until the first Present (no owner yet).
inline std::atomic<DWORD> g_cs_owner_thread{0};

// Maps the out interface. CsI_Count (base texture) is resolved with IDirect3DResource9::GetType (vtable slot 10).
inline CsIface CsResolveOutIface(void* obj, CsIface declared)
{
   if (declared != CsI_Count)
      return declared;

   using GetTypeFn = DWORD(__stdcall*)(void*);
   void** vt = *reinterpret_cast<void***>(obj);
   if (vt == nullptr || vt[10] == nullptr)
      return CsI_Count;

   switch (reinterpret_cast<GetTypeFn>(vt[10])(obj))
   {
   case D3DRTYPE_SURFACE:       return CsI_Surface;
   case D3DRTYPE_VOLUME:        return CsI_Volume;
   case D3DRTYPE_TEXTURE:       return CsI_Texture;
   case D3DRTYPE_VOLUMETEXTURE: return CsI_VolumeTexture;
   case D3DRTYPE_CUBETEXTURE:   return CsI_CubeTexture;
   case D3DRTYPE_VERTEXBUFFER:  return CsI_VertexBuffer;
   case D3DRTYPE_INDEXBUFFER:   return CsI_IndexBuffer;
   default:                     return CsI_Count;
   }
}

// Threaded modes. Owner = thread of the last Present. Worker = the CsQueue thread.
inline std::atomic<int> g_cs_mode_request{0}, g_cs_max_frames{1};
inline std::atomic<int> g_cs_async_refs{1};          // dev d3d9.cs_async_refs: 1 = owner-thread Surface AddRef/Release async (return 1)
inline std::atomic<int> g_cs_coalesce{0};            // dev d3d9.cs_coalesce: 1 = merge SetVertex/PixelShaderConstantF into the unpublished tail command
inline std::atomic<int> g_cs_mode_active{0};         // applied mode: 0 direct, 1 threaded, 2 drain after every async call
inline void (*g_cs_owner_present_cb)() = nullptr;    // owner thread, inside Present, before enqueue, only when the active mode != 0
inline bool g_cs_device_hooked = false;              // set by InstallD3D9Cs: false = device vtable hook failed

namespace cs_dispatch_detail {
inline constexpr uint32_t kCsRingBytes = 16u << 20;
inline constexpr uint32_t kCsMaxPayload = 256u << 10;  // larger async calls block instead
inline constexpr uint32_t kCsMaxArgs = 12;             // CsInvoke supports 1..12 arguments
inline constexpr uint32_t kCsPublishBatch = 16;        // owner publishes after this many async commands (or at any blocking point)
inline volatile LONG g_frames_queued = 0;              // Present commands queued and not yet executed
inline volatile LONG g_device_lost = 0;                // sticky D3DERR_DEVICELOST; cleared by a successful Reset
inline volatile LONG g_mail_pending = 0;
inline SRWLOCK g_mail_lock = SRWLOCK_INIT;
struct CsMailItem { CsSlot* slot; DWORD* args; CsDone done; CsMailItem* next; DWORD (*fn)(void*); void* ctx; };  // fn set: run fn(ctx) instead of slot
inline CsMailItem* g_mail_head = nullptr;
inline CsMailItem* g_mail_tail = nullptr;
}

inline bool CsRunning() { return cs_queue_detail::g_cs_queue.running; }

inline uint32_t CsAlign4(uint32_t v) { return (v + 3u) & ~3u; }

inline bool CsIsName(const CsMethod* m, const char* name) { return std::strcmp(m->name, name) == 0; }

inline bool CsIsCreate(const CsMethod* m) { return std::strncmp(m->name, "Create", 6) == 0; }

// Vertex count for a D3DPRIMITIVETYPE and primitive count.
inline uint32_t CsPrimVertexCount(DWORD type, DWORD count)
{
   switch (type)
   {
   case D3DPT_POINTLIST:     return count;
   case D3DPT_LINELIST:      return count * 2u;
   case D3DPT_LINESTRIP:     return count + 1u;
   case D3DPT_TRIANGLELIST:  return count * 3u;
   case D3DPT_TRIANGLESTRIP: return count + 2u;
   case D3DPT_TRIANGLEFAN:   return count + 2u;
   default:                  return 0;
   }
}

// Bytes copied for one pointer argument of an async row. args are the caller's values.
inline uint32_t CsPtrBytes(const CsPtrArg& p, const DWORD* args)
{
   switch (p.kind)
   {
   case CsP_Fixed:     return p.fixed_bytes;
   case CsP_Vec4Count: return args[p.count_arg] * 16u;
   case CsP_BoolCount: return args[p.count_arg] * 4u;
   case CsP_RectCount: return args[p.count_arg] * 16u;
   case CsP_UPVerts:
   {
      const DWORD stride = args[p.arg + 1];
      if (p.count_arg == 0)
         return CsPrimVertexCount(args[1], args[2]) * stride;                // DrawPrimitiveUP
      return (args[p.count_arg - 1] + args[p.count_arg]) * stride;           // DrawIndexedPrimitiveUP: MinVertexIndex + NumVertices
   }
   case CsP_UPIndices:
      return CsPrimVertexCount(args[1], args[p.count_arg]) * (args[p.arg + 1] == D3DFMT_INDEX16 ? 2u : 4u);
   default:
      return 0;
   }
}

// Owner thread, mode != 0. Copies the call and its async pointer data into the ring.
// Layout: +8 CsSlot*, +12 argc, +16 args[argc] (copied pointers hold their offset from the command start, 0 = null), then the data.
// publish = commit now (Present). Otherwise the command is published with its batch.
// False = not queued (the caller blocks instead).
inline bool CsEnqueueCall(CsSlot* slot, DWORD* args, bool publish = false)
{
   using namespace cs_dispatch_detail;
   const CsMethod* m = slot->method;
   if (m->argc == 0 || m->argc > kCsMaxArgs)
      return false;

   // Set{Vertex,Pixel}ShaderConstantF(StartRegister, pData, Vector4fCount). Same-range calls overwrite the unpublished
   // tail's data; contiguous ranges append to it. Never across another command, never into a published one.
   const bool const_f = m->iface == CsI_Device && m->argc == 4 && (m->slot == 94 || m->slot == 109);
   if (const_f && g_cs_coalesce.load(std::memory_order_relaxed) != 0 && args[2] != 0 && args[3] != 0 &&
       args[3] <= kCsMaxPayload / 16u)
   {
      const uint32_t nbytes = args[3] * 16u;
      CsCmd* prev = CsQueueUnpublishedTail();
      if (prev != nullptr && prev->op == CsOp_Call)
      {
         uint8_t* pp = reinterpret_cast<uint8_t*>(prev);
         CsSlot* pslot = nullptr;
         DWORD pa[4] = {};
         std::memcpy(&pslot, pp + 8, sizeof(pslot));
         std::memcpy(pa, pp + 16, sizeof(pa));          // pa[1] = StartRegister, pa[2] = data offset, pa[3] = count
         const uint8_t* src = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(args[2]));
         if (pslot == slot && pa[2] != 0)
         {
            if (pa[1] == args[1] && pa[3] == args[3])
            {
               std::memcpy(pp + pa[2], src, nbytes);
               if (publish)
                  CsQueueCommit();
               return true;
            }
            if (pa[1] + pa[3] == args[1])
            {
               const uint32_t old_bytes = prev->bytes;
               if (CsQueueTryExtend(nbytes) == prev)
               {
                  std::memcpy(pp + old_bytes, src, nbytes);      // the data is the command's last bytes
                  const DWORD count = pa[3] + args[3];
                  std::memcpy(pp + 16 + 4 * 3, &count, sizeof(count));
                  if (publish)
                     CsQueueCommit();
                  return true;
               }
            }
         }
      }
   }

   uint32_t sizes[4] = {};
   uint32_t total = 16u + 4u * m->argc;
   for (int i = 0; i < 4; ++i)
   {
      const CsPtrArg& pa = m->ptr[i];
      if (pa.kind == CsP_None || args[pa.arg] == 0)
         continue;
      sizes[i] = CsAlign4(CsPtrBytes(pa, args));
      total += sizes[i];
   }
   if (total > kCsMaxPayload)
      return false;

   CsCmd* c = CsQueueReserve(total);
   if (!c)
      return false;

   uint8_t* p = reinterpret_cast<uint8_t*>(c);
   c->op = CsOp_Call;

   DWORD tmp[kCsMaxArgs] = {};
   for (uint32_t j = 0; j < m->argc; ++j)
      tmp[j] = args[j];

   uint32_t cursor = 16u + 4u * m->argc;
   for (int i = 0; i < 4; ++i)
   {
      const CsPtrArg& pa = m->ptr[i];
      if (pa.kind == CsP_None || args[pa.arg] == 0)
         continue;
      std::memcpy(p + cursor, reinterpret_cast<const void*>(static_cast<uintptr_t>(args[pa.arg])), CsPtrBytes(pa, args));
      tmp[pa.arg] = cursor;
      cursor += sizes[i];
   }

   std::memcpy(p + 8, &slot, sizeof(slot));
   const uint32_t argc = m->argc;
   std::memcpy(p + 12, &argc, sizeof(argc));
   std::memcpy(p + 16, tmp, 4u * m->argc);


   CsQueueAfterWrite(c, const_f, kCsPublishBatch);
   if (publish)
      CsQueueCommit();
   return true;
}

inline void* CsOutObject(const CsMethod* m, const DWORD* args)
{
   if (m->out_arg == 0)
      return nullptr;
   void** out = reinterpret_cast<void**>(static_cast<uintptr_t>(args[m->out_arg]));
   return out ? *out : nullptr;
}

// Adoption of returned interfaces and mode-dependent hooks after a call completed.
// owner = the call ran on the owner thread (surface fills are owner-only). Create/Release hooks run on any thread.
inline void CsFinishCall(CsSlot* slot, DWORD* args, DWORD hr, bool owner)
{
   using namespace cs_dispatch_detail;
   const CsMethod* m = slot->method;
   const bool ok = static_cast<LONG>(hr) >= 0;

   void* obj = ok ? CsOutObject(m, args) : nullptr;
   if (obj != nullptr)
   {
      const CsIface iface = CsResolveOutIface(obj, m->out_iface);
      if (iface != CsI_Count)
         CsAdopt(obj, iface);
      CsShadowForgetDesc(obj);                                              // a returned interface may reuse a freed surface address
   }

   CsShadowFinish(slot, hr);
   if (g_cs_mode_active.load(std::memory_order_relaxed) == 0)
      return;

   if (ok && m->iface == CsI_Device && m->slot == 16)
      InterlockedExchange(&g_device_lost, 0);                               // successful Reset

   if (obj != nullptr && CsIsCreate(m))
   {
      CsShadowOnCreate(obj);
      if (m->iface == CsI_Device && (CsIsName(m, "CreateVertexBuffer") || CsIsName(m, "CreateIndexBuffer")))
         CsStagingOnCreate(obj, static_cast<UINT>(args[1]), static_cast<DWORD>(args[2]));
   }

   if ((m->iface == CsI_VertexBuffer || m->iface == CsI_IndexBuffer) && CsIsName(m, "Release"))
      CsStagingOnRelease(reinterpret_cast<void*>(static_cast<uintptr_t>(args[0])), static_cast<ULONG>(hr));
   else if ((m->iface == CsI_Texture || m->iface == CsI_CubeTexture || m->iface == CsI_VolumeTexture) &&
            CsIsName(m, "Release") && hr == 0)
      CsShadowOnRelease(reinterpret_cast<void*>(static_cast<uintptr_t>(args[0])));

   if (owner && ok && (CsIsName(m, "GetSurfaceLevel") || CsIsName(m, "GetCubeMapSurface")))
      CsShadowFill(slot, args);
   if (owner)
      CsShadowFillDevice(slot, args, hr);
}

// Direct call on the calling thread (mode 0, worker thread).
inline DWORD CsDirectCall(CsSlot* slot, DWORD* args)
{
   const DWORD hr = CsInvoke(slot->orig, args, slot->method->argc);
   CsFinishCall(slot, args, hr, false);
   return hr;
}

// Owner thread, blocking row: the worker runs the call; the caller waits. Falls back to a direct call if the queue is not running.
inline DWORD CsBlockingCall(CsSlot* slot, DWORD* args)
{
   if (!CsRunning())
      return CsInvoke(slot->orig, args, slot->method->argc);

   CsDone done{};
   CsCmd* c = CsQueueReserve(24);
   if (!c)
      return CsInvoke(slot->orig, args, slot->method->argc);

   uint8_t* p = reinterpret_cast<uint8_t*>(c);
   CsDone* dp = &done;
   c->op = CsOp_CallBlocking;
   std::memcpy(p + 8, &slot, sizeof(slot));
   std::memcpy(p + 12, &dp, sizeof(dp));
   std::memcpy(p + 16, &args, sizeof(args));
   CsQueueCommit();

   CsWait(&done.done);
   return done.result;
}

// Foreign thread (or no owner yet): blocking through the mailbox; the worker runs it between commands.
inline DWORD CsMailboxCall(CsSlot* slot, DWORD* args)
{
   using namespace cs_dispatch_detail;
   if (!CsRunning())
      return CsInvoke(slot->orig, args, slot->method->argc);

   CsMailItem it{};
   it.slot = slot;
   it.args = args;

   AcquireSRWLockExclusive(&g_mail_lock);
   if (g_mail_tail)
      g_mail_tail->next = &it;
   else
      g_mail_head = &it;
   g_mail_tail = &it;
   InterlockedIncrement(&g_mail_pending);
   ReleaseSRWLockExclusive(&g_mail_lock);
   CsQueueWake();                                                        // the worker polls the mailbox when it wakes

   CsWait(&it.done.done);
   return it.done.result;
}

// Foreign thread: runs fn(ctx) on the worker through the mailbox and waits. Direct call when the queue is not running.
inline DWORD CsMailboxRun(DWORD (*fn)(void*), void* ctx)
{
   using namespace cs_dispatch_detail;
   if (!CsRunning())
      return fn(ctx);

   CsMailItem it{};
   it.fn = fn;
   it.ctx = ctx;

   AcquireSRWLockExclusive(&g_mail_lock);
   if (g_mail_tail)
      g_mail_tail->next = &it;
   else
      g_mail_head = &it;
   g_mail_tail = &it;
   InterlockedIncrement(&g_mail_pending);
   ReleaseSRWLockExclusive(&g_mail_lock);
   CsQueueWake();

   CsWait(&it.done.done);
   return it.done.result;
}

// Worker: runs queued foreign calls. Called between commands and when idle.
inline void CsPollMailbox()
{
   using namespace cs_dispatch_detail;
   if (InterlockedCompareExchange(&g_mail_pending, 0, 0) == 0)
      return;

   AcquireSRWLockExclusive(&g_mail_lock);
   CsMailItem* it = g_mail_head;
   g_mail_head = nullptr;
   g_mail_tail = nullptr;
   ReleaseSRWLockExclusive(&g_mail_lock);

   while (it != nullptr)
   {
      CsMailItem* next = it->next;
      const DWORD hr = it->fn ? it->fn(it->ctx) : CsInvoke(it->slot->orig, it->args, it->slot->method->argc);
      InterlockedDecrement(&g_mail_pending);
      CsSignalDone(&it->done, hr);                                          // the caller may free `it` from here on
      it = next;
   }
}

// Worker: executes one command from the ring (CsQueue hands Call, CallBlocking and BufferUpload here).
inline void CsExecute(const CsCmd* c)
{
   using namespace cs_dispatch_detail;
   const uint8_t* p = reinterpret_cast<const uint8_t*>(c);
   switch (c->op)
   {
   case CsOp_Call:
   {
      CsSlot* slot = nullptr;
      uint32_t argc = 0;
      std::memcpy(&slot, p + 8, sizeof(slot));
      std::memcpy(&argc, p + 12, sizeof(argc));
      DWORD a[kCsMaxArgs] = {};
      std::memcpy(a, p + 16, 4u * argc);

      const CsMethod* m = slot->method;
      for (int i = 0; i < 4; ++i)
      {
         const CsPtrArg& pa = m->ptr[i];
         if (pa.kind != CsP_None && a[pa.arg] != 0)
            a[pa.arg] = static_cast<DWORD>(reinterpret_cast<uintptr_t>(p + a[pa.arg]));
      }

      const DWORD hr = CsInvoke(slot->orig, a, m->argc);

      if (m->iface == CsI_Device && m->slot == 17)
      {
         if (hr == static_cast<DWORD>(D3DERR_DEVICELOST))
            InterlockedExchange(&g_device_lost, 1);
         InterlockedDecrement(&g_frames_queued);
         WakeByAddressAll((void*)&g_frames_queued);
      }
      break;
   }
   case CsOp_CallBlocking:
   {
      CsSlot* slot = nullptr;
      CsDone* done = nullptr;
      DWORD* args = nullptr;
      std::memcpy(&slot, p + 8, sizeof(slot));
      std::memcpy(&done, p + 12, sizeof(done));
      std::memcpy(&args, p + 16, sizeof(args));
      const DWORD hr = CsInvoke(slot->orig, args, slot->method->argc);
      CsSignalDone(done, hr);                                               // the caller's stack args are read until here
      break;
   }
   case CsOp_BufferUpload:
      CsStagingExecute(c);
      break;
   default:
      break;
   }
}

// Owner thread: waits while more Presents are queued than g_cs_max_frames.
inline void CsWaitFramesQueued(CsSlot* present)
{
   using namespace cs_dispatch_detail;
   const LONG limit = std::max<LONG>(1, static_cast<LONG>(g_cs_max_frames.load(std::memory_order_relaxed)));
   LONG cur = InterlockedCompareExchange(&g_frames_queued, 0, 0);
   if (cur <= limit)
      return;

   const bool gui = IsGUIThread(FALSE) != FALSE;
   while (cur > limit)
   {
      LONG expect = cur;
      WaitOnAddress((void*)&g_frames_queued, &expect, sizeof(LONG), 1);
      if (gui)
      {
         MSG m;
         PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);
      }
      cur = InterlockedCompareExchange(&g_frames_queued, 0, 0);
   }
}


inline void CsAfterAsync()
{
   if (g_cs_mode_active.load(std::memory_order_relaxed) == 2)
      CsQueueDrain();
}

// Proxy state blocks (d3d9_cs_stateblock.h). Included here: it uses CsAfterAsync, CsMailboxRun and the owner/mode globals.
#include "d3d9_cs_stateblock.h"

// Owner thread, mode != 0: hooks that may answer a call without the worker. True = answered with hr.
inline bool CsOwnerPreHook(CsSlot* slot, DWORD* args, DWORD& hr)
{
   const CsMethod* m = slot->method;
   if (m->iface == CsI_Device && CsIsName(m, "CreateStateBlock"))
      return CsStateBlockCreate(slot, args, hr);
   if (m->iface == CsI_VertexBuffer || m->iface == CsI_IndexBuffer)
   {
      if (CsIsName(m, "Lock"))
         return CsStagingLock(slot, args, hr);
      if (CsIsName(m, "Unlock"))
         return CsStagingUnlock(slot, args, hr);
   }
   if (CsIsName(m, "GetSurfaceLevel") || CsIsName(m, "GetCubeMapSurface"))
      return CsShadowGetSurface(slot, args, hr);
   return CsShadowGetDevice(slot, args, hr);
}

// Owner thread, inside Present: drain, drop staging and shadows, start the queue on the first switch to non-zero.
inline void CsApplyModeRequest()
{
   using namespace cs_dispatch_detail;
   int req = g_cs_mode_request.load(std::memory_order_relaxed);
   if (req < 0 || req > 2)
      req = 0;
   const int active = g_cs_mode_active.load(std::memory_order_relaxed);
   if (req == active)
      return;

   if (CsRunning())
      CsQueueDrain();
   CsStagingDropAll();
   CsShadowDropAll();
   if (req != 0 && !CsRunning() && !CsQueueStart(kCsRingBytes, CsExecute, CsPollMailbox))
   {
      CsHookLog(2, "[BL CS] worker thread failed to start; staying in direct mode");
      req = 0;
   }
   g_cs_mode_active.store(req, std::memory_order_relaxed);
   CsHookLog(0, req ? "[BL CS] threaded mode on" : "[BL CS] threaded mode off");
}

// Device Present. Also defines the owner thread and applies the mode request.
inline DWORD CsPresent(CsSlot* slot, DWORD* args, DWORD tid, bool foreign)
{
   using namespace cs_dispatch_detail;
   if (foreign && CsRunning())
      CsQueueDrain();                                                       // a Present from another thread takes ownership
   g_cs_owner_thread.store(tid, std::memory_order_seq_cst);
   CsApplyModeRequest();
   CsSbFlushPending();                                                   // deferred proxy releases, before the Present is queued

   if (g_cs_mode_active.load(std::memory_order_relaxed) == 0)
   {
      const DWORD hr = CsDirectCall(slot, args);
      return hr;
   }

   if (InterlockedCompareExchange(&g_device_lost, 0, 0) != 0)
      return static_cast<DWORD>(D3DERR_DEVICELOST);

   if (g_cs_owner_present_cb)
      g_cs_owner_present_cb();

   // args[4] = pDirtyRegion: non-null blocks.
   if (args[4] == 0)
   {
      InterlockedIncrement(&g_frames_queued);
      if (CsEnqueueCall(slot, args, true))
      {
         CsAfterAsync();
         CsWaitFramesQueued(slot);
         return D3D_OK;
      }
      InterlockedDecrement(&g_frames_queued);
   }

   const DWORD hr = CsBlockingCall(slot, args);
   if (hr == static_cast<DWORD>(D3DERR_DEVICELOST))
      InterlockedExchange(&g_device_lost, 1);
   CsFinishCall(slot, args, hr, true);
   return hr;
}

// Stops the worker (owner thread, never from DllMain). Drops staging and shadows first.
inline void CsShutdown()
{
   using namespace cs_dispatch_detail;
   if (CsRunning())
      CsQueueDrain();
   CsStagingDropAll();
   CsShadowDropAll();
   CsQueueStop();
   g_cs_mode_active.store(0, std::memory_order_relaxed);
   g_cs_mode_request.store(0, std::memory_order_relaxed);
}

inline bool CsThreadingActive() { return g_cs_mode_active.load(std::memory_order_relaxed) != 0; }

// Target of every hooked device/interface thunk. args[0] is `this`; args[i] is the i-th stack dword.
extern "C" DWORD __cdecl CsDispatch(CsSlot* slot, DWORD* args)
{
   using namespace cs_dispatch_detail;
   const CsMethod* m = slot->method;
   const DWORD tid = GetCurrentThreadId();
   const DWORD owner = g_cs_owner_thread.load(std::memory_order_relaxed);
   const bool foreign = owner != 0 && owner != tid;

   // Owner: a mailbox post from another thread cannot commit the owner's pending batch, so the owner does it on its next call.
   if (!foreign && owner == tid && g_mail_pending != 0 && CsRunning())
      CsQueueCommit();

   const bool present = m->iface == CsI_Device && m->slot == 17;

   // Worker thread (dgVoodoo calling its own vtable) goes straight to the original.
   if (CsOnWorkerThread())
      return CsDirectCall(slot, args);
   if (present)
      return CsPresent(slot, args, tid, foreign);
   CsShadowPreCall(slot, args, owner == tid, g_cs_mode_active.load(std::memory_order_relaxed) != 0);
   if (g_cs_mode_active.load(std::memory_order_relaxed) == 0)
      return CsDirectCall(slot, args);

   // Foreign thread: blocking through the mailbox.
   if (foreign)
   {
      const DWORD hr = CsMailboxCall(slot, args);
      CsFinishCall(slot, args, hr, false);
      return hr;
   }

   // Owner thread, mode != 0.
   DWORD pre_hr = 0;
   if (CsOwnerPreHook(slot, args, pre_hr))
      return pre_hr;

   const bool surface_ref = m->iface == CsI_Surface && (CsIsName(m, "AddRef") || CsIsName(m, "Release"));
   if (surface_ref && g_cs_async_refs.load(std::memory_order_relaxed) != 0 && CsEnqueueCall(slot, args))
   {
      CsAfterAsync();
      return 1;
   }

   if (m->mode == CsM_Async && CsEnqueueCall(slot, args))
   {
      CsAfterAsync();
      return D3D_OK;
   }

   if (m->iface == CsI_Device && CsIsName(m, "Reset"))
   {
      CsQueueDrain();
      CsStagingDropAll();
      CsShadowDropAll();
   }

   const DWORD hr = CsBlockingCall(slot, args);
   CsFinishCall(slot, args, hr, true);
   return hr;
}


// Locates the device vtable with `accept` (see CsFindDeviceVtable: one match or nothing), hooks it, and returns it.
// Returns null when the locator fails; nothing is patched in that case (the locator logs the warning).
inline void** InstallD3D9Cs(HMODULE d3d9, bool (*accept)(void**))
{
   if (d3d9 == nullptr)
      return nullptr;
   void** vtable = CsFindDeviceVtable(d3d9, accept);
   if (vtable == nullptr)
      return nullptr;

   g_cs_device_hooked = CsHookVtable(vtable, CsI_Device);
   if (!g_cs_device_hooked)
      CsHookLog(2, "[BL CS] device vtable hook failed; the call filter still installs on the unhooked vtable");

   return vtable;
}
