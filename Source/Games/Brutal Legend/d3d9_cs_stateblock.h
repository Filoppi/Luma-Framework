// d3d9_cs_stateblock.h - proxy IDirect3DStateBlock9 objects for the D3D9 command-stream thread.
// IDirect3DDevice9::CreateStateBlock returns a proxy at once; the real block is created on the worker, in stream order.
// Included by d3d9_cs_dispatch.h after CsAfterAsync() and CsMailboxRun() (uses the dispatch owner/mode globals).
// Proxies are never hooked or adopted: they are not dispatched through CsDispatch.
// Routing: mode off or queue stopped = direct call; owner thread = async ring command (blocking where a result is needed);
// foreign thread = mailbox, after the create command has run.
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#include "d3d9_cs_tables.h"
#include "d3d9_cs_hook.h"
#include "d3d9_cs_queue.h"
#include "d3d9_cs_shadow.h"

// Dev value d3d9.cs_async_sb (0 = CreateStateBlock stays blocking, as before).
inline std::atomic<int> g_cs_async_sb{1};

// Set by the call filter (d3d9_call_filter.h): runs on the calling thread before a proxy Apply (the filter's D3DInvalidateAll).
// Only Apply is hooked on real blocks (slot 5), so only Apply is mirrored; Capture is not hooked.
inline void (*g_cs_sb_apply_cb)() = nullptr;

struct CsSbProxy;

// Ring payload. done == null: async (result dropped). Otherwise the worker signals done with the result.
struct CsSbCmd {
   DWORD (*fn)(void*);
   void* ctx;
   CsDone* done;
};

struct CsSbQuery {
   CsSbProxy* p;
   void** out;
};

struct CsSbProxy {
   void** vtbl;
   volatile LONG refs;
   void* real;               // written by the worker in the create command
   volatile LONG created;    // 1 once the create command has run on the worker
   DWORD device_arg;         // create arguments, kept until the create command runs
   DWORD type;
   CsSlot* create_slot;
   CsShadowViewportSnap snap;
   CsSbProxy* pending_next;  // pending-release stack link (foreign Release-to-0)
};

enum : int { CsSbR_Direct, CsSbR_Owner, CsSbR_Foreign };

// Lock-free stack of proxies whose last Release happened on a foreign thread (or with threading off on another thread).
// The owner pops it at its next Present, before enqueuing the Present, so the real Release lands in stream order.
inline std::atomic<CsSbProxy*> g_cs_sb_pending{nullptr};

inline void CsSbPushPending(CsSbProxy* p) {
   CsSbProxy* head = g_cs_sb_pending.load(std::memory_order_relaxed);
   do {
      p->pending_next = head;
   } while (!g_cs_sb_pending.compare_exchange_weak(head, p, std::memory_order_release, std::memory_order_relaxed));
}

// Owner thread only when threading is on and the queue runs; everything else is direct.
inline int CsSbRoute() {
   if (g_cs_mode_active.load(std::memory_order_relaxed) == 0 || !cs_queue_detail::g_cs_queue.running)
      return CsSbR_Direct;
   return g_cs_owner_thread.load(std::memory_order_relaxed) == GetCurrentThreadId() ? CsSbR_Owner : CsSbR_Foreign;
}

inline void* CsSbSlotFn(void* obj, int idx) {
   return (*static_cast<void***>(obj))[idx];
}

inline void CsSbRingRun(const void* d) {
   const auto* c = static_cast<const CsSbCmd*>(d);
   const DWORD hr = c->fn(c->ctx);
   if (c->done)
      CsSignalDone(c->done, hr);
}

// Owner thread: queues fn(ctx) on the worker, no wait.
inline void CsSbOwnerAsync(DWORD (*fn)(void*), void* ctx) {
   const CsSbCmd c{fn, ctx, nullptr};
   CsEnqueueCallback(&CsSbRingRun, &c, sizeof(c));
   CsAfterAsync();
}

// Owner thread: queues fn(ctx) on the worker behind everything already queued, and waits for its result.
inline DWORD CsSbOwnerBlocking(DWORD (*fn)(void*), void* ctx) {
   CsDone done{};
   const CsSbCmd c{fn, ctx, &done};
   CsEnqueueCallback(&CsSbRingRun, &c, sizeof(c));
   CsWait(&done.done);
   return done.result;
}

