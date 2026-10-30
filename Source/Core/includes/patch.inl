#pragma once

#include <charconv>
#include <span>
#include <variant>

namespace Patch
{
   namespace
   {
      // Validates the DXBC container and locates the SHEX/SHDR bytecode chunk.
      ByteCodeView FindShaderByteCode(const void* container, size_t container_size)
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
      std::vector<uint8_t> BuildPatchedContainer(const void* container, size_t container_size, size_t bytecode_offset, const void* new_bytecode, size_t new_bytecode_size)
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
      std::pair<reshade::api::pipeline_subobject*, bool> BuildPatchedCloneSubobjects(
         uint32_t subobject_count,
         const reshade::api::pipeline_subobject* subobjects,
         std::function<std::optional<std::pair<const uint8_t*, uint32_t>>(
             const reshade::api::shader_desc* clone_desc,
             const reshade::api::shader_desc* orig_desc,
             reshade::api::pipeline_subobject_type type)> find_patch)
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
            auto patch_opt = find_patch(clone_desc, static_cast<const reshade::api::shader_desc*>(subobject.data), subobject.type);
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

#if LUMA_PATCH_PROVIDERS != 0
      // Definitive outcome (patched or no-patch-needed) already determined for
      // this (method, shader); no-match is terminal per hash until recipes are reloaded.
      bool IsProcessed(const DeviceData& device_data, Method method, uint32_t shader_hash)
      {
         auto& context = device_data.patch_context;
         const std::shared_lock lock(context.mutex);
         return context.processed_shaders[static_cast<size_t>(method)].contains(shader_hash);
      }

#if LUMA_HAS_RECIPE_PROVIDERS
      std::shared_ptr<PatchedShaderData> PatchRecipe(DeviceData& device_data, const ShaderPatchRequest& request, uint64_t generation)
      {
         auto& context = device_data.patch_context;
         std::vector<std::pair<PatchContext::RecipeEntry, dxp::PatchOptions>> candidates;
         {
            const std::shared_lock lock(context.mutex);
            if (generation != context.reload_generation || context.processed_shaders[static_cast<size_t>(Method::Recipe)].contains(request.shader_hash))
               return nullptr;
            for (const PatchContext::RecipeEntry& entry : context.recipes)
            {
               if (!entry.enabled || (entry.stage != reshade::api::pipeline_subobject_type::unknown && entry.stage != request.type))
                  continue;
               dxp::PatchOptions options;
               if (auto it = context.recipe_options_by_hash.find(entry.hash); it != context.recipe_options_by_hash.end())
                  options = it->second;
               for (const auto& [env, value] : entry.env_overrides)
               {
                  if (!options.GetEnv(env)) options.SetEnv(env, value); // The game's env is wiring (e.g. cbuffer offsets), users can't override it
               }
               candidates.emplace_back(entry, std::move(options));
            }
         }
         // Enabled recipes are chained: each one patches the previous one's output.
         std::vector<uint8_t> code;
         std::shared_ptr<PatchedShaderData> patched;
         for (auto& [entry, options] : candidates)
         {
            options.logger = [identity = entry.identity](dxp::LogLevel level, const std::string& message)
            {
               const auto log_level = level == dxp::LogLevel::Error ? reshade::log::level::error
                  : level == dxp::LogLevel::Warning ? reshade::log::level::warning
                  : level == dxp::LogLevel::Debug ? reshade::log::level::debug : reshade::log::level::info;
               reshade::log::message(log_level, ("[Luma] [Recipe " + identity + "] " + message).c_str());
            };
            try
            {
               const std::span<const uint8_t> input = patched ? std::span<const uint8_t>(code)
                  : std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(request.shader_container), request.shader_container_size);
               auto report = entry.recipe->Execute(input, options);
               if (!report)
               {
                  reshade::log::message(reshade::log::level::warning,
                     std::format("[Luma] Recipe {} failed for shader {:08X}: {}", entry.identity, request.shader_hash, report.error()).c_str());
                  continue;
               }
               if (!report->modified || report->output_bytes.empty()) continue;
               auto result = std::make_shared<RecipeResult>();
               result->shader_hash = request.shader_hash;
               result->recipe_hash = entry.hash;
               result->identity = entry.identity;
               result->stage = request.type;
               if (!patched)
               {
                  patched = std::make_shared<PatchedShaderData>();
                  patched->method = Method::Recipe;
               }
               code = std::move(report->output_bytes);
               result->report = std::move(*report);
               patched->recipe_results.push_back(std::move(result));
            }
            catch (const std::exception& e)
            {
               reshade::log::message(reshade::log::level::warning,
                  std::format("[Luma] Recipe {} failed for shader {:08X}: {}", entry.identity, request.shader_hash, e.what()).c_str());
            }
         }
         if (patched) patched->code = std::move(code);
         const std::unique_lock lock(context.mutex);
         if (generation != context.reload_generation) return nullptr;
         context.processed_shaders[static_cast<size_t>(Method::Recipe)].emplace(request.shader_hash);
         const auto existing = context.patched_shaders.find(request.shader_hash);
         if (existing != context.patched_shaders.end() && existing->second && existing->second->method == Method::Bytecode)
            return existing->second;
         if (patched) context.patched_shaders.insert_or_assign(request.shader_hash, patched);
         return patched;
      }
#endif

      // Runs the declared async providers for one job (bytecode first, then
      // recipe: manual wins on conflicts). Stores the result and marks the
      // provider processed on any definitive outcome. Returns true if a patch
      // was stored.
      bool ProcessAsyncPatchJob(Game& game, DeviceData& device_data, PatchJob& job, uint32_t providers)
      {
         if (Patch::HasPatch(device_data, job.shader_hash))
         {
            return false;
         }

         const ByteCodeView view = FindShaderByteCode(job.shader_container.data(), job.shader_container.size());

         // Bytecode async provider (manual wins on conflicts).
#if LUMA_PATCH_BYTECODE_ASYNC
         if ((providers & LUMA_PATCH_PROVIDER_BYTECODE_ASYNC) != 0 && !Patch::IsProcessed(device_data, Method::Bytecode, job.shader_hash))
         {
            size_t new_byte_code_size = view.valid ? view.bytecode_size : 0;
            std::unique_ptr<std::byte[]> patched_byte_code = game.PatchShaderBytecodeAsync(reinterpret_cast<const std::byte*>(view.bytecode), new_byte_code_size, job.type, job.shader_hash, reinterpret_cast<const std::byte*>(job.shader_container.data()), job.shader_container.size());
            if (patched_byte_code && new_byte_code_size != 0 && view.valid)
            {
               auto data = std::make_shared<PatchedShaderData>();
               data->method = Method::Bytecode;
               data->code = BuildPatchedContainer(job.shader_container.data(), job.shader_container.size(), view.bytecode_offset, patched_byte_code.get(), new_byte_code_size);
               if (!data->code.empty())
               {
                  data->md5 = *reinterpret_cast<const Hash::MD5::Digest*>(data->code.data() + offsetof(DXBCHeader, hash));
                  Patch::StorePatched(device_data, job.shader_hash, std::move(data));
                  return true;
               }
            }
            Patch::SetProcessed(device_data, Method::Bytecode, job.shader_hash);
         }
#endif // LUMA_PATCH_BYTECODE_ASYNC

         // Recipe async provider.
#if LUMA_PATCH_RECIPE_ASYNC
         if ((providers & LUMA_PATCH_PROVIDER_RECIPE_ASYNC) != 0 && !Patch::IsProcessed(device_data, Method::Recipe, job.shader_hash))
         {
            ShaderPatchRequest request{};
            request.type = job.type;
            request.shader_hash = job.shader_hash;
            request.shader_container = reinterpret_cast<const std::byte*>(job.shader_container.data());
            request.shader_container_size = job.shader_container.size();

            return bool(Patch::PatchRecipe(device_data, request, job.generation));
         }
#endif

         return false;
      }

#if (LUMA_PATCH_PROVIDERS & (LUMA_PATCH_PROVIDER_BYTECODE_SYNC | LUMA_PATCH_PROVIDER_RECIPE_SYNC)) && LUMA_PATCH_SYNC_MODE_CLONE
      // Same detection as OnInitPipeline's "found_custom_shader_file" (per shader hash, so it covers every stage of a DX12 PSO).
      bool HasCustomShaderFile(const Shader::CachedPipeline& cached_pipeline)
      {
         const std::shared_lock lock(s_mutex_loading);
         return std::any_of(cached_pipeline.shader_hashes.begin(), cached_pipeline.shader_hashes.end(),
            [](uint32_t hash) { return custom_shaders_cache.contains(hash); });
      }
#endif

