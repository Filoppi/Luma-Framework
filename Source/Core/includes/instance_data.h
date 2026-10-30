#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "managed_resources.h"
#include "patch.hpp"
#include "trace.hpp"
#include "debug.h"
#include "resource_upgrades.hpp"

// Forward declarations
struct GameDeviceData;
struct CommandListData;
struct DeviceData;
namespace
{
   namespace
   {
      // Defined in "ResourceUpgradeManager" (also aliased in "core.hpp")
      using AutoTextureFormatUpgradeShaderHash = ResourceUpgradeManager::AutoTextureFormatUpgradeShaderHash;
   }
}

enum class DrawOrDispatchOverrideType
{
   None,
   Skip,
   Replaced,
};

// Same signature as "Game::OnDrawOrDispatch()". See "draw_callbacks_by_shader_hashes" and "dispatch_callbacks_by_shader_hashes".
using DrawOrDispatchCallback = DrawOrDispatchOverrideType(*)(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const Shader::ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func);

enum class ShaderReplaceDrawType
{
   None,
   // Skips the pixel or compute shader (this might draw black or leave the previous target textures value persisting, occasionally it can crash if the engine does weird things)
   Skip,
   // Tries to draw purple (magenta) instead. Doesn't always work (it will probably skip the shader if it doesn't, and send some warnings due to pixel and vertex shader signatures not matching)
   Purple,
   // Needs to be last (see "allow_replace_draw_nans")
   NaN,
   // TODO: Add a way to draw on a black render target to see the raw difference? Add a way to only draw 1 on alpha or RGB? Not very needed.
};
enum class ShaderCustomDepthStencilType
{
   None,
   IgnoreTestWriteDepth_IgnoreStencil,
   IgnoreTestDepth_IgnoreStencil,
};

// Applies to command lists and command queue (DirectX 11 command list and deferred or immediate contexts, though usually it's for "ID3D11DeviceContext").
// All runtime states are thread safe as long as they were in the original implementation.
struct __declspec(uuid("90d9d05b-fdf5-44ee-8650-3bfd0810667a")) CommandListData
{
   bool is_primary = false; // Immediate/Primary (as opposed to Async/Secondary/Deferred)

   CB::LumaInstanceDataPadded cb_luma_instance_data = {};
   // Always start from dirty given that deferred command lists inherit the cbuffers data from the immediate ones, but we don't know when they will get joined, so we always need to assume the data was dirty and needs to be re-set from scratch
   bool force_cb_luma_instance_data_dirty = true;
   bool async_set_cb_luma_instance_data_settings = false;

   // Whether the luma global settings have been set in this command list (device context), in case it was a deferred one
   bool async_set_cb_luma_global_settings = false;

   std::atomic<bool> write_finished{false};

   // Only used when checking for "ChainTextureFormatUpgradesType::DirectAndIndirectDependencies", as it might be disabled at runtime.
   uint enable_chain_indirect_texture_format_upgrades = 0; // TODO: ChainTextureFormatUpgradesType

   // Force resource scaling to output resolution for this command list (set during recording).
   // Used when has_drawn_sr isn't available during recording, potential issue if sr fails to draw.
   bool force_scale = false;

   reshade::api::pipeline pipeline_state_original_compute_shader = reshade::api::pipeline(0);
   reshade::api::pipeline pipeline_state_original_vertex_shader = reshade::api::pipeline(0);
   reshade::api::pipeline pipeline_state_original_pixel_shader = reshade::api::pipeline(0);

