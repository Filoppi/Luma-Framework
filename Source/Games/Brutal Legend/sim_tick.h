#pragma once

// Simulation tick logger and "Animation Interpolation" (60 Hz simulation) toggle for Brutal Legend (Win32 only).
// Hooks the main sim object's AdvanceAccum and Tick with safetyhook, verifies every target by bytes before hooking.
// Logs go to the ReShade log with the prefix "[BL SimTick]". CSV: Luma_BL_SimTick.csv next to the exe (only while sim.log is on).

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <safetyhook.hpp>
#if DEVELOPMENT
#include <filesystem>
#include <fstream>
#include <vector>
#endif
#include "frame_pacing.h"

#if DEVELOPMENT
// Dev counter ("sim.ticks_last"): sim ticks that ran during the last presented frame. Written in SimTick_OnPresent.
static uint32_t g_sim_ticks_last = 0;
#endif

#if defined(_M_IX86)
namespace BLSimTick
{
   // RVAs from the module base.
   static constexpr uintptr_t kRvaAdvanceAccum = 0x4C4CC0; // thiscall void(float frameDt)
   static constexpr uintptr_t kRvaTick = 0x4C4DF0; // thiscall int()
   static constexpr uintptr_t kRvaSetSim60Hz = 0x45FB00; // stdcall void(uint8_t enable)
   static constexpr uintptr_t kRvaRate = 0xB7288C; // float sim rate global
   static constexpr uintptr_t kRvaGameGlobal = 0xC05DC4; // global pointer; main sim = **(void***)(*(uintptr_t*)(base+0xC05DC4) + 0x10)
#if DEVELOPMENT
   static constexpr uintptr_t kOffAccumNum = 0xA4C; // float on the sim object
   static constexpr uintptr_t kOffAccumDen = 0xA40; // float on the sim object
   static constexpr size_t kSimObjectSize = 0xA50;
#endif

   static const uint8_t kSigAdvanceAccum[12] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08, 0xD9, 0x81, 0x4C, 0x0A, 0x00, 0x00 };
   static const uint8_t kSigTick[10] = { 0x56, 0x8B, 0xF1, 0x83, 0xBE, 0xD0, 0x09, 0x00, 0x00, 0x00 };
   static const uint8_t kSigSetSim60Hz[10] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x8A, 0x5D, 0x08 };

#if DEVELOPMENT
   static constexpr uint32_t kWindowPresents = 300; // stats and CSV flush interval

   struct Row
   {
      double time_ms;
      double present_ms;
      double frame_dt_ms;
      uint32_t ticks;
      float accum;
      float rate;
   };
#endif

   static uintptr_t g_base = 0;
   static safetyhook::InlineHook g_hook_advance;
   static safetyhook::InlineHook g_hook_tick;

   static std::atomic<bool> g_desired_60{ false }; // set from the "Animation Interpolation" setting in SimTick_OnPresent
   static int g_attempted_for = -1; // desired value of the last SetSim60Hz call in the current mismatch episode, -1 none
   static bool g_refused_logged = false;
   static std::atomic<uint32_t> g_ticks_since_present{ 0 };
   static std::atomic<float> g_last_frame_dt{ 0.f }; // seconds, from the last main-sim AdvanceAccum

#if DEVELOPMENT
   // Present-side state (only touched from OnPresent).
   static LARGE_INTEGER g_qpf = {};
   static LONGLONG g_start_qpc = 0;
   static LONGLONG g_last_present_qpc = 0;
   static LONGLONG g_window_start_qpc = 0;
   static uint32_t g_window_frames = 0;
   static uint32_t g_window_hist[4] = {};
   static uint32_t g_window_tick_changes = 0;
   static uint32_t g_prev_ticks = 0xFFFFFFFFu; // none yet
   static std::vector<Row> g_rows;
   static bool g_csv_started = false;
   static bool g_csv_error_logged = false;
