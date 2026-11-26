#pragma once

// Vtable locator, thunk emitter, vtable patching, object adoption and CsInvoke for the D3D9 command-stream
// thread.

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "d3d9_cs_tables.h"

// Every thunk is exactly this many bytes (see CsEmitThunk).
inline constexpr size_t kCsThunkSize = 4 + 1 + 5 + 5 + 3 + 3; // lea, push, push imm32, call, add esp, ret imm16
static_assert(kCsThunkSize == 21, "CS thunk layout must be 21 bytes");

inline constexpr size_t kCsDeviceMinSlots = 119;      // minimum consecutive pointers for a device vtable
inline constexpr size_t kCsMaxVtablesPerIface = 8;    // hooked vtables per interface
inline constexpr size_t kCsThunkPageBytes = 0x10000;  // one VirtualAlloc per 64 KB of thunks

// CsSlot is defined in d3d9_cs_tables.h.

extern "C" DWORD __cdecl CsDispatch(CsSlot* slot, DWORD* args);

inline SRWLOCK g_cs_lock = SRWLOCK_INIT;
inline uint8_t* g_cs_thunk_cur = nullptr;
inline uint8_t* g_cs_thunk_end = nullptr;

struct CsHookedVtable
{
   void** vtable = nullptr;
};
inline CsHookedVtable g_cs_hooked[CsI_Count][kCsMaxVtablesPerIface];
inline size_t g_cs_hooked_count[CsI_Count] = {};

// Level 0 = info, 1 = warning, 2 = error.
inline void CsHookLog(int level, const char* msg)
{
   if (g_cs_log)
      g_cs_log(level, msg);
}

inline void CsHookLogf(int level, const char* fmt, unsigned a, unsigned b = 0, unsigned c = 0)
{
   char buf[192];
   _snprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a, b, c);
   CsHookLog(level, buf);
}

// Returns true when the vtable's slots now point at thunks (or were already). Caller holds g_cs_lock exclusively.
inline bool CsHookVtableLocked(void** vtable, CsIface iface);

// Emits one 21-byte thunk at dst. Thunk: lea eax,[esp+4] / push eax / push imm32 slot / call target / add esp,8 /
// ret argc*4. Returns false when target is out of rel32 range.
inline bool CsEmitThunkTo(uint8_t* dst, uint32_t slot_imm, uint8_t argc, uintptr_t target)
{
   const uintptr_t next = reinterpret_cast<uintptr_t>(dst) + 15; // address after the call (offset 10, 5 bytes)
   const int64_t rel = static_cast<int64_t>(target) - static_cast<int64_t>(next);
   if (rel < INT32_MIN || rel > INT32_MAX)
      return false;

   const int32_t rel32 = static_cast<int32_t>(rel);
   const uint16_t pop_bytes = static_cast<uint16_t>(argc) * 4;

   dst[0] = 0x8D; dst[1] = 0x44; dst[2] = 0x24; dst[3] = 0x04; // lea eax,[esp+4]
   dst[4] = 0x50;                                              // push eax (args)
   dst[5] = 0x68;                                              // push imm32 (slot)
   std::memcpy(dst + 6, &slot_imm, 4);
   dst[10] = 0xE8;                                             // call rel32 CsDispatch
   std::memcpy(dst + 11, &rel32, 4);
   dst[15] = 0x83; dst[16] = 0xC4; dst[17] = 0x08;             // add esp,8 (cdecl cleanup)
   dst[18] = 0xC2;                                             // ret imm16 (stdcall cleanup)
   std::memcpy(dst + 19, &pop_bytes, 2);
   return true;
}

// Emits one 21-byte thunk at dst for the slot. Returns false when CsDispatch is out of rel32 range.
// Thunk: lea eax,[esp+4] / push eax / push imm32 slot / call CsDispatch / add esp,8 / ret argc*4
inline bool CsEmitThunk(uint8_t* dst, const CsSlot* slot, uint8_t argc)
{
   return CsEmitThunkTo(dst, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(slot)), argc,
      reinterpret_cast<uintptr_t>(&CsDispatch));
}

