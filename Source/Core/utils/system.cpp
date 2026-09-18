#include "system.h"

#include "assert.h"
#include <emmintrin.h>
#include <intrin.h>

// For "GetFileVersionInfoSize" etc
#pragma comment(lib, "Version.lib")

namespace System
{
   namespace {
      // Byte frequency table for SIMD anchor-byte selection
      static constexpr unsigned char byte_frequencies[256] = {
         0xFF,0xFB,0xF2,0xEE,0xEC,0xE7,0xDC,0xC8,0xED,0xB7,0xCC,0xC0,0xD3,0xCD,0x89,0xFA,
         0xF3,0xD6,0x8D,0x83,0xC1,0xAA,0x7A,0x72,0xC6,0x60,0x3E,0x2E,0x98,0x69,0x39,0x7C,
         0xEB,0x76,0x24,0x34,0xF9,0x50,0x04,0x07,0xE5,0xAC,0x53,0x65,0x9B,0x4D,0x6D,0x5C,
         0xDA,0x93,0x7F,0xCB,0x92,0x49,0x43,0x09,0xBA,0x8E,0x1E,0x91,0x8A,0x5B,0x11,0xA1,
         0xE8,0xF5,0x9E,0xAD,0xEF,0xE6,0x79,0x7B,0xFE,0xE0,0x1F,0x54,0xE4,0xBD,0x7D,0x6A,
         0xDF,0x67,0x7E,0xA4,0xB6,0xAF,0x88,0xA0,0xC3,0xA9,0x26,0x77,0xD1,0x71,0x61,0xC2,
         0x9A,0xCA,0x29,0x9F,0xD8,0xE2,0xD0,0x6E,0xB4,0xB8,0x25,0x3C,0xBF,0x73,0xB5,0xCF,
         0xD4,0x01,0xCE,0xBE,0xF1,0xDB,0x52,0x37,0x9D,0x63,0x02,0x6B,0x80,0x45,0x2B,0x95,
         0xE1,0xC4,0x36,0xF0,0xD5,0xE3,0x57,0x9C,0xB1,0xF7,0x82,0xFC,0x42,0xF6,0x18,0x33,
         0xD2,0x48,0x05,0x0F,0x41,0x1D,0x03,0x27,0x70,0x10,0x00,0x08,0x55,0x16,0x2F,0x0E,
         0x94,0x35,0x2C,0x40,0x6F,0x3B,0x1C,0x28,0x90,0x68,0x81,0x4B,0x56,0x30,0x2A,0x3D,
         0x97,0x17,0x06,0x13,0x32,0x0B,0x5A,0x75,0xA5,0x86,0x78,0x4F,0x2D,0x51,0x46,0x5F,
         0xE9,0xDE,0xA2,0xDD,0xC9,0x4C,0xAB,0xBB,0xC7,0xB9,0x74,0x8F,0xF8,0x6C,0x85,0x8B,
         0xC5,0x84,0x8C,0x66,0x21,0x23,0x64,0x59,0xA3,0x87,0x44,0x58,0x3A,0x0D,0x12,0x19,
         0xAE,0x5E,0x3F,0x38,0x31,0x22,0x0A,0x14,0xF4,0xD9,0x20,0xB0,0xB2,0x1A,0x0C,0x15,
         0xB3,0x47,0x5D,0xEA,0x4A,0x1B,0x99,0xBC,0xD7,0xA6,0x62,0x4E,0xA8,0x96,0xA7,0xFD
      };

