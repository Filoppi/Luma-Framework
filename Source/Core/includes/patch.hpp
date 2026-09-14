#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hash.h"

// shaders.h (and shader_define.h through it) relies on reshade.hpp having been
// included first (core.hpp does this via the DLSS/FSR headers).
#include <include/reshade.hpp>

#include "shaders.h"

// Container utilities (DXBC parsing + patched container building) and pipeline
// cloning helpers.
#include "utils/shader_compiler.hpp"
#include "utils/pipeline.hpp"

// ============================================================================
// Shader patching module configuration.
// Provider code paths, selected per game (plain 0/1) BEFORE including core.hpp:
//   LUMA_PATCH_BYTECODE_SYNC / _ASYNC  - manual bytecode manipulation
//   LUMA_PATCH_RECIPE_SYNC / _ASYNC    - DXP recipe patching
//   LUMA_PATCH_SYNC_MODE_CLONE         - 0: sync providers patch in-place at
//                                        shader creation; 1: pipeline clone
//                                        swapped in at bind time
//
// Runtime toggling (original <-> patched per draw, UseShaderVariant) needs a
// clone-based mode (CLONE=1 or an async provider); INPLACE patches are always-on.
//
// Library availability is separate (LUMA_USE_DXP, from the Luma props when
// UseLumaDXP=true; recipe provider flags imply it as well). It never enables
// provider code by itself.
// ============================================================================

#ifndef LUMA_PATCH_BYTECODE_SYNC
#define LUMA_PATCH_BYTECODE_SYNC 0
#endif
#ifndef LUMA_PATCH_BYTECODE_ASYNC
#define LUMA_PATCH_BYTECODE_ASYNC 0
#endif
#ifndef LUMA_PATCH_RECIPE_SYNC
#define LUMA_PATCH_RECIPE_SYNC 0
#endif
#ifndef LUMA_PATCH_RECIPE_ASYNC
#define LUMA_PATCH_RECIPE_ASYNC 0
#endif
#ifndef LUMA_PATCH_SYNC_MODE_CLONE
#define LUMA_PATCH_SYNC_MODE_CLONE 0
#endif

// Selects where async patch-clone pipelines get created (only matters with an
// async provider; sync providers are unaffected):
//   1 = On Present - the worker builds patched subobjects (CPU only); the
//                    render thread creates the pipelines at the present
//                    boundary. This might be prefered since NVIDIA drivers
//                    might lock when calling Create*Shader functions from
//                    different threads.
//   2 = On Worker  - the worker builds subobjects AND calls create_pipeline
//                    itself; present only registers finished clones.
#ifndef LUMA_ASYNC_CLONE_MODE
#define LUMA_ASYNC_CLONE_MODE 2
#endif
#if LUMA_ASYNC_CLONE_MODE != 1 && LUMA_ASYNC_CLONE_MODE != 2
#error "LUMA_ASYNC_CLONE_MODE must be 1 (On Present) or 2 (On Worker)"
#endif

// LUMA_ASYNC_CLONE_MODE 1 only: maximum clones created per present, to spread
// the device work across frames instead of one giant batch.
// 0 = unlimited
#ifndef LUMA_ASYNC_CLONE_PRESENT_BUDGET
#define LUMA_ASYNC_CLONE_PRESENT_BUDGET 0
#endif

#define LUMA_PATCH_PROVIDER_BYTECODE_SYNC (1u << 0)
#define LUMA_PATCH_PROVIDER_BYTECODE_ASYNC (1u << 1)
#define LUMA_PATCH_PROVIDER_RECIPE_SYNC (1u << 2)
#define LUMA_PATCH_PROVIDER_RECIPE_ASYNC (1u << 3)

#define LUMA_PATCH_PROVIDERS ((LUMA_PATCH_BYTECODE_SYNC ? LUMA_PATCH_PROVIDER_BYTECODE_SYNC : 0u) | (LUMA_PATCH_BYTECODE_ASYNC ? LUMA_PATCH_PROVIDER_BYTECODE_ASYNC : 0u) | (LUMA_PATCH_RECIPE_SYNC ? LUMA_PATCH_PROVIDER_RECIPE_SYNC : 0u) | (LUMA_PATCH_RECIPE_ASYNC ? LUMA_PATCH_PROVIDER_RECIPE_ASYNC : 0u))

// True when any DXP recipe provider is enabled (shared recipe code gate).
#define LUMA_HAS_RECIPE_PROVIDERS (LUMA_PATCH_RECIPE_SYNC || LUMA_PATCH_RECIPE_ASYNC)