// Worker-side bodies. ctx is always the proxy, except the query functions (CsSbQuery).

inline DWORD CsSbFnCreate(void* ctx) {
   auto* p = static_cast<CsSbProxy*>(ctx);
   DWORD a[3] = {p->device_arg, p->type, static_cast<DWORD>(reinterpret_cast<uintptr_t>(&p->real))};
   const DWORD hr = CsInvoke(p->create_slot->orig, a, 3);
   if (static_cast<LONG>(hr) < 0)
      CsHookLog(2, "[BL CS] async CreateStateBlock failed on the worker");
   InterlockedExchange(&p->created, 1);
   return hr;
}

inline DWORD CsSbFnApply(void* ctx) {
   auto* p = static_cast<CsSbProxy*>(ctx);
   if (!p->real)
      return static_cast<DWORD>(D3DERR_INVALIDCALL);
   return static_cast<DWORD>(reinterpret_cast<HRESULT(__stdcall*)(void*)>(CsSbSlotFn(p->real, 5))(p->real));
}

inline DWORD CsSbFnCapture(void* ctx) {
   auto* p = static_cast<CsSbProxy*>(ctx);
   if (!p->real)
      return static_cast<DWORD>(D3DERR_INVALIDCALL);
   return static_cast<DWORD>(reinterpret_cast<HRESULT(__stdcall*)(void*)>(CsSbSlotFn(p->real, 4))(p->real));
}

// Releases the real block and deletes the proxy. Runs on the worker (or directly in mode off).
inline DWORD CsSbFnRelease(void* ctx) {
   auto* p = static_cast<CsSbProxy*>(ctx);
   if (p->real)
      reinterpret_cast<ULONG(__stdcall*)(void*)>(CsSbSlotFn(p->real, 2))(p->real);
   delete p;
   return 0;
}

inline DWORD CsSbFnGetDevice(void* ctx) {
   auto* q = static_cast<CsSbQuery*>(ctx);
   if (!q->p->real)
      return static_cast<DWORD>(E_FAIL);
   return static_cast<DWORD>(reinterpret_cast<HRESULT(__stdcall*)(void*, void**)>(CsSbSlotFn(q->p->real, 3))(q->p->real, q->out));
}

// Owner thread, from CsPresent. Owner route (threading on): enqueued in stream order. Otherwise released here.
inline void CsSbFlushPending() {
   CsSbProxy* p = g_cs_sb_pending.exchange(nullptr, std::memory_order_acquire);
   while (p) {
      CsSbProxy* next = p->pending_next;
      if (CsSbRoute() == CsSbR_Owner)
         CsSbOwnerAsync(CsSbFnRelease, p);
      else
         CsSbFnRelease(p);
      p = next;
   }
}

// Blocking result (GetDevice). CsSbQueryInterface never blocks.
inline HRESULT CsSbBlocking(DWORD (*fn)(void*), CsSbProxy* p, CsSbQuery* q) {
   switch (CsSbRoute()) {
   case CsSbR_Owner:
      return static_cast<HRESULT>(CsSbOwnerBlocking(fn, q));
   case CsSbR_Foreign:
      CsWait(&p->created);
      return static_cast<HRESULT>(CsMailboxRun(fn, q));
   default:
      return static_cast<HRESULT>(fn(q));
   }
}

// Apply / Capture. Owner: async. Foreign: blocking mailbox. Direct: call.
inline HRESULT CsSbStateOp(CsSbProxy* p, DWORD (*fn)(void*), bool apply) {
   switch (CsSbRoute()) {
   case CsSbR_Owner:
      if (apply && p->type == D3DSBT_ALL)
         CsShadowRestoreViewport(p->snap);
      CsSbOwnerAsync(fn, p);
      return D3D_OK;
   case CsSbR_Foreign:
      CsWait(&p->created);
      if (apply && p->type == D3DSBT_ALL)
         CsShadowInvalidateViewport();
      return static_cast<HRESULT>(CsMailboxRun(fn, p));
   default:
      return static_cast<HRESULT>(fn(p));
   }
}

