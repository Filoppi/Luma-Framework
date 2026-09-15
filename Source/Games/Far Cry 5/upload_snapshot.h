#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace FC5
{
   // Upload lifetime/freshness only; device ordering and GPU trust are separate.
   struct UploadSnapshot
   {
      static constexpr uint64_t Bytes = 896;
      std::array<float, Bytes / sizeof(float)> rows{};
      const unsigned char* pending = nullptr;
      uint64_t offset = 0, generation = 0, epoch = 0;
      uint32_t thread = 0;
      bool valid = false, discard = false;

      void Invalidate() { valid = false; pending = nullptr; discard = false; }
      bool Fresh(uint64_t current_epoch, uint32_t thread_id) const
      { return valid && discard && !pending && epoch == current_epoch && thread == thread_id; }

      void Begin(const void* data, uint64_t map_offset, uint64_t map_size,
         uint64_t buffer_size, bool writable, uint32_t thread_id, uint64_t current_epoch = 0, bool full_discard = false)
      {
         valid = false; pending = nullptr;
         if (!writable || !data || map_offset > buffer_size || offset < map_offset ||
            offset > buffer_size || Bytes > buffer_size - offset) return;
         const uint64_t available = map_size < buffer_size - map_offset ? map_size : buffer_size - map_offset;
         const uint64_t relative = offset - map_offset;
         if (relative > available || Bytes > available - relative) return;
         pending = static_cast<const unsigned char*>(data) + relative;
         thread = thread_id;
         epoch = current_epoch;
         discard = full_discard && map_offset == 0 && available == buffer_size;
      }
      bool End(uint32_t thread_id, uint64_t current_epoch = 0)
      {
         valid = pending && thread == thread_id && epoch == current_epoch;
         if (valid) { std::memcpy(rows.data(), pending, Bytes); ++generation; }
         pending = nullptr;
         return valid;
      }
   };

   struct UploadTrust
   {
      uint64_t last_gpu_epoch = 0;
      uint32_t width = 0, height = 0, confirmed_frames = 0;
      void Invalidate() { *this = {}; }
      void Confirm(uint64_t epoch, uint32_t w, uint32_t h)
      {
         if (w != width || h != height) { Invalidate(); width = w; height = h; }
         if (epoch != last_gpu_epoch && confirmed_frames < 4) ++confirmed_frames;
         last_gpu_epoch = epoch;
      }
      bool Ready(uint64_t epoch, uint32_t w, uint32_t h) const
      {
         return confirmed_frames >= 4 && w == width && h == height &&
            epoch >= last_gpu_epoch && epoch - last_gpu_epoch < 120;
      }
   };
   struct UploadDeviceGate
   {
      bool device_seen = false, immediate_seen = false, deferred_seen = false, audit_failed = false;
      bool native_capture = false, native_failed = false;
      bool Safe() const { return device_seen && immediate_seen && (!deferred_seen || native_capture) && !native_failed && !audit_failed; }
   };
}
