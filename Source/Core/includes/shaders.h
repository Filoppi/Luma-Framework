#pragma once

#include "shader_define.h"

// Forward declarations
enum class ShaderReplaceDrawType;
enum class ShaderCustomDepthStencilType;

namespace Shader
{
   constexpr uint8_t meta_version = 2;

   // Only the ones ever compatible with Luma
   enum class Stage
   {
      Vertex,
      // Requires "GEOMETRY_SHADER_SUPPORT" for full support.
      Geometry,
      Pixel,
      Compute,

      Count,
   };

   static const std::string template_geometry_shader_name = "gs_";
   static const std::string template_vertex_shader_name = "vs_";
   static const std::string template_pixel_shader_name = "ps_";
   static const std::string template_compute_shader_name = "cs_";

   // How a shader patch was applied to this pipeline (see the Patch module).
   // TODO(Patch module): could move to patch.hpp later (scoped enums with a
   // fixed underlying type are forward-declarable, so shaders.h could keep the
   // field with just a forward declaration) once headers are self-contained.
   enum class PatchApplicationMode : uint8_t
   {
      None = 0,      // No patch applied
      Inplace = 1,   // Applied in-place at shader creation (sync, INPLACE mode)
      Sync = 2,      // Applied via a pipeline clone created synchronously at pipeline init (CLONE mode)
      Async = 3,     // Applied via a pipeline clone created on the background thread
   };

   // Which shader variant is (or should be) bound.
   enum class ShaderVariant : uint8_t
   {
      Original = 0,  // the game's original shader
      Patched  = 1,  // the pipeline clone (patch applied)
   };

   // What a pipeline's clone was built from (set at clone creation, reset before
   // each re-clone). Exactly one source per clone: a file wins over a patch on
   // the same hash. Used to preserve patch clones across unloads and to tell
   // patch toggles from file swaps.
   enum class CloneOrigin : uint8_t
   {
      None = 0,
      File = 1,
      Patch = 2,
   };

   // Mostly hardcoded to match a shader object, but it can work for other ReShade pipelines as well
   struct CachedPipeline
   {
      // Original pipeline (in DX9/10/11 it's just a ptr to a shader object, in DX12 to a PSO). Lifetime not handled by us.
      reshade::api::pipeline pipeline;
      // Cached device (makes it easier to access, even if there's usually only a global one in games (e.g. Prey))
      reshade::api::device* device; // TODO: delete, this isn't necessary, they are cached by device
      reshade::api::pipeline_layout layout; // DX12 stuff
      // Cloned subojects from the original pipeline (e.g. for shaders, this is their code/blob). Need to be destroyed when the original pipeline is.
      reshade::api::pipeline_subobject* subobjects_cache;
#if DX12
      uint32_t subobject_count;
#else // Always 1 if not in DX12
      static constexpr uint32_t subobject_count = 1;
#endif
      // True if we cloned it and "replaced" it with custom shaders
      bool cloned = false;
      // Needs to be destroyed when the original pipeline is.
      reshade::api::pipeline pipeline_clone;
      // How the patch was applied to this pipeline (for the frame capture view: "#", "#S", "#A")
      PatchApplicationMode patch_application_mode = PatchApplicationMode::None;
      // What the current clone was built from (set at clone creation, reset
      // before each re-clone). Preserves patch clones across unloads; a file
      // wins over a patch on the same clone.
      CloneOrigin clone_origin = CloneOrigin::None;
      // Original shaders hash (there should only be one except in DX12)
#if DX12
      std::vector<uint32_t> shader_hashes;
#else
      std::array<uint32_t, 1> shader_hashes;
#endif

#if DEVELOPMENT
      // Custom temp identifier for faster tracking
      std::string custom_name;

      static constexpr const char* shader_replace_draw_type_names[] = { "None", "Skip Draw", "Draw Purple", "Draw NaN" };
      ShaderReplaceDrawType replace_draw_type = ShaderReplaceDrawType(0); // Pixel and Compute Shaders only

      static constexpr const char* shader_custom_depth_stencil_type_names[] = { "None", "Depth Test/Write Disabled + Stencil Disabled", "Depth Test Disabled Write Enabled + Stencil Disabled" };
      ShaderCustomDepthStencilType custom_depth_stencil = ShaderCustomDepthStencilType(0); // Pixel Shader only

      struct RedirectData
      {
			enum class RedirectSourceType : uint8_t
         {
            None = 0,
            SRV = 1,
            UAV = 2,
         };
         RedirectSourceType source_type = RedirectSourceType::None;
         int source_index = 0;
         enum class RedirectTargetType : uint8_t
         {
            None = 0,
            RTV = 1,
            UAV = 2,
         };
         RedirectTargetType target_type = RedirectTargetType::None;
         int target_index = 0;
      } redirect_data;
#endif