      // SIMD anchor-byte scan for a single memory region (used internally by ScanModuleForPattern)
      static std::vector<std::byte*> ScanRegionWithSIMD(const std::byte* base, size_t size,
         std::span<const BytePattern> pattern, bool stop_at_first)
      {
         // Find anchor byte (least frequent = fewest false positives)
         uint8_t anchor = 0;
         size_t anchor_idx = SIZE_MAX;
         size_t min_freq = SIZE_MAX;
         std::vector<std::pair<size_t, uint8_t>> verify_pairs;

         for (size_t j = 0; j < pattern.size(); ++j)
         {
            if (!pattern[j].wildcard)
            {
               uint8_t val = static_cast<uint8_t>(pattern[j].value);
               verify_pairs.emplace_back(j, val);
               if (byte_frequencies[val] < min_freq)
               {
                  min_freq = byte_frequencies[val];
                  anchor = val;
                  anchor_idx = j;
               }
            }
         }
         if (anchor_idx == SIZE_MAX || size < pattern.size())
            return {};

         const size_t candidate_count = size - pattern.size() + 1;
         const uint8_t* data = reinterpret_cast<const uint8_t*>(base);
         std::vector<std::byte*> matches;

         // x64 guarantees SSE2. A vector covers candidate starts, not the whole
         // pattern, so verify every anchor hit before accepting it.
         const __m128i needle = _mm_set1_epi8(static_cast<char>(anchor));
         size_t offset = 0;
         for (; candidate_count - offset >= 16; offset += 16)
         {
            const auto cmp = _mm_cmpeq_epi8(
               _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset + anchor_idx)), needle);
            uint32_t mask = static_cast<uint32_t>(_mm_movemask_epi8(cmp));
            while (mask)
            {
               unsigned long bit = 0;
               _BitScanForward(&bit, mask);
               const size_t pos = offset + bit;
               bool ok = true;
               for (const auto& [idx, val] : verify_pairs)
                  if (data[pos + idx] != val) { ok = false; break; }
               if (ok)
               {
                  matches.push_back(const_cast<std::byte*>(base + pos));
                  if (stop_at_first) return matches;
               }
               mask &= mask - 1;
            }
         }
         for (; offset < candidate_count; ++offset)
         {
            if (data[offset + anchor_idx] != anchor)
               continue;
            bool ok = true;
            for (const auto& [idx, val] : verify_pairs)
               if (data[offset + idx] != val) { ok = false; break; }
            if (ok)
            {
               matches.push_back(const_cast<std::byte*>(base + offset));
               if (stop_at_first) return matches;
            }
         }
         return matches;
      }
   }

   std::filesystem::path GetSystemPath()
   {
      WCHAR buf[4096];
      GetSystemDirectoryW(buf, ARRAYSIZE(buf));
      return buf;
   }

   std::filesystem::path GetModulePath(HMODULE hModule)
   {
      std::vector<wchar_t> path(32768); // Maximum path length in Windows, there's no define for it, the old one is "MAX_PATH"
      DWORD length = GetModuleFileNameW(hModule, path.data(), (DWORD)path.size());
      if (length == 0 || length >= path.size()) // This should never happen unless we passed in an invalid module
      {
         assert(false);
         return {};
      }

      std::filesystem::path exe_path = std::wstring(path.data());
      return exe_path;
   }

   std::string GetProcessExecutableName()
   {
      std::string exe_name = GetModulePath().filename().string();
      return exe_name;
   }

   bool GetDLLVersion(const std::filesystem::path& file_path, uint64_t& file_version, uint64_t& product_version)
   {
      file_version = 0;
      product_version = 0;

      DWORD verHandle = 0;
      DWORD verSize = GetFileVersionInfoSize(file_path.c_str(), &verHandle);
      if (verSize != NULL)
      {
         LPSTR verData = new char[verSize];
         if (GetFileVersionInfo(file_path.c_str(), verHandle, verSize, verData))
         {
            LPBYTE lpBuffer = NULL;
            UINT size = 0;
            if (VerQueryValue(verData, L"\\", (VOID FAR * FAR*)&lpBuffer, &size))
            {
               if (size != 0 && lpBuffer != nullptr)
               {
                  VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
                  if (verInfo->dwSignature == 0xfeef04bd)
                  {
                     // Combine into a single 64-bit value: v1.v2.v3.v4. This can
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionMS >> 16) & 0xFFFF) << 48; // v1
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionMS >> 0) & 0xFFFF) << 32;  // v2
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionLS >> 16) & 0xFFFF) << 16; // v3
                     file_version |= static_cast<uint64_t>((verInfo->dwFileVersionLS >> 0) & 0xFFFF);        // v4

                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionMS >> 16) & 0xFFFF) << 48; // v1
                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionMS >> 0) & 0xFFFF) << 32;  // v2
                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionLS >> 16) & 0xFFFF) << 16; // v3
                     product_version |= static_cast<uint64_t>((verInfo->dwProductVersionLS >> 0) & 0xFFFF);        // v4

                     delete[] verData;
                     return true;
                  }
               }
            }
         }
         delete[] verData;
      }
      return false;
   }

   bool CopyToClipboard(const std::string& text)
   {
#ifdef WIN32
      // Convert UTF-8 to UTF-16
      int wideSize = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
      if (wideSize <= 0) return false;

      HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, wideSize * sizeof(wchar_t));
      if (!hMem) return false;

      wchar_t* wstr = static_cast<wchar_t*>(GlobalLock(hMem));
      if (!wstr)
      {
         GlobalFree(hMem);
         return false;
      }

      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wstr, wideSize);
      GlobalUnlock(hMem);

      if (!OpenClipboard(nullptr))
      {
         GlobalFree(hMem);
         return false;
      }

      EmptyClipboard();
      SetClipboardData(CF_UNICODETEXT, hMem); // Windows owns the memory after this
      CloseClipboard();

      return true;
#else
      return false;
