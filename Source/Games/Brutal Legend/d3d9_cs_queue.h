// d3d9_cs_queue.h - single-producer byte ring and worker thread for the command streams.
// Self-contained: no game names, no addresses, no dependency on other cs headers.
// CsQueue is one ring plus one worker thread. cs_queue_detail::g_cs_queue is the D3D9 instance (free functions below
// forward to it).
// Producer = owner thread (calls Reserve/Commit/Drain; Stop from any thread except the worker). Worker runs commands in stream order.
#pragma once

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#pragma comment(lib, "Synchronization.lib")
#pragma comment(lib, "User32.lib")

struct CsCmd { uint16_t op; uint16_t reserved; uint32_t bytes; };  // bytes includes the header, multiple of 8
enum : uint16_t { CsOp_Pad, CsOp_Call, CsOp_CallBlocking, CsOp_Callback, CsOp_BufferUpload };
struct CsDone { volatile LONG done; DWORD result; };


namespace cs_queue_detail {

#pragma warning(push)
#pragma warning(disable : 4324) // padded for cache-line separation on purpose
struct alignas(64) CsLine { std::atomic<uint32_t> v; };
#pragma warning(pop)

inline uint64_t CsNowTicks() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<uint64_t>(t.QuadPart);
}

inline uint64_t CsTicksPerSec() {
    static const uint64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<uint64_t>(f.QuadPart);
    }();
    return freq;
}

} // namespace cs_queue_detail


namespace cs_queue_detail {

// One byte ring + worker. Owner-thread methods (Reserve, Commit, AfterWrite, UnpublishedTail, TryExtend, Drain,
// EnqueueCallback, Stop) run on one producer thread only.
#pragma warning(push)
#pragma warning(disable : 4324) // padded for cache-line separation on purpose
struct CsQueue {
    CsLine head;       // published write index
    CsLine tail;       // consumed read index, written by the worker
    CsLine sleeping;   // 1 while the worker is (about to be) asleep
    CsLine wake_gen;   // wake generation: the sleeping worker WaitOnAddress()es on it; bumped by every wake
    CsLine quit;       // 1 once Stop asked the worker to exit (it exits only when the ring is empty)
    uint8_t* ring = nullptr;   // owner-private from here on
    uint32_t ring_bytes = 0;
    uint32_t reserved = 0;     // owner-private: bytes reserved so far (includes uncommitted)
    void (*execute)(const CsCmd*) = nullptr;
    void (*poll)() = nullptr;
    HANDLE thread = nullptr;
    DWORD tid = 0;             // worker thread id, valid while running
    uint32_t pending_cmds = 0; // owner-private: commands written since the last Commit (publish batching)
    CsCmd* coal_cmd = nullptr; // owner-private: last command, if the owner may still extend it (AfterWrite)
    uint32_t coal_end = 0;     // owner-private: `reserved` right after coal_cmd was written
    bool running = false;
    const wchar_t* thread_name = L"BL CS";

    bool Running() const { return running; }
    bool OnWorkerThread() const { return GetCurrentThreadId() == tid; }

    // ring_bytes: power of two, >= 4096. execute gets Call, CallBlocking and BufferUpload commands.
    // poll_mailbox may be null. Returns false on bad arguments or allocation failure.
    bool Start(uint32_t want_bytes, void (*exec_fn)(const CsCmd*), void (*poll_mailbox)(), const wchar_t* name) {
        if (running || !exec_fn || want_bytes < 4096u || (want_bytes & (want_bytes - 1)) != 0 || want_bytes > (1u << 30))
            return false;
        void* mem = VirtualAlloc(nullptr, want_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!mem) return false;
        ring = static_cast<uint8_t*>(mem);
        ring_bytes = want_bytes;
        reserved = 0;
        head.v.store(0, std::memory_order_relaxed);
        tail.v.store(0, std::memory_order_relaxed);
        sleeping.v.store(0, std::memory_order_relaxed);
        wake_gen.v.store(0, std::memory_order_relaxed);
        quit.v.store(0, std::memory_order_relaxed);
        execute = exec_fn;
        poll = poll_mailbox;
        thread_name = name ? name : L"BL CS";
        pending_cmds = 0;
        coal_cmd = nullptr;
        coal_end = 0;
        DWORD new_tid = 0;
        thread = CreateThread(nullptr, 0, WorkerMain, this, 0, &new_tid);
        if (!thread) {
            VirtualFree(mem, 0, MEM_RELEASE);
            ring = nullptr;
            return false;
        }
        tid = new_tid;
        running = true;
        return true;
    }

