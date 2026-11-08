#pragma once

#include <d3d11_1.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cfloat>
#include <cstring>
#include <format>
#include <memory>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <include/reshade.hpp>
#include <source/com_ptr.hpp>

// The game's draws redone with patched shaders (the motion vector ones, "motion_vector_patch.h"): swapping the shaders in and out, and
// uploading the extra constants they read.
namespace PatchedDraws
{
   // A patched shader left bound after its draw, with the game's shader it replaced (see "BindPatchedShader")
   template <typename T>
   struct BoundShader
   {
      T* patched = nullptr;
      com_ptr<T> game;
   };

   template <typename T>
   com_ptr<T> GetBoundShader(ID3D11DeviceContext* native_device_context)
   {
      com_ptr<T> shader;
      if constexpr (std::is_same_v<T, ID3D11VertexShader>)
         native_device_context->VSGetShader(&shader, nullptr, nullptr);
      else
         native_device_context->PSGetShader(&shader, nullptr, nullptr);
      return shader;
   }

   template <typename T>
   void SetBoundShader(ID3D11DeviceContext* native_device_context, T* shader)
   {
      if constexpr (std::is_same_v<T, ID3D11VertexShader>)
         native_device_context->VSSetShader(shader, nullptr, 0);
      else
         native_device_context->PSSetShader(shader, nullptr, 0);
   }

   // Binds a patched shader and leaves it bound after the draw (set directly, bypassing Core's state tracking). If the game hasn't
   // bound another since, the next draw has the same original shader: it binds it again (a no-op) or puts the game's back first.
   template <typename T>
   void BindPatchedShader(ID3D11DeviceContext* native_device_context, T* patched, BoundShader<T>* bound)
   {
      com_ptr<T> current = GetBoundShader<T>(native_device_context);
      if (current.get() == patched)
         return;
      bound->game = std::move(current);
      bound->patched = patched;
      SetBoundShader(native_device_context, patched);
   }

   // Puts the game's shader back where a patched one is still bound, before a draw that must not use it
   template <typename T>
   void RestoreGameShader(ID3D11DeviceContext* native_device_context, BoundShader<T>* bound)
   {
      if (!bound->patched)
         return;
      if (GetBoundShader<T>(native_device_context).get() == bound->patched)
         SetBoundShader(native_device_context, bound->game.get());
      bound->patched = nullptr;
      bound->game.reset();
   }

