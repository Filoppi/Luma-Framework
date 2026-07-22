#pragma once

#include "shared.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace QuantumBreakUpscaling
{
#if ENABLE_SR
   // The SR path needs current camera and jitter constants on the CPU. A direct staging readback at
   // every temporal resolve serializes the CPU with the GPU, so later CPU uploads are mirrored here.
   // The exact PS CB0/CB1 resources are discovered at the temporal resolve; subsequent Map/Unmap or
   // UpdateSubresource events refresh their snapshots. First use, unmirrorable writes, thread changes,
   // and tracking-capacity misses retain the original CopyResource + Map fallback.
   class ConstantBufferCache
   {
   public:
      // Each source buffer gets its own reusable staging resource for the fallback readback.
      enum class ReadbackSlot : uint8_t
      {
         FrameData,
         TemporalAA,
         Count
      };

      // Event callbacks -------------------------------------------------------

      // Map callbacks happen before the game writes through the returned pointer. Save the pointer
      // here; RecordUnmap copies it only after the game has finished writing.
      void RecordMap(ID3D11Buffer* buffer, void* mapped_data)
      {
         if (mapped_data == nullptr)
         {
            return;
         }

         auto* snapshot = FindTrackedSnapshot(buffer);
         if (snapshot != nullptr && !snapshot->requires_gpu_readback)
         {
            snapshot->pending_map = mapped_data;
         }
      }

      void RecordUnmap(ID3D11Buffer* buffer)
      {
         auto* snapshot = FindTrackedSnapshot(buffer);
         if (snapshot == nullptr || snapshot->requires_gpu_readback || snapshot->pending_map == nullptr)
         {
            return;
         }

         std::memcpy(snapshot->bytes.data(), snapshot->pending_map, snapshot->bytes.size());
         snapshot->pending_map = nullptr;
         snapshot->has_data = true;
      }

      // UpdateSubresource-style uploads provide the new bytes directly, so they can be copied now.
      void RecordUpdate(ID3D11Buffer* buffer, const void* source_data, uint64_t offset, uint64_t size)
      {
         if (source_data == nullptr)
         {
            return;
         }

         auto* snapshot = FindTrackedSnapshot(buffer);
         if (snapshot == nullptr || snapshot->requires_gpu_readback)
         {
            return;
         }

         if (offset >= snapshot->bytes.size())
         {
            snapshot->has_data = false;
            return;
         }

         const uint64_t available_size = snapshot->bytes.size() - offset;
         const uint64_t write_size = size == UINT64_MAX ? available_size : size;
         if (write_size > available_size)
         {
            snapshot->has_data = false;
            return;
         }

         std::memcpy(
            snapshot->bytes.data() + static_cast<size_t>(offset),
            source_data,
            static_cast<size_t>(write_size));
         if (offset == 0u && write_size == snapshot->bytes.size())
         {
            snapshot->has_data = true;
         }
      }

      // A GPU-side copy does not expose its resulting bytes to the CPU. Permanently use the safe
      // readback path for this resource rather than risking an out-of-date snapshot.
      void RequireGpuReadback(ID3D11Buffer* buffer)
      {
         auto* snapshot = FindTrackedSnapshot(buffer);
         if (snapshot == nullptr)
         {
            return;
         }

         snapshot->pending_map = nullptr;
         snapshot->has_data = false;
         snapshot->requires_gpu_readback = true;
      }

      // Destruction may be reported from another thread. Only clear the atomic identity there;
      // the owner thread removes the stale byte snapshot the next time it tracks a new resource.
      void Forget(ID3D11Buffer* buffer)
      {
         if (buffer == nullptr)
         {
            return;
         }

         bool was_tracked = false;
         for (auto& tracked_buffer : tracked_buffers_)
         {
            ID3D11Buffer* expected_buffer = buffer;
            if (tracked_buffer.compare_exchange_strong(
                   expected_buffer,
                   nullptr,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire))
            {
               was_tracked = true;
               break;
            }
         }

         if (was_tracked && IsCaptureThread())
         {
            snapshots_.erase(buffer);
         }
      }

      // Temporal-resolve read ------------------------------------------------

      // Returns the requested bytes from the write-through snapshot when possible. Otherwise it
      // performs the original CopyResource + Map readback and seeds the snapshot for later frames.
      bool Read(
         ID3D11Device* device,
         ID3D11DeviceContext* context,
         ID3D11Buffer* buffer,
         ReadbackSlot readback_slot,
         void* destination_data,
         uint32_t minimum_size)
      {
         if (device == nullptr || context == nullptr || buffer == nullptr || destination_data == nullptr)
         {
            return false;
         }

         if (CopySnapshot(buffer, destination_data, minimum_size))
         {
#if DEVELOPMENT || TEST
            ++cache_hits_;
#endif
            return true;
         }

         D3D11_BUFFER_DESC source_desc = {};
         buffer->GetDesc(&source_desc);
         if (source_desc.ByteWidth < minimum_size)
         {
            return false;
         }

         const bool tracked = Track(buffer, source_desc.ByteWidth);
         auto& readback_buffer = readback_buffers_[static_cast<size_t>(readback_slot)];
         if (!PrepareReadbackBuffer(device, source_desc.ByteWidth, readback_buffer))
         {
            return false;
         }

         context->CopyResource(readback_buffer.get(), buffer);

         D3D11_MAPPED_SUBRESOURCE mapped = {};
         const HRESULT map_result = context->Map(readback_buffer.get(), 0, D3D11_MAP_READ, 0, &mapped);
         if (FAILED(map_result) || mapped.pData == nullptr)
         {
            if (SUCCEEDED(map_result))
            {
               context->Unmap(readback_buffer.get(), 0);
            }
            return false;
         }

         std::memcpy(destination_data, mapped.pData, minimum_size);
         if (tracked)
         {
            StoreSnapshot(buffer, mapped.pData, source_desc.ByteWidth);
         }
         context->Unmap(readback_buffer.get(), 0);

#if DEVELOPMENT || TEST
         ++gpu_readbacks_;
#endif
         return true;
      }

      // Cache lifecycle -------------------------------------------------------

      // Enable event processing only while SR consumes these values. Deactivation discards the
      // snapshots so re-enabling SR cannot accidentally reuse values from an earlier frame.
      void Activate()
      {
         capture_active_.store(true, std::memory_order_release);
      }

      void Deactivate()
      {
         if (!capture_active_.exchange(false, std::memory_order_acq_rel))
         {
            return;
         }

         // Stop every callback from finding these resources immediately. This is safe from any
         // thread and prevents old snapshots from being reused if SR is enabled again later.
         for (auto& tracked_buffer : tracked_buffers_)
         {
            tracked_buffer.store(nullptr, std::memory_order_release);
         }

         // snapshots_ belongs to the render thread. If another thread disabled capture, leave the
         // now-unreachable entries for RemoveUntrackedSnapshots() to discard on the next safe read.
         const DWORD cached_thread_id = capture_thread_id_.load(std::memory_order_acquire);
         if (capture_thread_changed_.load(std::memory_order_acquire) || (cached_thread_id != 0u && cached_thread_id != GetCurrentThreadId()))
         {
            return;
         }

         snapshots_.clear();
      }

      // The real temporal-resolve draw establishes the one thread allowed to touch snapshots_.
      // Other threads are rejected before they look up device or cache data, so no mutex is needed.
      bool BindToCurrentThread()
      {
         if (capture_thread_changed_.load(std::memory_order_acquire))
         {
            return false;
         }

         const DWORD current_thread_id = GetCurrentThreadId();
         DWORD expected_thread_id = 0u;
         if (capture_thread_id_.compare_exchange_strong(
                expected_thread_id,
                current_thread_id,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
         {
            return true;
         }

         if (expected_thread_id == current_thread_id)
         {
            return true;
         }

         // If the game's render thread ever changes, disable the optimization instead of risking
         // concurrent access or stale values. Read() will continue through GPU readback.
         capture_thread_changed_.store(true, std::memory_order_release);
         capture_active_.store(false, std::memory_order_release);
         ASSERT_ONCE(expected_thread_id == current_thread_id);
         return false;
      }

      void ReleaseResources()
      {
         Deactivate();
         for (auto& readback_buffer : readback_buffers_)
         {
            readback_buffer = nullptr;
         }
      }

      // Cheap callback filters ------------------------------------------------

      static bool IsWriteAccess(reshade::api::map_access access)
      {
         return access == reshade::api::map_access::write_only || access == reshade::api::map_access::read_write || access == reshade::api::map_access::write_discard;
      }

      static bool IsCaptureThread()
      {
         if (!capture_active_.load(std::memory_order_acquire) || capture_thread_changed_.load(std::memory_order_acquire))
         {
            return false;
         }

         const DWORD cached_thread_id = capture_thread_id_.load(std::memory_order_acquire);
         return cached_thread_id != 0u && cached_thread_id == GetCurrentThreadId();
      }

      static void ResetCaptureThread()
      {
         capture_active_.store(false, std::memory_order_release);
         capture_thread_changed_.store(false, std::memory_order_release);
         capture_thread_id_.store(0u, std::memory_order_release);
      }

#if DEVELOPMENT || TEST
      uint64_t CacheHitCount() const
      {
         return cache_hits_;
      }

      uint64_t GpuReadbackCount() const
      {
         return gpu_readbacks_;
      }
#endif

   private:
      static constexpr size_t tracked_buffer_capacity = 16u;

      struct Snapshot
      {
         std::vector<uint8_t> bytes;
         void* pending_map = nullptr;
         bool has_data = false;
         bool requires_gpu_readback = false;
      };

      Snapshot* FindTrackedSnapshot(ID3D11Buffer* buffer)
      {
         if (!IsTracked(buffer))
         {
            return nullptr;
         }

         const auto snapshot_it = snapshots_.find(buffer);
         return snapshot_it != snapshots_.end() ? &snapshot_it->second : nullptr;
      }

      bool IsTracked(ID3D11Buffer* buffer) const
      {
         if (buffer == nullptr)
         {
            return false;
         }

         for (const auto& tracked_buffer : tracked_buffers_)
         {
            if (tracked_buffer.load(std::memory_order_acquire) == buffer)
            {
               return true;
            }
         }
         return false;
      }

      // The fixed atomic list is only a fast callback filter. If all slots are occupied, Read()
      // simply keeps using the correct but slower GPU path for additional resources.
      bool Track(ID3D11Buffer* buffer, uint32_t byte_width)
      {
         if (buffer == nullptr || byte_width == 0u || !IsCaptureThread())
         {
            return false;
         }

         if (IsTracked(buffer))
         {
            auto [snapshot_it, inserted] = snapshots_.try_emplace(buffer);
            auto& snapshot = snapshot_it->second;
            if (inserted || snapshot.bytes.size() != byte_width)
            {
               snapshot = {};
               snapshot.bytes.resize(byte_width);
            }
            return true;
         }

         auto free_slot_it = std::find_if(
            tracked_buffers_.begin(),
            tracked_buffers_.end(),
            [](const auto& tracked_buffer)
            { return tracked_buffer.load(std::memory_order_acquire) == nullptr; });
         if (free_slot_it == tracked_buffers_.end())
         {
            return false;
         }

         RemoveUntrackedSnapshots();
         auto& snapshot = snapshots_[buffer];
         snapshot = {};
         snapshot.bytes.resize(byte_width);

         // Publish the resource identity only after its snapshot is fully constructed.
         free_slot_it->store(buffer, std::memory_order_release);
         return true;
      }

      void RemoveUntrackedSnapshots()
      {
         for (auto snapshot_it = snapshots_.begin(); snapshot_it != snapshots_.end();)
         {
            if (!IsTracked(snapshot_it->first))
            {
               snapshot_it = snapshots_.erase(snapshot_it);
            }
            else
            {
               ++snapshot_it;
            }
         }
      }

      bool CopySnapshot(ID3D11Buffer* buffer, void* destination_data, uint32_t minimum_size)
      {
         if (buffer == nullptr || destination_data == nullptr || !IsCaptureThread())
         {
            return false;
         }

         auto* snapshot = FindTrackedSnapshot(buffer);
         if (snapshot == nullptr || snapshot->requires_gpu_readback || !snapshot->has_data || snapshot->bytes.size() < minimum_size)
         {
            return false;
         }

         std::memcpy(destination_data, snapshot->bytes.data(), minimum_size);
         return true;
      }

      void StoreSnapshot(ID3D11Buffer* buffer, const void* source_data, uint32_t size)
      {
         if (buffer == nullptr || source_data == nullptr || !IsCaptureThread())
         {
            return;
         }

         auto* snapshot = FindTrackedSnapshot(buffer);
         if (snapshot == nullptr || snapshot->bytes.size() != size)
         {
            return;
         }

         if (snapshot->requires_gpu_readback)
         {
            return;
         }

         std::memcpy(snapshot->bytes.data(), source_data, size);
         snapshot->has_data = true;
      }

      static bool PrepareReadbackBuffer(ID3D11Device* device, uint32_t byte_width, com_ptr<ID3D11Buffer>& readback_buffer)
      {
         bool needs_recreate = readback_buffer.get() == nullptr;
         if (!needs_recreate)
         {
            D3D11_BUFFER_DESC readback_desc = {};
            readback_buffer->GetDesc(&readback_desc);
            needs_recreate = readback_desc.ByteWidth != byte_width;
         }

         if (!needs_recreate)
         {
            return true;
         }

         D3D11_BUFFER_DESC readback_desc = {};
         readback_desc.ByteWidth = byte_width;
         readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         readback_desc.Usage = D3D11_USAGE_STAGING;

         readback_buffer = nullptr;
         const HRESULT create_result = device->CreateBuffer(&readback_desc, nullptr, &readback_buffer);
         return SUCCEEDED(create_result) && readback_buffer.get() != nullptr;
      }

      inline static std::atomic<DWORD> capture_thread_id_ = 0u;
      inline static std::atomic_bool capture_thread_changed_ = false;
      inline static std::atomic_bool capture_active_ = false;

      std::unordered_map<ID3D11Buffer*, Snapshot> snapshots_;
      std::array<std::atomic<ID3D11Buffer*>, tracked_buffer_capacity> tracked_buffers_ = {};
      std::array<com_ptr<ID3D11Buffer>, static_cast<size_t>(ReadbackSlot::Count)> readback_buffers_;

#if DEVELOPMENT || TEST
      uint64_t cache_hits_ = 0u;
      uint64_t gpu_readbacks_ = 0u;
#endif
   };
#endif
} // namespace QuantumBreakUpscaling