   Shader::ShaderHashesList<OneShaderPerPipeline> pipeline_state_original_graphics_shader_hashes;
   Shader::ShaderHashesList<OneShaderPerPipeline> pipeline_state_original_compute_shader_hashes;
   bool pipeline_state_has_custom_vertex_shader = false;
   bool pipeline_state_has_custom_pixel_shader = false;
   bool pipeline_state_has_custom_graphics_shader = false;
   bool pipeline_state_has_custom_compute_shader = false;
   Shader::CustomPipelineData custom_vertex_pipeline;
   Shader::CustomPipelineData custom_pixel_pipeline;
   Shader::CustomPipelineData custom_compute_pipeline;
   // Clones selected at bind time (0 = none). "pipeline_state_has_custom_*_shader" tells whether they are bound right now,
   // as a draw can fall back to the original shaders (see "Shader::UsePipelineVariant()").
   reshade::api::pipeline pipeline_state_custom_vertex_shader = reshade::api::pipeline(0);
   reshade::api::pipeline pipeline_state_custom_pixel_shader = reshade::api::pipeline(0);
   reshade::api::pipeline pipeline_state_custom_compute_shader = reshade::api::pipeline(0);

   // Determined by "draw_callbacks_by_shader_hashes" when binding shaders
   DrawOrDispatchCallback draw_callback = nullptr;
   // Determined by "dispatch_callbacks_by_shader_hashes" when binding shaders
   DrawOrDispatchCallback dispatch_callback = nullptr;

   // Pre-cached state to know if a target UI shader is bound, only relevant for the UI separation feature.
   bool is_known_ui_shader_bound = false;

   const AutoTextureFormatUpgradeShaderHash* auto_graphics_texture_format_upgrades = nullptr;
   const AutoTextureFormatUpgradeShaderHash* auto_compute_texture_format_upgrades = nullptr;

   enum ViewState
   {
      NotSet,
      // Could be null or valid
      Set,
      SetAndUpgraded,
   };

   // Note: views can be silently unbound by the API in DX11 if set to a new incompatible stage (e.g. can't set an SRV resource as RTV at the same time, the last set will win),
   // however, at worst we'll have false positives here, and we can always check for the current stage again.
   std::array<ViewState, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> ps_srvs_state = {};
   std::array<ViewState, D3D11_1_UAV_SLOT_COUNT> ps_uavs_state = {};
   std::array<ViewState, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> ps_rtvs_state = {};
   std::array<ViewState, 1> ps_dsvs_state = {};
   std::array<ViewState, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> cs_srvs_state = {};
   std::array<ViewState, D3D11_1_UAV_SLOT_COUNT> cs_uavs_state = {};
   bool any_upgraded_ps_srvs = false;
   bool any_upgraded_ps_uavs = false;
   bool any_upgraded_ps_rtvs = false;
   bool any_upgraded_ps_dsvs = false; // There's only one but we kept the name aligned
   bool any_upgraded_cs_srvs = false;
   bool any_upgraded_cs_uavs = false;

   // Whether we already scaled the currently bound viewports/scissors to match scaled (indirect upgraded) render targets.
   // Cleared whenever the game binds new ones (see "OnBindViewports()" and "OnBindScissorRects()"), so we never scale the same ones twice.
   bool viewports_scaled = false;
   bool scissors_scaled = false;