// Library availability: defined by the Luma props when UseLumaDXP=true, which
// also adds the dxp include/link paths. Never enables provider code by itself.
#ifndef LUMA_USE_DXP
#define LUMA_USE_DXP 0
#endif

// Recipe providers need the library — fail early instead of a confusing
// "cannot open include file: dxp/RecipeReport.hpp" error.
#if LUMA_HAS_RECIPE_PROVIDERS && !LUMA_USE_DXP
#error "LUMA_PATCH_RECIPE_SYNC/_ASYNC require UseLumaDXP=true in the project (defines LUMA_USE_DXP)"
#endif

// Only one sync and one async provider may be enabled: the dispatch compiles
// only the enabled branches.
#if LUMA_PATCH_BYTECODE_SYNC && LUMA_PATCH_RECIPE_SYNC
#error "Only one sync provider may be enabled: LUMA_PATCH_BYTECODE_SYNC XOR LUMA_PATCH_RECIPE_SYNC"
#endif
#if LUMA_PATCH_BYTECODE_ASYNC && LUMA_PATCH_RECIPE_ASYNC
#error "Only one async provider may be enabled: LUMA_PATCH_BYTECODE_ASYNC XOR LUMA_PATCH_RECIPE_ASYNC"
#endif

#if LUMA_USE_DXP
#include <dxp/RecipeReport.hpp>
#endif

class Game;      // Global per-game implementation (game.h)
class DeviceData; // Global device data (instance_data.h)

namespace Patch
{
   // The two patch providers; tracking is per method: a "no patch needed"
   // outcome is deterministic per (method, hash) and must not suppress the
   // other method's attempt on the same shader.
   enum class Method : uint8_t
   {
      Bytecode = 0,
      Recipe = 1,
   };

   constexpr size_t METHOD_COUNT = 2;

   // A stored patch: patched container bytes + provider metadata.
   struct PatchedShaderData
   {
      std::vector<uint8_t> code;
      Method method = Method::Bytecode;
      std::optional<Hash::MD5::Digest> md5; // Bytecode method: MD5 of the patched container (debug)
#if LUMA_USE_DXP
      std::optional<dxp::RecipeReport> report; // Recipe method: bindings/usage reports
#endif
   };

   // Thread-safe store of patches + per-method processed markers.
   struct PatchContext
   {
      std::unordered_map<uint32_t, std::shared_ptr<PatchedShaderData>> patched_shaders;
      std::array<std::unordered_set<uint32_t>, METHOD_COUNT> processed_shaders;
      // Debug-data-stripped containers by pre-strip hash (Heavy Rain-style
      // unification). Kept separately from patches: stripping is not a patch.
      std::unordered_map<uint32_t, std::shared_ptr<const std::vector<uint8_t>>> stripped_containers;
      // Bind-time default per shader (absent = enabled). Replaced by
      // OnBindPatchedShader(game.h) callback; UseShaderVariant overrides at draw time.
      // Never re-runs providers.
      std::unordered_map<uint32_t, bool> patch_enabled_by_default;
      mutable std::shared_mutex mutex;

      bool HasPatch(uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         return patched_shaders.contains(shader_hash);
      }

      // Bind-time default for this shader. Not "is the patch applied now" —
      // the game decides via OnBindPatchedShader.
      bool IsPatchEnabled(uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         auto it = patch_enabled_by_default.find(shader_hash);
         return it == patch_enabled_by_default.end() || it->second;
      }

      // Sets the bind-time default; returns true if changed.
      bool SetPatchEnabled(uint32_t shader_hash, bool enabled)
      {
         const std::unique_lock lock(mutex);
         auto [it, inserted] = patch_enabled_by_default.try_emplace(shader_hash, enabled);
         if (!inserted && it->second == enabled)
         {
            return false;
         }
         it->second = enabled;
         return true;
      }

      // Clears all bind-time defaults.
      void ResetPatchToggles()
      {
         const std::unique_lock lock(mutex);
         patch_enabled_by_default.clear();
      }

      // Definitive outcome (patched or no-patch-needed) already determined for
      // this (method, shader); no-match is terminal per hash.
      bool IsProcessed(Method method, uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         return processed_shaders[static_cast<size_t>(method)].contains(shader_hash);
      }

      // Returns true if newly inserted (not yet processed).
      bool SetProcessed(Method method, uint32_t shader_hash)
      {
         const std::unique_lock lock(mutex);
         return processed_shaders[static_cast<size_t>(method)].emplace(shader_hash).second;
      }