      // Whether a pipeline needs patch work: a stored patch exists, or a declared
      // async provider is unresolved for it (no-match is terminal).
      bool ShouldQueuePatchPipeline(const DeviceData& device_data, uint32_t shader_hash)
      {
         if (Patch::HasPatch(device_data, shader_hash)) return true;
         if ((LUMA_PATCH_PROVIDERS & LUMA_PATCH_PROVIDER_BYTECODE_ASYNC) != 0 && !Patch::IsProcessed(device_data, Patch::Method::Bytecode, shader_hash)) return true;
         if ((LUMA_PATCH_PROVIDERS & LUMA_PATCH_PROVIDER_RECIPE_ASYNC) != 0 && !Patch::IsProcessed(device_data, Patch::Method::Recipe, shader_hash)) return true;
         return false;
      }
#endif // LUMA_PATCH_PROVIDERS != 0

#if LUMA_USE_DXP
      // Recipe resources are Luma's cbuffers or "managed_resources" entries named after the recipe's handle
      // (games register theirs, and reset them when they aren't valid). Returns false if missing (or not supported).
      bool ResolveRecipeBinding(DeviceData& device_data, const RecipeResult& result, const std::string& handle,
         const dxp::ResourceBinding& binding, RecipeBinding& out)
      {
         out.stage = result.stage;
         out.slot = binding.register_index;
         const uint32_t handle_hash = Math::CompileTimeStringHash(handle);
         switch (binding.binding_class)
         {
         case dxp::BindingClass::Texture:
         case dxp::BindingClass::RawResource:
         case dxp::BindingClass::StructuredResource:
         {
            if (out.slot >= D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT) return false;
            const std::shared_lock lock_device(device_data.mutex);
            auto resource = device_data.managed_resources.shader_resource_views.find(handle_hash);
            if (resource == device_data.managed_resources.shader_resource_views.end() && handle == "fast_noise")
               resource = device_data.managed_resources.shader_resource_views.find(Math::CompileTimeStringHash("FAST Noise"));
            if (resource != device_data.managed_resources.shader_resource_views.end()) out.srv = resource->second.get();
            return out.srv != nullptr;
         }
         case dxp::BindingClass::CBuffer:
         {
            if (out.slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT) return false;
            if (handle == "luma_settings") out.buffer = device_data.luma_global_settings;
            else if (handle == "luma_data") out.buffer = device_data.luma_instance_data;
            else
            {
               const std::shared_lock lock_device(device_data.mutex);
               const auto resource = device_data.managed_resources.buffers.find(handle_hash);
               if (resource != device_data.managed_resources.buffers.end()) out.buffer = resource->second.get();
            }
            return out.buffer != nullptr;
         }
         case dxp::BindingClass::Sampler:
         {
            if (out.slot >= D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) return false;
            const std::shared_lock lock_device(device_data.mutex);
            const auto resource = device_data.managed_resources.samplers.find(handle_hash);
            if (resource != device_data.managed_resources.samplers.end()) out.sampler = resource->second.get();
            return out.sampler != nullptr;
         }
         case dxp::BindingClass::TextureUav:
         case dxp::BindingClass::Uav:
         {
            // Graphics stages share UAV slots with the render targets, only compute is supported
            if (out.stage != reshade::api::pipeline_subobject_type::compute_shader || out.slot >= device_data.uav_max_count) return false;
            const std::shared_lock lock_device(device_data.mutex);
            const auto resource = device_data.managed_resources.unordered_access_views.find(handle_hash);
            if (resource != device_data.managed_resources.unordered_access_views.end()) out.uav = resource->second.get();
            return out.uav != nullptr;
         }
         default:
            return false;
         }
      }

      void SetRecipeBinding(ID3D11DeviceContext* context, CommandListData& cmd_list_data, const RecipeBinding& binding)
      {
         if (binding.buffer)
         {
            cmd_list_data.SetConstantBuffers(context, Shader::GetShaderStage(binding.stage), binding.slot, binding.buffer.get());
            return;
         }
         if (binding.uav)
         {
            ID3D11UnorderedAccessView* const view = binding.uav.get();
            context->CSSetUnorderedAccessViews(binding.slot, 1, &view, nullptr);
            return;
         }
         ID3D11ShaderResourceView* const view = binding.srv.get();
         ID3D11SamplerState* const sampler = binding.sampler.get();
         switch (binding.stage)
         {
         case reshade::api::pipeline_subobject_type::vertex_shader:
            if (view) context->VSSetShaderResources(binding.slot, 1, &view);
            else context->VSSetSamplers(binding.slot, 1, &sampler);
            break;
         case reshade::api::pipeline_subobject_type::pixel_shader:
            if (view) context->PSSetShaderResources(binding.slot, 1, &view);
            else context->PSSetSamplers(binding.slot, 1, &sampler);
            break;
         case reshade::api::pipeline_subobject_type::compute_shader:
            if (view) context->CSSetShaderResources(binding.slot, 1, &view);
            else context->CSSetSamplers(binding.slot, 1, &sampler);
            break;
#if GEOMETRY_SHADER_SUPPORT
         case reshade::api::pipeline_subobject_type::geometry_shader:
            if (view) context->GSSetShaderResources(binding.slot, 1, &view);
            else context->GSSetSamplers(binding.slot, 1, &sampler);
            break;
#endif
         }
      }
#endif
   }

   inline std::pair<reshade::api::pipeline, bool> ClonePipelineWithPatches(
      reshade::api::device* device,
      reshade::api::pipeline_layout layout,
      const reshade::api::pipeline_subobject* subobjects,
      uint32_t subobject_count,
      std::function<std::optional<std::pair<const uint8_t*, uint32_t>>(
          const reshade::api::shader_desc* clone_desc,
          const reshade::api::shader_desc* orig_desc,
          reshade::api::pipeline_subobject_type type)> find_patch)
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

#if LUMA_PATCH_PROVIDERS != 0
   inline bool HasPatch(const DeviceData& device_data, uint32_t shader_hash)
   {
      auto& context = device_data.patch_context;
      const std::shared_lock lock(context.mutex);
      return context.patched_shaders.contains(shader_hash);
   }

   inline bool SetProcessed(DeviceData& device_data, Method method, uint32_t shader_hash)
   {
      auto& context = device_data.patch_context;
      const std::unique_lock lock(context.mutex);
      return context.processed_shaders[static_cast<size_t>(method)].emplace(shader_hash).second;
   }

   inline std::shared_ptr<PatchedShaderData> GetShaderData(const DeviceData& device_data, uint32_t shader_hash)
   {
      auto& context = device_data.patch_context;
      const std::shared_lock lock(context.mutex);
      auto it = context.patched_shaders.find(shader_hash);
      if (it != context.patched_shaders.end() && it->second && !it->second->code.empty())
      {
         return it->second; // Increments ref-count safely inside the lock
      }
      return nullptr;
   }

   inline void StorePatched(DeviceData& device_data, uint32_t shader_hash, std::shared_ptr<PatchedShaderData> data)
   {
      auto& context = device_data.patch_context;
      // Create the fully populated object first to minimize time spent inside the unique_lock
      const std::unique_lock lock(context.mutex);
      // Overwrites or inserts in a single, fast operation
      context.patched_shaders.insert_or_assign(shader_hash, std::move(data));
   }

   inline std::shared_ptr<const std::vector<uint8_t>> GetStrippedContainer(const DeviceData& device_data, uint32_t pre_strip_shader_hash)
   {
      auto& context = device_data.patch_context;
      const std::shared_lock lock(context.mutex);
      auto it = context.stripped_containers.find(pre_strip_shader_hash);
      return it != context.stripped_containers.end() ? it->second : nullptr;
   }

   inline void StoreStrippedContainer(DeviceData& device_data, uint32_t pre_strip_shader_hash, std::shared_ptr<const std::vector<uint8_t>> data)
   {
      auto& context = device_data.patch_context;
      const std::unique_lock lock(context.mutex);
      context.stripped_containers.insert_or_assign(pre_strip_shader_hash, std::move(data));
   }

   template <typename TVisitor>
   inline void ForEachPatchedShader(const DeviceData& device_data, TVisitor&& visitor)
   {
      auto& context = device_data.patch_context;
      const std::shared_lock lock(context.mutex);
      for (const auto& [shader_hash, patched_shader] : context.patched_shaders)
      {
         if (!patched_shader || patched_shader->code.empty())
         {
            continue;
         }

         std::forward<TVisitor>(visitor)(shader_hash, *patched_shader);
      }
   }