// Bump-allocates 21-byte slots from RWX pages. Caller holds g_cs_lock exclusively.
inline uint8_t* CsAllocThunkLocked()
{
   if (g_cs_thunk_cur == nullptr || g_cs_thunk_cur + kCsThunkSize > g_cs_thunk_end)
   {
      void* page = VirtualAlloc(nullptr, kCsThunkPageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
      if (page == nullptr)
         return nullptr;
      g_cs_thunk_cur = static_cast<uint8_t*>(page);
      g_cs_thunk_end = g_cs_thunk_cur + kCsThunkPageBytes;
   }
   uint8_t* r = g_cs_thunk_cur;
   g_cs_thunk_cur += kCsThunkSize;
   return r;
}

// Writes one pointer-sized vtable entry with temporary write access. Returns false if protection fails.
inline bool CsWriteVtableSlot(void** entry, void* value)
{
   DWORD old = 0;
   if (!VirtualProtect(entry, sizeof(void*), PAGE_READWRITE, &old))
      return false;
   *entry = value;
   DWORD ignored = 0;
   VirtualProtect(entry, sizeof(void*), old, &ignored);
   return true;
}

inline bool CsHookVtableLocked(void** vtable, CsIface iface)
{
   if (vtable == nullptr || static_cast<size_t>(iface) >= CsI_Count)
      return false;

   const size_t idx = static_cast<size_t>(iface);
   for (size_t i = 0; i < g_cs_hooked_count[idx]; ++i)
   {
      if (g_cs_hooked[idx][i].vtable == vtable)
         return true;
   }
   if (g_cs_hooked_count[idx] >= kCsMaxVtablesPerIface)
   {
      CsHookLogf(1, "[BL CS] vtable limit reached for iface %u", static_cast<unsigned>(idx));
      return false;
   }

   // Register first so a partial failure below is never hooked a second time.
   g_cs_hooked[idx][g_cs_hooked_count[idx]].vtable = vtable;
   ++g_cs_hooked_count[idx];

   size_t count = 0;
   const CsMethod* rows = CsMethodTable(iface, count);
   if (rows == nullptr || count == 0)
   {
      CsHookLogf(2, "[BL CS] no method table for iface %u", static_cast<unsigned>(idx));
      return false;
   }

   size_t hooked = 0;
   bool ok = true;
   for (size_t i = 0; i < count && ok; ++i)
   {
      const CsMethod& row = rows[i];
      if (row.mode == CsM_Pass)
         continue;
      void* orig = vtable[row.slot];
      if (orig == nullptr)
         continue;

      CsSlot* slot = new CsSlot(); // zero-initialized, never freed
      slot->method = &row;
      slot->orig = orig;

      uint8_t* thunk = CsAllocThunkLocked();
      if (thunk == nullptr || !CsEmitThunk(thunk, slot, row.argc))
      {
         ok = false;
         break;
      }
      if (!CsWriteVtableSlot(&vtable[row.slot], thunk))
      {
         ok = false;
         break;
      }
      ++hooked;
   }

   CsHookLogf(ok ? 0 : 2, ok ? "[BL CS] hooked vtable iface %u: %u slots" : "[BL CS] hook failed iface %u after %u slots",
      static_cast<unsigned>(idx), static_cast<unsigned>(hooked));
   return ok;
}

inline bool CsHookVtable(void** vtable, CsIface iface)
{
   AcquireSRWLockExclusive(&g_cs_lock);
   const bool ok = CsHookVtableLocked(vtable, iface);
   ReleaseSRWLockExclusive(&g_cs_lock);
   return ok;
}

// Hooks *(void***)object for iface when that vtable is new. Cheap (shared lock, no writes) when already known.
inline bool CsAdopt(void* object, CsIface iface)
{
   if (object == nullptr || static_cast<size_t>(iface) >= CsI_Count)
      return false;

   void** vtable = *reinterpret_cast<void***>(object);
   if (vtable == nullptr)
      return false;

   const size_t idx = static_cast<size_t>(iface);
   AcquireSRWLockShared(&g_cs_lock);
   bool known = false;
   for (size_t i = 0; i < g_cs_hooked_count[idx]; ++i)
   {
      if (g_cs_hooked[idx][i].vtable == vtable)
      {
         known = true;
         break;
      }
   }
   ReleaseSRWLockShared(&g_cs_lock);
   if (known)
      return true;

   return CsHookVtable(vtable, iface);
}

// Finds the one device vtable in d3d9.dll: a run of >= kCsDeviceMinSlots pointers into the module image that
// the predicate accepts. Returns null unless exactly one vtable is accepted.
inline void** CsFindDeviceVtable(HMODULE d3d9, bool (*accept)(void** vtable))
{
   if (d3d9 == nullptr || accept == nullptr)
      return nullptr;

   const uintptr_t base = reinterpret_cast<uintptr_t>(d3d9);
   const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(d3d9);
   const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
   const uintptr_t image_end = base + nt->OptionalHeader.SizeOfImage;
   auto in_image = [&](uintptr_t v) { return v >= base && v < image_end; };

   // Committed, readable, non-guard regions of the image (adjacent ones merged).
   const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                          PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
   std::pair<uintptr_t, uintptr_t> regions[64];
   size_t region_count = 0;
   for (uintptr_t addr = base;;)
   {
      MEMORY_BASIC_INFORMATION mbi = {};
      if (VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)) == 0)
         break;
      if (reinterpret_cast<uintptr_t>(mbi.AllocationBase) != base)
         break;
      if (mbi.State == MEM_COMMIT && (mbi.Protect & readable) != 0 && (mbi.Protect & PAGE_GUARD) == 0)
      {
         const uintptr_t rb = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
         const uintptr_t re = rb + mbi.RegionSize;
         if (region_count > 0 && regions[region_count - 1].second == rb)
            regions[region_count - 1].second = re;
         else if (region_count < 64)
            regions[region_count++] = { rb, re };
      }
      addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
   }

   constexpr size_t word = sizeof(uintptr_t);
   uintptr_t found = 0;
   int matches = 0;
   for (size_t r = 0; r < region_count; ++r)
   {
      const uintptr_t rb = regions[r].first;
      const uintptr_t re = regions[r].second;
      for (uintptr_t p = rb; p + kCsDeviceMinSlots * word <= re; p += word)
      {
         const uintptr_t* v = reinterpret_cast<const uintptr_t*>(p);
         size_t n = 0;
         while (p + (n + 1) * word <= re && in_image(v[n]))
            ++n;
         if (n < kCsDeviceMinSlots)
            continue;
         if (accept(reinterpret_cast<void**>(p)))
         {
            ++matches;
            found = p;
         }
      }
   }

   if (matches != 1)
   {
      CsHookLogf(1, "[BL CS] device vtable matched %u time(s), expected 1",
         static_cast<unsigned>(matches));
      return nullptr;
   }

   CsHookLogf(0, "[BL CS] device vtable at d3d9.dll+0x%X",
      static_cast<unsigned>(found - base));
   return reinterpret_cast<void**>(found);
}

