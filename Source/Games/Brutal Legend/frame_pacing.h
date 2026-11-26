#pragma once

// Engine Frame Pacing for Brutal Legend (Win32 only). Setting "Engine Frame Pacing Fix" (config key "FramePacing", default on).
// Render side: the three blend calls that interpolate sim records are redirected to BlendWrap, which blends the two queue
// records bracketing a smoothed sim clock. The game's own queue trim/backpressure limits are raised (0x80->0xC0, 0xC0->0x100)
// while the setting is on, giving a 3-frame buffer.
// Main loop: the Sleep(1) poll (push 1 / call [Sleep]) is redirected to SleepWrap, which waits until the next sim tick.
// Sim side: sim_tick.h's Tick hook calls FramePacing_OnSimTick after the original returns (PLL clock samples).
// Every site is signature-checked before anything is written; any mismatch patches nothing and logs once.
// Logs: "[BL Pacing]". The periodic line and the Mcp registrations are DEVELOPMENT only.
// Character palettes: the paced blend also sets the record pair for anim_interp.h (AnimInterp_SetPair), which blends them.

#include "anim_interp.h"
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace BLFramePacing
{
   // Dev values. Defined in every config (main.cpp registers them with Mcp under DEVELOPMENT).
   static float g_latency_ticks = 1.5f; // maps the sim tick counter to record ticks
   static uint32_t g_dev_hold = 0;        // target above newest record (clamped)
   static uint32_t g_dev_under = 0;       // target below oldest record (clamped)
   static uint32_t g_dev_resync = 0;      // PLL resyncs
   static uint32_t g_dev_passthrough = 0; // blend calls that fell back to the original args (setting on)
   static uint32_t g_dev_render_nowait = 0; // render-thread waits made non-blocking (setting on, clock valid)
   static float g_dev_last_alpha = 0.f;   // alpha of the last paced blend
   static int32_t g_dev_newest = 0;       // newest record tick seen by the last pairing
} // namespace BLFramePacing

#if defined(_M_IX86)
namespace BLFramePacing
{
   static constexpr uintptr_t kImageBase = 0x400000;
   static constexpr uintptr_t kVaBlend = 0x7749A0;             // stdcall void(dst, A, B, float alpha), ret 0x10
   static constexpr uintptr_t kVaBlendCalls[3] = { 0x759BE6, 0x759C19, 0x759C4B }; // E8 rel32 -> kVaBlend
   static constexpr uintptr_t kVaTrimEdx = 0x759A09;           // 81 FA imm32: cmp edx,0x80 (imm at +2)
   static constexpr uintptr_t kVaTrimEax = 0x759ACE;           // 3D imm32: cmp eax,0x80 (imm at +1)
   static constexpr uintptr_t kVaProducer = 0x758B97;          // 3D imm32: cmp eax,0xC0 (imm at +1)
   static constexpr uintptr_t kVaSleepPush = 0xA342A2;         // 6A 01: push 1
   static constexpr uintptr_t kVaSleepCall = 0xA342A4;         // FF 15 abs32: call [Sleep] (6 bytes)
   static constexpr uintptr_t kVaRenderPush = 0x75A21E;        // 6A 0A 50: push 0xA; push eax (handle)
   static constexpr uintptr_t kVaRenderWait = 0x75A221;        // FF 15 abs32: call [WaitForSingleObject] (6 bytes)
   static constexpr uintptr_t kVaRenderGlobal = 0x1005ACC;     // R = *(void**)
   static constexpr uintptr_t kOffQueueCount = 0x774;          // uint32, entries = value >> 6
   static constexpr uintptr_t kOffQueueArray = 0x77C;          // void** records, oldest first
   static constexpr uintptr_t kOffRecTick = 0x1218;            // int32 record tick
   static constexpr uintptr_t kOffBlendScalar = 0x1224;        // float, recomputed after each blend (see FixBlendScalar).
   static constexpr uintptr_t kOffSimTick = 0xA2C;             // int32 sim tick counter
   static constexpr uintptr_t kOffTickDt = 0xA40;              // float
   static constexpr uintptr_t kOffTimeScale = 0xA48;           // float
   static constexpr uintptr_t kOffAccum = 0xA4C;               // float
   static constexpr size_t kSimSize = 0xA50;

