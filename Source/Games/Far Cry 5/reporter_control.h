#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#pragma comment(lib, "bcrypt.lib")

namespace FC5
{
   // Experimental controller. Never terminate a thread or drain another owner's
   // suspend count. Caller may persist intent, but never thread IDs or handles.
   struct ReporterHandle
   {
      HANDLE value = nullptr;
      explicit ReporterHandle(HANDLE h = nullptr) : value(h) {}
      ~ReporterHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
      ReporterHandle(const ReporterHandle&) = delete;
   };
   struct ReporterSuspension
   {
      HANDLE thread;
      bool owned = false;
      explicit ReporterSuspension(HANDLE h) : thread(h)
      {
         const DWORD previous = SuspendThread(thread);
         if (previous == 0) owned = true;
         else if (previous != DWORD(-1)) ResumeThread(thread);
      }
      ~ReporterSuspension() { if (owned) ResumeThread(thread); }
   };
   class ReporterControl
   {
      HANDLE target = nullptr;
      const char* status = "Reporting worker not suspended by Luma.";
      static uint64_t CPU(HANDLE h)
      {
         FILETIME c{}, e{}, k{}, u{};
         if (!GetThreadTimes(h, &c, &e, &k, &u)) return 0;
         return (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
                (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime);
      }
      static bool AtPoll(HANDLE h, uintptr_t base)
      {
         alignas(16) CONTEXT context{};
         context.ContextFlags = CONTEXT_CONTROL;
         if (!GetThreadContext(h, &context)) return false;
         constexpr uintptr_t offsets[] = {0x8068100,0x8068107,0x8068109,0x8068111,
            0x8068114,0x8068116,0x8068130,0x8068137,0x8068139,0x806813b,
            0x8068170,0x8068177,0x806817e,0x8068181,0x8068188,0x806818f,0x8068191};
         return std::find(std::begin(offsets), std::end(offsets), context.Rip - base) != std::end(offsets);
      }
   public:
      static bool VerifiedCode(const std::array<unsigned char, 194>& code)
      {
         // SHA256 of the complete supported polling function, not game bytes.
         constexpr unsigned char expected[] = {0x5b,0xcc,0x3d,0xb4,0x83,0xcb,0x63,0x1a,
            0xfd,0x14,0xdb,0xa9,0x73,0xeb,0x45,0xf6,0x1c,0x70,0x86,0x23,0x4b,0xf2,0xd2,0x03,
            0xad,0x12,0xce,0x97,0xb1,0x17,0x76,0x8d};
         BCRYPT_ALG_HANDLE alg = nullptr;
         if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
         unsigned char hash[32]{};
         const auto result = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(code.data()),
            static_cast<ULONG>(code.size()), hash, sizeof(hash));
         BCryptCloseAlgorithmProvider(alg, 0);
         return result >= 0 && std::equal(std::begin(hash), std::end(hash), std::begin(expected));
      }
      ReporterControl() = default;
      ReporterControl(const ReporterControl&) = delete;
      ~ReporterControl() { Resume(); }
      bool Suspended() const { return target != nullptr; }
      const char* Status() const { return status; }
      bool Resume()
      {
         if (!target) return true;
         const auto previous = ResumeThread(target);
         if (previous == DWORD(-1) && WaitForSingleObject(target, 0) != WAIT_OBJECT_0)
         { status = "Resume failed; retry Resume or close the game."; return false; }
         CloseHandle(target); target = nullptr;
         status = previous > 1 && previous != DWORD(-1) ?
            "Our suspension removed; another tool still owns a suspension." : "Reporting worker resumed.";
         return true;
      }
      bool Pause()
      {
         if (target) return true;
         const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"FC_m64.dll"));
         std::array<unsigned char, 194> code{}; SIZE_T bytes = 0;
         if (!base || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(base + 0x80680e0),
             code.data(), code.size(), &bytes) || bytes != code.size() || !VerifiedCode(code))
         { status = "Unsupported/modified reporter code; no suspension performed."; return false; }
         struct Candidate { DWORD id; uint64_t before, delta; };
         std::vector<Candidate> candidates;
         const auto pid = GetCurrentProcessId(), caller = GetCurrentThreadId();
         ReporterHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
         THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
         if (Thread32First(snapshot.value, &entry)) do
         {
            if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == caller) continue;
            ReporterHandle h(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID));
            if (h.value) candidates.push_back({entry.th32ThreadID, CPU(h.value), 0});
         } while (Thread32Next(snapshot.value, &entry));
         Sleep(500); // Explicit toggle or one saved startup request, never recurring.
         for (auto& c : candidates)
         {
            ReporterHandle h(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, c.id));
            const auto now = CPU(h.value); c.delta = now >= c.before ? now - c.before : 0;
         }
         std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.delta > b.delta; });
         DWORD match = 0;
         constexpr DWORD access = THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
         for (size_t i = 0; i < (std::min)(size_t(5), candidates.size()); ++i)
         {
            if (candidates[i].delta < 250000) continue;
            ReporterHandle h(OpenThread(access, FALSE, candidates[i].id));
            if (!h.value || GetProcessIdOfThread(h.value) != pid) continue;
            unsigned hits = 0;
            for (unsigned n = 0; n < 5; ++n)
            {
               // No allocation, logging, locking or sleeping while temporarily suspended.
               { ReporterSuspension held(h.value); if (held.owned && AtPoll(h.value, base)) ++hits; }
               Sleep(10);
            }
            if (hits < 3) continue;
            if (match) { status = "Ambiguous reporter identity; no sustained suspension."; return false; }
            match = candidates[i].id;
         }
         if (!match) { status = "No verified active reporter found; try again during gameplay."; return false; }
         ReporterHandle h(OpenThread(access, FALSE, match));
         if (!h.value || GetProcessIdOfThread(h.value) != pid)
         { status = "Reporter exited before suspension."; return false; }
         for (unsigned attempt = 0; attempt < 20; ++attempt)
         {
            {
               ReporterSuspension held(h.value);
               if (held.owned && AtPoll(h.value, base))
               {
                  target = h.value; h.value = nullptr; held.owned = false;
                  status = "Reporting worker suspended until unchecked or game exits.";
                  return true;
               }
            }
            Sleep(1);
         }
         status = "Could not stop at a verified polling instruction; left running.";
         return false;
      }
   };
}