    // Owner thread only. Waits until the worker has executed everything committed so far (publishes the pending batch first).
    void Drain() {
        if (!running) return;
        if (reserved != head.v.load(std::memory_order_relaxed)) Commit();
        const uint32_t h = head.v.load(std::memory_order_relaxed);
        for (uint32_t spins = 0; tail.v.load(std::memory_order_acquire) != h; ++spins) {
            if (spins < 4000) _mm_pause();
            else Sleep(0);
        }
    }

    // Owner thread only. Returns space for `bytes` (rounded up to 8, header included). The caller sets
    // op and fills the body, then calls Commit. Pads at the wrap point. Waits while the ring is full.
    // Returns null if not running or the command is larger than half the ring.
    CsCmd* Reserve(uint32_t bytes) {
        coal_cmd = nullptr;   // any new command ends the previous one's extendable state
        if (!running || bytes < 8 || bytes > ring_bytes / 2) return nullptr;
        bytes = (bytes + 7u) & ~7u;
        const uint32_t mask = ring_bytes - 1;
        uint32_t pos = reserved & mask;
        const uint32_t to_end = ring_bytes - pos;
        const uint32_t pad = (to_end < bytes) ? to_end : 0;
        const uint32_t need = pad + bytes;
        for (uint32_t spins = 0;; ++spins) {
            const uint32_t tl = tail.v.load(std::memory_order_acquire);
            if (static_cast<uint32_t>(reserved + need - tl) <= ring_bytes) break;
            // The worker only sees published commands: publish the pending batch, or a full ring would never drain.
            if (reserved != head.v.load(std::memory_order_relaxed)) Commit();
            if (spins < 4000) _mm_pause();
            else Sleep(0);
        }
        if (pad) {
            CsCmd* p = reinterpret_cast<CsCmd*>(ring + pos);
            p->op = CsOp_Pad;
            p->reserved = 0;
            p->bytes = pad;
            reserved += pad;
            pos = 0;
        }
        CsCmd* c = reinterpret_cast<CsCmd*>(ring + pos);
        c->reserved = 0;
        c->bytes = bytes;
        reserved += bytes;
        return c;
    }

    // Owner thread only. Publishes everything reserved so far and wakes the worker if it sleeps.
    void Commit() {
        pending_cmds = 0;
        head.v.store(reserved, std::memory_order_seq_cst);
        if (sleeping.v.load(std::memory_order_seq_cst)) {
            wake_gen.v.fetch_add(1, std::memory_order_seq_cst);
            WakeByAddressSingle(static_cast<void*>(&wake_gen.v));
        }
    }

    // Owner thread only. Call after a command was reserved and filled. extendable = the command may later be merged
    // into (TryExtend). Publishes once `batch` commands are pending; otherwise they stay unpublished until then.
    void AfterWrite(CsCmd* c, bool extendable, uint32_t batch) {
        coal_cmd = extendable ? c : nullptr;
        coal_end = reserved;
        if (++pending_cmds >= batch) Commit();
    }

    // Owner thread only. The last command if it is still unpublished (the worker has not seen it, so it may be rewritten
    // or extended), else null.
    CsCmd* UnpublishedTail() {
        if (!coal_cmd || coal_end != reserved || head.v.load(std::memory_order_relaxed) == reserved) return nullptr;
        return coal_cmd;
    }

    // Owner thread only. Grows the unpublished tail command by `extra` bytes (multiple of 8) when the ring has room and the
    // command ends the ring's contiguous run. Returns the command, or null (the caller writes a new command instead).
    CsCmd* TryExtend(uint32_t extra) {
        CsCmd* c = UnpublishedTail();
        if (!c) return nullptr;
        const uint32_t off = static_cast<uint32_t>(reinterpret_cast<uint8_t*>(c) - ring);
        if (off + c->bytes + extra > ring_bytes) return nullptr;
        if (static_cast<uint32_t>(reserved + extra - tail.v.load(std::memory_order_acquire)) > ring_bytes) return nullptr;
        c->bytes += extra;
        reserved += extra;
        coal_end = reserved;
        return c;
    }

    // Any thread. Wakes the worker if it sleeps, so it polls the mailbox now. Publish the work BEFORE calling.
    void Wake() {
        wake_gen.v.fetch_add(1, std::memory_order_seq_cst);
        if (sleeping.v.load(std::memory_order_seq_cst))
            WakeByAddressSingle(static_cast<void*>(&wake_gen.v));
    }