      std::shared_ptr<PatchedShaderData> GetShaderData(uint32_t shader_hash) const
      {
         const std::shared_lock lock(mutex);
         auto it = patched_shaders.find(shader_hash);
         if (it != patched_shaders.end() && it->second && !it->second->code.empty())
         {
            return it->second; // Increments ref-count safely inside the lock
         }
         return nullptr;
      }

      void StorePatched(uint32_t shader_hash, std::shared_ptr<PatchedShaderData> data)
      {
         // Create the fully populated object first to minimize time spent inside the unique_lock
         const std::unique_lock lock(mutex);
         // Overwrites or inserts in a single, fast operation
         patched_shaders.insert_or_assign(shader_hash, std::move(data));
      }

      std::shared_ptr<const std::vector<uint8_t>> GetStrippedContainer(uint32_t pre_strip_shader_hash) const
      {
         const std::shared_lock lock(mutex);
         auto it = stripped_containers.find(pre_strip_shader_hash);
         return it != stripped_containers.end() ? it->second : nullptr;
      }

      void StoreStrippedContainer(uint32_t pre_strip_shader_hash, std::shared_ptr<const std::vector<uint8_t>> data)
      {
         const std::unique_lock lock(mutex);
         stripped_containers.insert_or_assign(pre_strip_shader_hash, std::move(data));
      }

      template <typename TVisitor>
      void ForEachPatchedShader(TVisitor&& visitor) const
      {
         const std::shared_lock lock(mutex);
         for (const auto& [shader_hash, patched_shader] : patched_shaders)
         {
            if (!patched_shader || patched_shader->code.empty())
            {
               continue;
            }

            std::forward<TVisitor>(visitor)(shader_hash, *patched_shader);
         }
      }
   };

   // Request passed to recipe providers (full DXBC container).
   struct ShaderPatchRequest
   {
      reshade::api::pipeline_subobject_type type = reshade::api::pipeline_subobject_type::unknown;
      uint32_t shader_hash = uint32_t(-1);
      const std::byte* shader_container = nullptr;
      size_t shader_container_size = 0;
      // Async path only: the job's owned container, so games can pass it
      // straight to Recipe::Execute without copying.
      const std::vector<uint8_t>* shader_container_owned = nullptr;
   };

   // SHEX/SHDR chunk view inside a DXBC container (as seen by bytecode providers).
   struct ByteCodeView
   {
      const uint8_t* bytecode = nullptr;
      uint32_t bytecode_size = 0;
      uint32_t bytecode_offset = 0; // Chunk bytecode offset relative to the container start
      bool valid = false;
   };

   // Async patch job: owns one container copy + the parsed bytecode view offset.
   struct PatchJob
   {
      uint32_t shader_hash = uint32_t(-1);
      reshade::api::pipeline_subobject_type type = reshade::api::pipeline_subobject_type::unknown;
      std::vector<uint8_t> shader_container;
      uint32_t bytecode_offset = 0;
   };

   // Validates the DXBC container and locates the SHEX/SHDR bytecode chunk.
   inline ByteCodeView FindShaderByteCode(const void* container, size_t container_size)
   {
      ByteCodeView view;
      if (container == nullptr || container_size < sizeof(Shader::DXBCHeader))
      {
         return view;
      }

      const auto* header = static_cast<const Shader::DXBCHeader*>(container);
      if (memcmp(header->format_name, "DXBC", 4) != 0 || header->file_size != container_size)
      {
         return view;
      }

      for (uint32_t i = 0; i < header->chunk_count; ++i)
      {
         if (header->chunk_offsets[i] + sizeof(Shader::DXBCChunk) > container_size)
         {
            return view;
         }

         const auto* chunk = reinterpret_cast<const Shader::DXBCChunk*>(static_cast<const uint8_t*>(container) + header->chunk_offsets[i]);
         if (memcmp(&chunk->type_name, "SHEX", 4) == 0 || memcmp(&chunk->type_name, "SHDR", 4) == 0)
         {
            if (header->chunk_offsets[i] + sizeof(Shader::DXBCChunk) + sizeof(Shader::DXBCByteCodeChunk) > container_size)
            {
               return view;
            }

            const auto* chunk_byte_code = reinterpret_cast<const Shader::DXBCByteCodeChunk*>(chunk->chunk_data);
            const size_t bytecode_offset = header->chunk_offsets[i] + sizeof(Shader::DXBCChunk) + offsetof(Shader::DXBCByteCodeChunk, byte_code);
            const uint32_t bytecode_size = (chunk_byte_code->chunk_size_dword * sizeof(uint32_t)) - sizeof(Shader::DXBCByteCodeChunk); // Stored in DWORDs and counts the size and program version/type in its count, so we remove them

            if (bytecode_offset + bytecode_size > container_size)
            {
               return view;
            }

            view.bytecode = static_cast<const uint8_t*>(container) + bytecode_offset;
            view.bytecode_size = bytecode_size;
            view.bytecode_offset = uint32_t(bytecode_offset);
            view.valid = true;
            return view;
         }
      }

      return view;
   }