#endif

   static void Log(reshade::log::level lv, const char* fmt, ...)
   {
      char buf[512];
      va_list args;
      va_start(args, fmt);
      vsnprintf(buf, sizeof(buf), fmt, args);
      va_end(args);
      reshade::log::message(lv, buf);
   }

   static float* RateGlobal()
   {
      return reinterpret_cast<float*>(g_base + kRvaRate);
   }

   // Main sim object, null-checked at every step. Returns nullptr if anything is unreadable.
   static void* MainSelf()
   {
      if (!g_base)
         return nullptr;
      const uintptr_t* outer_p = reinterpret_cast<const uintptr_t*>(g_base + kRvaGameGlobal);
      if (IsBadReadPtr(outer_p, sizeof(uintptr_t)))
         return nullptr;
      const uintptr_t outer = *outer_p;
      if (!outer || IsBadReadPtr(reinterpret_cast<const void*>(outer + 0x10), sizeof(void*)))
         return nullptr;
      void* const* holder = *reinterpret_cast<void* const* const*>(outer + 0x10);
      if (!holder || IsBadReadPtr(holder, sizeof(void*)))
         return nullptr;
      return *holder;
   }

   // Compares `n` bytes at rva against `expect`. Logs the bytes actually found on mismatch.
   static bool MatchBytes(uintptr_t rva, const uint8_t* expect, size_t n, const char* name)
   {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + rva);
      if (IsBadReadPtr(p, n))
      {
         Log(reshade::log::level::error, "[BL SimTick] %s at RVA 0x%06X: unreadable", name, static_cast<unsigned>(rva));
         return false;
      }
      if (memcmp(p, expect, n) == 0)
         return true;
      char found[64] = {};
      for (size_t i = 0; i < n && i < 16; ++i)
         snprintf(found + i * 3, sizeof(found) - i * 3, "%02X ", p[i]);
      Log(reshade::log::level::error, "[BL SimTick] %s at RVA 0x%06X: bytes mismatch (found %s)", name, static_cast<unsigned>(rva), found);
      return false;
   }

   // Calls the unhooked SetSim60Hz (stdcall).
   static void CallSetSim60Hz(bool enable)
   {
      using Fn = void(__stdcall*)(uint8_t);
      reinterpret_cast<Fn>(g_base + kRvaSetSim60Hz)(enable ? 1 : 0);
   }

   static void __fastcall HookedAdvanceAccum(void* self, void* /*edx*/, float frame_dt)
   {
      if (self && self == MainSelf())
      {
         g_last_frame_dt.store(frame_dt);

         const bool want = g_desired_60.load();
         float* rate_p = RateGlobal();
         const float before = *rate_p;
         const bool is_60 = std::fabs(before - 60.f) < 0.01f;
         if (want == is_60)
         {
            g_attempted_for = -1; // nothing to do, allow a new attempt if the game later drops back
         }
         else if (g_attempted_for != (want ? 1 : 0))
         {
            g_attempted_for = want ? 1 : 0;
            CallSetSim60Hz(want);
            const float after = *rate_p;
            if (after == before)
            {
               if (!g_refused_logged)
               {
                  g_refused_logged = true;
                  Log(reshade::log::level::warning, "[BL SimTick] 60 Hz refused (desired=%d, rate stays %.3f)", want ? 1 : 0, after);
               }
            }
            else
            {
               Log(reshade::log::level::info, "[BL SimTick] SetSim60Hz(%d): rate %.3f -> %.3f", want ? 1 : 0, before, after);
            }
         }
      }
      g_hook_advance.thiscall<void>(self, frame_dt);
   }

   static int __fastcall HookedTick(void* self, void* /*edx*/)
   {
      const bool is_main = self && self == MainSelf();
      if (is_main)
         g_ticks_since_present.fetch_add(1, std::memory_order_relaxed);
      const int result = g_hook_tick.thiscall<int>(self);
      if (is_main)
         FramePacing_OnSimTick(self); // Engine Frame Pacing clock sample (frame_pacing.h)
      return result;
   }

   static void Install()
   {
      g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
      if (!g_base)
      {
         Log(reshade::log::level::error, "[BL SimTick] module base unavailable, sim tick hooks inactive");
         return;
      }

      // Verify everything first. Any mismatch hooks nothing.
      bool all_ok = true;
      all_ok = MatchBytes(kRvaAdvanceAccum, kSigAdvanceAccum, sizeof(kSigAdvanceAccum), "AdvanceAccum") && all_ok;
      all_ok = MatchBytes(kRvaTick, kSigTick, sizeof(kSigTick), "Tick") && all_ok;
      all_ok = MatchBytes(kRvaSetSim60Hz, kSigSetSim60Hz, sizeof(kSigSetSim60Hz), "SetSim60Hz") && all_ok;
      if (!all_ok)
      {
         Log(reshade::log::level::error, "[BL SimTick] signature mismatch, sim tick hooks inactive (nothing hooked)");
         return;
      }

      auto h_adv = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kRvaAdvanceAccum), reinterpret_cast<void*>(&HookedAdvanceAccum));
      auto h_tick = safetyhook::InlineHook::create(reinterpret_cast<void*>(g_base + kRvaTick), reinterpret_cast<void*>(&HookedTick));
      if (!h_adv || !h_tick)
      {
         Log(reshade::log::level::error, "[BL SimTick] inline hook creation failed (advance=%d tick=%d), sim tick hooks inactive",
             h_adv ? 1 : 0, h_tick ? 1 : 0);
         return; // the Result objects unhook whatever was created
      }
      g_hook_advance = std::move(*h_adv);
      g_hook_tick = std::move(*h_tick);

#if DEVELOPMENT
      QueryPerformanceFrequency(&g_qpf);
#endif
      Log(reshade::log::level::info, "[BL SimTick] sim tick hooks installed");
   }