   static const uint8_t kSigBlend[8] = { 0x55, 0x8B, 0xEC, 0xD9, 0xEE, 0x83, 0xEC, 0x54 };
   static const uint8_t kSigTrimEdx[6] = { 0x81, 0xFA, 0x80, 0x00, 0x00, 0x00 };
   static const uint8_t kSigTrimEax[5] = { 0x3D, 0x80, 0x00, 0x00, 0x00 };
   static const uint8_t kSigProducer[5] = { 0x3D, 0xC0, 0x00, 0x00, 0x00 };
   static const uint8_t kSigSleepPush[4] = { 0x6A, 0x01, 0xFF, 0x15 }; // the abs32 after FF 15 is relocated and not compared
   static const uint8_t kSigRenderPush[3] = { 0x6A, 0x0A, 0x50 };     // push 0xA; push eax
   static const uint8_t kSigRenderCall[2] = { 0xFF, 0x15 };           // the abs32 after FF 15 is relocated and not compared

   static uintptr_t g_base = 0;
   static bool g_installed = false;
   static bool g_imm_applied = false;
   static bool g_imm_logged_error = false;
   static bool g_mismatch_logged = false;
   static bool g_wait_timer_failed = false;
   static std::atomic<bool> g_enabled{ false };
   static std::atomic<void*> g_sim{ nullptr };
   static uint32_t g_presents = 0;

   // Sim clock. Seqlock: single writer (sim Tick hook), readers on the render and main threads.
   static std::atomic<uint32_t> g_seq{ 0 };
   static bool g_have_sample = false; // writer state
   static double g_tb = 0.0;          // time origin (seconds, QPC)
   static double g_nb = 0.0;          // sim tick at tb
   static double g_P = 0.0;           // seconds per sim tick
   static double g_last_t = 0.0;      // time of last sample
   static int32_t g_last_n = 0;       // sim tick of last sample (writer state, read for dev log)

   // PLL error stats (writer), reset by the dev log.
   static std::atomic<float> g_err_abs_sum{ 0.f };
   static std::atomic<float> g_err_max{ 0.f };
   static std::atomic<uint32_t> g_err_count{ 0 };

   struct ClockSnap
   {
      bool valid;
      double tb, nb, P, last_t;
   };

   using BlendFn = void(__stdcall*)(void*, void*, void*, float);

   static void Log(reshade::log::level lv, const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(lv, buf);
   }

   static double QpcSeconds()
   {
      static const double inv_qpf = []() {
         LARGE_INTEGER f{};
         QueryPerformanceFrequency(&f);
         return 1.0 / static_cast<double>(f.QuadPart);
      }();
      LARGE_INTEGER c{};
      QueryPerformanceCounter(&c);
      return static_cast<double>(c.QuadPart) * inv_qpf;
   }

   static uint8_t* Site(uintptr_t va)
   {
      return reinterpret_cast<uint8_t*>(g_base + (va - kImageBase));
   }

   static ClockSnap ReadClock()
   {
      ClockSnap s{ false, 0.0, 0.0, 0.0, 0.0 };
      for (int i = 0; i < 16; ++i)
      {
         const uint32_t s1 = g_seq.load(std::memory_order_acquire);
         if (s1 & 1u)
            continue;
         s.tb = g_tb;
         s.nb = g_nb;
         s.P = g_P;
         s.last_t = g_last_t;
         s.valid = g_have_sample;
         std::atomic_thread_fence(std::memory_order_acquire);
         if (g_seq.load(std::memory_order_relaxed) == s1)
            return s;
      }
      s.valid = false;
      return s;
   }