   // A dynamic constant buffer holding "data", (re)created at its size; "copy_size" (non zero): only that much of it is written, the
   // buffer only grows (uploads of varying size to one slot)
   inline bool WriteDynamicConstants(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, com_ptr<ID3D11Buffer>* buffer, const void* data, UINT size, UINT copy_size = 0)
   {
      D3D11_BUFFER_DESC desc = {};
      if (*buffer)
         (*buffer)->GetDesc(&desc);
      // A trimmed upload ("copy_size") reuses any buffer big enough
      if (desc.ByteWidth < size || (copy_size == 0 && desc.ByteWidth != size))
      {
         buffer->reset();
         desc = {size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
         native_device->CreateBuffer(&desc, nullptr, &(*buffer));
      }
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (!*buffer || FAILED(native_device_context->Map(buffer->get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
         return false;
      std::memcpy(mapped.pData, data, copy_size == 0 ? size : copy_size);
      native_device_context->Unmap(buffer->get(), 0);
      return true;
   }

   // The previous frame's per-draw constants a patched draw reads: in a ring when the device supports constant buffer offsets (one map
   // for all of a draw's buffers, no renaming), else in a dynamic buffer per slot, renamed at every upload
   struct PreviousConstants
   {
      static constexpr UINT ring_size = 4 << 20;
      com_ptr<ID3D11Buffer> ring;
      // The immediate context (not referenced: it lives as long as the device). Only its binds use the ring: a deferred one would get
      // its constant buffers bound on this one, and must DISCARD its first Map of a buffer in each command list.
      ID3D11DeviceContext1* ring_context = nullptr;
      UINT ring_offset = 0;
      bool checked = false;
      std::vector<com_ptr<ID3D11Buffer>> buffers; // Without the ring, one per slot

      // Binds each "slots" pair's previous slot: its upload, or where there is none (null), its current buffer (zero motion).
      // "log_tag" names the game in the log line saying which of the two ways it uses. "read_sizes" (optional, 0 = all): the bytes of
      // each upload the shader reads ("DXBC::ConstantBufferBytes"), the ring gets only those.
      void Bind(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, std::span<const std::pair<uint32_t, uint32_t>> slots,
         std::span<const std::vector<uint8_t>* const> uploads, std::span<ID3D11Buffer* const> current, const char* log_tag, std::span<const UINT> read_sizes = {})
      {
         if (native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && !std::exchange(checked, true))
         {
            D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
            com_ptr<ID3D11DeviceContext1> context1;
            const D3D11_BUFFER_DESC desc = {ring_size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
            if (SUCCEEDED(native_device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) && options.ConstantBufferOffsetting &&
                options.MapNoOverwriteOnDynamicConstantBuffer && SUCCEEDED(native_device_context->QueryInterface(&context1)) &&
                SUCCEEDED(native_device->CreateBuffer(&desc, nullptr, &ring)))
            {
               ring_context = context1.get();
               ring_offset = ring_size; // The first write discards
            }
            reshade::log::message(reshade::log::level::info,
               std::format("[{} MV] previous constants {}", log_tag, ring ? "in a ring (constant buffer offsets)" : "in renamed buffers (no constant buffer offsets)").c_str());
         }

         constexpr size_t max_slots = 8;
         const size_t count = (std::min)(slots.size(), max_slots);
         assert(slots.size() <= max_slots && uploads.size() >= count && current.size() >= count);
         ID3D11Buffer* buffers_to_bind[max_slots];
         UINT bytes[max_slots] = {}, sizes[max_slots] = {};
         UINT total = 0;
         for (size_t i = 0; i < count; i++)
         {
            buffers_to_bind[i] = current[i];
            if (!uploads[i])
               continue;
            bytes[i] = UINT(uploads[i]->size());
            if (i < read_sizes.size() && read_sizes[i] != 0)
               bytes[i] = (std::min)(bytes[i], read_sizes[i]);
            // "FirstConstant" and "NumConstants" are multiples of 16 constants (256 bytes)
            total += sizes[i] = (bytes[i] + 255u) & ~255u;
         }
         if (ring && native_device_context == ring_context && total != 0 && total <= ring_size)
         {
            const bool restart = ring_offset + total > ring_size;
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(native_device_context->Map(ring.get(), 0, restart ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped)))
            {
               UINT offset = restart ? 0u : ring_offset;
               UINT first_constants[max_slots], constant_counts[max_slots];
               for (size_t i = 0; i < count; i++)
               {
                  if (!uploads[i])
                     continue;
                  std::memcpy(static_cast<uint8_t*>(mapped.pData) + offset, uploads[i]->data(), bytes[i]);
                  first_constants[i] = offset / 16;
                  constant_counts[i] = sizes[i] / 16;
                  offset += sizes[i];
               }
               native_device_context->Unmap(ring.get(), 0);
               ring_offset = offset;
               ID3D11Buffer* const ring_buffer = ring.get();
               for (size_t i = 0; i < count; i++)
               {
                  if (uploads[i])
                     ring_context->VSSetConstantBuffers1(slots[i].second, 1, &ring_buffer, &first_constants[i], &constant_counts[i]);
                  else
                     native_device_context->VSSetConstantBuffers(slots[i].second, 1, &buffers_to_bind[i]);
               }
               return;
            }
         }
         // Uploads of one slot vary in size (a whole copy, a trimmed one): its buffer only grows, and takes only what the shader reads
         buffers.resize(count);
         for (size_t i = 0; i < count; i++)
         {
            if (uploads[i] && bytes[i] != 0 && WriteDynamicConstants(native_device, native_device_context, std::addressof(buffers[i]), uploads[i]->data(), sizes[i], bytes[i]))
            {
               buffers_to_bind[i] = buffers[i].get();
            }
            native_device_context->VSSetConstantBuffers(slots[i].second, 1, &buffers_to_bind[i]);
         }
      }
   };

   // Pointer and "HashCombine" keys: one multiply spreads them over the buckets (std::hash runs FNV-1a on every byte)
   struct KeyHash
   {
      size_t operator()(uint64_t key) const noexcept
      {
         return size_t((key * 0x9E3779B97F4A7C15ull) >> 32);
      }
   };

   // CPU copies of the constant buffers the patched draws read their object and camera from, by buffer: a draw registers its buffer
   // ("Register", empty until its first upload), the buffer hook copies every UpdateSubresource, and a draw's constants are its
   // buffer's latest upload ("Snapshot"). Only "Restart" and "MayBeRegistered" may come from another thread: the game holds its own
   // lock around every other call, taken only for "MayBeRegistered" buffers.
   struct ObjectConstants
   {
      using Copy = std::shared_ptr<const std::vector<uint8_t>>;
      struct Entry
      {
         // The last upload, rewritten in place
         std::vector<uint8_t> latest;
         Copy copy;               // "latest" whole, for the draws since that upload reading all of it (skinned: shared; made by the first)
         UINT size = 0;           // The buffer's
         uint32_t generation = 0; // "generation" at the upload: an older one predates a pause of the hooks
      };
      std::unordered_map<uint64_t, Entry, KeyHash> entries;
      // Raised when the hooks restart ("Restart"): the uploads they missed make every older "latest" stale. A counter rather than
      // emptying the entries, which the frame's thread owns.
      std::atomic<uint32_t> generation = 1;
      // The first registered buffers and their entries (a node's address stays while the map grows), so a buffer hook finds them
      // without a lookup. With more registered, every buffer takes the map lookup. The handles are read lock free
      // ("MayBeRegistered"); an unregistered buffer's slot holds "free_slot" until the next registration takes it.
      static constexpr uint32_t max_filtered = 32;
      static constexpr uint64_t free_slot = 1; // Never a handle
      std::array<std::atomic<uint64_t>, max_filtered> filtered_handles = {};
      std::array<Entry*, max_filtered> filtered_entries = {};
      std::atomic<uint32_t> filtered_count = 0;
      std::atomic<bool> filter_overflow = false;
      // Every pooled copy, and by size those nobody held anymore at the last "TrimPool" (taken by the next copies of that size)
      std::vector<std::shared_ptr<std::vector<uint8_t>>> pool;
      std::unordered_map<size_t, std::vector<uint32_t>> pool_free;
      size_t made = 0; // Copies asked for since the last "TrimPool"

      // False if the buffer surely isn't registered. Lock free, from any thread: the buffer hooks skip the game's other buffers
      // before taking the lock.
      bool MayBeRegistered(uint64_t handle) const
      {
         if (filter_overflow.load(std::memory_order_acquire))
            return true;
         const uint32_t count = filtered_count.load(std::memory_order_acquire);
         for (uint32_t i = 0; i < count; i++)
         {
            if (filtered_handles[i].load(std::memory_order_acquire) == handle)
               return true;
         }
         return false;
      }

      // The registered buffer's entry, null if the buffer isn't one
      Entry* Find(uint64_t handle)
      {
         if (!filter_overflow.load(std::memory_order_relaxed))
         {
            const uint32_t count = filtered_count.load(std::memory_order_relaxed);
            for (uint32_t i = 0; i < count; i++)
            {
               if (filtered_handles[i].load(std::memory_order_relaxed) == handle)
                  return filtered_entries[i];
            }
            return nullptr;
         }
         const auto it = entries.find(handle);
         return (it != entries.end() ? &it->second : nullptr);
      }

      // The buffer's entry, registered (and filtered while there is room) on its first draw
      Entry& Register(ID3D11Buffer* buffer)
      {
         const uint64_t handle = reinterpret_cast<uint64_t>(buffer);
         Entry* entry = Find(handle);
         if (entry)
            return *entry;
         entry = &entries[handle];
         D3D11_BUFFER_DESC desc;
         buffer->GetDesc(&desc);
         entry->size = desc.ByteWidth;
         // An unregistered buffer's slot, else the next one (the entry before the handle, which "Find" reads first)
         const uint32_t count = filtered_count.load(std::memory_order_relaxed);
         uint32_t slot = 0;
         while (slot < count && filtered_handles[slot].load(std::memory_order_relaxed) != free_slot)
            slot++;
         if (slot < max_filtered)
         {
            filtered_entries[slot] = entry;
            filtered_handles[slot].store(handle, std::memory_order_release);
            if (slot == count)
               filtered_count.store(count + 1, std::memory_order_release);
         }
         else
         {
            filter_overflow.store(true, std::memory_order_release);
         }
         return *entry;
      }

      // A destroyed buffer leaves the entries and the filter (its address can come back as another buffer, of another size); the
      // copies the draws took stay theirs. True if it was registered.
      bool Unregister(uint64_t handle)
      {
         if (entries.erase(handle) == 0)
            return false;
         const uint32_t count = filtered_count.load(std::memory_order_relaxed);
         for (uint32_t i = 0; i < count; i++)
         {
            if (filtered_handles[i].load(std::memory_order_relaxed) == handle)
            {
               filtered_handles[i].store(free_slot, std::memory_order_release);
               break;
            }
         }
         return true;
      }

      // A copy of "size" bytes from "bytes": one nobody held anymore at the last "TrimPool", else a new one
      Copy NewCopy(const uint8_t* bytes, size_t size)
      {
         made++;
         if (const auto free_copies = pool_free.find(size); free_copies != pool_free.end() && !free_copies->second.empty())
         {
            auto copy = pool[free_copies->second.back()];
            free_copies->second.pop_back();
            std::memcpy(copy->data(), bytes, size);
            return copy;
         }
         auto copy = std::make_shared<std::vector<uint8_t>>(bytes, bytes + size);
         pool.push_back(copy);
         return copy;
      }

      // What a draw keeps of its buffer's latest upload: the entry's one copy for the draws since that upload. Null without an upload
      // since the last "Restart".
      Copy Snapshot(Entry& entry)
      {
         if (entry.generation != generation)
            return nullptr;
         if (!entry.copy)
            entry.copy = NewCopy(entry.latest.data(), entry.latest.size());
         return entry.copy;
      }

      // The "update_buffer_region" event (before the UpdateSubresource runs; the signed ReShade doesn't raise it): a partial update is
      // merged into the last upload, else into zeros. True if copied.
      bool OnUpdate(uint64_t handle, const void* data, uint64_t offset, uint64_t size)
      {
         Entry* const entry = Find(handle);
         if (!entry || offset >= entry->size)
            return false;
         const uint32_t current = generation;
         if (entry->latest.size() != entry->size || entry->generation != current)
            entry->latest.assign(entry->size, 0);
         std::memcpy(entry->latest.data() + offset, data, size_t((std::min)(size, uint64_t(entry->size) - offset)));
         entry->copy.reset();
         entry->generation = current;
         return true;
      }

      // The hooks restart after a pause (motion vectors off): every upload so far is stale. Any thread.
      void Restart()
      {
         generation++;
      }

      // At present: the pooled copies only the pool holds (superseded, no object or camera keeps them) are free for the next ones, as
      // many as were asked for since the last call ("active", else none): frames without a scene (loading, videos, menus) would keep
      // their peak otherwise
      void TrimPool(bool active)
      {
         size_t kept_free = (active ? std::exchange(made, 0) : 0);
         std::erase_if(pool, [&](const auto& copy)
            {
               if (copy.use_count() != 1)
                  return false;
               if (kept_free == 0)
                  return true;
               kept_free--;
               return false; });
         for (auto& [size, free_copies] : pool_free)
         {
            free_copies.clear();
         }
         for (uint32_t i = 0; i < uint32_t(pool.size()); i++)
         {
            if (pool[i].use_count() == 1)
               pool_free[pool[i]->size()].push_back(i);
         }
      }

      // Frees the uploads (the buffers stay registered and filtered, their draws uncopied until the next upload). Only from the thread
      // of the buffer hooks.
      void ReleaseUploads()
      {
         for (auto& [buffer, entry] : entries)
         {
            std::vector<uint8_t>().swap(entry.latest);
            entry.copy.reset();
         }
      }
   };

   // The immediate context's blend and depth stencil states, vertex buffer 0 and the index buffer as the game last bound them (the bind hooks
   // below), so the draw hooks don't query them (an AddRef and a Release each, several per draw). Not referenced: an object lives while bound. Luma's own passes bind theirs natively and put the game's
   // back. "tracked" is false after a bind this can't read (another add-on's combined pipeline): queried then.
   struct BoundStates
   {
      ID3D11BlendState* blend_state = nullptr;
      ID3D11DepthStencilState* depth_stencil_state = nullptr;
      ID3D11Buffer* vertex_buffer = nullptr;
      UINT vertex_offset = 0;
      ID3D11Buffer* index_buffer = nullptr;
      UINT index_offset = 0;
      bool tracked = true;
      // "DepthTested"'s last state and its result (see "ResetDepthTest")
      ID3D11DepthStencilState* depth_test_state = nullptr;
      bool depth_test = true;

      // A bound object: "object" (its tracked binding), else what "query" writes to its argument (an object pointer to take over)
      template <typename T, typename Query>
      T* Get(T* object, const Query& query)
      {
         if (tracked)
            return object;
         com_ptr<T> queried;
         query(&queried);
         return queried.get();
      }

      ID3D11BlendState* BlendState(ID3D11DeviceContext* native_device_context)
      {
         return Get(blend_state, [&](ID3D11BlendState** state)
            { native_device_context->OMGetBlendState(state, nullptr, nullptr); });
      }
      ID3D11DepthStencilState* DepthStencilState(ID3D11DeviceContext* native_device_context)
      {
         return Get(depth_stencil_state, [&](ID3D11DepthStencilState** state)
            { native_device_context->OMGetDepthStencilState(state, nullptr); });
      }
      // The buffers with their offsets (optional): queried with them, else tracked with them
      ID3D11Buffer* VertexBuffer(ID3D11DeviceContext* native_device_context, UINT* offset = nullptr)
      {
         UINT queried_offset = vertex_offset;
         ID3D11Buffer* const buffer = Get(vertex_buffer, [&](ID3D11Buffer** queried)
            {
               UINT stride;
               native_device_context->IAGetVertexBuffers(0, 1, queried, &stride, &queried_offset); });
         if (offset)
            *offset = queried_offset;
         return buffer;
      }
      ID3D11Buffer* IndexBuffer(ID3D11DeviceContext* native_device_context, UINT* offset = nullptr)
      {
         UINT queried_offset = index_offset;
         ID3D11Buffer* const buffer = Get(index_buffer, [&](ID3D11Buffer** queried)
            {
               DXGI_FORMAT format;
               native_device_context->IAGetIndexBuffer(queried, &format, &queried_offset); });
         if (offset)
            *offset = queried_offset;
         return buffer;
      }

      // The bound depth stencil state tests depth (meshes do; full screen passes and composites don't), cached by state
      bool DepthTested(ID3D11DeviceContext* native_device_context)
      {
         ID3D11DepthStencilState* const state = DepthStencilState(native_device_context);
         if (state != depth_test_state)
         {
            D3D11_DEPTH_STENCIL_DESC depth_desc = CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);
            if (state)
               state->GetDesc(&depth_desc);
            depth_test = depth_desc.DepthEnable;
            depth_test_state = state;
         }
         return depth_test;
      }
      // Forgets "DepthTested"'s cached state (when the scene opens: a released state's address comes back for another)
      void ResetDepthTest()
      {
         depth_test_state = nullptr;
         depth_test = true;
      }
   };

   // "BoundStates"' ReShade events. "get" gives a command list's states, null unless it's the immediate context's ("is_primary").
   // "bind_pipeline": ReShade raises one per state, and a reset binds none to every stage.
   template <auto get>
   void OnBindPipeline(reshade::api::command_list* cmd_list, reshade::api::pipeline_stage stages, reshade::api::pipeline pipeline)
   {
      using reshade::api::pipeline_stage;
      if ((stages & (pipeline_stage::output_merger | pipeline_stage::depth_stencil)) == 0)
         return;
      auto* const states = get(cmd_list);
      if (!states)
         return;
      if (stages == pipeline_stage::output_merger)
      {
         states->blend_state = reinterpret_cast<ID3D11BlendState*>(pipeline.handle);
      }
      else if (stages == pipeline_stage::depth_stencil)
      {
         states->depth_stencil_state = reinterpret_cast<ID3D11DepthStencilState*>(pipeline.handle);
      }
      else if (pipeline.handle == 0)
      {
         states->blend_state = nullptr;
         states->depth_stencil_state = nullptr;
      }
      else
      {
         states->tracked = false;
      }
   }

   // "bind_vertex_buffers": vertex buffer 0's binding
   template <auto get>
   void OnBindVertexBuffers(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::resource* buffers, const uint64_t* offsets, const uint32_t* strides)
   {
      if (first != 0 || count == 0)
         return;
      if (auto* const states = get(cmd_list))
      {
         states->vertex_buffer = reinterpret_cast<ID3D11Buffer*>(buffers[0].handle);
         states->vertex_offset = UINT(offsets[0]);
      }
   }

   // "bind_index_buffer"
   template <auto get>
   void OnBindIndexBuffer(reshade::api::command_list* cmd_list, reshade::api::resource buffer, uint64_t offset, uint32_t index_size)
   {
      if (auto* const states = get(cmd_list))
      {
         states->index_buffer = reinterpret_cast<ID3D11Buffer*>(buffer.handle);
         states->index_offset = UINT(offset);
      }
   }

   // An object's world transform, the tie-break between the motion vector draws sharing a draw key (props): its axes and translation
   // (3x4). The translation alone ties for modular pieces that share a pivot and differ only by rotation (arches).
   using ObjectTransform = std::array<float, 12>;

   // A row vector matrix's (UE3's LocalToWorld) first three rows' xyz and its translation row's, from its first row
   inline ObjectTransform ReadRowVectorTransform(const uint8_t* first_row)
   {
      ObjectTransform transform;
      for (size_t row = 0; row < 4; row++)
      {
         std::memcpy(transform.data() + row * 3, first_row + row * 16, 3 * sizeof(float));
      }
      return transform;
   }

   // Squared distance between two objects' transforms: the nearest is the same object a frame earlier
   inline float TransformDistance(const ObjectTransform& a, const ObjectTransform& b)
   {
      float distance = 0.f;
      for (size_t i = 0; i < a.size(); i++)
      {
         distance += (a[i] - b[i]) * (a[i] - b[i]);
      }
      return distance;
   }

   // A draw key and an exact transform: equal transforms (operator==, so -0 as 0) hash equally. 32 bit FNV-1a over the transform (a
   // 64 bit multiply is a call on x86), mixed into the key's hash.
   inline uint64_t TransformHash(uint64_t key, const ObjectTransform& transform)
   {
      uint32_t hash = 2166136261u;
      for (const float value : transform)
      {
         const float normalized = (value == 0.f ? 0.f : value);
         uint32_t bits;
         std::memcpy(&bits, &normalized, sizeof(bits));
         hash = (hash ^ bits) * 16777619u;
      }
      return key ^ ((uint64_t(hash) << 32) | hash);
   }

   // A frame's motion vector objects by draw key and exact transform, for "FindNearest": the same object a frame later is nearly
   // always at distance 0, found here without scanning its key's candidates. That scan is quadratic in the copies of one mesh: dense
   // foliage with a long view distance (thousands of one tree) fell to 4 fps. Filled as the frame draws ("Add", the object's index
   // in its key's list), sorted once it's last frame's ("Seal", deferred to the first "Find": a frame whose draws all match by their
   // draw order never sorts). Reused: no allocation once grown.
   struct ExactTransformIndex
   {
      // Sorted on demand by the const "Find" (one thread: the draws')
      mutable std::vector<std::pair<uint64_t, uint32_t>> entries; // "TransformHash", index
      mutable bool sorted = true;

      void Add(uint64_t key, const ObjectTransform& transform, size_t index)
      {
         entries.emplace_back(TransformHash(key, transform), uint32_t(index));
         sorted = false;
      }
      // Sorted by hash, then index: a hash's first entry is its first drawn object, the one "FindNearest"'s scan picks at distance 0
      void Seal()
      {
         sorted = false;
      }
      // The first object drawn with this key and hash (another transform on a hash collision), UINT32_MAX if none
      uint32_t Find(uint64_t key, const ObjectTransform& transform) const
      {
         if (!sorted)
         {
            std::sort(entries.begin(), entries.end());
            sorted = true;
         }
         const uint64_t hash = TransformHash(key, transform);
         const auto entry = std::lower_bound(entries.begin(), entries.end(), std::pair<uint64_t, uint32_t>(hash, 0));
         return (entry != entries.end() && entry->first == hash) ? entry->second : UINT32_MAX;
      }
   };

   // The candidate (an object of last frame with the same draw key) nearest to "transform" among those "compatible" accepts (same
   // constants layout); null if none. At distance 0 the same object, else the nearest guess.
   template <typename Object, typename Compatible>
   const Object* FindNearest(const std::vector<Object>& candidates, const ObjectTransform& transform, Compatible compatible)
   {
      const Object* match = nullptr;
      float nearest = FLT_MAX;
      for (const Object& candidate : candidates)
      {
         if (!compatible(candidate))
            continue;
         const float distance = TransformDistance(candidate.transform, transform);
         if (distance < nearest)
         {
            nearest = distance;
            match = &candidate;
            // Nothing is nearer, and a later tie wouldn't replace it
            if (distance == 0.f)
               break;
         }
      }
      return match;
   }

   // The same, with last frame's "exact" index of the candidates (sealed): the scan only runs for objects not drawn last frame with
   // this transform (moved, skinned, streamed in)
   template <typename Object, typename Compatible>
   const Object* FindNearest(const std::vector<Object>& candidates, const ExactTransformIndex& exact, uint64_t key, const ObjectTransform& transform, Compatible compatible)
   {
      if (const uint32_t index = exact.Find(key, transform); index < candidates.size())
      {
         const Object& candidate = candidates[index];
         if (candidate.transform == transform && compatible(candidate))
            return &candidate;
      }
      return FindNearest(candidates, transform, compatible);
   }

   // The same, trying first the candidates drawn at this draw's place in its key's order ("order": the key's draws before it this
   // frame) and its neighbors (an object streamed in or out shifts the rest by one): the game draws a key's copies in the same order
   // every frame, so the index (its sort) is only needed for a long list. Among copies with identical transforms (an ambiguous
   // tie-break) this picks the one at the draw's place, not the first.
   template <typename Object, typename Compatible>
   const Object* FindNearest(const std::vector<Object>& candidates, size_t order, const ExactTransformIndex& exact, uint64_t key, const ObjectTransform& transform, Compatible compatible)
   {
      for (const size_t place : {order, order + 1, order - 1}) // order - 1 wraps past the end at 0
      {
         if (place >= candidates.size())
            continue;
         const Object& candidate = candidates[place];
         if (candidate.transform == transform && compatible(candidate))
            return &candidate;
      }
      // Short lists scan, as fast as the index without its sort
      constexpr size_t SCAN_LIMIT = 32;
      if (candidates.size() <= SCAN_LIMIT)
         return FindNearest(candidates, transform, compatible);
      return FindNearest(candidates, exact, key, transform, compatible);
   }

   // The motion vector objects (each with a "transform") by draw key, this frame's and last frame's, with their exact transform
   // indices kept in step: the lists and the indices only change together here, so no draw falls back to the quadratic scan.
   template <typename Object, typename Hash = std::hash<uint64_t>>
   class FrameObjects
   {
   public:
      using Map = std::unordered_map<uint64_t, std::vector<Object>, Hash>;

      // Kept as drawn for the next frame. "indexed" false: never looked up by its transform (a draw without one).
      Object& Push(uint64_t key, Object&& object, bool indexed = true)
      {
         auto& objects = current[key];
         if (indexed)
         {
            current_exact.Add(key, object.transform, objects.size());
         }
         return objects.emplace_back(std::move(object));
      }

      // At frame start: this frame's objects become the previous ones, gone if "previous_valid" is false (frames without a scene came
      // between). Swapped, not rebuilt: the lists keep their nodes and capacity (an empty list matches nothing); keys drawn in neither
      // of the last two frames go.
      void EndFrame(bool previous_valid)
      {
         previous.swap(current);
         previous_exact.entries.swap(current_exact.entries);
         current_exact.entries.clear();
         std::erase_if(current, [](const auto& entry)
            { return entry.second.empty(); });
         for (auto& entry : current)
         {
            entry.second.clear();
         }
         if (!previous_valid)
         {
            previous.clear();
            previous_exact.entries.clear();
         }
         previous_exact.Seal();
      }

      // Frees everything (device teardown, motion vectors off)
      void Reset()
      {
         current.clear();
         previous.clear();
         current_exact = {};
         previous_exact = {};
      }

      // Last frame's object with this key nearest to "transform", see "FindNearest" (by draw order: called before this draw's "Push").
      // It outlives the draw: the previous objects only change at the next "EndFrame".
      template <typename Compatible>
      const Object* FindNearest(uint64_t key, const ObjectTransform& transform, Compatible compatible) const
      {
         const auto candidates = previous.find(key);
         if (candidates == previous.end())
            return nullptr;
         const auto drawn = current.find(key);
         const size_t order = (drawn != current.end() ? drawn->second.size() : 0);
         return PatchedDraws::FindNearest(candidates->second, order, previous_exact, key, transform, compatible);
      }

   private:
      Map current;
      Map previous;
      ExactTransformIndex current_exact;
      ExactTransformIndex previous_exact; // Sealed
   };

   // Constant copies (shared, null until a buffer's first upload) holding the same bytes
   inline bool SameBytes(const std::shared_ptr<const std::vector<uint8_t>>& a, const std::shared_ptr<const std::vector<uint8_t>>& b)
   {
      return a == b || (a && b && *a == *b);
   }

   // A "create_pipeline" callback giving every blend state Luma's extra targets, so no draw needs its own copy of the game's state
   // (ReShade turns independent blending on only when a target now differs): the motion vector target ("target_slot", bound only by
   // the motion vector draws) unblended and, at "reactive_slot", FSR's masks (x reactive, y transparency & composition) max
   // blended: the strongest alpha blended draw per pixel (max never exceeds what one wrote).
   template <uint32_t target_slot, uint32_t reactive_slot>
   bool OnCreateBlendState(reshade::api::device* device, reshade::api::pipeline_layout layout, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects)
   {
      for (uint32_t i = 0; i < subobject_count; i++)
      {
         if (subobjects[i].type != reshade::api::pipeline_subobject_type::blend_state)
            continue;
         auto& desc = *static_cast<reshade::api::blend_desc*>(subobjects[i].data);
         desc.blend_enable[target_slot] = false;
         desc.render_target_write_mask[target_slot] = 0xF;
         desc.blend_enable[reactive_slot] = true;
         desc.source_color_blend_factor[reactive_slot] = desc.dest_color_blend_factor[reactive_slot] = reshade::api::blend_factor::one;
         desc.source_alpha_blend_factor[reactive_slot] = desc.dest_alpha_blend_factor[reactive_slot] = reshade::api::blend_factor::one;
         desc.color_blend_op[reactive_slot] = desc.alpha_blend_op[reactive_slot] = reshade::api::blend_op::max;
         desc.render_target_write_mask[reactive_slot] = 0x3;
         return true;
      }
      return false;
   }
} // namespace PatchedDraws