#if DEVELOPMENT
   static std::filesystem::path CsvPath()
   {
      wchar_t buf[MAX_PATH] = {};
      GetModuleFileNameW(nullptr, buf, MAX_PATH);
      return std::filesystem::path(buf).parent_path() / L"Luma_BL_SimTick.csv";
   }

   // Writes buffered rows. The first flush of the session truncates the file and writes the header once.
   static void FlushRows()
   {
      if (g_rows.empty())
         return;
      std::ofstream f;
      if (!g_csv_started)
         f.open(CsvPath(), std::ios::out | std::ios::trunc);
      else
         f.open(CsvPath(), std::ios::out | std::ios::app);
      if (!f)
      {
         if (!g_csv_error_logged)
         {
            g_csv_error_logged = true;
            Log(reshade::log::level::error, "[BL SimTick] could not open Luma_BL_SimTick.csv");
         }
         g_rows.clear();
         return;
      }
      if (!g_csv_started)
      {
         f << "time_ms,present_ms,frame_dt_ms,ticks,accum,rate\n";
         g_csv_started = true;
      }
      f.setf(std::ios::fixed);
      f.precision(3);
      for (const Row& r : g_rows)
      {
         f << r.time_ms << ',' << r.present_ms << ',' << r.frame_dt_ms << ',' << r.ticks << ','
           << r.accum << ',' << r.rate << '\n';
      }
      g_rows.clear();
   }
#endif
} // namespace BLSimTick

// Called from OnPresent (main.cpp). sim60 = "Animation Interpolation" (60 Hz sim) setting, log = dev toggle "sim.log".
static void SimTick_OnPresent(bool sim60, bool log)
{
   using namespace BLSimTick;

   g_desired_60.store(sim60);

   const uint32_t ticks = g_ticks_since_present.exchange(0, std::memory_order_relaxed);
#if DEVELOPMENT
   g_sim_ticks_last = ticks;

   LARGE_INTEGER now_li;
   QueryPerformanceCounter(&now_li);
   const LONGLONG now = now_li.QuadPart;
   if (g_qpf.QuadPart == 0)
      QueryPerformanceFrequency(&g_qpf);
   const double qpf = static_cast<double>(g_qpf.QuadPart);
   if (g_start_qpc == 0)
   {
      g_start_qpc = now;
      g_window_start_qpc = now;
   }

   const double time_ms = static_cast<double>(now - g_start_qpc) * 1000.0 / qpf;
   const double present_ms = g_last_present_qpc ? static_cast<double>(now - g_last_present_qpc) * 1000.0 / qpf : 0.0;
   g_last_present_qpc = now;

   float accum = 0.f;
   void* self = MainSelf();
   if (self && !IsBadReadPtr(reinterpret_cast<const char*>(self) + kSimObjectSize - 0x10, 0x10))
   {
      const float num = *reinterpret_cast<const float*>(reinterpret_cast<const char*>(self) + kOffAccumNum);
      const float den = *reinterpret_cast<const float*>(reinterpret_cast<const char*>(self) + kOffAccumDen);
      accum = den != 0.f ? num / den : 0.f;
   }
   const float rate = g_base ? *RateGlobal() : 0.f;

   // Window stats (always collected, logged only when sim.log is on).
   g_window_hist[ticks >= 3 ? 3 : ticks]++;
   if (g_prev_ticks != 0xFFFFFFFFu && ticks != g_prev_ticks)
      g_window_tick_changes++;
   g_prev_ticks = ticks;
   g_window_frames++;

   if (log)
   {
      g_rows.push_back({ time_ms, present_ms, g_last_frame_dt.load() * 1000.0, ticks, accum, rate });
   }

   if (g_window_frames >= kWindowPresents)
   {
      const double elapsed_s = static_cast<double>(now - g_window_start_qpc) / qpf;
      if (log)
      {
         const double fps = elapsed_s > 0.0 ? g_window_frames / elapsed_s : 0.0;
         const double change_pct = 100.0 * g_window_tick_changes / g_window_frames;
         Log(reshade::log::level::info,
             "[BL SimTick] last %u presents: avg fps %.2f, ticks/frame 0:%u 1:%u 2:%u 3+:%u, rate %.3f, ticks changed %.1f%%",
             g_window_frames, fps, g_window_hist[0], g_window_hist[1], g_window_hist[2], g_window_hist[3], rate, change_pct);
         FlushRows();
      }
      else
      {
         g_rows.clear();
      }
      g_window_start_qpc = now;
      g_window_frames = 0;
      g_window_tick_changes = 0;
      for (uint32_t& h : g_window_hist)
         h = 0;
   }
#else
   (void)ticks;
   (void)log;
#endif
}

// Called from OnInit (main.cpp), next to InstallFmodUpgrade.
static void InstallSimTick()
{
   BLSimTick::Install();
}
#else
static void SimTick_OnPresent(bool, bool) {}
static void InstallSimTick() {}
#endif