   static void WriteClock(double tb, double nb, double P, double last_t, int32_t n)
   {
      const uint32_t s = g_seq.load(std::memory_order_relaxed);
      g_seq.store(s + 1u, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_release);
      g_have_sample = true;
      g_tb = tb;
      g_nb = nb;
      g_P = P;
      g_last_t = last_t;
      g_last_n = n;
      g_seq.store(s + 2u, std::memory_order_release);
   }

   // Sim Tick hook (sim_tick.h), main sim only, after the original Tick returned.
   static void OnSimTick(void* sim)
   {
      if (!sim || IsBadReadPtr(sim, kSimSize))
         return;
      const float dt = *reinterpret_cast<const float*>(static_cast<const char*>(sim) + kOffTickDt);
      const float scale = *reinterpret_cast<const float*>(static_cast<const char*>(sim) + kOffTimeScale);
      if (!(dt > 0.f) || !(scale > 0.f))
         return; // paused: no sample, the render side goes passthrough once the last sample is stale
      const int32_t n = *reinterpret_cast<const int32_t*>(static_cast<const char*>(sim) + kOffSimTick);
      g_sim.store(sim, std::memory_order_relaxed);

      const double P = static_cast<double>(dt) / static_cast<double>(scale);
      const double t = QpcSeconds();

      double tb = g_tb, nb = g_nb;
      bool resync = !g_have_sample;
      if (!resync)
      {
         const double est = nb + (t - tb) / P;
         const double err = static_cast<double>(n) - est;
         const float abs_err = static_cast<float>(std::fabs(err));
         g_err_abs_sum.store(g_err_abs_sum.load(std::memory_order_relaxed) + abs_err, std::memory_order_relaxed);
         if (abs_err > g_err_max.load(std::memory_order_relaxed))
            g_err_max.store(abs_err, std::memory_order_relaxed);
         g_err_count.fetch_add(1, std::memory_order_relaxed);

         if (std::fabs(err) > 3.0 || (t - g_last_t) > 4.0 * P)
            resync = true;
         else
            tb -= 0.05 * err * P;
      }
      if (resync)
      {
         tb = t;
         nb = static_cast<double>(n);
         if (g_have_sample)
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&BLFramePacing::g_dev_resync));
      }
      WriteClock(tb, nb, P, t, n);
   }

   // Picks the record pair bracketing the render target tick. False = passthrough.
   static bool ComputePair(void** out0, void** out1, float* out_a, int32_t* out_t0, int32_t* out_t1)
   {
      const double now = QpcSeconds();
      const ClockSnap c = ReadClock();
      if (!c.valid || !(c.P > 0.0) || (now - c.last_t) > 4.0 * c.P)
         return false;

      const uintptr_t global_va = g_base + (kVaRenderGlobal - kImageBase);
      if (IsBadReadPtr(reinterpret_cast<const void*>(global_va), sizeof(void*)))
         return false;
      const uintptr_t r = *reinterpret_cast<const uintptr_t*>(global_va);
      if (!r || IsBadReadPtr(reinterpret_cast<const void*>(r), kOffQueueArray + sizeof(void*)))
         return false;
      const uint32_t count = *reinterpret_cast<const uint32_t*>(r + kOffQueueCount) >> 6;
      void** arr = *reinterpret_cast<void***>(r + kOffQueueArray);
      if (count < 2 || !arr || IsBadReadPtr(arr, count * sizeof(void*)))
         return false;

      auto tick_of = [](void* rec, int32_t* out) -> bool {
         if (!rec || IsBadReadPtr(static_cast<const char*>(rec) + kOffRecTick, sizeof(int32_t)))
            return false;
         *out = *reinterpret_cast<const int32_t*>(static_cast<const char*>(rec) + kOffRecTick);
         return true;
      };

      int32_t oldest = 0, newest = 0;
      if (!tick_of(arr[0], &oldest) || !tick_of(arr[count - 1], &newest))
         return false;
      BLFramePacing::g_dev_newest = newest;

      double T = c.nb + (now - c.tb) / c.P - static_cast<double>(g_latency_ticks);
      if (T > static_cast<double>(newest))
      {
         T = static_cast<double>(newest);
         InterlockedIncrement(reinterpret_cast<volatile LONG*>(&BLFramePacing::g_dev_hold));
      }
      if (T < static_cast<double>(oldest))
      {
         T = static_cast<double>(oldest);
         InterlockedIncrement(reinterpret_cast<volatile LONG*>(&BLFramePacing::g_dev_under));
      }

      for (uint32_t i = 0; i + 1 < count; ++i)
      {
         int32_t t0 = 0, t1 = 0;
         if (!tick_of(arr[i], &t0) || !tick_of(arr[i + 1], &t1))
            return false;
         if (t1 > t0 && T >= static_cast<double>(t0) && T <= static_cast<double>(t1))
         {
            *out0 = arr[i];
            *out1 = arr[i + 1];
            *out_t0 = t0;
            *out_t1 = t1;
            *out_a = static_cast<float>((T - t0) / static_cast<double>(t1 - t0));
            return true;
         }
      }
      return false;
   }

   // The game's blend reads +0x1228 of Y where it should read +0x1224. Recompute it as X*(1-w)+Y*w from +0x1224 of both inputs.
   static void FixBlendScalar(void* dst, const void* X, const void* Y, float w)
   {
      if (!dst || !X || !Y)
         return;
      constexpr size_t kN = kOffBlendScalar + sizeof(float);
      if (IsBadWritePtr(dst, kN) || IsBadReadPtr(X, kN) || IsBadReadPtr(Y, kN))
         return;
      const float wc = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
      const float x = *reinterpret_cast<const float*>(static_cast<const char*>(X) + kOffBlendScalar);
      const float y = *reinterpret_cast<const float*>(static_cast<const char*>(Y) + kOffBlendScalar);
      *reinterpret_cast<float*>(static_cast<char*>(dst) + kOffBlendScalar) = x * (1.f - wc) + y * wc;
   }

   // Redirected from the three blend call sites (stdcall, same signature).
   // FixBlendScalar is applied on every path, independent of the pacing toggle.
   static void __stdcall BlendWrap(void* dst, void* A, void* B, float alpha)
   {
      const BlendFn orig = reinterpret_cast<BlendFn>(g_base + (kVaBlend - kImageBase));
      if (!g_enabled.load(std::memory_order_relaxed))
      {
         orig(dst, A, B, alpha);
         FixBlendScalar(dst, A, B, alpha);
         AnimInterp_ClearPair();
         return;
      }
      void* r0 = nullptr;
      void* r1 = nullptr;
      float a = 0.f;
      int32_t t0 = 0, t1 = 0;
      if (!ComputePair(&r0, &r1, &a, &t0, &t1))
      {
         InterlockedIncrement(reinterpret_cast<volatile LONG*>(&BLFramePacing::g_dev_passthrough));
         orig(dst, A, B, alpha);
         FixBlendScalar(dst, A, B, alpha);
         AnimInterp_ClearPair();
         return;
      }
      BLFramePacing::g_dev_last_alpha = a;
      if (AnimInterp_Enabled())
      {
         // Newest record as A, so the palettes can be blended between the same two ticks (weight a on the new tick).
         orig(dst, r1, r0, 1.f - a);
         FixBlendScalar(dst, r1, r0, 1.f - a);
         AnimInterp_SetPair(t0, t1, a);
      }
      else
      {
         if (a < 0.5f)
         {
            orig(dst, r0, r1, a);
            FixBlendScalar(dst, r0, r1, a);
         }
         else
         {
            orig(dst, r1, r0, 1.f - a);
            FixBlendScalar(dst, r1, r0, 1.f - a);
         }
         AnimInterp_ClearPair();
      }
   }

   // Time to the next sim tick in seconds, clamped to [0, 2*tickDt]. Negative = unavailable (use Sleep).
   static double WaitSeconds()
   {
      const void* sim = g_sim.load(std::memory_order_relaxed);
      if (!sim || IsBadReadPtr(sim, kSimSize))
         return -1.0;
      const float dt = *reinterpret_cast<const float*>(static_cast<const char*>(sim) + kOffTickDt);
      const float scale = *reinterpret_cast<const float*>(static_cast<const char*>(sim) + kOffTimeScale);
      const float acc = *reinterpret_cast<const float*>(static_cast<const char*>(sim) + kOffAccum);
      if (!(dt > 0.f) || !(scale > 0.f))
         return -1.0;
      double w = (static_cast<double>(dt) - static_cast<double>(acc)) / static_cast<double>(scale);
      const double hi = 2.0 * static_cast<double>(dt);
      if (w < 0.0) w = 0.0;
      if (w > hi) w = hi;
      return w;
   }

   // High-resolution waitable timer (resolved at runtime, Windows 10 1803+). False = unavailable, caller uses Sleep.
   static bool WaitTimer(double seconds)
   {
      using CreateTimerExFn = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
      static CreateTimerExFn create_ex = []() -> CreateTimerExFn {
         HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
         return k32 ? reinterpret_cast<CreateTimerExFn>(GetProcAddress(k32, "CreateWaitableTimerExW")) : nullptr;
      }();
      if (g_wait_timer_failed || !create_ex)
         return false;

      static thread_local HANDLE t_timer = nullptr;
      if (!t_timer)
      {
         const DWORD kHighRes = 0x00000002; // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
         t_timer = create_ex(nullptr, nullptr, kHighRes, TIMER_ALL_ACCESS);
         if (!t_timer)
         {
            g_wait_timer_failed = true;
            Log(reshade::log::level::warning, "[BL Pacing] high-resolution waitable timer unavailable, using Sleep");
            return false;
         }
      }
      LARGE_INTEGER due{};
      due.QuadPart = -static_cast<LONGLONG>(seconds * 1e7); // 100 ns units, relative
      if (due.QuadPart >= 0)
         due.QuadPart = -1;
      if (!SetWaitableTimer(t_timer, &due, 0, nullptr, nullptr, FALSE))
         return false;
      WaitForSingleObject(t_timer, INFINITE);
      return true;
   }

   // Redirected from the main loop's "push 1 / call [Sleep]" (stdcall, same signature).
   static void __stdcall SleepWrap(DWORD ms)
   {
      if (!g_enabled.load(std::memory_order_relaxed))
      {
         Sleep(ms);
         return;
      }
      const double w = WaitSeconds();
      if (w < 0.0)
      {
         Sleep(ms);
         return;
      }
      if (w <= 0.0)
      {
         SwitchToThread();
         return;
      }
      if (!WaitTimer(w))
         Sleep(ms);
   }

   // Redirected from the render thread's WaitForSingleObject(h, 10) (stdcall, same signature and cleanup).
   static DWORD WINAPI RenderWaitStub(HANDLE h, DWORD ms)
   {
      const ClockSnap c = ReadClock();
      if (g_enabled.load(std::memory_order_relaxed) && c.valid && c.P > 0.0)
      {
#if DEVELOPMENT
         InterlockedIncrement(reinterpret_cast<volatile LONG*>(&g_dev_render_nowait));
#endif
         return WaitForSingleObject(h, 0);
      }
      return WaitForSingleObject(h, ms);
   }

   static bool MatchBytes(uintptr_t va, const uint8_t* expect, size_t n, const char* name)
   {
      const uint8_t* p = Site(va);
      if (IsBadReadPtr(p, n) || memcmp(p, expect, n) != 0)
      {
         if (!g_mismatch_logged)
         {
            g_mismatch_logged = true;
            Log(reshade::log::level::error, "[BL Pacing] %s at VA 0x%06X: bytes mismatch or unreadable", name, static_cast<unsigned>(va));
         }
         return false;
      }
      return true;
   }

   static bool CallTargetIs(uintptr_t call_va, uintptr_t target_va)
   {
      const uint8_t* p = Site(call_va);
      if (IsBadReadPtr(p, 5) || p[0] != 0xE8)
         return false;
      int32_t rel = 0;
      memcpy(&rel, p + 1, 4);
      return reinterpret_cast<uintptr_t>(p + 5) + rel == reinterpret_cast<uintptr_t>(Site(target_va));
   }

   // E8 rel32 reaches the target from the site (signed 32-bit displacement).
   static bool RelFits(uint8_t* at, const void* to)
   {
      const int64_t d = static_cast<int64_t>(reinterpret_cast<uintptr_t>(to)) - (static_cast<int64_t>(reinterpret_cast<uintptr_t>(at)) + 5);
      return d >= INT32_MIN && d <= INT32_MAX;
   }

   static bool WriteBytes(uint8_t* dst, const uint8_t* src, size_t n)
   {
      DWORD old_protect = 0;
      if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old_protect))
         return false;
      memcpy(dst, src, n);
      DWORD tmp = 0;
      VirtualProtect(dst, n, old_protect, &tmp);
      FlushInstructionCache(GetCurrentProcess(), dst, n);
      return true;
   }

   // E8 rel32 to `to` over `len` bytes (extra bytes become NOPs).
   static bool WriteCall(uint8_t* at, const void* to, size_t len)
   {
      uint8_t buf[8];
      buf[0] = 0xE8;
      const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (reinterpret_cast<uintptr_t>(at) + 5));
      memcpy(buf + 1, &rel, 4);
      for (size_t i = 5; i < len; ++i) buf[i] = 0x90;
      return WriteBytes(at, buf, len);
   }

   static bool WriteImm32(uintptr_t va, uint32_t value)
   {
      uint8_t* at = Site(va);
      uint8_t buf[4];
      memcpy(buf, &value, 4);
      if (!WriteBytes(at, buf, 4))
         return false;
      uint32_t back = 0;
      memcpy(&back, at, 4);
      return back == value;
   }

   // Trim limits and producer backpressure. On = 3-frame buffer, off = vanilla values.
   static void ApplyImmPatches(bool on)
   {
      const uint32_t trim = on ? 0xC0u : 0x80u;
      const uint32_t producer = on ? 0x100u : 0xC0u;
      bool ok = WriteImm32(kVaTrimEdx + 2, trim);
      ok = WriteImm32(kVaTrimEax + 1, trim) && ok;
      ok = WriteImm32(kVaProducer + 1, producer) && ok;
      if (!ok && !g_imm_logged_error)
      {
         g_imm_logged_error = true;
         Log(reshade::log::level::error, "[BL Pacing] queue limit write failed or did not read back");
      }
      g_imm_applied = on;
   }

   static void Install()
   {
      g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      if (!g_base)
         return;

      // Verify everything first. Any mismatch patches nothing.
      bool ok = true;
      ok = MatchBytes(kVaBlend, kSigBlend, sizeof(kSigBlend), "Blend") && ok;
      for (uintptr_t c : kVaBlendCalls)
      {
         if (!CallTargetIs(c, kVaBlend))
         {
            if (!g_mismatch_logged) { g_mismatch_logged = true; Log(reshade::log::level::error, "[BL Pacing] blend call at VA 0x%06X: target mismatch", static_cast<unsigned>(c)); }
            ok = false;
         }
         else if (!RelFits(Site(c), reinterpret_cast<void*>(&BlendWrap)))
         {
            Log(reshade::log::level::error, "[BL Pacing] wrapper out of rel32 range of blend call 0x%06X", static_cast<unsigned>(c));
            ok = false;
         }
      }
      ok = MatchBytes(kVaTrimEdx, kSigTrimEdx, sizeof(kSigTrimEdx), "Trim cmp edx") && ok;
      ok = MatchBytes(kVaTrimEax, kSigTrimEax, sizeof(kSigTrimEax), "Trim cmp eax") && ok;
      ok = MatchBytes(kVaProducer, kSigProducer, sizeof(kSigProducer), "Producer cmp eax") && ok;
      ok = MatchBytes(kVaSleepPush, kSigSleepPush, sizeof(kSigSleepPush), "Main loop Sleep") && ok;
      ok = MatchBytes(kVaRenderPush, kSigRenderPush, sizeof(kSigRenderPush), "Render wait push") && ok;
      ok = MatchBytes(kVaRenderWait, kSigRenderCall, sizeof(kSigRenderCall), "Render wait call") && ok;
      if (ok)
      {
         uint32_t iat = 0;
         memcpy(&iat, Site(kVaSleepCall) + 2, 4);
         if (IsBadReadPtr(reinterpret_cast<const void*>(static_cast<uintptr_t>(iat)), sizeof(void*)))
            ok = false;
         if (!RelFits(Site(kVaSleepCall), reinterpret_cast<void*>(&SleepWrap)))
            ok = false;
         uint32_t iat_wait = 0;
         memcpy(&iat_wait, Site(kVaRenderWait) + 2, 4);
         if (IsBadReadPtr(reinterpret_cast<const void*>(static_cast<uintptr_t>(iat_wait)), sizeof(void*)))
            ok = false;
         if (!RelFits(Site(kVaRenderWait), reinterpret_cast<void*>(&RenderWaitStub)))
            ok = false;
      }
      if (!ok)
      {
         Log(reshade::log::level::error, "[BL Pacing] signature mismatch, frame pacing inactive (nothing patched)");
         return;
      }

      for (uintptr_t c : kVaBlendCalls)
         WriteCall(Site(c), reinterpret_cast<void*>(&BlendWrap), 5);
      WriteCall(Site(kVaSleepCall), reinterpret_cast<void*>(&SleepWrap), 6);
      WriteCall(Site(kVaRenderWait), reinterpret_cast<void*>(&RenderWaitStub), 6);
      g_installed = true;
      Log(reshade::log::level::info, "[BL Pacing] frame pacing redirects installed (3 blend calls, main loop wait, render wait)");
   }
} // namespace BLFramePacing

