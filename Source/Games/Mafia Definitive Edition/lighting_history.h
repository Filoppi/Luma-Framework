#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace MafiaDE
{
   // Core supplies instructions without the program/size header and rebuilds the DXBC container.
   inline std::unique_ptr<std::byte[]> PatchReflectionHistoryCap(const std::byte* code, size_t& size)
   {
      constexpr uint32_t original[] = {0x08000033, 0x00100042, 1, 0x0010002a, 1, 0x0020800a, 0, 1};
      constexpr uint32_t replacement[] = {0x07000033, 0x00100042, 1, 0x0010002a, 1, 0x00004001, 0x3f800000};
      if (code == nullptr || size < sizeof(original) || size % sizeof(uint32_t) != 0) return nullptr;

      // Match the complete min r1.z, r1.z, cb0[1].x at DWORD boundaries, never a fixed offset.
      size_t match = size;
      for (size_t offset = 0; offset <= size - sizeof(original); offset += sizeof(uint32_t))
      {
         if (std::memcmp(code + offset, original, sizeof(original)) != 0) continue;
         if (match != size) return nullptr;
         match = offset;
      }
      if (match == size) return nullptr;

      // Raise only this cap to l(1.0); retain the later .96875 clamp and all rejection/spatial gates.
      const size_t new_size = size - sizeof(uint32_t);
      auto patched = std::make_unique<std::byte[]>(new_size);
      std::memcpy(patched.get(), code, match);
      std::memcpy(patched.get() + match, replacement, sizeof(replacement));
      std::memcpy(patched.get() + match + sizeof(replacement), code + match + sizeof(original), size - match - sizeof(original));
      size = new_size;
      return patched;
   }

   inline std::unique_ptr<std::byte[]> PatchDiffuseHistoryCap(const std::byte* code, size_t& size)
   {
      constexpr uint32_t original[] = {0x08000033, 0x00100082, 1, 0x0010003a, 1, 0x0020800a, 0, 0};
      constexpr uint32_t replacement[] = {0x07000033, 0x00100082, 1, 0x0010003a, 1, 0x00004001, 0x3f800000};
      if (!code || size < sizeof(original) || size % sizeof(uint32_t) != 0) return nullptr;
      size_t match = size;
      for (size_t offset = 0; offset <= size - sizeof(original); offset += sizeof(uint32_t))
      {
         if (std::memcmp(code + offset, original, sizeof(original)) != 0) continue;
         if (match != size) return nullptr;
         match = offset;
      }
      if (match == size) return nullptr;
      // Lift only min r1.w, r1.w, cb0[0].x; preserve metadata, brightness and the final .96875 clamp.
      const size_t new_size = size - sizeof(uint32_t);
      auto patched = std::make_unique<std::byte[]>(new_size);
      std::memcpy(patched.get(), code, match);
      std::memcpy(patched.get() + match, replacement, sizeof(replacement));
      std::memcpy(patched.get() + match + sizeof(replacement), code + match + sizeof(original), size - match - sizeof(original));
      size = new_size;
      return patched;
   }
} // namespace MafiaDE
