#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
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
// Toggling recipes at runtime and falling back to the original shaders at draw
// time need a clone-based mode (CLONE=1 or an async provider); INPLACE patches are always-on.
//
// Library availability is separate (LUMA_USE_DXP, from the Luma props when
// UseLumaDXP=true). DXP games default to async recipes unless a sync recipe
// or async bytecode provider is selected.
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
#define LUMA_PATCH_RECIPE_ASYNC (LUMA_USE_DXP && !LUMA_PATCH_RECIPE_SYNC && !LUMA_PATCH_BYTECODE_ASYNC)
#endif
#ifndef LUMA_PATCH_SYNC_MODE_CLONE
#define LUMA_PATCH_SYNC_MODE_CLONE LUMA_PATCH_RECIPE_SYNC
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
#if LUMA_PATCH_RECIPE_SYNC && !LUMA_PATCH_SYNC_MODE_CLONE
#error "Synchronous recipes require LUMA_PATCH_SYNC_MODE_CLONE for live reload"
#endif

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
// also adds the dxp include/link paths.
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
#include <dxp/sm5/Recipe.hpp>
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

#if LUMA_USE_DXP
   struct RecipeResult
   {
      uint32_t shader_hash = 0;
      uint32_t recipe_hash = 0;
      std::string identity;
      reshade::api::pipeline_subobject_type stage = reshade::api::pipeline_subobject_type::unknown;
      dxp::RecipeReport report;
   };

   // The patch part of a clone ("Shader::CustomPipelineData::patch_data"), filled while the clone is built.
   struct ClonePatchData
   {
      // Keeps the clone alive with its reports, so command lists that bound it can still use it after a reload
      com_ptr<ID3D11DeviceChild> native_pipeline_clone;
      std::vector<std::shared_ptr<const RecipeResult>> recipe_results;
   };
#endif

   // A stored patch: patched container bytes + provider metadata.
   struct PatchedShaderData
   {
      std::vector<uint8_t> code;
      Method method = Method::Bytecode;
      std::optional<Hash::MD5::Digest> md5; // Bytecode method: MD5 of the patched container (debug)
#if LUMA_USE_DXP
      // Recipe method: one report per recipe that modified the shader, in execution order.
      std::vector<std::shared_ptr<const RecipeResult>> recipe_results;
#endif
   };

#if LUMA_HAS_RECIPE_PROVIDERS
   // In which builds a recipe (or one of its options) is shown in the "Patch Settings" tab.
   enum class RecipeVisibility : uint8_t
   {
      Development, // "DEVELOPMENT" builds only
      Test,        // "DEVELOPMENT" and "TEST" builds
      Always,      // All builds (users too)
   };

   // A recipe env variable the game exposes. Its type and default come from the recipe's "env:" block.
   // Variables not listed here are only shown in development builds.
   struct RecipeOption
   {
      std::string env;
      std::string label;
      std::string tooltip;
      RecipeVisibility visibility = RecipeVisibility::Always;
   };

   // Game-side description of a recipe (by its identity, e.g. "PS/dithering_fix.recipe.yml").
   // Unregistered recipes still load, enabled, and are only shown in development builds.
   struct RecipeRegistration
   {
      std::string identity;
      std::string display_name;
      std::string description;
      bool default_enabled = true;
      RecipeVisibility visibility = RecipeVisibility::Development;
      std::vector<RecipeOption> options;
   };

   // Global (all devices) recipe settings, saved in the "<NAME> Recipes" config section.
   // Changes are applied at the next present by re-patching lazily (see "ApplyRecipeSettingsChanges").
   // Register before devices are created (e.g. in "Game::OnInit").
   void RegisterRecipe(RecipeRegistration registration);
   bool IsRecipeEnabled(std::string_view identity);
   void SetRecipeEnabled(std::string_view identity, bool enabled);
   // User env override, or nullopt if the recipe's own (or the game's) value is used.
   std::optional<dxp::PrimitiveValue> GetRecipeEnv(std::string_view identity, const std::string& env);
   void SetRecipeEnv(std::string_view identity, const std::string& env, dxp::PrimitiveValue value);
   void ResetRecipeEnv(std::string_view identity, const std::string& env);
   std::optional<RecipeRegistration> GetRecipeRegistration(std::string_view identity);
   // Parses an env value with the type of the recipe's default value. Empty means no override.
   std::optional<dxp::PrimitiveValue> RecipeEnvFromString(std::string_view text, const dxp::PrimitiveValue& type_of);
#endif

   struct ShaderPatchRequest;

   // Thread-safe store of patches + per-method processed markers.
   struct PatchContext
   {
      std::unordered_map<uint32_t, std::shared_ptr<PatchedShaderData>> patched_shaders;
      std::array<std::unordered_set<uint32_t>, METHOD_COUNT> processed_shaders;
      // Debug-data-stripped containers by pre-strip hash (Heavy Rain-style
      // unification). Kept separately from patches: stripping is not a patch.
      std::unordered_map<uint32_t, std::shared_ptr<const std::vector<uint8_t>>> stripped_containers;
      mutable std::shared_mutex mutex;
#if LUMA_HAS_RECIPE_PROVIDERS
      struct RecipeEntry
      {
         uint32_t hash = 0;
         std::string identity;
         reshade::api::pipeline_subobject_type stage = reshade::api::pipeline_subobject_type::unknown;
         std::shared_ptr<const dxp::sm5::Recipe> recipe;
         // User settings, copied from the global recipe settings when the recipes are loaded.
         bool enabled = true;
         std::unordered_map<std::string, dxp::PrimitiveValue> env_overrides;
      };
      // Root folder recipes are loaded from: <root>/<GAME_NAME>/Recipes.
      std::filesystem::path recipes_path;
      // Recipes are immutable once parsed and safe to share across threads
      // (Execute is const and thread-safe); shared_ptr keeps async jobs valid.
      // Enabled recipes run chained in this (load) order.
      std::vector<RecipeEntry> recipes;
      // Game-provided env (not user facing), see "SetOptions".
      std::unordered_map<uint32_t, dxp::PatchOptions> recipe_options_by_hash;
      std::atomic<uint64_t> reload_generation = 0;
      // Global recipe settings version the recipes were loaded with.
      uint64_t applied_settings_version = 0;
#endif
   };

   // Request passed to recipe providers (full DXBC container).
   struct ShaderPatchRequest
   {
      reshade::api::pipeline_subobject_type type = reshade::api::pipeline_subobject_type::unknown;
      uint32_t shader_hash = uint32_t(-1);
      const std::byte* shader_container = nullptr;
      size_t shader_container_size = 0;
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
#if LUMA_HAS_RECIPE_PROVIDERS
      uint64_t generation = 0;
#endif
   };