   // Splices new SHEX/SHDR bytecode into a copied container, fixing chunk
   // offsets/sizes and recomputing the container MD5 (mandatory, or D3D may
   // refuse the shader). Returns an empty vector on invalid input.
   inline std::vector<uint8_t> BuildPatchedContainer(const void* container, size_t container_size, size_t bytecode_offset, const void* new_bytecode, size_t new_bytecode_size)
   {
      std::vector<uint8_t> out;
      if (container == nullptr || new_bytecode == nullptr || container_size < sizeof(Shader::DXBCHeader) || bytecode_offset >= container_size)
      {
         return out;
      }

      const auto* header = static_cast<const Shader::DXBCHeader*>(container);

      // Locate the chunk that owns the bytecode and validate the sizes.
      const Shader::DXBCChunk* chunk = nullptr;
      uint32_t chunk_index = 0;
      for (uint32_t i = 0; i < header->chunk_count; ++i)
      {
         if (header->chunk_offsets[i] + sizeof(Shader::DXBCChunk) > container_size)
         {
            return out;
         }
         const auto* candidate = reinterpret_cast<const Shader::DXBCChunk*>(static_cast<const uint8_t*>(container) + header->chunk_offsets[i]);
         if (header->chunk_offsets[i] + sizeof(Shader::DXBCChunk) + offsetof(Shader::DXBCByteCodeChunk, byte_code) == bytecode_offset)
         {
            chunk = candidate;
            chunk_index = i;
            break;
         }
      }
      if (chunk == nullptr)
      {
         return out;
      }

      const auto* chunk_byte_code = reinterpret_cast<const Shader::DXBCByteCodeChunk*>(chunk->chunk_data);
      const uint32_t old_bytecode_size = (chunk_byte_code->chunk_size_dword * sizeof(uint32_t)) - sizeof(Shader::DXBCByteCodeChunk);
      if (bytecode_offset + old_bytecode_size > container_size)
      {
         return out;
      }

      const int32_t bytecode_size_diff = int32_t(new_bytecode_size) - int32_t(old_bytecode_size); // int32 should always be enough
      ASSERT_ONCE(bytecode_size_diff % int32_t(sizeof(uint32_t)) == 0); // Make sure it's a multiple of DWORD (4 bytes), it's probably mandatory for it to be

      const size_t new_container_size = container_size + bytecode_size_diff;
      out.resize(new_container_size);

      // Copy everything up to the byte code (the body itself is copied below)
      std::memcpy(out.data(), container, bytecode_offset);

      // Copy anything after this chunk (chunk_size counts the 8-byte chunk
      // header, so the tail starts 8 bytes into the bytecode, which the copy
      // below overwrites).
      const size_t old_tail_offset = header->chunk_offsets[chunk_index] + chunk->chunk_size;
      const size_t new_tail_offset = old_tail_offset + bytecode_size_diff;
      const size_t tail_size = container_size - old_tail_offset;
      if (new_tail_offset + tail_size <= new_container_size)
      {
         std::memcpy(out.data() + new_tail_offset, static_cast<const uint8_t*>(container) + old_tail_offset, tail_size);
      }

      // Copy the replaced byte code in
      std::memcpy(out.data() + bytecode_offset, new_bytecode, new_bytecode_size);

      // Update sizes
      auto* new_header = reinterpret_cast<Shader::DXBCHeader*>(out.data());
      new_header->file_size += bytecode_size_diff;
      auto* new_chunk = reinterpret_cast<Shader::DXBCChunk*>(out.data() + new_header->chunk_offsets[chunk_index]);
      new_chunk->chunk_size += bytecode_size_diff; // Chunk size
      auto* new_chunk_byte_code = reinterpret_cast<Shader::DXBCByteCodeChunk*>(new_chunk->chunk_data);
      new_chunk_byte_code->chunk_size_dword += bytecode_size_diff / int32_t(sizeof(uint32_t)); // Byte code size in DWORD (4 bytes)

      // Update chunk offsets of all chunks after this one
      for (uint32_t j = chunk_index + 1; j < new_header->chunk_count; ++j)
      {
         new_header->chunk_offsets[j] += bytecode_size_diff;
      }

      // Recalculate and set the container MD5, or the shader might fail to load.
      // Official implementation: https://github.com/doitsujin/dxbc-spirv/blob/32866c0d0a0236b93681d25405e57a3e9d6868d3/dxbc/dxbc_container.cpp#L11 (should match 100%)
      Hash::MD5::Digest md5_digest = Shader::CalcDXBCHash(out.data(), out.size());
      std::memcpy(new_header->hash, &md5_digest.data, Shader::DXBCHeader::hash_size);

      return out;
   }

