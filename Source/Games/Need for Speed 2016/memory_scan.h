#pragma once

#include <cctype>

// Searches the game's exe image in memory: all of its sections, unlike "System::ScanModuleForPattern()", which only searches code. The
// image is over 100 MB and is searched on the render thread, so the exact searches skip ahead with memchr or compare whole words.
namespace MemoryScan
{
   enum class RegionKind
   {
      All,
      Executable,
      Writable,
   };

   struct Region
   {
      const uint8_t* begin;
      const uint8_t* end;
   };

   // A value from game memory, which can be unaligned
   template <typename T>
   T Read(const uint8_t* address)
   {
      T value;
      std::memcpy(&value, address, sizeof(T));
      return value;
   }

   // Any read access, and not a guard page
   inline bool IsReadableProtection(DWORD protection)
   {
      constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
      return !(protection & (PAGE_GUARD | PAGE_NOACCESS)) && (protection & kReadable);
   }

   // How many bytes from "address" on are committed readable memory, up to the end of its memory region
   inline size_t GetReadableSize(const void* address)
   {
      MEMORY_BASIC_INFORMATION info = {};
      if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info))
         return 0;
      if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect))
         return 0;
      return uintptr_t(info.BaseAddress) + info.RegionSize - uintptr_t(address);
   }

   // A system call: keep it off per match loops that can run millions of times
   inline bool IsReadable(const void* address, size_t size)
   {
      return address && GetReadableSize(address) >= size;
   }

   // Committed, readable, and executable or writable if asked for
   inline bool IsOfKind(const MEMORY_BASIC_INFORMATION& info, RegionKind kind)
   {
      if (info.State != MEM_COMMIT || !IsReadableProtection(info.Protect))
         return false;
      const bool executable = info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
      const bool writable = info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
      return kind == RegionKind::All || (kind == RegionKind::Executable && executable) || (kind == RegionKind::Writable && writable);
   }

   // The exe image's readable memory regions of a kind. They start page aligned.
   inline std::vector<Region> GetImageRegions(RegionKind kind)
   {
      const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandle(nullptr));
      const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
      const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos_header->e_lfanew);
      const uint8_t* image_end = base + nt_headers->OptionalHeader.SizeOfImage;

      std::vector<Region> regions;
      MEMORY_BASIC_INFORMATION info = {};
      for (const uint8_t* begin = base; begin < image_end && VirtualQuery(begin, &info, sizeof(info)) == sizeof(info);)
      {
         const uint8_t* end = (std::min)(image_end, static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize);
         if (IsOfKind(info, kind))
            regions.push_back({begin, end});
         begin = end;
      }
      return regions;
   }

   // A pattern with wildcards, through core's scanner (which compares at every position)
   inline std::vector<const uint8_t*> FindPattern(const std::vector<System::BytePattern>& pattern, RegionKind kind)
   {
      std::vector<const uint8_t*> matches;
      for (const Region& region : GetImageRegions(kind))
      {
         const auto* bytes = reinterpret_cast<const std::byte*>(region.begin);
         for (std::byte* match : System::ScanMemoryForPattern(bytes, size_t(region.end - region.begin), pattern))
            matches.push_back(reinterpret_cast<const uint8_t*>(match));
      }
      return matches;
   }

   // Every copy of exact "bytes" in the exe image: memchr skips to the candidates for the first byte, which is far faster than comparing
   // at every position
   inline std::vector<const uint8_t*> FindBytes(std::span<const uint8_t> bytes, RegionKind kind)
   {
      std::vector<const uint8_t*> matches;
      for (const Region& region : GetImageRegions(kind))
      {
         if (size_t(region.end - region.begin) < bytes.size())
            continue;
         const uint8_t* last = region.end - bytes.size(); // The last position a copy fits at
         for (const uint8_t* candidate = region.begin; candidate <= last; candidate++)
         {
            candidate = static_cast<const uint8_t*>(std::memchr(candidate, bytes[0], size_t(last - candidate) + 1));
            if (!candidate)
               break;
            if (std::memcmp(candidate, bytes.data(), bytes.size()) == 0)
               matches.push_back(candidate);
         }
      }
      return matches;
   }

   // Every pointer to "target" stored in the exe image. Pointers are stored aligned, so only aligned words are compared.
   inline std::vector<const uint8_t*> FindPointersTo(const void* target)
   {
      std::vector<const uint8_t*> pointers;
      for (const Region& region : GetImageRegions(RegionKind::All))
         for (const uint8_t* word = region.begin; word + sizeof(void*) <= region.end; word += sizeof(void*))
            if (Read<const void*>(word) == target)
               pointers.push_back(word);
      return pointers;
   }

   // Every copy of a null terminated string in the exe image. A match right after a name character is the end of a longer name, so it's
   // skipped.
   inline std::vector<const char*> FindStrings(const char* text)
   {
      // With the null terminator
      const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(text), std::strlen(text) + 1);
      std::vector<const char*> strings;
      for (const uint8_t* match : FindBytes(bytes, RegionKind::All))
      {
         uint8_t previous = 0;
         if (IsReadable(match - 1, 1))
            previous = match[-1];
         if (!std::isalnum(previous) && previous != '_' && previous != '.')
            strings.push_back(reinterpret_cast<const char*>(match));
      }
      return strings;
   }

   // Whether "text" points to a readable, printable, null terminated string of up to "max_length" characters
   inline bool IsPrintableString(const char* text, size_t max_length)
   {
      const size_t readable_length = (std::min)(max_length, GetReadableSize(text));
      for (size_t i = 0; i < readable_length; i++)
      {
         if (text[i] == '\0')
            return i > 0;
         if (text[i] < 0x20 || text[i] > 0x7E)
            return false;
      }
      return false;
   }

   // The target of an x64 RIP relative operand, "operand_offset" bytes into an instruction of "instruction_size" bytes
   inline const uint8_t* GetRipTarget(const uint8_t* instruction, size_t operand_offset, size_t instruction_size)
   {
      return instruction + instruction_size + Read<int32_t>(instruction + operand_offset);
   }
} // namespace MemoryScan
