#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <span>
#include <vector>

namespace System
{
   // Returns the path to the "System32" directory
   std::filesystem::path GetSystemPath();

   // Pass in null/0 to get the path to the process executable (.exe)
   std::filesystem::path GetModulePath(HMODULE hModule = 0);

   std::string GetProcessExecutableName();

   bool GetDLLVersion(const std::filesystem::path& file_path, uint64_t& file_version, uint64_t& product_version);

   bool CopyToClipboard(const std::string& text);

   void OpenExplorerToFile(const std::filesystem::path& file_path);

   // Check if we have enough contiguous memory to allocate x bytes.
   // Especially useful in x86 processes with limited space and fragmented free memory.
   bool CanAllocate(size_t bytes);

   struct BytePattern
   {
      std::byte value;
      bool wildcard; // True if this byte is "??"
      
      enum class WildcardType { Wildcard };

      constexpr BytePattern(std::byte _value)
          : value(_value), wildcard(false)
      {
      }
      constexpr BytePattern(unsigned int _value)
          : value(static_cast<std::byte>(_value)), wildcard(false)
      {
      }
      constexpr explicit BytePattern(WildcardType)
          : value(std::byte{0x00}), wildcard(true)
      {
         // We could leave "value" uninitialized, but we'd get a warning
      }
   };
   static constexpr BytePattern ANY{BytePattern::WildcardType::Wildcard};

   enum class PatchMemoryType
   {
      // PAGE_EXECUTE_READWRITE
      Code,
      // PAGE_READWRITE
      Data,
   };

   // Returns all matches. The BytePattern overload supports "??" wildcard bytes.
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<BytePattern>& pattern, bool stop_at_first = false);
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, std::span<const BytePattern> pattern, bool stop_at_first = false);
   std::vector<std::byte*> ScanModuleForPattern(const std::vector<BytePattern>& pattern, bool stop_at_first = false);
   std::vector<std::byte*> ScanModuleForPattern(std::span<const BytePattern> pattern, bool stop_at_first = false);
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::byte* pattern, size_t pattern_size, bool stop_at_first = false);
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<std::byte>& pattern, bool stop_at_first = false);
   std::vector<std::byte*> ScanMemoryForPattern(const std::byte* base, size_t size, const std::vector<uint8_t>& pattern, bool stop_at_first = false);

   bool PatchMemory(void* address, const void* data, size_t size, PatchMemoryType type = PatchMemoryType::Code);

   // Allocate within +/-2GB to make it reachable by 32 bit offsets
   void* VirtualAllocNear(void* target, size_t size, DWORD protect = PAGE_EXECUTE_READWRITE);
}