#if LUMA_HAS_RECIPE_PROVIDERS
   namespace
   {
      // Global recipe settings, shared by all devices.
      struct RecipeSettings
      {
         std::string identity;
         std::optional<RecipeRegistration> registration;
         std::optional<bool> enabled; // Not read from the config yet if empty
         std::unordered_map<std::string, dxp::PrimitiveValue> env_overrides;
         std::unordered_set<std::string> env_read; // Env keys already read from the config (their type comes from the recipe)
      };
      std::shared_mutex s_recipe_settings_mutex;
      std::unordered_map<uint32_t, RecipeSettings> s_recipe_settings;
      // Bumped on every change, devices reapply their patches when it differs from theirs.
      std::atomic<uint64_t> s_recipe_settings_version = 1;

      const char* GetRecipeConfigSection()
      {
         static const std::string section = std::string(NAME) + " Recipes";
         return section.c_str();
      }

      std::string RecipeEnvToString(const dxp::PrimitiveValue& value)
      {
         return std::visit([](auto v) -> std::string
         {
            if constexpr (std::is_same_v<decltype(v), bool>) return v ? "1" : "0";
            else return std::format("{}", v);
         }, value);
      }

      // Needs "s_recipe_settings_mutex" locked (unique).
      RecipeSettings& GetRecipeSettings(std::string_view identity)
      {
         RecipeSettings& settings = s_recipe_settings[Math::CompileTimeStringHash(identity)];
         if (settings.identity.empty()) settings.identity = identity;
         if (!settings.enabled)
         {
            bool enabled = settings.registration ? settings.registration->default_enabled : true;
            reshade::get_config_value(nullptr, GetRecipeConfigSection(), settings.identity.c_str(), enabled);
            settings.enabled = enabled;
         }
         return settings;
      }

      // Reads the saved env overrides of freshly loaded recipes and warns about registrations that don't match them.
      void LoadRecipeSettings(const std::vector<PatchContext::RecipeEntry>& recipes)
      {
         const std::unique_lock lock(s_recipe_settings_mutex);
         for (const PatchContext::RecipeEntry& entry : recipes)
         {
            RecipeSettings& settings = GetRecipeSettings(entry.identity);
            for (const auto& [env, default_value] : entry.recipe->GetEnv())
            {
               if (!settings.env_read.emplace(env).second) continue;
               char value[64] = {};
               size_t value_size = sizeof(value);
               if (!reshade::get_config_value(nullptr, GetRecipeConfigSection(), (settings.identity + ":" + env).c_str(), value, &value_size)) continue;
               if (auto parsed = RecipeEnvFromString(std::string_view(value, strnlen(value, sizeof(value))), default_value))
                  settings.env_overrides.insert_or_assign(env, *parsed);
            }
         }
         for (const auto& [hash, settings] : s_recipe_settings)
         {
            if (!settings.registration) continue;
            const auto entry = std::find_if(recipes.begin(), recipes.end(), [&](const PatchContext::RecipeEntry& recipe) { return recipe.hash == hash; });
            if (entry == recipes.end())
            {
               reshade::log::message(reshade::log::level::warning, ("[Luma] Registered recipe was not found: " + settings.identity).c_str());
               continue;
            }
            const auto& env = entry->recipe->GetEnv();
            for (const RecipeOption& option : settings.registration->options)
            {
               if (!env.contains(option.env))
                  reshade::log::message(reshade::log::level::warning, ("[Luma] Recipe " + settings.identity + " has no env variable \"" + option.env + "\" (registered option)").c_str());
            }
         }
      }

      void CopyRecipeSettings(std::vector<PatchContext::RecipeEntry>& recipes)
      {
         const std::shared_lock lock(s_recipe_settings_mutex);
         for (PatchContext::RecipeEntry& entry : recipes)
         {
            const auto settings = s_recipe_settings.find(entry.hash);
            entry.enabled = settings == s_recipe_settings.end() || settings->second.enabled.value_or(true);
            entry.env_overrides = settings != s_recipe_settings.end() ? settings->second.env_overrides : std::unordered_map<std::string, dxp::PrimitiveValue>{};
         }
      }
   }

   std::optional<dxp::PrimitiveValue> RecipeEnvFromString(std::string_view text, const dxp::PrimitiveValue& type_of)
   {
      return std::visit([&](auto v) -> std::optional<dxp::PrimitiveValue>
      {
         using T = decltype(v);
         if constexpr (std::is_same_v<T, bool>)
         {
            if (text == "1" || text == "true") return dxp::PrimitiveValue(true);
            if (text == "0" || text == "false") return dxp::PrimitiveValue(false);
            return std::nullopt;
         }
         else
         {
            T parsed{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (text.empty() || error != std::errc() || end != text.data() + text.size()) return std::nullopt;
            return dxp::PrimitiveValue(parsed);
         }
      }, type_of);
   }

   void RegisterRecipe(RecipeRegistration registration)
   {
      const std::unique_lock lock(s_recipe_settings_mutex);
      RecipeSettings& settings = s_recipe_settings[Math::CompileTimeStringHash(registration.identity)];
      settings.identity = registration.identity;
      settings.registration = std::move(registration);
      settings.enabled.reset(); // Re-read with the registered default
   }

   bool IsRecipeEnabled(std::string_view identity)
   {
      const std::unique_lock lock(s_recipe_settings_mutex);
      return *GetRecipeSettings(identity).enabled;
   }

   void SetRecipeEnabled(std::string_view identity, bool enabled)
   {
      const std::unique_lock lock(s_recipe_settings_mutex);
      RecipeSettings& settings = GetRecipeSettings(identity);
      if (*settings.enabled == enabled) return;
      settings.enabled = enabled;
      reshade::set_config_value(nullptr, GetRecipeConfigSection(), settings.identity.c_str(), enabled);
      ++s_recipe_settings_version;
   }

   std::optional<dxp::PrimitiveValue> GetRecipeEnv(std::string_view identity, const std::string& env)
   {
      const std::unique_lock lock(s_recipe_settings_mutex);
      const RecipeSettings& settings = GetRecipeSettings(identity);
      const auto value = settings.env_overrides.find(env);
      return value != settings.env_overrides.end() ? std::optional(value->second) : std::nullopt;
   }

   void SetRecipeEnv(std::string_view identity, const std::string& env, dxp::PrimitiveValue value)
   {
      const std::unique_lock lock(s_recipe_settings_mutex);
      RecipeSettings& settings = GetRecipeSettings(identity);
      settings.env_read.emplace(env);
      if (const auto previous = settings.env_overrides.find(env); previous != settings.env_overrides.end() && previous->second == value) return;
      settings.env_overrides.insert_or_assign(env, value);
      reshade::set_config_value(nullptr, GetRecipeConfigSection(), (settings.identity + ":" + env).c_str(), RecipeEnvToString(value).c_str());
      ++s_recipe_settings_version;
   }

   void ResetRecipeEnv(std::string_view identity, const std::string& env)
   {
      const std::unique_lock lock(s_recipe_settings_mutex);
      RecipeSettings& settings = GetRecipeSettings(identity);
      settings.env_read.emplace(env);
      if (settings.env_overrides.erase(env) == 0) return;
      reshade::set_config_value(nullptr, GetRecipeConfigSection(), (settings.identity + ":" + env).c_str(), ""); // Empty = no override
      ++s_recipe_settings_version;
   }

   std::optional<RecipeRegistration> GetRecipeRegistration(std::string_view identity)
   {
      const std::shared_lock lock(s_recipe_settings_mutex);
      const auto settings = s_recipe_settings.find(Math::CompileTimeStringHash(identity));
      return settings != s_recipe_settings.end() ? settings->second.registration : std::nullopt;
   }

   void SetOptions(DeviceData& device_data, uint32_t recipe_hash, dxp::PatchOptions options)
   {
      auto& context = device_data.patch_context;
      const std::unique_lock lock(context.mutex);
      context.recipe_options_by_hash.insert_or_assign(recipe_hash, std::move(options));
   }

   // Parses the recipe files (no core locks needed) with the current user settings.
   std::vector<PatchContext::RecipeEntry> ParseRecipes(DeviceData& device_data, uint64_t& settings_version)
   {
      auto& context = device_data.patch_context;
      std::filesystem::path root_path;
      {
         const std::shared_lock lock(context.mutex);
         root_path = context.recipes_path;
      }

      std::vector<PatchContext::RecipeEntry> loaded;
      std::unordered_map<uint32_t, std::string> identities_by_hash;
      auto load_directory = [&](const std::filesystem::path& directory, reshade::api::pipeline_subobject_type stage)
      {
         std::error_code error;
         if (!std::filesystem::is_directory(directory, error)) return;

         std::vector<std::filesystem::path> files;
         for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
         {
            const std::string name = it->path().filename().string();
            constexpr std::string_view suffix = ".recipe.yml";
            if (it->is_regular_file(error) && name.size() >= suffix.size()
                && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
               files.push_back(it->path());
         }
         std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs)
         {
            return lhs.filename().generic_string() < rhs.filename().generic_string();
         });

         for (const auto& file : files)
         {
            const std::string identity = std::filesystem::relative(file, root_path, error).generic_string();
            if (error)
            {
               error.clear();
               continue;
            }
            const uint32_t hash = Math::CompileTimeStringHash(identity);
            if (auto collision = identities_by_hash.find(hash); collision != identities_by_hash.end() && collision->second != identity)
            {
               reshade::log::message(reshade::log::level::error,
                  ("[Luma] Recipe identity hash collision; skipping " + identity).c_str());
               continue;
            }

            auto parsed = dxp::sm5::Recipe::ParseFromFile(file.string());
            if (!parsed)
            {
               reshade::log::message(reshade::log::level::warning,
                  ("[Luma] Failed to load recipe " + file.string() + ": " + parsed.error()).c_str());
               continue;
            }

            identities_by_hash.insert_or_assign(hash, identity);
            loaded.push_back(PatchContext::RecipeEntry{ hash, identity, stage,
               std::make_shared<dxp::sm5::Recipe>(std::move(*parsed)) });
         }
      };

      if (!root_path.empty())
      {
         load_directory(root_path, reshade::api::pipeline_subobject_type::unknown);
         load_directory(root_path / "VS", reshade::api::pipeline_subobject_type::vertex_shader);
         load_directory(root_path / "PS", reshade::api::pipeline_subobject_type::pixel_shader);
         load_directory(root_path / "CS", reshade::api::pipeline_subobject_type::compute_shader);
#if GEOMETRY_SHADER_SUPPORT
         load_directory(root_path / "GS", reshade::api::pipeline_subobject_type::geometry_shader);
#endif

         std::error_code error;
         for (std::filesystem::directory_iterator it(root_path, error), end; !error && it != end; it.increment(error))
         {
            if (!it->is_directory(error)) continue;
            const std::string folder = it->path().filename().string();
            if (folder == "VS" || folder == "PS" || folder == "CS"
#if GEOMETRY_SHADER_SUPPORT
                || folder == "GS"
#endif
               ) continue;
            reshade::log::message(reshade::log::level::warning,
               ("[Luma] Recipe stage folder is not supported: " + it->path().string()).c_str());
         }
      }

      // Read the saved user settings of these recipes and validate the game's registrations against them.
      settings_version = s_recipe_settings_version.load();
      LoadRecipeSettings(loaded);
      CopyRecipeSettings(loaded);
      return loaded;
   }

   void LoadRecipes(DeviceData& device_data)
   {
      uint64_t settings_version = 0;
      std::vector<PatchContext::RecipeEntry> recipes = ParseRecipes(device_data, settings_version);
      auto& context = device_data.patch_context;
      const std::unique_lock lock(context.mutex);
      context.recipes = std::move(recipes);
      context.applied_settings_version = settings_version;
   }

