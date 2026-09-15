#pragma once
#include <chrono>
namespace FC5
{
   // CPU wall time only: Map may wait for preceding GPU work. Not GPU timestamps.
   struct PerfCounter
   {
      using Clock = std::chrono::steady_clock;
      uint64_t count = 0;
      double total_ms = 0, peak_ms = 0;
      void Add(Clock::time_point start)
      {
         const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
         ++count; total_ms += ms; peak_ms = (std::max)(peak_ms, ms);
      }
      void Log(const char* label, uint64_t frame)
      {
         if (count)
         {
            char line[256];
            std::snprintf(line, sizeof(line), "[FC5 perf] frame=%llu CPU %s n=%llu total_ms=%.3f avg_ms=%.3f peak_ms=%.3f",
               frame, label, count, total_ms, total_ms / count, peak_ms);
            reshade::log::message(reshade::log::level::info, line);
         }
         count = 0; total_ms = peak_ms = 0;
      }
   };
   struct PerfScope
   {
      PerfCounter& counter;
      PerfCounter::Clock::time_point start = PerfCounter::Clock::now();
      explicit PerfScope(PerfCounter& c) : counter(c) {}
      ~PerfScope() { counter.Add(start); }
   };

   // Diagnostic only: CPU accounting is quantized. Aggregate many calls; cycles
   // are execution counts, NOT milliseconds or GPU time. No queue flush/wait.
   struct ThreadWorkCounter
   {
      struct Bucket
      {
         uint64_t count = 0, cycles = 0;
         double wall_ms = 0, cpu_ms = 0;
      } fast, slow;
      uint64_t failed = 0;
      static bool Enabled()
      {
         static const bool enabled = [] {
            char value[8]{};
            return GetEnvironmentVariableA("FC5_NGX_THREAD_TIMING", value, sizeof(value)) == 1 && value[0] == '1';
         }();
         return enabled;
      }
      static bool Read(uint64_t& cpu, ULONG64& cycles)
      {
         FILETIME created{}, exited{}, kernel{}, user{};
         const auto thread = GetCurrentThread();
         if (!GetThreadTimes(thread, &created, &exited, &kernel, &user) ||
             !QueryThreadCycleTime(thread, &cycles)) return false;
         cpu = (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
               (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
         return true;
      }
      void Log(uint64_t frame)
      {
         if (!Enabled()) return;
         const Bucket buckets[] = { fast, slow };
         for (unsigned i = 0; i < 2; ++i)
         {
            const auto& b = buckets[i];
            if (!b.count) continue;
            char line[320];
            std::snprintf(line, sizeof(line),
               "[FC5 thread] frame=%llu NGX %s n=%llu wall_ms=%.3f cpu_ms=%.3f cycles=%llu failed=%llu",
               frame, i ? "ge1ms" : "lt1ms", b.count, b.wall_ms, b.cpu_ms, b.cycles, failed);
            reshade::log::message(reshade::log::level::info, line);
         }
         fast = {}; slow = {}; failed = 0;
      }
   };
   struct ThreadWorkScope
   {
      ThreadWorkCounter& counter;
      bool active;
      uint64_t cpu = 0;
      ULONG64 cycles = 0;
      PerfCounter::Clock::time_point start{};
      explicit ThreadWorkScope(ThreadWorkCounter& c) : counter(c), active(ThreadWorkCounter::Enabled())
      {
         if (!active) return;
         if (!ThreadWorkCounter::Read(cpu, cycles)) { ++counter.failed; active = false; return; }
         start = PerfCounter::Clock::now();
      }
      ~ThreadWorkScope()
      {
         if (!active) return;
         const double wall = std::chrono::duration<double, std::milli>(PerfCounter::Clock::now() - start).count();
         uint64_t end_cpu = 0; ULONG64 end_cycles = 0;
         if (!ThreadWorkCounter::Read(end_cpu, end_cycles) || end_cpu < cpu || end_cycles < cycles)
         { ++counter.failed; return; }
         auto& b = wall >= 1.0 ? counter.slow : counter.fast;
         ++b.count; b.wall_ms += wall; b.cpu_ms += double(end_cpu - cpu) / 10000.0;
         b.cycles += end_cycles - cycles;
      }
   };
}