   void ResetUpgradedViews()
   {
      ps_srvs_state.fill(ViewState::NotSet);
      ps_uavs_state.fill(ViewState::NotSet);
      ps_rtvs_state.fill(ViewState::NotSet);
      ps_dsvs_state.fill(ViewState::NotSet);
      cs_srvs_state.fill(ViewState::NotSet);
      cs_uavs_state.fill(ViewState::NotSet);
      any_upgraded_ps_srvs = false;
      any_upgraded_ps_uavs = false;
      any_upgraded_ps_rtvs = false;
      any_upgraded_ps_dsvs = false;
      any_upgraded_cs_srvs = false;
      any_upgraded_cs_uavs = false;
   }
   void UpdateUpgradedPSSRVs()
   {
      any_upgraded_ps_srvs = std::any_of(ps_srvs_state.begin(), ps_srvs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }
   void UpdateUpgradedPSUAVs()
   {
      any_upgraded_ps_uavs = std::any_of(ps_uavs_state.begin(), ps_uavs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }
   void UpdateUpgradedPSRTVs()
   {
      any_upgraded_ps_rtvs = std::any_of(ps_rtvs_state.begin(), ps_rtvs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }
   void UpdateUpgradedPSDSVs()
   {
      any_upgraded_ps_dsvs = std::any_of(ps_dsvs_state.begin(), ps_dsvs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }
   void UpdateUpgradedCSSRVs()
   {
      any_upgraded_cs_srvs = std::any_of(cs_srvs_state.begin(), cs_srvs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }
   void UpdateUpgradedCSUAVs()
   {
      any_upgraded_cs_uavs = std::any_of(cs_uavs_state.begin(), cs_uavs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }

   static void SetConstantBuffers(ID3D11DeviceContext* device_context, reshade::api::shader_stage stages, uint32_t slot, ID3D11Buffer* const buffers, uint32_t num = 1)
   {
      if ((stages & reshade::api::shader_stage::vertex) == reshade::api::shader_stage::vertex)
         device_context->VSSetConstantBuffers(slot, num, &buffers);
      if ((stages & reshade::api::shader_stage::geometry) == reshade::api::shader_stage::geometry)
         device_context->GSSetConstantBuffers(slot, num, &buffers);
      if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
         device_context->PSSetConstantBuffers(slot, num, &buffers);
      if ((stages & reshade::api::shader_stage::compute) == reshade::api::shader_stage::compute)
         device_context->CSSetConstantBuffers(slot, num, &buffers);
   }

#if ENABLE_AUTO_CBUFFER_RESTORATION
   com_ptr<ID3D11Buffer> original_constant_buffers[(uint8_t)Shader::Stage::Count][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
   UINT original_constant_buffers_first_constant[(uint8_t)Shader::Stage::Count][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
   UINT original_constant_buffers_num_constant[(uint8_t)Shader::Stage::Count][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};

   void ClearOriginalConstantBuffers()
   {
      for (auto& original_constant_buffers_stage : original_constant_buffers)
         for (auto& original_constant_buffer : original_constant_buffers_stage)
            original_constant_buffer.reset();

      std::fill_n(&original_constant_buffers_first_constant[0][0], sizeof(original_constant_buffers_first_constant) / sizeof(UINT), 0);
      std::fill_n(&original_constant_buffers_num_constant[0][0], sizeof(original_constant_buffers_num_constant) / sizeof(UINT), 4096);
   }

   void RestoreOriginalConstantBuffers(ID3D11DeviceContext* device_context)
   {
      auto SetOriginalConstantBuffersStage = [&](ID3D11DeviceContext* device_context, bool use_device_context_1, Shader::Stage stage)
      {
         const size_t stage_index = (size_t)stage;
         ID3D11Buffer* const* original_constant_buffers_const = reinterpret_cast<ID3D11Buffer* const*>(original_constant_buffers[stage_index]);
         if (use_device_context_1)
         {
            if (stage == Shader::Stage::Vertex)
               static_cast<ID3D11DeviceContext1*>(device_context)->VSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const, original_constant_buffers_first_constant[stage_index], original_constant_buffers_num_constant[stage_index]);
            else if (stage == Shader::Stage::Geometry)
               static_cast<ID3D11DeviceContext1*>(device_context)->GSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const, original_constant_buffers_first_constant[stage_index], original_constant_buffers_num_constant[stage_index]);
            else if (stage == Shader::Stage::Pixel)
               static_cast<ID3D11DeviceContext1*>(device_context)->PSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const, original_constant_buffers_first_constant[stage_index], original_constant_buffers_num_constant[stage_index]);
            else if (stage == Shader::Stage::Compute)
               static_cast<ID3D11DeviceContext1*>(device_context)->CSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const, original_constant_buffers_first_constant[stage_index], original_constant_buffers_num_constant[stage_index]);
            return;
         }
         if (stage == Shader::Stage::Vertex)
            device_context->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const);
         else if (stage == Shader::Stage::Geometry)
            device_context->GSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const);
         else if (stage == Shader::Stage::Pixel)
            device_context->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const);
         else if (stage == Shader::Stage::Compute)
            device_context->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, original_constant_buffers_const);
      };

      com_ptr<ID3D11DeviceContext1> device_context_1;
      device_context->QueryInterface(&device_context_1);
      const bool use_device_context_1 = device_context_1 != nullptr;
      for (uint8_t stage = 0; stage < (uint8_t)Shader::Stage::Count; stage++)
      {
#if !GEOMETRY_SHADER_SUPPORT // Note: we assume the mod didn't modify geometry shaders bindings if "GEOMETRY_SHADER_SUPPORT" is false. It's an optimization.
         if (stage == (uint8_t)Shader::Stage::Geometry)
            continue;
#endif
         SetOriginalConstantBuffersStage(device_context, use_device_context_1, static_cast<Shader::Stage>(stage));
      }
   }
#endif // ENABLE_AUTO_CBUFFER_RESTORATION

#if DEVELOPMENT
   std::shared_mutex mutex_trace;
   std::vector<TraceDrawCallData> trace_draw_calls_data;
   // The entries of the draw being processed (first, count), "first" is SIZE_MAX outside of them (see "Trace::Draw()")
   std::pair<size_t, size_t> trace_current_draw = {SIZE_MAX, 0};