#endif

   // Included by core.hpp after Game/DeviceData and core declarations are complete.
   // patch.hpp is included by instance_data.h, so definitions stay here until
   // the headers are self-contained.

   // Runs the sync providers for one shader and returns any patch produced.
   // Returns the patch (reused or fresh), or nullptr if none. Attempted
   // providers are marked processed on any definitive outcome (patched or
   // no-patch-needed — deterministic per method+hash, so they are never re-run
   // for the same shader until recipes reload). Bytecode storage is deferred
   // to the caller, so in-place shaders are validated first.
   // "providers" is the game's effective provider mask.
   std::shared_ptr<PatchedShaderData> PatchShaderSync(Game& game, DeviceData& device_data, const ShaderPatchRequest& request, const ByteCodeView& view, uint32_t providers)
   {
      const uint32_t shader_hash = request.shader_hash;

      // 1. Reuse an already stored patch (previous sync or async outcome).
      if (auto stored = Patch::GetShaderData(device_data, shader_hash); stored)
      {
         return stored;
      }

      // 2. Bytecode sync provider (manual wins on conflicts).
#if LUMA_PATCH_BYTECODE_SYNC
      if ((providers & LUMA_PATCH_PROVIDER_BYTECODE_SYNC) != 0 && !Patch::IsProcessed(device_data, Method::Bytecode, shader_hash))
      {
         size_t new_byte_code_size = view.valid ? view.bytecode_size : 0;
         std::unique_ptr<std::byte[]> patched_byte_code = game.PatchShaderBytecodeSync(reinterpret_cast<const std::byte*>(view.bytecode), new_byte_code_size, request.type, shader_hash, request.shader_container, request.shader_container_size);
         if (patched_byte_code && new_byte_code_size != 0 && view.valid)
         {
            auto data = std::make_shared<PatchedShaderData>();
            data->method = Method::Bytecode;
            data->code = BuildPatchedContainer(request.shader_container, request.shader_container_size, view.bytecode_offset, patched_byte_code.get(), new_byte_code_size);
            if (!data->code.empty())
            {
               data->md5 = *reinterpret_cast<const Hash::MD5::Digest*>(data->code.data() + offsetof(DXBCHeader, hash));
               Patch::SetProcessed(device_data, Method::Bytecode, shader_hash);
               return data;
            }
         }
         Patch::SetProcessed(device_data, Method::Bytecode, shader_hash);
      }
#endif

      // 3. Recipe sync provider.
#if LUMA_PATCH_RECIPE_SYNC
      if ((providers & LUMA_PATCH_PROVIDER_RECIPE_SYNC) != 0 && !Patch::IsProcessed(device_data, Method::Recipe, shader_hash))
      {
         return Patch::PatchRecipe(device_data, request, device_data.patch_context.reload_generation.load());
      }
#endif

      return nullptr;
   }

   void TryPatchPipeline(DeviceData& device_data, uint64_t pipeline_handle)
   {
      Shader::CachedPipeline* cached_pipeline = nullptr;
      {
         const std::shared_lock lock(s_mutex_generic);
         const auto pipeline = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
         if (pipeline == device_data.pipeline_cache_by_pipeline_handle.end()) return;
         cached_pipeline = pipeline->second;
         if (!cached_pipeline || cached_pipeline->manually_unloaded || patches_unloaded || cached_pipeline->cloned || cached_pipeline->pipeline_clone.handle != 0
             || cached_pipeline->patch_application_mode == Shader::PatchApplicationMode::Inplace) return;
      }

#if (LUMA_PATCH_PROVIDERS & (LUMA_PATCH_PROVIDER_BYTECODE_SYNC | LUMA_PATCH_PROVIDER_RECIPE_SYNC)) && LUMA_PATCH_SYNC_MODE_CLONE
      // Sync providers in CLONE mode run at pipeline init or bind: the
      // patch is stored and the pipeline is cloned with it (swapped in
      // at bind time). In INPLACE mode sync runs in OnCreatePipeline
      // instead and this block is not compiled.
      if (std::any_of(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(),
             [&](uint32_t hash) { return HasPatch(device_data, hash)
                || (LUMA_PATCH_BYTECODE_SYNC && !IsProcessed(device_data, Method::Bytecode, hash))
                || (LUMA_PATCH_RECIPE_SYNC && !IsProcessed(device_data, Method::Recipe, hash)); }))
      {
         bool found_sync_patch_this_pipeline = false;
         for (uint32_t i = 0; i < cached_pipeline->subobject_count; ++i)
         {
            const auto& subobject = cached_pipeline->subobjects_cache[i];
            if (subobject.type != reshade::api::pipeline_subobject_type::vertex_shader
                && subobject.type != reshade::api::pipeline_subobject_type::pixel_shader
                && subobject.type != reshade::api::pipeline_subobject_type::compute_shader
#if GEOMETRY_SHADER_SUPPORT
                && subobject.type != reshade::api::pipeline_subobject_type::geometry_shader
#endif
               ) continue;
            const auto* desc = static_cast<const reshade::api::shader_desc*>(subobject.data);
            if (desc->code_size == 0) continue;
            const uint32_t hash = Shader::BinToHash(static_cast<const uint8_t*>(desc->code), desc->code_size);
            if (HasPatch(device_data, hash))
            {
               found_sync_patch_this_pipeline = true;
               continue;
            }
            if ((!LUMA_PATCH_BYTECODE_SYNC || IsProcessed(device_data, Method::Bytecode, hash))
                && (!LUMA_PATCH_RECIPE_SYNC || IsProcessed(device_data, Method::Recipe, hash))) continue;
            const std::shared_lock lock_device(device_data.mutex);
            ShaderPatchRequest request{subobject.type, hash, static_cast<const std::byte*>(desc->code), desc->code_size};
            ByteCodeView view{};
#if LUMA_PATCH_BYTECODE_SYNC
            if (!IsProcessed(device_data, Method::Bytecode, hash)) view = FindShaderByteCode(desc->code, desc->code_size);
#endif
            if (auto patch = PatchShaderSync(*game, device_data, request, view, LUMA_PATCH_PROVIDERS); patch)
            {
               if (patch->method == Method::Bytecode) StorePatched(device_data, hash, patch);
               found_sync_patch_this_pipeline = true;
#if DEVELOPMENT
               reshade::log::message(reshade::log::level::debug, std::format("[Patch] Shader {:08X} patched via sync path", hash).c_str());
#endif
            }
         }
         // Clone pipeline with injected sync patches (if any found). A pipeline whose
         // providers just ran may also need its file clone back (e.g. after a recipe reload).
         if (found_sync_patch_this_pipeline || (auto_load && !last_pressed_unload && HasCustomShaderFile(*cached_pipeline)))
         {
            LoadCustomShaders(device_data, {cached_pipeline->pipeline.handle}, false);
            const auto pipeline_clone = cached_pipeline->pipeline_clone;
            const uint32_t subobject_count = cached_pipeline->subobject_count;
            if (cached_pipeline->cloned && pipeline_clone.handle != 0)
            {
               cached_pipeline->patch_application_mode = Shader::PatchApplicationMode::Sync;
#if DEVELOPMENT
               reshade::log::message(reshade::log::level::debug, std::format("[Patch] Sync-patched pipeline {:x} -> clone {:x}", cached_pipeline->pipeline.handle, pipeline_clone.handle).c_str());
#endif

#if DEVELOPMENT && LUMA_PATCH_PROVIDERS != 0
               // Update live patch debug info for all patched shaders in this pipeline
               {
                  const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                  for (uint32_t i = 0; i < subobject_count; ++i)
                  {
                     const auto& subobject = cached_pipeline->subobjects_cache[i];
                     if (subobject.type != reshade::api::pipeline_subobject_type::pixel_shader
                         && subobject.type != reshade::api::pipeline_subobject_type::vertex_shader
                         && subobject.type != reshade::api::pipeline_subobject_type::compute_shader
#if GEOMETRY_SHADER_SUPPORT
                         && subobject.type != reshade::api::pipeline_subobject_type::geometry_shader
#endif
                         ) continue;

                     auto* desc = static_cast<reshade::api::shader_desc*>(cached_pipeline->subobjects_cache[i].data);
                     uint32_t hash = Shader::BinToHash(static_cast<const uint8_t*>(desc->code), desc->code_size);
                     if (auto shader_data = Patch::GetShaderData(device_data, hash); shader_data)
                     {
                        if (auto it = shader_cache.find(hash); it != shader_cache.end() && it->second)
                        {
                           SetLivePatchedShaderInfo(it->second, hash, shader_data->code.data(), static_cast<uint32_t>(shader_data->code.size()));
                        }
                     }
                  }
               }
#endif
            }
         }
      }
#endif // CLONE sync mode
#if LUMA_PATCH_PROVIDERS & (LUMA_PATCH_PROVIDER_BYTECODE_ASYNC | LUMA_PATCH_PROVIDER_RECIPE_ASYNC)
      // Patches are automatic: queueing must not depend on the "Auto Load
      // Shaders" checkbox (a user tool for their own files), only on the unloaded
      // state. Note: the clone lambda injects cached files on these clones too, so
      // a file overrides a patch regardless of the checkbox.
      const std::shared_lock lock(s_mutex_generic);
      if (!cached_pipeline->cloned && std::any_of(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(),
             [&](uint32_t hash) { return ShouldQueuePatchPipeline(device_data, hash); }))
      {
         const std::unique_lock lock_loading(s_mutex_loading);
         if (device_data.pipelines_to_reload.emplace(pipeline_handle).second) NotifyAsyncCloneQueue(device_data);
      }
#endif
   }