// Proxy vtable (IDirect3DStateBlock9 slots 0..5). __stdcall methods.

inline HRESULT __stdcall CsSbQueryInterface(CsSbProxy* p, REFIID riid, void** out) {
   if (!out)
      return E_POINTER;
   *out = nullptr;
   if (std::memcmp(&riid, &__uuidof(IUnknown), sizeof(GUID)) == 0 ||
       std::memcmp(&riid, &__uuidof(IDirect3DStateBlock9), sizeof(GUID)) == 0) {
      *out = p;
      InterlockedIncrement(&p->refs);
      return S_OK;
   }
   return E_NOINTERFACE;
}

inline ULONG __stdcall CsSbAddRef(CsSbProxy* p) {
   return static_cast<ULONG>(InterlockedIncrement(&p->refs));
}

inline ULONG __stdcall CsSbRelease(CsSbProxy* p) {
   const LONG r = InterlockedDecrement(&p->refs);
   if (r > 0)
      return static_cast<ULONG>(r);

   if (CsSbRoute() == CsSbR_Owner) {
      CsSbOwnerAsync(CsSbFnRelease, p);
      return 0;
   }
   // Threading off on the owner thread (or no Present yet): release now. Anything else is deferred to the owner's next Present.
   const DWORD owner = g_cs_owner_thread.load(std::memory_order_relaxed);
   if (owner == 0 || owner == GetCurrentThreadId())
      CsSbFnRelease(p);
   else
      CsSbPushPending(p);
   return 0;
}

inline HRESULT __stdcall CsSbGetDevice(CsSbProxy* p, void** dev) {
   if (!dev)
      return E_POINTER;
   CsSbQuery q{p, dev};
   return CsSbBlocking(CsSbFnGetDevice, p, &q);
}

inline HRESULT __stdcall CsSbCapture(CsSbProxy* p) {
   if (p->type == D3DSBT_ALL)
      p->snap = CsShadowSnapshotViewport();
   return CsSbStateOp(p, CsSbFnCapture, false);
}

inline HRESULT __stdcall CsSbApply(CsSbProxy* p) {
   if (g_cs_sb_apply_cb)
      g_cs_sb_apply_cb();   // calling thread, before the real Apply is queued or run (mirrors the call filter's Apply hook)
   return CsSbStateOp(p, CsSbFnApply, true);
}

inline void* g_cs_sb_vtbl[6] = {
   reinterpret_cast<void*>(&CsSbQueryInterface),
   reinterpret_cast<void*>(&CsSbAddRef),
   reinterpret_cast<void*>(&CsSbRelease),
   reinterpret_cast<void*>(&CsSbGetDevice),
   reinterpret_cast<void*>(&CsSbCapture),
   reinterpret_cast<void*>(&CsSbApply),
};

// Owner thread, threading active, from CsOwnerPreHook. True = answered: *ppSB is the proxy and hr = D3D_OK.
// Never adopted (CsFinishCall is skipped). The create command runs on the worker before any later command on the proxy.
inline bool CsStateBlockCreate(CsSlot* slot, DWORD* args, DWORD& hr) {
   if (g_cs_async_sb.load(std::memory_order_relaxed) == 0 || CsSbRoute() != CsSbR_Owner)
      return false;

   void** out = reinterpret_cast<void**>(static_cast<uintptr_t>(args[2]));
   if (!out)
      return false;

   auto* p = new (std::nothrow) CsSbProxy{};
   if (!p)
      return false;
   p->vtbl = g_cs_sb_vtbl;
   p->refs = 1;
   p->device_arg = args[0];
   p->type = args[1];
   p->create_slot = slot;
   if (p->type == D3DSBT_ALL)
      p->snap = CsShadowSnapshotViewport();
   CsShadowOnCreate(p);

   *out = p;
   CsSbOwnerAsync(CsSbFnCreate, p);
   hr = static_cast<DWORD>(D3D_OK);
   return true;
}

// True if the object is a CS state-block proxy (its vtable is ours). Used by the call filter (d3d9_call_filter.h) in release builds too.
inline bool CsSbIsProxy(const void* obj) {
   return obj && (*static_cast<void***>(const_cast<void*>(obj))) == g_cs_sb_vtbl;
}