   bool requires_join = false;

   bool any_draw_done = false;
   bool any_dispatch_done = false;

   ShaderCustomDepthStencilType temp_custom_depth_stencil = ShaderCustomDepthStencilType::None;
#endif
};

struct __declspec(uuid("cfebf6d4-d184-4e1a-ac14-09d088e560ca")) DeviceData
{
   // Only for "swapchains", "back_buffers" and "upgraded_resources" (and related) and "patch_context".
   // Device object creation etc is usually single threaded anyway, except for the destructor.
   std::shared_mutex mutex;

   std::thread thread_auto_loading;
   std::atomic<bool> thread_auto_loading_running = false;

   // TODO(Patch module): move the patch_context member below and this async clone
   // machinery (AsyncCloneEntry + the LUMA_PATCH_PROVIDERS-gated members) into
   // patch.hpp once the include-order rework lands (see patch.inl).
#if LUMA_PATCH_PROVIDERS & (LUMA_PATCH_PROVIDER_BYTECODE_ASYNC | LUMA_PATCH_PROVIDER_RECIPE_ASYNC)
   // Unified async clone entry: mode 1 populates subobjects+layout (created at
   // present boundary), mode 2 populates pipeline_clone (created by worker).
   // Only one of the two paths is active per build configuration.
   struct AsyncCloneEntry
   {
      uint64_t pipeline_handle = 0;
      // Mode 1: subobjects built by worker, pipeline created at present boundary
      reshade::api::pipeline_subobject* subobjects = nullptr;
      uint32_t subobject_count = 0;
      reshade::api::pipeline_layout layout = {};
      // Mode 2: pipeline already compiled by worker
      reshade::api::pipeline pipeline_clone = {};
      // Shared
      reshade::api::device* device = nullptr;
      uint32_t shader_hash = 0; // shader the clone was built for (verify at publish)
      Shader::CloneOrigin clone_origin = Shader::CloneOrigin::None; // what the clone was built from (recorded by the worker)
      Shader::CustomPipelineData custom_pipeline_data;
#if LUMA_HAS_RECIPE_PROVIDERS
      uint64_t generation = 0;
#endif
   };
   // Ready queue shared by the worker and OnPresent. The jobs mutex protects
   // appending entries and detaching the queue for publication.
   std::deque<AsyncCloneEntry> async_ready_queue;

   std::atomic<bool> async_shutdown = false;
   std::mutex async_jobs_mutex;
   std::condition_variable async_jobs_cv;
   uint64_t async_queue_version = 0;
#endif // async providers