#endif // LUMA_PATCH_PROVIDERS != 0

   // Unloads patch-only clones by preserving their objects. Mixed pipelines
   // rebuild from files, so patch bindings are no longer exposed.
   void UnloadPatches(DeviceData& device_data)
   {
      patches_unloaded = true; // Also prevents new pipelines from being queued for patch cloning
      std::unordered_set<uint64_t> mixed;
      {
         const std::unique_lock lock(s_mutex_generic);
         for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
         {
            Shader::CachedPipeline* cached_pipeline = pair.second;
            if (cached_pipeline == nullptr) continue;
            if (cached_pipeline->custom_pipeline_data.file_stages != reshade::api::shader_stage(0)
                && cached_pipeline->custom_pipeline_data.patch_stages != reshade::api::shader_stage(0))
            {
               mixed.emplace(pair.first);
               continue;
            }
            if (cached_pipeline->clone_origin != Shader::CloneOrigin::Patch) continue;
            const auto pipeline_clone_handle = cached_pipeline->pipeline_clone.handle;
            if (ClearCustomShader(device_data, cached_pipeline, false, false)) // Preserve the clone object
            {
               device_data.pipeline_cache_by_pipeline_clone_handle.erase(pipeline_clone_handle);
            }
         }
      }
      if (!mixed.empty()) LoadCustomShaders(device_data, mixed, false);
   }

#if LUMA_HAS_RECIPE_PROVIDERS
   // Drops the recipe patches and their clones, then loads the recipes again: pipelines re-patch on their next bind.
   // Note: a pipeline cloned with a file shader is not rebuilt, so it won't pick up a newly matching recipe.
   // That would require reloading every file clone too, not just the patched ones. In DX11 it never matters, as a
   // pipeline is a single shader and its file always wins over a patch; it only matters for DX12 PSOs.
   void ReloadRecipes(DeviceData& device_data)
   {
      auto& context = device_data.patch_context;
      uint64_t settings_version = 0;
      std::vector<PatchContext::RecipeEntry> recipes = ParseRecipes(device_data, settings_version); // Disk reads stay outside the core locks
      std::unordered_set<uint64_t> affected;
      std::unordered_map<uint64_t, reshade::api::device*> to_destroy;
      {
         const std::shared_lock lock_destroy(device_data.pipeline_cache_destruction_mutex);
         const std::unique_lock lock_generic(s_mutex_generic);
         const std::unique_lock lock_loading(s_mutex_loading);
         {
            const std::unique_lock lock(context.mutex);
            context.recipes = std::move(recipes);
            context.applied_settings_version = settings_version;
            ++context.reload_generation; // In flight patches and clones are dropped
            std::erase_if(context.patched_shaders, [](const auto& pair) { return pair.second && pair.second->method == Method::Recipe; });
            context.processed_shaders[static_cast<size_t>(Method::Recipe)].clear();
         }
         for (const auto& [handle, pipeline] : device_data.pipeline_cache_by_pipeline_handle)
         {
            if (pipeline && pipeline->custom_pipeline_data.patch_data) affected.emplace(handle);
         }
         // Also clears their clone description (files included). They are re-patched (and get their
         // files back) on their next bind, like every pipeline whose recipe outcome was cleared above.
         if (!affected.empty()) UnloadCustomShaders(device_data, affected, false, &to_destroy);
         for (uint64_t handle : affected) device_data.pipelines_to_reload.erase(handle);
      }
      for (const auto& [handle, device] : to_destroy) device->destroy_pipeline({handle});
   }

   void ApplyRecipeSettingsChanges(DeviceData& device_data)
   {
      uint64_t applied_settings_version;
      {
         const std::shared_lock lock(device_data.patch_context.mutex);
         applied_settings_version = device_data.patch_context.applied_settings_version;
      }
      if (applied_settings_version != s_recipe_settings_version.load()) ReloadRecipes(device_data);
   }

   void DrawRecipeSettings(const DeviceData& device_data)
   {
      auto IsVisible = [](RecipeVisibility visibility)
      {
         switch (visibility)
         {
         case RecipeVisibility::Always: return true;
         case RecipeVisibility::Test: return bool(DEVELOPMENT || TEST);
         default: return bool(DEVELOPMENT);
         }
      };

      struct RecipeInfo
      {
         std::string identity;
         std::shared_ptr<const dxp::sm5::Recipe> recipe;
         std::optional<dxp::PatchOptions> game_options;
      };
      std::vector<RecipeInfo> recipes;
      {
         const auto& context = device_data.patch_context;
         const std::shared_lock lock(context.mutex);
         for (const PatchContext::RecipeEntry& entry : context.recipes)
         {
            const auto game_options = context.recipe_options_by_hash.find(entry.hash);
            recipes.push_back(RecipeInfo{ entry.identity, entry.recipe, game_options != context.recipe_options_by_hash.end() ? std::optional(game_options->second) : std::nullopt });
         }
      }

      bool any_visible = false;
      for (const RecipeInfo& info : recipes)
      {
         std::optional<RecipeRegistration> registration;
         {
            const std::shared_lock lock(s_recipe_settings_mutex);
            if (const auto settings = s_recipe_settings.find(Math::CompileTimeStringHash(info.identity)); settings != s_recipe_settings.end())
               registration = settings->second.registration;
         }
         if (!IsVisible(registration ? registration->visibility : RecipeVisibility::Development)) continue;
         any_visible = true;

         ImGui::PushID(info.identity.c_str());
         const std::string& name = registration && !registration->display_name.empty() ? registration->display_name : info.identity;
         bool enabled = IsRecipeEnabled(info.identity);
         if (ImGui::Checkbox(name.c_str(), &enabled))
         {
            SetRecipeEnabled(info.identity, enabled);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            std::string tooltip = registration ? registration->description : std::string();
#if DEVELOPMENT || TEST
            tooltip += (tooltip.empty() ? "" : "\n") + info.identity + (registration ? "" : " (not registered by the game)");
#endif
            if (!tooltip.empty()) ImGui::SetTooltip("%s", tooltip.c_str());
         }
         const bool default_enabled = registration ? registration->default_enabled : true;
         if (enabled != default_enabled)
         {
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FK_UNDO))
            {
               SetRecipeEnabled(info.identity, default_enabled);
            }
         }

         if (enabled)
         {
            // Registered options first, then the recipe's other env variables (development builds only)
            struct Option
            {
               std::string env;
               std::string label;
               std::string tooltip;
            };
            std::vector<Option> options;
            const auto& env = info.recipe->GetEnv();
            if (registration)
            {
               for (const RecipeOption& option : registration->options)
               {
                  if (env.contains(option.env) && IsVisible(option.visibility))
                     options.push_back(Option{ option.env, option.label.empty() ? option.env : option.label, option.tooltip });
               }
            }
            if (IsVisible(RecipeVisibility::Development))
            {
               std::vector<std::string> unlisted;
               for (const auto& [key, value] : env)
               {
                  const bool registered = registration && std::any_of(registration->options.begin(), registration->options.end(), [&](const RecipeOption& option) { return option.env == key; });
                  if (!registered) unlisted.push_back(key);
               }
               std::sort(unlisted.begin(), unlisted.end());
               for (const std::string& key : unlisted)
                  options.push_back(Option{ key, key + " (hidden)", {} });
            }

            ImGui::Indent();
            for (const Option& option : options)
            {
               ImGui::PushID(option.env.c_str());
               // Effective value: the game's (read only), else the user's, else the recipe's own
               dxp::PrimitiveValue value = env.at(option.env);
               const std::optional<dxp::PrimitiveValue> game_value = info.game_options ? info.game_options->GetEnv(option.env) : std::nullopt;
               const std::optional<dxp::PrimitiveValue> user_value = game_value ? std::nullopt : GetRecipeEnv(info.identity, option.env);
               if (game_value) value = *game_value;
               else if (user_value) value = *user_value;
               const std::string label = game_value ? option.label + " (set by the game)" : option.label;

               // Numbers apply on Enter, every change reloads the recipes
               ImGui::BeginDisabled(game_value.has_value());
               const bool changed = std::visit([&](auto& v) -> bool
                  {
                     using T = std::decay_t<decltype(v)>;
                     if constexpr (std::is_same_v<T, bool>)
                     {
                        return ImGui::Checkbox(label.c_str(), &v);
                     }
                     else
                     {
                        constexpr ImGuiDataType data_type = std::is_same_v<T, int32_t> ? ImGuiDataType_S32 : std::is_same_v<T, uint32_t> ? ImGuiDataType_U32 : std::is_same_v<T, int64_t> ? ImGuiDataType_S64 : std::is_same_v<T, uint64_t> ? ImGuiDataType_U64 : ImGuiDataType_Double;
                        return ImGui::InputScalar(label.c_str(), data_type, &v, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
                     }
                  }, value);
               ImGui::EndDisabled();
               if (changed)
               {
                  SetRecipeEnv(info.identity, option.env, value);
               }
               if (!option.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
               {
                  ImGui::SetTooltip("%s", option.tooltip.c_str());
               }
               if (user_value)
               {
                  ImGui::SameLine();
                  if (ImGui::SmallButton(ICON_FK_UNDO))
                  {
                     ResetRecipeEnv(info.identity, option.env);
                  }
               }
               ImGui::PopID();
            }
            ImGui::Unindent();
         }
         ImGui::PopID();
      }
      if (!any_visible)
      {
         ImGui::TextUnformatted("No patch settings available.");
      }
   }