    // Stops the worker. Any thread except the worker itself; it does not touch the ring, so the producer must not be
    // committing while it runs (CsShutdown drains first). The worker finishes everything committed, then exits and is
    // joined. Mailbox items still pending after the join are served on the calling thread. Never call from DllMain.
    void Stop() {
        if (!running || GetCurrentThreadId() == tid) return;
        quit.v.store(1, std::memory_order_seq_cst);
        Wake();
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        thread = nullptr;
        running = false;
        if (poll) poll();
        VirtualFree(ring, 0, MEM_RELEASE);
        ring = nullptr;
        reserved = 0;
    }

    // Owner thread only. Runs fn(data) on the worker in stream order; the data is copied into the ring.
    void EnqueueCallback(void (*fn)(const void*), const void* data, uint32_t bytes) {
        const uint32_t total = (static_cast<uint32_t>(8 + sizeof(fn) + bytes) + 7u) & ~7u;
        CsCmd* c = Reserve(total);
        if (!c) return;
        c->op = CsOp_Callback;
        uint8_t* p = reinterpret_cast<uint8_t*>(c);
        std::memcpy(p + 8, &fn, sizeof(fn));
        if (bytes) std::memcpy(p + 8 + sizeof(fn), data, bytes);
        Commit();
    }

    // Worker thread proc. param = the CsQueue*.
    static DWORD WINAPI WorkerMain(LPVOID param) {
        CsQueue& s = *static_cast<CsQueue*>(param);
        SetThreadDescription(GetCurrentThread(), s.thread_name);
        const uint32_t mask = s.ring_bytes - 1;
        uint32_t t = s.tail.v.load(std::memory_order_relaxed);
        uint32_t unpublished_cmds = 0; // commands consumed since the last tail publish (publish batching)
        uint32_t since_poll = 0;       // commands since the last mailbox poll
        for (;;) {
            uint32_t h = s.head.v.load(std::memory_order_acquire);
            if (h == t) {
                // Spin about 200 us before sleeping. The mailbox is polled throughout, so foreign calls do not wait for a wake.
                const uint64_t start = CsNowTicks();
                const uint64_t limit = CsTicksPerSec() / 5000;
                do {
                    _mm_pause();
                    if (s.poll) s.poll();
                    h = s.head.v.load(std::memory_order_acquire);
                } while (h == t && CsNowTicks() - start < limit);
                if (h == t) {
                    // Sleep protocol (no lost wake-up). Read the wake generation BEFORE the final checks. Every wake
                    // after that read changes the generation, so WaitOnAddress returns at once instead of blocking.
                    uint32_t gen = s.wake_gen.v.load(std::memory_order_seq_cst);
                    if (s.poll) s.poll();                       // serves mailbox items posted before the read
                    h = s.head.v.load(std::memory_order_seq_cst);
                    if (h == t && s.quit.v.load(std::memory_order_seq_cst)) return 0;
                    if (h == t) {
                        // Dekker pair with Commit/Wake: either they see sleeping == 1 and bump the generation,
                        // or this re-check sees their work.
                        s.sleeping.v.store(1, std::memory_order_seq_cst);
                        h = s.head.v.load(std::memory_order_seq_cst);
                        if (h == t && s.quit.v.load(std::memory_order_seq_cst) == 0) {
                            WaitOnAddress(static_cast<void*>(&s.wake_gen.v), &gen, sizeof(uint32_t), 1); // 1 ms slice as a safety net
                        }
                        s.sleeping.v.store(0, std::memory_order_seq_cst);
                        if (s.poll) s.poll();                   // immediately after every wake
                    }
                    continue;
                }
            }
            while (t != h) {
                uint8_t* p = s.ring + (t & mask);
                const CsCmd* c = reinterpret_cast<const CsCmd*>(p);
                switch (c->op) {
                case CsOp_Pad:
                    break;
                case CsOp_Callback: {
                    // Callbacks may wait on other threads: everything before this command must be visible to the producer.
                    s.tail.v.store(t, std::memory_order_release);
                    unpublished_cmds = 0;
                    void (*fn)(const void*);
                    std::memcpy(&fn, p + 8, sizeof(fn));
                    fn(p + 8 + sizeof(fn));
                    break;
                }
                case CsOp_CallBlocking:
                case CsOp_BufferUpload:
                    // May block on the producer or on a completion: publish first so no waiter sees a stale tail.
                    s.tail.v.store(t, std::memory_order_release);
                    unpublished_cmds = 0;
                    s.execute(c);
                    break;
                default:
                    s.execute(c);
                    break;
                }
                t += c->bytes;
                if (++unpublished_cmds >= 32) {
                    s.tail.v.store(t, std::memory_order_release);
                    unpublished_cmds = 0;
                }
                if (++since_poll >= 64) {
                    since_poll = 0;
                    if (s.poll) s.poll();
                }
            }
            // Inner range done: publish everything consumed, poll once. The outer loop re-reads head (acquire)
            // and goes straight back to processing if new work arrived, without the empty-ring spin.
            s.tail.v.store(t, std::memory_order_release);
            unpublished_cmds = 0;
            since_poll = 0;
            if (s.poll) s.poll();
        }
    }
};
#pragma warning(pop)

// D3D9 instance.
inline CsQueue g_cs_queue{};

} // namespace cs_queue_detail