// Called from sim_tick.h's Tick hook (main sim only, after the original returns).
static void FramePacing_OnSimTick(void* sim)
{
   BLFramePacing::OnSimTick(sim);
}

// Called from OnInit (main.cpp), after InstallSimTick.
static void InstallFramePacing()
{
   BLFramePacing::Install();
}

// Called from OnPresent (main.cpp) with the "Engine Frame Pacing Fix" setting.
static void FramePacing_OnPresent(bool enabled)
{
   using namespace BLFramePacing;
   g_enabled.store(enabled, std::memory_order_relaxed);
   if (!g_installed)
      return;
   if (enabled != g_imm_applied)
   {
      ApplyImmPatches(enabled);
      Log(reshade::log::level::info, "[BL Pacing] queue limits %s", enabled ? "raised (3-frame buffer)" : "vanilla");
   }
   ++g_presents;
#if DEVELOPMENT
   if (g_presents % 600 == 0)
   {
      const uint32_t cnt = g_err_count.exchange(0, std::memory_order_relaxed);
      const float sum = g_err_abs_sum.exchange(0.f, std::memory_order_relaxed);
      const float mx = g_err_max.exchange(0.f, std::memory_order_relaxed);
      Log(reshade::log::level::info,
          "[BL Pacing] presents %u: hold %u under %u resync %u passthrough %u render_nowait %u, latency %.2f, last alpha %.3f, "
          "PLL |err| mean %.3f max %.3f over %u, newest-n %d",
          g_presents, g_dev_hold, g_dev_under, g_dev_resync, g_dev_passthrough, g_dev_render_nowait, static_cast<double>(g_latency_ticks),
          static_cast<double>(g_dev_last_alpha), cnt ? static_cast<double>(sum / cnt) : 0.0, static_cast<double>(mx), cnt,
          static_cast<int>(g_dev_newest - g_last_n));
   }
#endif
}
#else
static void FramePacing_OnSimTick(void*) {}
static void InstallFramePacing() {}
static void FramePacing_OnPresent(bool) {}
#endif