#endif

   // Re-reads the recipes and drops their patches, which rebuild on their next bind.
   // Bytecode-only patches re-register preserved clones and re-queue those destroyed by a full "Unload Shaders".
   void ReloadPatches(DeviceData& device_data)
   {
      patches_unloaded = false;
      // Like "Load Shaders" does for files, re-enable pipelines the user unloaded one by one
      // if they hold a stored patch (checked before the recipe reload drops recipe patches).
#if LUMA_PATCH_PROVIDERS != 0
      {
         const std::unique_lock lock(s_mutex_generic);
         for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
         {
            Shader::CachedPipeline* cached_pipeline = pair.second;
            if (cached_pipeline != nullptr && cached_pipeline->manually_unloaded
                && std::any_of(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(),
                   [&](uint32_t hash) { return HasPatch(device_data, hash); }))
               cached_pipeline->manually_unloaded = false;
         }
      }
#endif
#if LUMA_HAS_RECIPE_PROVIDERS
      ReloadRecipes(device_data);
#endif
      {
         const std::unique_lock lock(s_mutex_generic);
         for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
         {
            Shader::CachedPipeline* cached_pipeline = pair.second;
            if (cached_pipeline == nullptr || cached_pipeline->cloned || cached_pipeline->clone_origin != Shader::CloneOrigin::Patch || cached_pipeline->pipeline_clone.handle == 0) continue;
            cached_pipeline->cloned = true;
            device_data.pipeline_cache_by_pipeline_clone_handle[cached_pipeline->pipeline_clone.handle] = cached_pipeline;
            device_data.cloned_pipeline_count++;
            device_data.cloned_pipelines_changed = true;
         }
      }
#if !LUMA_HAS_RECIPE_PROVIDERS && (LUMA_PATCH_PROVIDERS & LUMA_PATCH_PROVIDER_BYTECODE_ASYNC)
      // Stored-but-uncloned pipelines go through the async worker (providers may
      // not have run while unloaded, hence the same predicate as the queue gate).
      {
         std::unordered_set<uint64_t> to_reload;
         {
            const std::shared_lock lock(s_mutex_generic);
            for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
            {
               Shader::CachedPipeline* cached_pipeline = pair.second;
               if (cached_pipeline == nullptr || cached_pipeline->cloned || cached_pipeline->pipeline_clone.handle != 0 || cached_pipeline->shader_hashes.size() == 0) continue;
               if (std::any_of(cached_pipeline->shader_hashes.begin(), cached_pipeline->shader_hashes.end(),
                  [&](uint32_t hash) { return ShouldQueuePatchPipeline(device_data, hash); }))
               {
                  to_reload.emplace(cached_pipeline->pipeline.handle);
               }
            }
         }
         if (!to_reload.empty())
         {
            const std::unique_lock lock_loading(s_mutex_loading);
            for (uint64_t handle : to_reload)
            {
               device_data.pipelines_to_reload.emplace(handle);
            }
            NotifyAsyncCloneQueue(device_data);
         }
      }
#endif
   }

   // Registered patch clones count (for the "Unload Patches" button title).
   uint32_t CountPatchedClones(const DeviceData& device_data)
   {
      uint32_t count = 0;
      const std::shared_lock lock(s_mutex_generic);
      for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
      {
         const Shader::CachedPipeline* cached_pipeline = pair.second;
         if (cached_pipeline != nullptr && cached_pipeline->cloned && cached_pipeline->custom_pipeline_data.patch_stages != reshade::api::shader_stage(0))
         {
            count++;
         }
      }
      return count;
   }