#if LUMA_PATCH_PROVIDERS != 0
   inline bool HasPatch(const DeviceData& device_data, uint32_t shader_hash);

   // Returns true if newly inserted (not yet processed).
   inline bool SetProcessed(DeviceData& device_data, Method method, uint32_t shader_hash);

   inline std::shared_ptr<PatchedShaderData> GetShaderData(const DeviceData& device_data, uint32_t shader_hash);

   inline void StorePatched(DeviceData& device_data, uint32_t shader_hash, std::shared_ptr<PatchedShaderData> data);

   inline std::shared_ptr<const std::vector<uint8_t>> GetStrippedContainer(const DeviceData& device_data, uint32_t pre_strip_shader_hash);

   inline void StoreStrippedContainer(DeviceData& device_data, uint32_t pre_strip_shader_hash, std::shared_ptr<const std::vector<uint8_t>> data);

   template <typename TVisitor>
   inline void ForEachPatchedShader(const DeviceData& device_data, TVisitor&& visitor);

#if LUMA_HAS_RECIPE_PROVIDERS
   // Game-provided env for one recipe (e.g. offsets in Luma's cbuffers). Takes precedence over the user's env overrides.
   void SetOptions(DeviceData& device_data, uint32_t recipe_hash, dxp::PatchOptions options);
   // Reads the recipe files from disk (with the current user settings). Does not touch existing patches.
   void LoadRecipes(DeviceData& device_data);
   // Drops recipe patches and their clones, then loads the recipes again; pipelines re-patch on their next bind.
   void ReloadRecipes(DeviceData& device_data);
   // Reloads the recipes if their settings changed since they were last loaded (call at present).
   void ApplyRecipeSettingsChanges(DeviceData& device_data);
   // ImGui ("Patch Settings" tab): enable toggles and env variables of the recipes, filtered by their visibility for this build.
   void DrawRecipeSettings(const DeviceData& device_data);
#endif

   // Runs the sync providers for one shader (returns a patch, or nullptr).
   // The caller stores bytecode patches after validation. Providers are marked processed on any definitive
   // outcome. Recipes can re-run after reload. Implemented in patch.inl
   // (needs the full Game/DeviceData types).
   std::shared_ptr<PatchedShaderData> PatchShaderSync(Game& game, DeviceData& device_data, const ShaderPatchRequest& request, const ByteCodeView& view, uint32_t providers);

   void TryPatchPipeline(DeviceData& device_data, uint64_t pipeline_handle);
#endif // LUMA_PATCH_PROVIDERS != 0

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
          const reshade::api::shader_desc* orig_desc,
          reshade::api::pipeline_subobject_type type)> find_patch);

#if LUMA_USE_DXP
   // A resource a recipe added, resolved for the current draw.
   struct RecipeBinding
   {
      reshade::api::pipeline_subobject_type stage = reshade::api::pipeline_subobject_type::unknown;
      UINT slot = 0;
      com_ptr<ID3D11ShaderResourceView> srv;
      com_ptr<ID3D11Buffer> buffer;
      com_ptr<ID3D11SamplerState> sampler;
      com_ptr<ID3D11UnorderedAccessView> uav;
   };
   // Appends the resources the recipes of a clone's stage added: Luma's cbuffers or "managed_resources" entries named after
   // the recipe handles (games register theirs). Returns false if any is missing, the stage should then use its original shader.
   bool ResolveRecipeBindings(DeviceData& device_data, const Shader::CustomPipelineData& data, reshade::api::pipeline_subobject_type type,
      std::vector<RecipeBinding>& bindings, bool& has_recipe);
   void SetRecipeBindings(ID3D11DeviceContext* context, CommandListData& cmd_list_data, const std::vector<RecipeBinding>& bindings);
#endif

   void UnloadPatches(DeviceData& device_data);
   void ReloadPatches(DeviceData& device_data);
   uint32_t CountPatchedClones(const DeviceData& device_data);

#if LUMA_PATCH_PROVIDERS & (LUMA_PATCH_PROVIDER_BYTECODE_ASYNC | LUMA_PATCH_PROVIDER_RECIPE_ASYNC)
   // TODO(Patch module): job collection depends on core.hpp's globals
   // (s_mutex_generic, pipeline_cache_destruction_mutex). Keep its
   // implementation in patch.inl until those are extracted.
   void GeneratePatchedShadersAsync(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines_filter);
   void NotifyAsyncCloneQueue(DeviceData& device_data);
   void ProcessAsyncCloneBatch(DeviceData& device_data, const std::unordered_set<uint64_t>& pipelines);
   void PublishReadyAsyncClones(DeviceData& device_data);
#endif
} // namespace Patch