      bool HasGeometryShader() const
      {
         for (uint32_t i = 0; i < subobject_count; i++)
         {
            if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::geometry_shader) return true;
         }
         return false;
      }
      bool HasVertexShader() const
      {
         for (uint32_t i = 0; i < subobject_count; i++)
         {
            if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::vertex_shader) return true;
         }
         return false;
      }
      bool HasPixelShader() const
      {
         for (uint32_t i = 0; i < subobject_count; i++)
         {
            if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::pixel_shader) return true;
         }
         return false;
      }
      bool HasComputeShader() const
      {
         for (uint32_t i = 0; i < subobject_count; i++)
         {
            if (subobjects_cache[i].type == reshade::api::pipeline_subobject_type::compute_shader) return true;
         }
         return false;
      }
   };

   struct CachedShader
   {
#if ALLOW_SHADERS_DUMPING || DEVELOPMENT
      void* data = nullptr; // Shader binary, allocated by ourselves. A copy of the original shader data. Immutable once set.
      size_t size = 0;
#endif
      reshade::api::pipeline_subobject_type type = reshade::api::pipeline_subobject_type::unknown;

#if ALLOW_SHADERS_DUMPING || DEVELOPMENT
      std::string type_and_version;
      std::string disasm;
#if DEVELOPMENT
      std::string live_patched_disasm; // Only valid if "live_patched_data" also is
#endif
#endif

#if DEVELOPMENT
      // Reflections data (update "meta_version" if you change these):
      bool found_reflections = false;
      // Constant Buffers
      bool cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
      // Samplers
      bool samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
      // Render Target Views
      bool rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      // Shader Resource Views
      bool srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
      // Unordered Access Views
      bool uavs[D3D11_1_UAV_SLOT_COUNT] = {};

      // Own storage for live patched shader bytes (e.g. DXP clone injection), so debug UI pointers stay valid.
      std::vector<uint8_t> live_patched_owned_data;
      const void* live_patched_data = nullptr; // Usually nullptr. Valid if we edited the byte code of the shader live when it was created (including shader debug data stripped)
      size_t live_patched_size = 0;
#endif

#if ALLOW_SHADERS_DUMPING || DEVELOPMENT
      ~CachedShader()
      {
         free(data);
         data = nullptr;
      }
#endif
   };

   // Defines a "permutation" of a luma native shader we load and compile from disk
   struct ShaderDefinition
   {
      const char* file_name = nullptr; // What shader code file to load from disk
      reshade::api::pipeline_subobject_type type = reshade::api::pipeline_subobject_type::unknown; // pixel, vertex etc shader
      const char* shader_target_version = nullptr; // E.g. "5_0" (do not include "ps_" or "cs_" etc). Defaults to the most appropriate version for the current API.
      const char* function_name = nullptr; // Will fall back to (e.g.) "main" if not specified
      std::vector<SimplerShaderDefine> defines_data; // Extra defines data this shader can have on top of the global ones
   };

   struct CachedCustomShader
   {
      std::vector<uint8_t> code;
      bool is_hlsl = false;
      bool is_luma_native = false; // If false, the shader is game specific, if true, the shader is native to Luma (belonging to Luma's own shaders, whether globally for all mods or in a single specific game mod)
      std::filesystem::path file_path; // This should point to the source hlsl wherever possible (or a cso blob as fallback). Note that if the file name contains multiple hashes, it might not be a unique name.
      std::size_t preprocessed_hash = 0; // A value of 0 won't ever be generated by the hash algorithm (this does not necessarily match the final shader binary hash)
      std::string compilation_errors; // Compilation errors and warnings log
#if DEVELOPMENT || TEST
      bool compilation_error = false;
      std::string preprocessed_code;
      std::string disasm;
#if DEVELOPMENT
      ShaderDefinition definition; // Might be empty in non luma native shaders
#endif
#endif
   };

   // When we only have one element, set it to 64 bits to we can set it to UINT64_MAX to highlight the hash is invalid/none
   template <bool SingleShaderHashes>
   using ShaderHashesType = std::conditional_t<SingleShaderHashes, uint64_t, uint32_t>;

   template <bool SingleShaderHashes>
   using ShaderHashesContainer = std::conditional_t<SingleShaderHashes,
      std::array<ShaderHashesType<SingleShaderHashes>, 1>,
      std::unordered_set<ShaderHashesType<SingleShaderHashes>>>;

   template <typename T1, typename T2>
   bool ShaderHashesContains(const std::unordered_set<T1>& container, const T2& value)
   {
      return container.contains(value);
   }
   template <typename T1, typename T2, std::size_t N>
   bool ShaderHashesContains(const std::array<T1, N>& container, const T2& value)
   {
      ASSERT_ONCE(container[0] != UINT64_MAX || value != UINT64_MAX); // If both are UINT64_MAX, something is wrong!... We need to return false for that case!
      static_assert(N == 1, "Only supports std::array<T, 1>");
      return container[0] == value;
   }

   template <bool SingleShaderHashes = false>
   struct ShaderHashesList
   {
      ShaderHashesContainer<SingleShaderHashes> pixel_shaders;
      ShaderHashesContainer<SingleShaderHashes> vertex_shaders;
#if GEOMETRY_SHADER_SUPPORT
      ShaderHashesContainer<SingleShaderHashes> geometry_shaders;
#endif
      ShaderHashesContainer<SingleShaderHashes> compute_shaders;

      bool Contains(uint32_t shader_hash, reshade::api::shader_stage shader_stage) const
      {
         // NOTE: we could probably check if the value matches a specific shader stage (e.g. a switch?), but I'm not 100% sure other flags are ever set
         if ((shader_stage & reshade::api::shader_stage::pixel) != 0)
         {
            if (ShaderHashesContains(pixel_shaders, shader_hash)) return true;
         }
         if ((shader_stage & reshade::api::shader_stage::vertex) != 0)
         {
            if (ShaderHashesContains(vertex_shaders, shader_hash)) return true;
         }
#if GEOMETRY_SHADER_SUPPORT
         if ((shader_stage & reshade::api::shader_stage::geometry) != 0)
         {
            if (ShaderHashesContains(geometry_shaders, shader_hash)) return true;
         }
#endif
         if ((shader_stage & reshade::api::shader_stage::compute) != 0)
         {
            return ShaderHashesContains(compute_shaders, shader_hash);
         }
         return false;
      }
      template <bool OtherSingleShaderHashes>
      bool Contains(const ShaderHashesList<OtherSingleShaderHashes>& other) const
      {
         for (const ShaderHashesType<OtherSingleShaderHashes> shader_hash : other.pixel_shaders)
         {
            if (ShaderHashesContains(pixel_shaders, shader_hash))
            {
               return true;
            }
         }
         for (const ShaderHashesType<OtherSingleShaderHashes> shader_hash : other.vertex_shaders)
         {
            if (ShaderHashesContains(vertex_shaders, shader_hash))
            {
               return true;
            }
         }
#if GEOMETRY_SHADER_SUPPORT
         for (const ShaderHashesType<OtherSingleShaderHashes> shader_hash : other.geometry_shaders)
         {
            if (ShaderHashesContains(geometry_shaders, shader_hash))
            {
               return true;
            }
         }
#endif
         for (const ShaderHashesType<OtherSingleShaderHashes> shader_hash : other.compute_shaders)
         {
            if (ShaderHashesContains(compute_shaders, shader_hash))
            {
               return true;
            }
         }
         return false;
      }
      bool Empty() const
      {
         if constexpr (SingleShaderHashes)
         {
            return pixel_shaders[0] == UINT64_MAX && vertex_shaders[0] == UINT64_MAX && compute_shaders[0] == UINT64_MAX
#if GEOMETRY_SHADER_SUPPORT
               && geometry_shaders[0] == UINT64_MAX
#endif
               ;
         }
         else
         {
            // Values are expected to be non null if the array has element
            return pixel_shaders.empty() && vertex_shaders.empty() && compute_shaders.empty()
#if GEOMETRY_SHADER_SUPPORT
               && geometry_shaders.empty()
#endif
               ;
         }
      }
      bool HasAny(reshade::api::shader_stage shader_stage) const
      {
         if ((shader_stage & reshade::api::shader_stage::pixel) != 0)
         {
            if constexpr (SingleShaderHashes)
            {
               if (pixel_shaders[0] != UINT64_MAX)
                  return true;
            }
            else
            {
               if (!pixel_shaders.empty())
                  return true;
            }
         }
         if ((shader_stage & reshade::api::shader_stage::vertex) != 0)
         {
            if constexpr (SingleShaderHashes)
            {
               if (vertex_shaders[0] != UINT64_MAX)
                  return true;
            }
            else
            {
               if (!vertex_shaders.empty())
                  return true;
            }
         }
#if GEOMETRY_SHADER_SUPPORT
         if ((shader_stage & reshade::api::shader_stage::geometry) != 0)
         {
            if constexpr (SingleShaderHashes)
            {
               if (geometry_shaders[0] != UINT64_MAX)
                  return true;
            }
            else
            {
               if (!geometry_shaders.empty())
                  return true;
            }
         }
#endif
         if ((shader_stage & reshade::api::shader_stage::compute) != 0)
         {
            if constexpr (SingleShaderHashes)
            {
               if (compute_shaders[0] != UINT64_MAX)
                  return true;
            }
            else
            {
               if (!compute_shaders.empty())
                  return true;
            }
         }
         return false;
      }
      void Clear()
      {
         if constexpr (SingleShaderHashes)
         {
            pixel_shaders[0] = UINT64_MAX;
            vertex_shaders[0] = UINT64_MAX;
#if GEOMETRY_SHADER_SUPPORT
            geometry_shaders[0] = UINT64_MAX;
#endif
            compute_shaders[0] = UINT64_MAX;
         }
         else
         {
            pixel_shaders.clear();
            vertex_shaders.clear();
#if GEOMETRY_SHADER_SUPPORT
            geometry_shaders.clear();
#endif
            compute_shaders.clear();
         }
      }
   };

   uint32_t BinToHash(const uint8_t* bin, size_t size)
   {
      return compute_crc32(reinterpret_cast<const uint8_t*>(bin), size);
   }
   uint32_t StrToHash(const std::string& str)
   {
      return compute_crc32(reinterpret_cast<const uint8_t*>(str.data()), str.size());
   }
   uint32_t StrToHash(std::string_view str)
   {
      return compute_crc32(reinterpret_cast<const uint8_t*>(str.data()), str.size());
   }
   uint64_t ShiftHash32ToHash64(uint32_t hash)
   {
      return static_cast<uint64_t>(hash) << 32;
   }

	// Hash is meant to be 8 hex characters long (32 bits)
	// Note that these can fail to put them around a try catch if you need to handle the exception
   uint32_t Hash_StrToNum(const char* hash_hex_string)
   {
      assert(strlen(hash_hex_string) == 8 /*HASH_CHARACTERS_LENGTH*/);
      return std::stoul(hash_hex_string, nullptr, 16);
   }
   uint32_t Hash_StrToNum(const std::string& hash_hex_string)
   {
      assert(hash_hex_string.length() == 8 /*HASH_CHARACTERS_LENGTH*/);
      return std::stoul(hash_hex_string, nullptr, 16);
   }
   std::string Hash_NumToStr(uint32_t hash, bool hex_prefix = false)
   {
#if 0 // Old school method with hex prefix (upper case)
      wchar_t hash_string[11];
      swprintf_s(hash_string, L"0x%08X", hash);
#endif
      if (hex_prefix)
      {
         return std::format("{}{:08X}", "0x", hash); // Somehow formatting "0x{:08X}" directly removes the first zero from the hash string
      }
      return std::format("{:08X}", hash); // big "X" to return capital letters
   }

   // TODO: add this around the code instead of uint32_t
   struct ShaderHash
   {
      explicit ShaderHash(const std::string& str) { hash = Hash_StrToNum(str); }
      explicit ShaderHash(const char* str) { hash = Hash_StrToNum(str); }
      explicit ShaderHash(uint32_t _hash) : hash(_hash) {}

      explicit operator uint32_t() const { return hash; }

      std::string ToString(bool hex_prefix = false) const { return Hash_NumToStr(hash, hex_prefix); }

      uint32_t hash;
   };

   // For "target" we mean a version identifier
   reshade::api::pipeline_subobject_type ShaderTargetToType(const std::string& target)
   {
      if (target.starts_with(template_pixel_shader_name))
      {
         return reshade::api::pipeline_subobject_type::pixel_shader;
      }
      else if (target.starts_with(template_vertex_shader_name))
      {
         return reshade::api::pipeline_subobject_type::vertex_shader;
      }
      else if (target.starts_with(template_compute_shader_name))
      {
         return reshade::api::pipeline_subobject_type::compute_shader;
      }
#if GEOMETRY_SHADER_SUPPORT
      else if (target.starts_with(template_geometry_shader_name))
      {
         return reshade::api::pipeline_subobject_type::geometry_shader;
      }
#endif
      ASSERT_ONCE(false);
      return reshade::api::pipeline_subobject_type::unknown;
   }

   // Doesn't return the version, just the shader model part, including "_" after it (e.g. "ps_")
   std::string_view ShaderTypeToTarget(reshade::api::pipeline_subobject_type type)
   {
      std::string_view template_shader_name;
      switch (type)
      {
      case reshade::api::pipeline_subobject_type::vertex_shader:
      {
         template_shader_name = template_vertex_shader_name;
         break;
      }
      case reshade::api::pipeline_subobject_type::geometry_shader:
      {
         template_shader_name = template_geometry_shader_name;
         break;
      }
      case reshade::api::pipeline_subobject_type::pixel_shader:
      {
         template_shader_name = template_pixel_shader_name;
         break;
      }
      case reshade::api::pipeline_subobject_type::compute_shader:
      {
         template_shader_name = template_compute_shader_name;
         break;
      }
      default:
      {
         template_shader_name = "xs_"; // Unknown
         break;
      }
      }
      return template_shader_name;
   }
}
