#pragma once

// Dynamic VB/IB staging for the D3D9 command-stream thread.
// Owner thread: Lock on a dynamic buffer returns a pointer into a shadow block; Unlock enqueues
// CsOp_BufferUpload. The worker runs the real Lock, memcpy, Unlock, then drops its block reference.
// Only D3DUSAGE_DYNAMIC buffers are recorded.

#include <windows.h>
#include <d3d9.h>
#include <malloc.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "d3d9_cs_tables.h"
#include "d3d9_cs_hook.h"
#include "d3d9_cs_queue.h"

namespace cs_staging_detail {

constexpr uint64_t kCapBytes = 64ull << 20; // global shadow cap
constexpr uint32_t kMinClass = 12;          // 4 KB
constexpr uint32_t kClassCount = 34;        // classes 12..32 used
constexpr uint64_t kWwMinBytes = 64ull << 10; // blocks >= 64 KB use VirtualAlloc + MEM_WRITE_WATCH
constexpr uintptr_t kPageBytes = 4096;

// Block header; data follows at sizeof(CsStgBlock) (16-byte aligned).
#pragma warning(push)
#pragma warning(disable : 4324) // padded to 16 on purpose (data alignment)
struct alignas(16) CsStgBlock {
    volatile LONG refs;
    uint32_t cls;
    uint32_t ww;           // 1 = VirtualAlloc'd with MEM_WRITE_WATCH (own free list)
    uint64_t cap;
    CsStgBlock* next;
    uint8_t* data() { return reinterpret_cast<uint8_t*>(this) + sizeof(CsStgBlock); }
};
#pragma warning(pop)

// Per-buffer record. Fields are touched by the owner thread only; the map is guarded by g_map_lock.
struct CsStgRec {
    void* buffer;
    UINT length;
    CsStgBlock* current;   // record owns one reference
    void* lock_fn;         // orig Lock, stored at the last write lock of the session
    UINT lo, hi;           // union of write ranges in the open session
    bool locked;           // a shadow lock session is open
    bool lock_write;       // the open session has a write range (upload at Unlock)
    bool discard_pending;  // next upload uses D3DLOCK_DISCARD
};

// Payload of CsOp_BufferUpload (follows the 8-byte CsCmd header).
struct CsStgUpload {
    void* buffer;
    CsStgBlock* block;     // worker owns one reference
    void* lock_fn;         // orig Lock
    void* unlock_fn;       // orig Unlock
    UINT offset;
    UINT size;
    DWORD flags;
};

constexpr uint32_t kStgUploadBytes = (8u + static_cast<uint32_t>(sizeof(CsStgUpload)) + 7u) & ~7u;

using StgLockFn = DWORD(__stdcall*)(void*, UINT, UINT, void**, DWORD);
using StgUnlockFn = DWORD(__stdcall*)(void*);

inline SRWLOCK g_map_lock = SRWLOCK_INIT;   // guards g_map (Create/Release can arrive from any thread)
inline SRWLOCK g_pool_lock = SRWLOCK_INIT;  // guards g_free, g_total, block refcount transitions to 0
inline std::unordered_map<void*, CsStgRec*> g_map;
inline std::unordered_set<void*> g_static_set; // buffers queried once and found non-dynamic (map lock)
inline CsStgBlock* g_free[kClassCount] = {};
inline CsStgBlock* g_free_ww[kClassCount] = {}; // write-watch blocks (pool lock)
inline uint64_t g_total = 0;                // bytes of all live blocks (pool lock)


inline uint32_t ClassFor(UINT length, uint64_t& cap) {
    uint32_t cls = kMinClass;
    uint64_t c = 1ull << kMinClass;
    while (c < length) {
        c <<= 1;
        ++cls;
    }
    cap = c;
    return cls;
}

inline CsStgRec* Find(void* buffer) {
    AcquireSRWLockShared(&g_map_lock);
    auto it = g_map.find(buffer);
    CsStgRec* r = (it == g_map.end()) ? nullptr : it->second;
    ReleaseSRWLockShared(&g_map_lock);
    return r;
}

// Drops one reference; the block goes to its free list at zero.
inline void Unref(CsStgBlock* b) {
    if (InterlockedDecrement(&b->refs) != 0) return;
    AcquireSRWLockExclusive(&g_pool_lock);
    CsStgBlock** head = b->ww ? &g_free_ww[b->cls] : &g_free[b->cls];
    b->next = *head;
    *head = b;
    ReleaseSRWLockExclusive(&g_pool_lock);
}

// Frees every block on the free lists (refcount 0).
inline void ReclaimFree() {
    AcquireSRWLockExclusive(&g_pool_lock);
    for (uint32_t c = 0; c < kClassCount; ++c) {
        for (CsStgBlock** head : {&g_free[c], &g_free_ww[c]}) {
            while (CsStgBlock* b = *head) {
                *head = b->next;
                g_total -= b->cap;
                if (b->ww) VirtualFree(b, 0, MEM_RELEASE);
                else _aligned_free(b);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_pool_lock);
}

// Pops a free block of the class, or allocates one if under the cap. Returned block has refs = 1.
// Blocks >= kWwMinBytes come from the write-watch list only; if MEM_WRITE_WATCH is unavailable the
// block falls back to a plain allocation (ww = 0) and is recycled on the normal list.
inline CsStgBlock* TryTake(uint32_t cls, uint64_t cap) {
    const bool ww = cap >= kWwMinBytes;
    CsStgBlock* b = nullptr;
    AcquireSRWLockExclusive(&g_pool_lock);
    CsStgBlock** head = ww ? &g_free_ww[cls] : &g_free[cls];
    if (*head) {
        b = *head;
        *head = b->next;
    } else if (cap <= kCapBytes && g_total + cap <= kCapBytes) {
        const size_t bytes = sizeof(CsStgBlock) + static_cast<size_t>(cap);
        if (ww) {
            const size_t rounded = (bytes + kPageBytes - 1) & ~(kPageBytes - 1);
            b = static_cast<CsStgBlock*>(VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
        }
        if (b) {
            b->ww = 1;
        } else {
            b = static_cast<CsStgBlock*>(_aligned_malloc(bytes, 16));
            if (b) b->ww = 0;
        }
        if (b) {
            b->cls = cls;
            b->cap = cap;
            g_total += cap;
        }
    }
    if (b) b->refs = 1;
    ReleaseSRWLockExclusive(&g_pool_lock);
    return b;
}

// Owner thread. At the cap: drain the queue, reclaim free blocks, retry. Null = cap hit.
inline CsStgBlock* AllocFor(UINT length) {
    uint64_t cap = 0;
    const uint32_t cls = ClassFor(length, cap);
    if (CsStgBlock* b = TryTake(cls, cap)) return b;
    CsQueueDrain();
    ReclaimFree();
    CsStgBlock* b = TryTake(cls, cap);
    return b;
}

// Releases the record's reference to its current shadow. Owner thread, no open session.
inline void DropCurrent(CsStgRec* r) {
    if (CsStgBlock* b = r->current) {
        r->current = nullptr;
        Unref(b);
    }
}

// Takes over the caller's reference to b; drops the previous current block.
inline void SetCurrent(CsStgRec* r, CsStgBlock* b) {
    CsStgBlock* old = r->current;
    r->current = b;
    if (old) Unref(old);
}

inline void WriteUpload(uint8_t* p, const CsStgUpload& up) {
    CsCmd hdr{};
    hdr.op = CsOp_BufferUpload;
    hdr.bytes = kStgUploadBytes;
    std::memcpy(p, &hdr, sizeof(hdr));
    std::memcpy(p + sizeof(CsCmd), &up, sizeof(up));
}


} // namespace cs_staging_detail

inline std::atomic<int> g_cs_staging{1}; // dev value d3d9.cs_staging
inline std::atomic<int> g_cs_writewatch{1}; // dev value d3d9.cs_writewatch: 0 = upload the whole locked range

// Owner thread (or any thread for Create). Records only D3DUSAGE_DYNAMIC buffers.
// Also drops any old record at the same address (address reuse).
inline void CsStagingOnCreate(void* buffer, UINT length, DWORD usage) {
    using namespace cs_staging_detail;
    if (!buffer) return;
    AcquireSRWLockExclusive(&g_map_lock);
    g_static_set.erase(buffer);
    auto it = g_map.find(buffer);
    if (it != g_map.end()) {
        DropCurrent(it->second);
        delete it->second;
        g_map.erase(it);
    }
    if ((usage & D3DUSAGE_DYNAMIC) && length) {
        g_map.emplace(buffer, new CsStgRec{buffer, length, nullptr, nullptr, 0, 0, false, false, false});
    }
    ReleaseSRWLockExclusive(&g_map_lock);
}

// Owner thread. For a buffer with no record and no static marker (created before staging was on):
// GetDesc through the object's own vtable (slot 13 in both IDirect3DVertexBuffer9 and
// IDirect3DIndexBuffer9, see d3d9_cs_tables.h; the call goes through the CS thunk). Dynamic -> record;
// anything else -> static marker so it is never asked again.
inline void CsStagingQueryUnknown(void* buffer) {
    using namespace cs_staging_detail;
    using GetDescFn = DWORD(__stdcall*)(void*, D3DVERTEXBUFFER_DESC*);
    constexpr size_t kGetDescSlot = 13;
    void** vtbl = *reinterpret_cast<void***>(buffer);
    D3DVERTEXBUFFER_DESC desc{}; // same first five fields as D3DINDEXBUFFER_DESC
    const DWORD hr = reinterpret_cast<GetDescFn>(vtbl[kGetDescSlot])(buffer, &desc);
    if (hr == 0 && (desc.Usage & D3DUSAGE_DYNAMIC) && desc.Size) {
        CsStagingOnCreate(buffer, desc.Size, desc.Usage);
        return;
    }
    AcquireSRWLockExclusive(&g_map_lock);
    g_static_set.insert(buffer);
    ReleaseSRWLockExclusive(&g_map_lock);
}

// After a blocking Release. Only new_count == 0 drops the record.
inline void CsStagingOnRelease(void* buffer, ULONG new_count) {
    using namespace cs_staging_detail;
    if (new_count != 0 || !buffer) return;
    AcquireSRWLockExclusive(&g_map_lock);
    g_static_set.erase(buffer);
    auto it = g_map.find(buffer);
    if (it != g_map.end()) {
        DropCurrent(it->second);
        delete it->second;
        g_map.erase(it);
    }
    ReleaseSRWLockExclusive(&g_map_lock);
}

// Caller has drained. Before Reset and on mode change.
inline void CsStagingDropAll() {
    using namespace cs_staging_detail;
    AcquireSRWLockExclusive(&g_map_lock);
    for (auto& kv : g_map) {
        DropCurrent(kv.second);
        delete kv.second;
    }
    g_map.clear();
    g_static_set.clear();
    ReleaseSRWLockExclusive(&g_map_lock);
    ReclaimFree();
}

// Worker: CsOp_BufferUpload -> orig Lock(offset, size, flags) / memcpy / orig Unlock / unref block.
inline void CsStagingExecute(const CsCmd* cmd) {
    using namespace cs_staging_detail;
    CsStgUpload up{};
    std::memcpy(&up, reinterpret_cast<const uint8_t*>(cmd) + sizeof(CsCmd), sizeof(up));
    void* dst = nullptr;
    const DWORD hr = reinterpret_cast<StgLockFn>(up.lock_fn)(up.buffer, up.offset, up.size, &dst, up.flags);
    if (hr == 0 && dst) {
        std::memcpy(dst, up.block->data() + up.offset, up.size);
        reinterpret_cast<StgUnlockFn>(up.unlock_fn)(up.buffer);
    }
    Unref(up.block);
}

// Owner thread, mode != 0. true = handled (hr = D3D_OK, or an error for an invalid range).
namespace cs_staging_detail {

// Write-watch helpers (ww blocks only). Ranges are rounded out to whole pages of the block's region.
inline void ResetRangeWatch(CsStgBlock* b, UINT lo, UINT hi) {
    const uintptr_t d = reinterpret_cast<uintptr_t>(b->data());
    const uintptr_t rs = (d + lo) & ~(kPageBytes - 1);
    const uintptr_t re = (d + hi + kPageBytes - 1) & ~(kPageBytes - 1);
    ResetWriteWatch(reinterpret_cast<void*>(rs), static_cast<SIZE_T>(re - rs));
}

// Dirty span of [lo, hi) as offsets into the block data: first dirty page start to last dirty page end,
// clamped to the range. any = false when no page in the range was written. false = GetWriteWatch failed.
inline bool DirtySpan(CsStgBlock* b, UINT lo, UINT hi, UINT& off, UINT& sz, bool& any) {
    const uintptr_t d = reinterpret_cast<uintptr_t>(b->data());
    const uintptr_t lo_p = d + lo;
    const uintptr_t hi_p = d + hi;
    const uintptr_t rs = lo_p & ~(kPageBytes - 1);
    const uintptr_t re = (hi_p + kPageBytes - 1) & ~(kPageBytes - 1);
    const SIZE_T n = static_cast<SIZE_T>(re - rs);
    thread_local std::vector<void*> pages;
    pages.resize(n / kPageBytes);
    ULONG_PTR count = static_cast<ULONG_PTR>(pages.size());
    ULONG gran = 0;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, reinterpret_cast<void*>(rs), n, pages.data(), &count, &gran) != 0) return false;
    any = false;
    uintptr_t first = 0, last = 0;
    for (ULONG_PTR i = 0; i < count; ++i) {
        const uintptr_t p = reinterpret_cast<uintptr_t>(pages[i]);
        const uintptr_t ps = (std::max)(p, lo_p);
        const uintptr_t pe = (std::min)(p + kPageBytes, hi_p);
        if (ps >= pe) continue;
        if (!any || ps < first) first = ps;
        if (!any || pe > last) last = pe;
        any = true;
    }
    if (any) {
        off = static_cast<UINT>(first - d);
        sz = static_cast<UINT>(last - first);
    }
    return true;
}

} // namespace cs_staging_detail

inline bool CsStagingLock(const CsSlot* s, const DWORD* args, DWORD& hr) {
    using namespace cs_staging_detail;
    void* buf = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0]));
    CsStgRec* rec = Find(buf);
    if (!rec) {
        AcquireSRWLockShared(&g_map_lock);
        const bool is_static = g_static_set.count(buf) != 0;
        ReleaseSRWLockShared(&g_map_lock);
        if (is_static) return false;
        CsStagingQueryUnknown(buf);
        rec = Find(buf);
        if (!rec) return false;
    }
    void** ppb = reinterpret_cast<void**>(static_cast<uintptr_t>(args[3]));
    if (!ppb) return false;

    const DWORD flags = args[4];
    const bool readonly = (flags & D3DLOCK_READONLY) != 0;
    const bool discard = !readonly && (flags & D3DLOCK_DISCARD) != 0;
    const bool nooverwrite = !readonly && !discard && (flags & D3DLOCK_NOOVERWRITE) != 0;

    // Staging off: pass through and invalidate the shadow (the real buffer is about to change).
    if (g_cs_staging.load(std::memory_order_relaxed) == 0 && !rec->locked) {
        CsQueueDrain();
        DropCurrent(rec);
        return false;
    }

    // SizeToLock 0 = whole buffer (offset ignored).
    UINT lo = 0, hi = rec->length;
    if (args[2] != 0) {
        const uint64_t end = static_cast<uint64_t>(args[1]) + args[2];
        if (args[1] >= rec->length || end > rec->length) {
            hr = static_cast<DWORD>(D3DERR_INVALIDCALL);
            return true;
        }
        lo = args[1];
        hi = static_cast<UINT>(end);
    }

    CsStgBlock* b = rec->current;
    if (readonly) {
        // No upload at Unlock. Serve the current shadow after the queue has drained.
        CsQueueDrain();
        if (!b) return false;
    } else if (discard) {
        if (!rec->locked) {
            CsStgBlock* nb = AllocFor(rec->length);
            if (!nb) {
                CsQueueDrain();
                DropCurrent(rec);
                return false;
            }
            SetCurrent(rec, nb);
            b = nb;
        }
        rec->discard_pending = true;
    } else if (nooverwrite) {
        if (!b) {
            // No shadow yet, so drain and pass through to the real buffer. A later DISCARD lock creates the shadow.
            CsQueueDrain();
            return false;
        }
    } else {
        // Other flags (0, NOSYSLOCK, ...): drain, then serve the shadow if one exists.
        // No shadow: pass through to the real buffer (Unlock returns false because locked stays false).
        CsQueueDrain();
        if (!b) return false;
    }

    if (!rec->locked) {
        rec->locked = true;
        rec->lock_write = false;
    }
    if (!readonly) {
        if (!rec->lock_write) {
            rec->lock_write = true;
            rec->lo = lo;
            rec->hi = hi;
        } else {
            rec->lo = (std::min)(rec->lo, lo);
            rec->hi = (std::max)(rec->hi, hi);
        }
        rec->lock_fn = s->orig;
    }

    if (b->ww && g_cs_writewatch.load(std::memory_order_relaxed) != 0) ResetRangeWatch(b, lo, hi);
    *ppb = b->data() + lo;
    hr = 0;
    return true;
}