#endif
   }

   void OpenExplorerToFile(const std::filesystem::path& file_path)
   {
#ifdef WIN32
      PIDLIST_ABSOLUTE pidl = nullptr;
      HRESULT hr = SHParseDisplayName(file_path.wstring().c_str(), nullptr, &pidl, 0, nullptr);
      if (SUCCEEDED(hr) && (pidl != nullptr)) {
         SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
         CoTaskMemFree(pidl);
      }
#else
      std::string command = "xdg-open " + file_path.parent_path().string();
      std::system(command.c_str());
#endif
   }

   bool CanAllocate(size_t bytes)
   {
      SYSTEM_INFO sysInfo;
      GetSystemInfo(&sysInfo);

      MEMORY_BASIC_INFORMATION mbi = {};
      BYTE* addr = (BYTE*)sysInfo.lpMinimumApplicationAddress;
      BYTE* max_addr = (BYTE*)sysInfo.lpMaximumApplicationAddress;

      while (addr < max_addr)
      {
         if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
            break;

         if (mbi.State == MEM_FREE && mbi.RegionSize >= bytes)
            return true;

         addr += mbi.RegionSize;
      }
      return false;
   }

   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<BytePattern>& pattern, bool stop_at_first)
   {
      return ScanMemoryForPattern(base, size, std::span<const BytePattern>(pattern), stop_at_first);
   }

   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, std::span<const BytePattern> pattern, bool stop_at_first)
   {
      if (base == nullptr || pattern.size() == 0 || pattern.size() > size)
         return {};

      std::vector<std::byte*> matches;
      for (size_t i = 0; i <= size - pattern.size(); ++i)
      {
         bool found = true;
         for (size_t j = 0; j < pattern.size(); ++j)
         {
            if (!pattern[j].wildcard && base[i + j] != pattern[j].value)
            {
               found = false;
               break;
            }
         }
         if (found)
         {
            matches.push_back(const_cast<std::byte*>(base + i));
            if (stop_at_first) break;
         }
      }
      return matches;
   }
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::byte* pattern, size_t pattern_size, bool stop_at_first)
   {
      if (pattern == nullptr || base == nullptr || pattern_size == 0 || pattern_size > size)
         return {};

      std::vector<std::byte*> matches;
      for (size_t i = 0; i <= size - pattern_size; ++i) {
         if (std::memcmp(base + i, pattern, pattern_size) == 0) {
            matches.push_back(const_cast<std::byte*>(base + i));
            if (stop_at_first) break;
         }
      }
      return matches;
   }
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<std::byte>& pattern, bool stop_at_first)
   {
      return ScanMemoryForPattern(base, size, pattern.data(), pattern.size(), stop_at_first);
   }
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<uint8_t>& pattern, bool stop_at_first)
   {
      return ScanMemoryForPattern(base, size, reinterpret_cast<const std::byte*>(pattern.data()), pattern.size(), stop_at_first);
   }

   bool PatchMemory(void* address, const void* data, size_t size, PatchMemoryType type)
   {
      // "PAGE_EXECUTE_READWRITE" can be temporarily set on data too, it's not going to be a problem
      const DWORD new_protect = type == PatchMemoryType::Code ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;

      DWORD old_protect;
      BOOL success = VirtualProtect(address, size, new_protect, &old_protect);
      if (success)
      {
         std::memcpy(address, data, size);

         DWORD temp_protect;
         VirtualProtect(address, size, old_protect, &temp_protect);

         const DWORD old_protect_masked = old_protect & 0xFF;
         bool was_executable = old_protect_masked == PAGE_EXECUTE ||
                              old_protect_masked == PAGE_EXECUTE_READ ||
                              old_protect_masked == PAGE_EXECUTE_READWRITE ||
                              old_protect_masked == PAGE_EXECUTE_WRITECOPY;

         // We only really need to check for "is_executable" but for extra safety this shouldn't hurt.
         // If the code wasn't executable, then we don't need to flush it, even if we temporarily made it so.
         if (type == PatchMemoryType::Code || was_executable)
         {
            FlushInstructionCache(GetCurrentProcess(), address, size);
         }

         // Error out if we tried to patch code as data or data as code (they are both ok but neither is good).
         assert(was_executable == (type == PatchMemoryType::Code));

         return true;
      }
      return false;
   }

   void* VirtualAllocNear(void* target, size_t size, DWORD protect)
   {
      // Avoid messes with ReShade redefining min/max
      auto localMin = [](auto a, auto b) { return (a < b) ? a : b; };
      auto localMax = [](auto a, auto b) { return (a > b) ? a : b; };

      SYSTEM_INFO sys_info;
      GetSystemInfo(&sys_info);

      constexpr uintptr_t two_gb = 0x80000000ull;

      uintptr_t start_addr = reinterpret_cast<uintptr_t>(target);
      uintptr_t min_addr = localMax(reinterpret_cast<uintptr_t>(sys_info.lpMinimumApplicationAddress), (start_addr > two_gb) ? (start_addr - two_gb) : 0); // The start of valid address, including this one
      uintptr_t max_addr = localMin(reinterpret_cast<uintptr_t>(sys_info.lpMaximumApplicationAddress), start_addr + two_gb); // This address is not allocatable, the last valid one is the one before
      // If memory is executable, remove the allocated size from the max searchable range, because all the memory we allocation should be within 2GB of the starting point (e.g. so we can jump pack to the original point).
      // This should be optional but whatever.
      constexpr DWORD execute_mask = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
      if ((protect & execute_mask) != 0)
         max_addr -= size - 1;

      MEMORY_BASIC_INFORMATION mbi{};
      uintptr_t addr = min_addr;

      while (addr < max_addr)
      {
         if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

         uintptr_t block_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
         uintptr_t block_end = block_start + mbi.RegionSize;

         if (mbi.State == MEM_FREE)
         {
            // Even if the whole memory region is available, simply allocate at the start of it (or anyway the first valid point that is valid)
            addr = localMax(block_start, min_addr);
            do
            {
               if (addr + size <= localMin(block_end, max_addr))
               {
                  // Try to allocate at this region's base address. This can fail for any reason.
                  // It also doesn't exactly allocate where we say so it needs to be tested.
                  void* ptr = VirtualAlloc(reinterpret_cast<LPVOID>(addr), size, MEM_RESERVE | MEM_COMMIT, protect);
                  if (ptr)
                  {
                     if (reinterpret_cast<uintptr_t>(ptr) >= min_addr && reinterpret_cast<uintptr_t>(ptr) < max_addr)
                     {
                        // Theoretically this test can't fail, but we do it anyway for extra safety
                        int64_t offset_test = reinterpret_cast<uintptr_t>(ptr) - start_addr;
                        if (offset_test >= INT32_MIN && offset_test <= INT32_MAX) // TODO: I'm not 100% these tests match with the "two_gb" limits, but I guess so
                           return ptr; // good
                     }
                     addr = reinterpret_cast<uintptr_t>(ptr);
                     VirtualFree(ptr, 0, MEM_RELEASE); // bad
                     break; // This could cause infinite loops given that VirtualAlloc() rounds down
                  }
                  addr += 1; // Slow but best chances of finding a match (I think)
               }
               else
               {
                  break;
               }
            } while (true);
         }

         // Move to the next region
         addr = block_end;
      }

      return nullptr; // No suitable block found
   }

   std::vector<std::byte*> ScanModuleForPattern(const std::vector<BytePattern>& pattern, bool stop_at_first)
   {
      return ScanModuleForPattern(std::span<const BytePattern>(pattern), stop_at_first);
   }

   std::vector<std::byte*> ScanModuleForPattern(std::span<const BytePattern> pattern, bool stop_at_first)
   {
      std::vector<std::byte*> matches;
      if (pattern.empty())
         return matches;

      const std::byte* base = reinterpret_cast<const std::byte*>(GetModuleHandle(nullptr));
      if (base == nullptr)
         return matches;

      const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
      if (dos_header->e_magic != IMAGE_DOS_SIGNATURE || dos_header->e_lfanew < 0)
         return matches;
      const auto* pe_header = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos_header->e_lfanew);
      if (pe_header->Signature != IMAGE_NT_SIGNATURE)
         return matches;

      const auto* first_section = IMAGE_FIRST_SECTION(pe_header);
      for (int i = 0; i < pe_header->FileHeader.NumberOfSections; ++i, ++first_section)
      {
         if (!(first_section->Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

         const uintptr_t section_start = reinterpret_cast<uintptr_t>(base) + first_section->VirtualAddress;
         const uintptr_t section_end = section_start + first_section->Misc.VirtualSize;
         MEMORY_BASIC_INFORMATION mbi = {};
         uintptr_t cur = section_start;
         while (cur < section_end &&
                VirtualQuery(reinterpret_cast<const void*>(cur), &mbi, sizeof(mbi)) == sizeof(mbi))
         {
            const uintptr_t region_end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            const uintptr_t scan_end = (region_end < section_end) ? region_end : section_end;
            const DWORD protection = mbi.Protect & 0xFF;
            if (mbi.State == MEM_COMMIT && !(mbi.Protect & PAGE_GUARD) &&
                protection != PAGE_NOACCESS && protection != 0 && scan_end > cur)
            {
               auto page_matches = ScanRegionWithSIMD(reinterpret_cast<const std::byte*>(cur),
                  scan_end - cur, pattern, stop_at_first);
               matches.insert(matches.end(), page_matches.begin(), page_matches.end());
               if (stop_at_first && !matches.empty())
                  goto done;
            }
            if (region_end <= cur) break;
            cur = region_end;
         }
      }
done:
      return matches;
   }

}