   // Resource upgrade / mirroring state and configuration (see resource_upgrades.hpp).
   // Owns the mirror bookkeeping and upgrade/scale configuration; core.hpp passes the frame's
   // resolution/SR state into its methods. Migrated subsystem-by-subsystem.
   ResourceUpgradeManager resource_upgrades;

#if LUMA_PATCH_PROVIDERS != 0
   // Stored shader patches (both bytecode and recipe methods) + per-method
   // processed markers. See Patch::PatchContext.
   Patch::PatchContext patch_context;
#endif

   std::unordered_set<reshade::api::swapchain*> swapchains;
   std::unordered_set<uint64_t> back_buffers; // From all the swapchains (whether they are upgraded or not)
   ID3D11Device* native_device = nullptr; // Doesn't need mutex, always valid given it's a ptr to "itself"
   reshade::api::device* reshade_device = nullptr; // Always valid as it's our parent. Not owned by this.
   com_ptr<ID3D11DeviceContext> primary_command_list; // The immediate/primary command list is always valid
   CommandListData* primary_command_list_data = nullptr; // The immediate/primary command list is always valid

   UINT uav_max_count = D3D11_1_UAV_SLOT_COUNT; // DX11.1. Use "D3D11_PS_CS_UAV_REGISTER_COUNT" for DX11.

   com_ptr<IDXGISwapChain3> GetMainNativeSwapchain() const
   {
      ASSERT_ONCE(swapchains.size() == 1);
      if (swapchains.empty()) return nullptr;
      IDXGISwapChain* native_swapchain = (IDXGISwapChain*)((*swapchains.begin())->get_native());
      com_ptr<IDXGISwapChain3> native_swapchain3;
      // The cast pointer is actually the same, we are just making sure the type is right.
      HRESULT hr = native_swapchain->QueryInterface(&native_swapchain3);
      ASSERT_ONCE(SUCCEEDED(hr));
      return native_swapchain3;
   }

   // Lock when removing a pipeline from "pipeline_cache_by_pipeline_handle" (and thus "pipeline_cache_by_pipeline_clone_handle", "pipeline_caches_by_shader_hash").
   // Only needs to also be locked if you don't already lock "s_mutex_generic" in other places, to make sure the pipelines you are reading aren't getting destroyed during read.
   std::shared_mutex pipeline_cache_destruction_mutex;
   // Pipelines by handle. Multiple pipelines can target the same shader, and even have multiple shaders within themselves.
   // This contains all pipelines (from the game) that we can replace shaders of (e.g. pixel shaders, vertex shaders, ...).
   // It's basically data we append (link) to pipelines, done manually because we have no other way.
   // The data here is allocated by itself. // TODO: make this a unique pointer for easier handling.
   std::unordered_map<uint64_t, Shader::CachedPipeline*> pipeline_cache_by_pipeline_handle;
   // Same as "pipeline_cache_by_pipeline_handle" but mapped to cloned (custom) pipeline handles.
   std::unordered_map<uint64_t, Shader::CachedPipeline*> pipeline_cache_by_pipeline_clone_handle;
   // All the pipelines linked to a shader. By original shader hash.
   std::unordered_map<uint32_t, std::unordered_set<Shader::CachedPipeline*>> pipeline_caches_by_shader_hash;

   std::unordered_set<uint64_t> pipelines_to_reload;
   static_assert(sizeof(reshade::api::pipeline::handle) == sizeof(uint64_t));

   // Custom samplers mapped to original ones by texture LOD bias
   std::unordered_map<uint64_t, std::unordered_map<float, com_ptr<ID3D11SamplerState>>> custom_sampler_by_original_sampler;

#if ENABLE_SR
   SR::Type sr_type = SR::Type::None; // If active, the SR tech enabled by the user and supported+initialized correctly on this device
   std::map<SR::Type, SR::InstanceData*> sr_implementations_instances; // All implementations allowed by the current mod, might not all be compatible
   SR::InstanceData* GetSRInstanceData() const
   {
      auto it = sr_implementations_instances.find(sr_type);
      return (it != sr_implementations_instances.end()) ? it->second : nullptr;
   }
#endif