// ---- D3D9 free-function interface (unchanged signatures; forwards to cs_queue_detail::g_cs_queue) ----

// ring_bytes: power of two, >= 4096. execute gets Call, CallBlocking and BufferUpload commands.
// poll_mailbox may be null. Returns false on bad arguments or allocation failure.
inline bool CsQueueStart(uint32_t ring_bytes, void (*execute)(const CsCmd*), void (*poll_mailbox)()) {
    return cs_queue_detail::g_cs_queue.Start(ring_bytes, execute, poll_mailbox, L"BL D3D9 CS");
}

// Owner thread only. Waits until the worker has executed everything committed so far.
inline void CsQueueDrain() { cs_queue_detail::g_cs_queue.Drain(); }

// Owner thread only. See CsQueue::Reserve.
inline CsCmd* CsQueueReserve(uint32_t bytes) { return cs_queue_detail::g_cs_queue.Reserve(bytes); }

// Owner thread only. Publishes everything reserved so far and wakes the worker if it sleeps.
inline void CsQueueCommit() { cs_queue_detail::g_cs_queue.Commit(); }

// Owner thread only. See CsQueue::AfterWrite.
inline void CsQueueAfterWrite(CsCmd* c, bool extendable, uint32_t batch) { cs_queue_detail::g_cs_queue.AfterWrite(c, extendable, batch); }

// Owner thread only. See CsQueue::UnpublishedTail.
inline CsCmd* CsQueueUnpublishedTail() { return cs_queue_detail::g_cs_queue.UnpublishedTail(); }

// Owner thread only. See CsQueue::TryExtend.
inline CsCmd* CsQueueTryExtend(uint32_t extra) { return cs_queue_detail::g_cs_queue.TryExtend(extra); }

// Any thread. See CsQueue::Wake.
inline void CsQueueWake() { cs_queue_detail::g_cs_queue.Wake(); }

// Stops the D3D9 worker. See CsQueue::Stop. Never call from DllMain.
inline void CsQueueStop() { cs_queue_detail::g_cs_queue.Stop(); }

// Owner thread only. Runs fn(data) on the worker in stream order; the data is copied into the ring.
inline void CsEnqueueCallback(void (*fn)(const void*), const void* data, uint32_t bytes) {
    cs_queue_detail::g_cs_queue.EnqueueCallback(fn, data, bytes);
}

// Wait until *flag becomes nonzero: spin, then WaitOnAddress in 1 ms slices.
// Window threads pump PeekMessage so sent messages are served while waiting.
inline void CsWait(volatile LONG* flag) {
    using namespace cs_queue_detail;
    for (int i = 0; i < 2000; ++i) {
        if (*flag) {
            _ReadWriteBarrier();
            return;
        }
        _mm_pause();
    }
    const bool gui = IsGUIThread(FALSE) != FALSE;
    while (!*flag) {
        LONG expect = 0;
        WaitOnAddress(const_cast<LONG*>(flag), &expect, sizeof(LONG), 1);
        if (gui) {
            MSG m;
            PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);
        }
    }
    _ReadWriteBarrier();
}

// Worker side: store the result and release CsWait on d.
inline void CsSignalDone(CsDone* d, DWORD result) {
    d->result = result;
    _InterlockedExchange(&d->done, 1);
    WakeByAddressAll(const_cast<LONG*>(&d->done));
}

// True on the D3D9 worker thread.
inline bool CsOnWorkerThread() { return cs_queue_detail::g_cs_queue.OnWorkerThread(); }