#if LUMA_PATCH_PROVIDERS & (LUMA_PATCH_PROVIDER_BYTECODE_ASYNC | LUMA_PATCH_PROVIDER_RECIPE_ASYNC)
   void GeneratePatchedShadersAsync(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines_filter)
   {
      std::vector<Patch::PatchJob> patch_jobs;
      // One job per shader per cycle: many pipelines share a shader hash, and
      // the IsProcessed markers aren't set until jobs run (after collection). A
      // local set is used instead of a persistent "Processing" state — it dies
      // with the call, so a stale marker can never block a shader.
      std::unordered_set<uint32_t> queued_shader_hashes;

      {
         std::shared_lock lock_pipeline_destroy(device_data.pipeline_cache_destruction_mutex);
         const std::shared_lock lock_generic(s_mutex_generic);

         for (uint64_t pipeline_handle : pipelines_filter)
         {
            auto cached_pipeline_it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
            if (cached_pipeline_it == device_data.pipeline_cache_by_pipeline_handle.end() || cached_pipeline_it->second == nullptr || cached_pipeline_it->second->manually_unloaded)
            {
               continue;
            }

            const Shader::CachedPipeline* cached_pipeline = cached_pipeline_it->second;
            for (uint32_t i = 0; i < cached_pipeline->subobject_count; ++i)
            {
               const auto& subobject = cached_pipeline->subobjects_cache[i];
               switch (subobject.type)
               {
#if GEOMETRY_SHADER_SUPPORT
               case reshade::api::pipeline_subobject_type::geometry_shader:
#endif
               case reshade::api::pipeline_subobject_type::vertex_shader:
               case reshade::api::pipeline_subobject_type::compute_shader:
               case reshade::api::pipeline_subobject_type::pixel_shader:
               {
                  const auto* shader_desc = static_cast<const reshade::api::shader_desc*>(subobject.data);
                  if (shader_desc == nullptr || shader_desc->code == nullptr || shader_desc->code_size < sizeof(DXBCHeader))
                  {
                     break;
                  }

                  const uint32_t shader_hash = Shader::BinToHash(static_cast<const uint8_t*>(shader_desc->code), shader_desc->code_size);
                  // Queue gate: nothing to do if a patch already exists, or if every
                  // declared async provider already reached a definitive outcome
                  // (patched or no-patch-needed) for this shader.
                  if (Patch::HasPatch(device_data, shader_hash))
                  {
                     break;
                  }

                  bool any_declared_async_unprocessed = false;
                  if ((LUMA_PATCH_PROVIDERS & LUMA_PATCH_PROVIDER_BYTECODE_ASYNC) != 0 && !Patch::IsProcessed(device_data, Patch::Method::Bytecode, shader_hash))
                  {
                     any_declared_async_unprocessed = true;
                  }
                  if ((LUMA_PATCH_PROVIDERS & LUMA_PATCH_PROVIDER_RECIPE_ASYNC) != 0 && !Patch::IsProcessed(device_data, Patch::Method::Recipe, shader_hash))
                  {
                     any_declared_async_unprocessed = true;
                  }
                  if (!any_declared_async_unprocessed)
                  {
                     break;
                  }

                  const auto* shader_header = reinterpret_cast<const DXBCHeader*>(shader_desc->code);
                  if (memcmp(shader_header->format_name, "DXBC", 4) != 0 || shader_header->file_size != shader_desc->code_size)
                  {
                     break;
                  }

                  if (!queued_shader_hashes.emplace(shader_hash).second)
                  {
                     break; // Already queued this cycle
                  }

                  Patch::PatchJob patch_job;
                  patch_job.shader_hash = shader_hash;
                  patch_job.type = subobject.type;
#if LUMA_HAS_RECIPE_PROVIDERS
                  patch_job.generation = device_data.patch_context.reload_generation.load();
#endif
                  patch_job.shader_container.assign(static_cast<const uint8_t*>(shader_desc->code), static_cast<const uint8_t*>(shader_desc->code) + shader_desc->code_size);

                  const Patch::ByteCodeView view = Patch::FindShaderByteCode(patch_job.shader_container.data(), patch_job.shader_container.size());
                  if (!view.valid)
                  {
                     break;
                  }
                  patch_job.bytecode_offset = view.bytecode_offset;

                  patch_jobs.emplace_back(std::move(patch_job));
                  break;
               }
               default:
                  break;
               }
            }
         }
      }

      for (auto& patch_job : patch_jobs)
      {
         Patch::ProcessAsyncPatchJob(*game, device_data, patch_job, LUMA_PATCH_PROVIDERS);
      }
   }

   // Wakes the persistent async clone worker after the job queue changed.
   void NotifyAsyncCloneQueue(DeviceData& device_data)
   {
      {
         const std::lock_guard lock(device_data.async_jobs_mutex);
         device_data.async_queue_version++;
      }
      device_data.async_jobs_cv.notify_all();
   }

   // Compiles clones for one batch of pipeline handles on the worker thread.
   // The subobject data is deep-copied under s_mutex_generic (shared) before
   // compiling, so no raw pipeline pointers are held across locks and the
   // game's pipeline destruction can never free memory we are still reading.
   // Finished builds are handed to the ready queue; the present boundary
   // publishes them (see PublishReadyAsyncClones).
   void ProcessAsyncCloneBatch(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines)
   {
      for (const uint64_t pipeline_handle : pipelines)
      {
         reshade::api::pipeline_subobject* subobjects_copy = nullptr;
         uint32_t subobject_count = 0;
         reshade::api::device* device = nullptr;
         reshade::api::pipeline_layout layout = {};
         uint32_t shader_hash = 0;
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const auto pipeline_it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
            if (pipeline_it == device_data.pipeline_cache_by_pipeline_handle.end() || pipeline_it->second == nullptr)
            {
               continue; // Pipeline was destroyed before we got to it
            }
            CachedPipeline* cached_pipeline = pipeline_it->second;
            if (cached_pipeline->manually_unloaded || cached_pipeline->cloned || cached_pipeline->pipeline_clone.handle != 0)
            {
               continue; // Manually unloaded, already cloned, or a preserved patch clone survives unloads (nothing to rebuild)
            }
            subobjects_copy = Shader::ClonePipelineSubobjects(cached_pipeline->subobject_count, cached_pipeline->subobjects_cache);
            subobject_count = cached_pipeline->subobject_count;
            device = cached_pipeline->device;
            layout = cached_pipeline->layout;
            shader_hash = cached_pipeline->shader_hashes[0]; // identity check for the publish step
         }

         if (subobjects_copy == nullptr)
         {
            continue;
         }

         Shader::CloneOrigin clone_origin = Shader::CloneOrigin::None;
         Shader::CustomPipelineData custom_pipeline_data;
#if LUMA_HAS_RECIPE_PROVIDERS
         uint64_t generation = 0;
#endif
         // Build the patched subobjects here (CPU only); LUMA_ASYNC_CLONE_MODE
         // selects where the pipeline is created. "s_mutex_loading" keeps custom-shader
         // file bytes alive through the memcpy below (worker vs. reload race).
         reshade::api::pipeline_subobject* patched_subobjects = nullptr;
         {
            const std::shared_lock lock_loading(s_mutex_loading);
#if LUMA_HAS_RECIPE_PROVIDERS
            generation = device_data.patch_context.reload_generation.load();
#endif
            auto [built, injected] = Patch::BuildPatchedCloneSubobjects(
               subobject_count, subobjects_copy,
               [&device_data, &clone_origin, &custom_pipeline_data](
                   const reshade::api::shader_desc*,
                   const reshade::api::shader_desc* orig_desc,
                   reshade::api::pipeline_subobject_type type) -> std::optional<std::pair<const uint8_t*, uint32_t>>
               {
                  const uint32_t shader_hash = Shader::BinToHash(static_cast<const uint8_t*>(orig_desc->code), orig_desc->code_size);

                  // Custom shader files first (same lookup order as LoadCustomShaders).
                  if (auto custom_it = custom_shaders_cache.find(shader_hash); custom_it != custom_shaders_cache.end())
                  {
                     const CachedCustomShader* custom_shader = custom_it->second;
                     if (custom_shader != nullptr && !custom_shader->is_luma_native && !custom_shader->code.empty())
                     {
                        clone_origin = Shader::CloneOrigin::File;
                        Shader::RecordCloneShader(custom_pipeline_data, type);
                        return std::make_pair(custom_shader->code.data(), static_cast<uint32_t>(custom_shader->code.size()));
                     }
                  }

                  // Recipe / bytecode patches (only exist when patch providers
                  // are enabled; other games have no patch_context).
                  if (auto patched = Patch::GetShaderData(device_data, shader_hash); !patches_unloaded && patched)
                  {
                     if (clone_origin != Shader::CloneOrigin::File) clone_origin = Shader::CloneOrigin::Patch;
                     Shader::RecordCloneShader(custom_pipeline_data, type, patched);
                     return std::make_pair(patched->code.data(), static_cast<uint32_t>(patched->code.size()));
                  }
                  return std::nullopt;
               });
            if (injected)
            {
               patched_subobjects = built;
            }
         }

         Shader::DestroyPipelineSubojects(subobjects_copy, subobject_count);

         if (patched_subobjects == nullptr) continue;
         DeviceData::AsyncCloneEntry entry{pipeline_handle, nullptr, 0, {}, {}, device, shader_hash, clone_origin, std::move(custom_pipeline_data)
#if LUMA_HAS_RECIPE_PROVIDERS
            , generation
#endif
         };
#if LUMA_ASYNC_CLONE_MODE == 1
         // Hand the build to the present boundary, which creates the pipeline
         // on the render thread (bounded per frame by
         // LUMA_ASYNC_CLONE_PRESENT_BUDGET).
         entry.subobjects = patched_subobjects;
         entry.subobject_count = subobject_count;
         entry.layout = layout;
#elif LUMA_ASYNC_CLONE_MODE == 2
         // Device call on the worker thread: fastest on paper,
         // but NVIDIA driver may block on Create*Shaders
         const bool created = device->create_pipeline(layout, subobject_count, patched_subobjects, &entry.pipeline_clone);
         Shader::DestroyPipelineSubojects(patched_subobjects, subobject_count);
         if (!created || entry.pipeline_clone.handle == 0) continue;
#endif // LUMA_ASYNC_CLONE_MODE
#if DEVELOPMENT
#if LUMA_ASYNC_CLONE_MODE == 1
         reshade::log::message(reshade::log::level::debug,
            std::format("[AsyncClone] queued clone build for pipeline {:x}", pipeline_handle).c_str());
#else
         reshade::log::message(reshade::log::level::debug,
            std::format("[AsyncClone] compiled clone {:x} for pipeline {:x}", entry.pipeline_clone.handle, pipeline_handle).c_str());
#endif
#endif
         const std::lock_guard lock_jobs(device_data.async_jobs_mutex);
         device_data.async_ready_queue.push_back(std::move(entry));
      }
   }

#if LUMA_ASYNC_CLONE_MODE == 1
   // Creates and registers pending clones at the present boundary: re-resolves
   // each pipeline handle under s_mutex_generic (unique) and registers the
   // clone, or drops it if the pipeline was destroyed / already cloned in the
   // meantime. Runs strictly between frames — the live clone map never mutates
   // mid-frame. At most LUMA_ASYNC_CLONE_PRESENT_BUDGET clones are created per
   // present; the rest stay queued for the next frame (spreads the device
   // work, removing the hitch from a giant one-frame batch).