   // Resources:

#if ENABLE_SR
   com_ptr<ID3D11Texture2D> sr_output_color;
   com_ptr<ID3D11Texture2D> sr_exposure;
   float sr_exposure_texture_value = 1.f;
#endif // ENABLE_SR

   // Native Shaders (from "native_shaders_definitions")
   bool created_native_shaders = false;
   std::unordered_map<uint32_t, com_ptr<ID3D11VertexShader>> native_vertex_shaders;
#if GEOMETRY_SHADER_SUPPORT
   std::unordered_map<uint32_t, com_ptr<ID3D11GeometryShader>> native_geometry_shaders;
#endif
   std::unordered_map<uint32_t, com_ptr<ID3D11PixelShader>> native_pixel_shaders;
   std::unordered_map<uint32_t, com_ptr<ID3D11ComputeShader>> native_compute_shaders;

#if DEVELOPMENT
   std::unordered_map<const ID3D11InputLayout*, std::vector<D3D11_INPUT_ELEMENT_DESC>> input_layouts_descs;
#endif

   // Native Shaders Resources
   com_ptr<ID3D11Texture2D> temp_copy_source_texture;
   com_ptr<ID3D11Texture2D> temp_copy_target_texture;
   com_ptr<ID3D11Texture2D> display_composition_texture; // Temporary copy texture of the swapchain that we use to draw back to the swapchain with a display composition shader (e.g. to change brightness, transfer function, gamut etc)
   com_ptr<ID3D11ShaderResourceView> display_composition_srv;

   // CBuffers
   com_ptr<ID3D11Buffer> luma_global_settings;
   com_ptr<ID3D11Buffer> luma_instance_data;
   com_ptr<ID3D11Buffer> luma_ui_data;
   CB::LumaUIDataPadded cb_luma_ui_data = {};
   std::atomic<bool> cb_luma_global_settings_dirty = true;

   // UI
   com_ptr<ID3D11Texture2D> ui_texture;
   com_ptr<ID3D11RenderTargetView> ui_texture_rtv;
   com_ptr<ID3D11ShaderResourceView> ui_texture_srv;
   com_ptr<ID3D11RenderTargetView> ui_initial_original_rtv; // Leave nullptr to fall back on the current swapchain. This is an RTV but what matters is actually the resource behind it.
   com_ptr<ID3D11RenderTargetView> ui_latest_original_rtv;

   // Misc
   com_ptr<ID3D11SamplerState> sampler_state_linear;
   com_ptr<ID3D11SamplerState> sampler_state_point;
   com_ptr<ID3D11BlendState> default_blend_state; // No blend
   com_ptr<ID3D11DepthStencilState> default_depth_stencil_state; // Depth/Stencil disabled
#if DEVELOPMENT
   com_ptr<ID3D11DepthStencilState> depth_test_false_write_true_stencil_false_state;
#endif

   // Pointer to the current DX buffer for the "global per view" cbuffer.
   com_ptr<ID3D11Buffer> cb_per_view_global_buffer;
#if DEVELOPMENT
   std::unordered_set<ID3D11Buffer*> cb_per_view_global_buffers;
#endif
   void* cb_per_view_global_buffer_map_data = nullptr;
#if DEVELOPMENT
   std::shared_ptr<void> debug_draw_frozen_draw_state_stack;
   com_ptr<ID3D11Resource> debug_draw_texture;
   DXGI_FORMAT debug_draw_texture_format = DXGI_FORMAT_UNKNOWN; // The view format, not the texture format
   uint4 debug_draw_texture_size = {}; // 3rd and 4th channels are Array/MS/Mips