   // CPU half of "ClonePipelineWithPatches": copies the subobjects and splices
   // patched bytecode via "find_patch" (same lookup order as LoadCustomShaders:
   // files first, then patches). No device call — the async worker defers
   // creation to the present boundary (see ProcessAsyncCloneBatch).
   inline std::pair<reshade::api::pipeline_subobject*, bool> BuildPatchedCloneSubobjects(
      uint32_t subobject_count,
      const reshade::api::pipeline_subobject* subobjects,
      std::function<std::optional<std::pair<const uint8_t*, uint32_t>>(
          const reshade::api::shader_desc* clone_desc,
          const reshade::api::shader_desc* orig_desc)> find_patch)
   {
      auto* new_subobjects = Shader::ClonePipelineSubobjects(subobject_count, subobjects);
      bool injected = false;

      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         switch (subobject.type)
         {
         case reshade::api::pipeline_subobject_type::geometry_shader:
         case reshade::api::pipeline_subobject_type::vertex_shader:
         case reshade::api::pipeline_subobject_type::compute_shader:
         case reshade::api::pipeline_subobject_type::pixel_shader:
            break;
         default:
            continue;
         }

         auto* clone_desc = static_cast<reshade::api::shader_desc*>(new_subobjects[i].data);
         auto patch_opt = find_patch(clone_desc, static_cast<const reshade::api::shader_desc*>(subobject.data));
         if (!patch_opt)
            continue;

         auto [patch_data, patch_size] = *patch_opt;
         free(const_cast<void*>(clone_desc->code));
         clone_desc->code_size = patch_size;
         clone_desc->code = malloc(patch_size);
         std::memcpy(const_cast<void*>(clone_desc->code), patch_data, patch_size);
         injected = true;
      }

      if (!injected)
      {
         Shader::DestroyPipelineSubojects(new_subobjects, subobject_count);
         return {nullptr, false};
      }

      return {new_subobjects, true};
   }

   // Clones the pipeline replacing shader subobjects via "find_patch" (same
   // lookup order as LoadCustomShaders: files first, then patches). No re-entry
   // guard needed: addon-created pipelines bypass ReShade's event dispatch.
   inline std::pair<reshade::api::pipeline, bool> ClonePipelineWithPatches(
      reshade::api::device* device,
      reshade::api::pipeline_layout layout,
      const reshade::api::pipeline_subobject* subobjects,
      uint32_t subobject_count,
      std::function<std::optional<std::pair<const uint8_t*, uint32_t>>(
          const reshade::api::shader_desc* clone_desc,
          const reshade::api::shader_desc* orig_desc)> find_patch)
   {
      auto [new_subobjects, injected] = BuildPatchedCloneSubobjects(subobject_count, subobjects, std::move(find_patch));
      if (!injected)
      {
         return {{}, false};
      }

      reshade::api::pipeline pipeline_clone = {};
      const bool ok = device->create_pipeline(layout, subobject_count, new_subobjects, &pipeline_clone);
      Shader::DestroyPipelineSubojects(new_subobjects, subobject_count);

      if (!ok)
         ASSERT_ONCE(pipeline_clone.handle == 0);

      return {pipeline_clone, ok};
   }

   // Runs the sync providers for one shader and stores any patch produced
   // (returns it, or nullptr). Providers are marked processed on any definitive
   // outcome, so they never re-run. Implemented in core.hpp (needs the full
   // Game/DeviceData types).
   std::shared_ptr<PatchedShaderData> PatchShaderSync(Game& game, DeviceData& device_data, const ShaderPatchRequest& request, const ByteCodeView& view, uint32_t providers);

   // Runs the async providers for one job (bytecode first: manual wins on
   // conflicts). Stores the result, marks processed on any definitive outcome.
   // Returns true if a patch was stored. Implemented in core.hpp.
   bool ProcessAsyncPatchJob(Game& game, DeviceData& device_data, PatchJob& job, uint32_t providers);
} // namespace Patch