#elif LUMA_ASYNC_CLONE_MODE == 2
   // Registers worker-built clones at the present boundary: re-resolves each
   // pipeline handle under s_mutex_generic (unique) and registers the clone,
   // or destroys it if the pipeline was destroyed / already cloned in the
   // meantime. Runs strictly between frames — the live clone map never mutates
   // mid-frame.
#endif
   //
   // Drain under the jobs mutex, then process the detached entries without it.
   void PublishReadyAsyncClones(DeviceData& device_data)
   {
      std::deque<DeviceData::AsyncCloneEntry> ready;
      {
         const std::lock_guard lock_jobs(device_data.async_jobs_mutex);
#if LUMA_ASYNC_CLONE_MODE == 1
         // Budget-gated drain: spread device work across frames.
         const size_t budget = LUMA_ASYNC_CLONE_PRESENT_BUDGET == 0 ? device_data.async_ready_queue.size()
            : std::min<size_t>(LUMA_ASYNC_CLONE_PRESENT_BUDGET, device_data.async_ready_queue.size());
         for (size_t i = 0; i < budget; ++i)
         {
            ready.push_back(std::move(device_data.async_ready_queue.front()));
            device_data.async_ready_queue.pop_front();
         }
#else
         // Full drain: all worker-compiled clones are ready.
         ready.swap(device_data.async_ready_queue);
#endif
      }
      if (ready.empty())
      {
         return;
      }

      // The game recycles pipeline handles heavily (destroy + recreate at the
      // same address), so every registration re-verifies the shader identity —
      // otherwise the wrong shader would run (possible GPU fault under churn).
      auto IsValidTarget = [&device_data](const DeviceData::AsyncCloneEntry& entry) -> Shader::CachedPipeline*
      {
         const auto pipeline_it = device_data.pipeline_cache_by_pipeline_handle.find(entry.pipeline_handle);
         if (pipeline_it == device_data.pipeline_cache_by_pipeline_handle.end() || pipeline_it->second == nullptr || pipeline_it->second->manually_unloaded || pipeline_it->second->cloned
             || (patches_unloaded && entry.custom_pipeline_data.patch_stages != reshade::api::shader_stage(0))
#if LUMA_HAS_RECIPE_PROVIDERS
             || (entry.custom_pipeline_data.patch_data && entry.generation != device_data.patch_context.reload_generation)
#endif
             || pipeline_it->second->shader_hashes[0] != entry.shader_hash
             // A preserved patch clone still owns its object; the fresh clone would overwrite and leak it.
             || pipeline_it->second->pipeline_clone.handle != 0)
         {
            return nullptr;
         }
         return pipeline_it->second;
      };

      // Verify before creating/registering (unique lock), then create outside
      // the lock: device calls must not happen while holding s_mutex_generic
      // (same pattern as the destroy path).
      std::vector<std::pair<reshade::api::device*, reshade::api::pipeline>> orphans;
      std::vector<std::pair<DeviceData::AsyncCloneEntry, Shader::CachedPipeline*>> verified;
      {
         const std::unique_lock lock_generic(s_mutex_generic);
         for (const auto& entry : ready)
         {
            if (Shader::CachedPipeline* cached_pipeline = IsValidTarget(entry); cached_pipeline != nullptr)
            {
               verified.emplace_back(entry, cached_pipeline);
            }
#if LUMA_ASYNC_CLONE_MODE == 2
            else if (entry.pipeline_clone.handle != 0)
            {
               orphans.emplace_back(entry.device, entry.pipeline_clone);
            }
#endif
         }
      }

#if LUMA_ASYNC_CLONE_MODE == 1
      // Create pipelines from subobjects on the render thread.
      std::vector<std::pair<reshade::api::pipeline, std::pair<DeviceData::AsyncCloneEntry, Shader::CachedPipeline*>>> created;
      for (const auto& v : verified)
      {
         const DeviceData::AsyncCloneEntry& entry = v.first;
         reshade::api::pipeline pipeline_clone = {};
         if (entry.device->create_pipeline(entry.layout, entry.subobject_count, entry.subobjects, &pipeline_clone))
         {
            created.emplace_back(pipeline_clone, v);
         }
      }
#else
      // Mode 2: pipelines already compiled by worker — just collect them.
      std::vector<std::pair<reshade::api::pipeline, std::pair<DeviceData::AsyncCloneEntry, Shader::CachedPipeline*>>> created;
      for (const auto& v : verified)
      {
         created.emplace_back(v.first.pipeline_clone, v);
      }
#endif

      {
         const std::unique_lock lock_generic(s_mutex_generic);
         for (const auto& [clone, v] : created)
         {
            const DeviceData::AsyncCloneEntry& entry = v.first;
            // The pipeline may have been destroyed/recycled since the verify
            // above — re-check before registering.
            CachedPipeline* cached_pipeline = IsValidTarget(entry);
            if (cached_pipeline == nullptr || cached_pipeline != v.second)
            {
               orphans.emplace_back(entry.device, clone);
               continue;
            }
            cached_pipeline->pipeline_clone = clone;
            cached_pipeline->cloned = true;
            cached_pipeline->patch_application_mode = Shader::PatchApplicationMode::Async;
            cached_pipeline->clone_origin = entry.clone_origin;
            cached_pipeline->custom_pipeline_data = entry.custom_pipeline_data;
#if LUMA_USE_DXP
            if (cached_pipeline->custom_pipeline_data.patch_data && (clone.handle & 1) == 0
                && cached_pipeline->device->get_api() == reshade::api::device_api::d3d11)
            {
               // Keep clone ownership with its reports so old command lists can switch variants after reload.
               cached_pipeline->custom_pipeline_data.patch_data->native_pipeline_clone.reset(reinterpret_cast<ID3D11DeviceChild*>(clone.handle));
            }
#endif
            device_data.pipeline_cache_by_pipeline_clone_handle[clone.handle] = cached_pipeline;

            if (entry.clone_origin == Shader::CloneOrigin::Patch)
            {
               const uint32_t shader_hash = cached_pipeline->shader_hashes[0];
               if (auto patched = Patch::GetShaderData(device_data, shader_hash); patched)
               {
                  if (auto it = shader_cache.find(shader_hash); it != shader_cache.end() && it->second)
                  {
#if DEVELOPMENT
                     SetLivePatchedShaderInfo(it->second, shader_hash, patched->code.data(), static_cast<uint32_t>(patched->code.size()));
#endif
#if ALLOW_SHADER_PATCHES_DUMPING
                     QueuePatchedShaderForDump(shader_hash);
#endif
                  }
               }
            }
            device_data.cloned_pipeline_count++;
            device_data.cloned_pipelines_changed = true;
#if DEVELOPMENT
            reshade::log::message(reshade::log::level::debug,
               std::format("[AsyncClone] published clone {:x} for pipeline {:x}", clone.handle, entry.pipeline_handle).c_str());
#endif
         }
      }

#if LUMA_ASYNC_CLONE_MODE == 1
      // Free the worker's subobject copies (whether created or not).
      for (const auto& entry : ready)
      {
         Shader::DestroyPipelineSubojects(entry.subobjects, entry.subobject_count);
      }
#endif

      // Device calls must not happen while holding s_mutex_generic (same
      // pattern as the destroy path).
      for (const auto& [device, clone] : orphans)
      {
         device->destroy_pipeline(clone);
      }
   }
#endif

#if LUMA_USE_DXP
   bool ResolveRecipeBindings(DeviceData& device_data, const Shader::CustomPipelineData& data, reshade::api::pipeline_subobject_type type,
      std::vector<RecipeBinding>& bindings, bool& has_recipe)
   {
      if (!data.patch_data) return true;
      const size_t first_binding = bindings.size();
      bool resolved_all = true;
      for (const auto& result : data.patch_data->recipe_results)
      {
         if (result->stage != type) continue;
         has_recipe = true;
         for (const auto& [handle, binding] : result->report.new_bindings)
         {
            RecipeBinding resolved;
            if (ResolveRecipeBinding(device_data, *result, handle, binding, resolved))
            {
               bindings.push_back(std::move(resolved));
               continue;
            }
            resolved_all = false;
            static thread_local std::unordered_set<std::string> unresolved;
            if (unresolved.emplace(result->identity + ":" + handle).second)
            {
               reshade::log::message(reshade::log::level::warning,
                  std::format("[Luma] Recipe {} resource '{}' is not available (in \"managed_resources\"), drawing with the original shader", result->identity, handle).c_str());
            }
         }
      }
      if (!resolved_all) bindings.resize(first_binding);
      return resolved_all;
   }

   void SetRecipeBindings(ID3D11DeviceContext* context, CommandListData& cmd_list_data, const std::vector<RecipeBinding>& bindings)
   {
      for (const RecipeBinding& binding : bindings) SetRecipeBinding(context, cmd_list_data, binding);
   }
#endif
} // namespace Patch