// Calls fn as __stdcall with argc DWORD arguments (args[0] is `this`). Returns E_FAIL for unsupported argc.
template <size_t>
using CsDwordArg = DWORD;

template <size_t... I>
inline DWORD CsInvokeImpl(void* fn, const DWORD* args, std::index_sequence<I...>)
{
   using Fn = DWORD(__stdcall*)(CsDwordArg<I>...);
   return reinterpret_cast<Fn>(fn)(args[I]...);
}

template <size_t N>
inline DWORD CsInvokeN(void* fn, const DWORD* args)
{
   return CsInvokeImpl(fn, args, std::make_index_sequence<N>{});
}

inline DWORD CsInvoke(void* fn, const DWORD* args, uint8_t argc)
{
   if (fn == nullptr || args == nullptr)
      return static_cast<DWORD>(E_POINTER);
   switch (argc)
   {
   case 1: return CsInvokeN<1>(fn, args);
   case 2: return CsInvokeN<2>(fn, args);
   case 3: return CsInvokeN<3>(fn, args);
   case 4: return CsInvokeN<4>(fn, args);
   case 5: return CsInvokeN<5>(fn, args);
   case 6: return CsInvokeN<6>(fn, args);
   case 7: return CsInvokeN<7>(fn, args);
   case 8: return CsInvokeN<8>(fn, args);
   case 9: return CsInvokeN<9>(fn, args);
   case 10: return CsInvokeN<10>(fn, args);
   case 11: return CsInvokeN<11>(fn, args);
   case 12: return CsInvokeN<12>(fn, args);
   default: return static_cast<DWORD>(E_FAIL);
   }
}