   struct TrackBufferData
   {
      std::string hash; // Resource ptr hash
      bool data_valid = false;
      std::vector<float> data;
      com_ptr<ID3D11Buffer> cb;
      
      UINT first_constant = 0; // D3D11.1: start offset in float4 units (0 if D3D11.0 or full-buffer bind)
      UINT num_constants = 0;  // D3D11.1: count in float4 units (0 if D3D11.0 info unavailable)

      void Clear()
      {
         hash.clear();
         data_valid = false;
         data.clear();
         cb.reset();
         first_constant = 0;
         num_constants = 0;
      }
   };
   TrackBufferData track_buffer_data;
#endif

   // Generic states that can be used by multiple games (you don't need to set them if you ignore the whole thing)

   // Whether the "main" post processing passes have finished drawing (it also implied we detected scene rendering and some cbuffers etc)
   std::atomic<bool> has_drawn_main_post_processing = false;
   // Useful to know if rendering was skipped in the previous frame (e.g. in case we were in a UI view)
   bool has_drawn_main_post_processing_previous = false;
   // Might not be used by all games but it's a global feature (not necessarily related to "ENABLE_SR", the game might have a native implementation)
   std::atomic<bool> has_drawn_sr = false;
   // Set to true once we can tell with certainty that TAA was active in the game
   std::atomic<bool> taa_detected = false;

#if ENABLE_SR
   std::atomic<bool> force_reset_sr = false;
   std::atomic<bool> sr_suppressed = false;
   std::atomic<float> sr_render_resolution_scale = 1.f;
#endif

   // TODO: make changes thread safe
   // Render resolution without padding.
   float2 render_resolution = { 1, 1 };
   float2 previous_render_resolution = { 1, 1 };
   // Note: this is the window/swapchain res
   float2 output_resolution = { 1, 1 };
   float2 display_resolution = { 1, 1 };

   // Last frame's scale-relevant state, used to detect when indirect mirrors (and anything else tied to
   // the render scale) must be invalidated because the render resolution or SR selection changed.
   float2 last_render_resolution = { 1, 1 };
#if ENABLE_SR
   SR::Type last_sr_type = SR::Type::None;
#endif

   // Live settings (set by the code, not directly by users):
   float default_user_peak_white = default_peak_white;
   float texture_mip_lod_bias_offset = 0.f;
#if ENABLE_SR
   float sr_scene_exposure = 1.f;
   float sr_scene_pre_exposure = 1.f;
#endif

   std::atomic<bool> cloned_pipelines_changed = false; // Atomic so it doesn't rely on "s_mutex_generic"
   uint32_t cloned_pipeline_count = 0; // How many pipelines (shaders/passes) we replaced with custom ones and are currently "replaced" (if zero, we can assume the mod isn't doing much)

#if ENABLE_SR
   bool has_drawn_sr_imgui = false;
#endif

   // Per game custom data
   GameDeviceData* game = nullptr;

   std::vector<ID3D11Buffer*> GetLumaCBuffers() const
   {
      std::vector<ID3D11Buffer*> buffers;
      if (luma_global_settings)
         buffers.push_back(luma_global_settings.get());
      if (luma_instance_data)
         buffers.push_back(luma_instance_data.get());
      if (luma_ui_data)
         buffers.push_back(luma_ui_data.get());
      return buffers;
   }

   ManagedResources managed_resources;
};

struct __declspec(uuid("c5805458-2c02-4ebf-b139-38b85118d971")) SwapchainData
{
   // Probably not particularly useful as the swapchain would be single threaded, if not for its destruction callback
   std::shared_mutex mutex;

   std::unordered_set<uint64_t> back_buffers;

   std::vector<com_ptr<ID3D11RenderTargetView>> display_composition_rtvs;

   // Whether the original SDR (vanilla) swapchain was linear space (e.g. sRGB formats)
    bool vanilla_was_linear_space = false;
};