// Owner thread. true = handled. Enqueues one upload per session (union range), or none for READONLY.
inline bool CsStagingUnlock(const CsSlot* s, const DWORD* args, DWORD& hr) {
    using namespace cs_staging_detail;
    void* buffer = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0]));
    CsStgRec* rec = Find(buffer);
    if (!rec || !rec->locked) return false;
    rec->locked = false;
    hr = 0;
    if (!rec->lock_write) return true;

    const bool discard = rec->discard_pending;
    const DWORD flags = discard ? static_cast<DWORD>(D3DLOCK_DISCARD) : static_cast<DWORD>(D3DLOCK_NOOVERWRITE);
    rec->discard_pending = false;

    CsStgBlock* b = rec->current;
    const UINT full_sz = rec->hi - rec->lo;
    UINT off = rec->lo;
    UINT sz = full_sz;
    if (b->ww && g_cs_writewatch.load(std::memory_order_relaxed) != 0) {
        UINT dirty_off = 0, dirty_sz = 0;
        bool any = false;
        if (DirtySpan(b, rec->lo, rec->hi, dirty_off, dirty_sz, any)) {
            if (any) {
                off = dirty_off;
                sz = dirty_sz;
            } else if (discard) {
                // Nothing written under DISCARD: contents are undefined anyway, keep a 4 KB upload (renaming semantics).
                off = rec->lo;
                sz = (std::min)(full_sz, 4096u);
            } else {
                // Nothing written under NOOVERWRITE: the real buffer already holds the right bytes.
                return true;
            }
        }
    }

    InterlockedIncrement(&b->refs); // worker's reference
    const CsStgUpload up{buffer, b, rec->lock_fn, s->orig, off, sz, flags};

    if (CsCmd* c = CsQueueReserve(kStgUploadBytes)) {
        WriteUpload(reinterpret_cast<uint8_t*>(c), up);
        CsQueueCommit();
    } else {
        // Queue not running or ring too small: drain, then run the upload here.
        CsQueueDrain();
        alignas(8) uint8_t tmp[kStgUploadBytes];
        WriteUpload(tmp, up);
        CsStagingExecute(reinterpret_cast<const CsCmd*>(tmp));
    }
    return true;
}
