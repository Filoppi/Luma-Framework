#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

// Forward declarations
struct GameDeviceData;

enum class DrawOrDispatchOverrideType
{
   None,
   Skip,
   Replaced,
};

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

struct DrawDispatchData
{
   // Vertex Shader
   uint32_t vertex_count = 0;
   uint32_t instance_count = 0;
   uint32_t first_vertex = 0;
   uint32_t first_instance = 0;

   uint32_t index_count = 0;
   uint32_t first_index = 0;
   int32_t vertex_offset = 0;

   bool indexed = false;

   // Compute Shader
   uint3 dispatch_count = {};

   // Shared
   bool indirect = false;
};

struct TraceDrawCallData
{
   TraceDrawCallData()
   {
      thread_id = std::this_thread::get_id();

      std::fill_n(samplers_filter, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, static_cast<D3D11_FILTER>(-1)); // Default to no sampler
   }

   enum class TraceDrawCallType
   {
      // Any type of shader (including compute)
      Shader,
      // Copy resource and similar function
      CopyResource,
      ClearResource,
      BindPipeline,
      BindResource,
      CPURead,
      CPUWrite,
      Present,
      CreateCommandList,
      AppendCommandList,
      ResetCommmandList,
      FlushCommandList,
		Custom, // Custom draw call for custom passes we added/replaced
   };

   TraceDrawCallType type = TraceDrawCallType::Shader;

#if 1 // For now add a new "TraceDrawCallData" per shader (e.g. one for vertex and one for pixel, instead of doing it per draw call), this is due to legacy code that would require too much refactor
   uint64_t pipeline_handle = 0;
#else
   uint64_t pipeline_handles = 0; // The actual list of pipelines that run within the traced frame (within this deferred command list, and then merged into the immediate one later)
   ShaderHashesList shader_hashes;
#endif

   // The original command list (can be useful to have later)
   com_ptr<ID3D11DeviceContext> command_list = nullptr;
   // The thread this call was made on (usually 1:1 with deferred (async) command lists)
   std::thread::id thread_id = {};

   // Depth/Stencil
   enum class DepthStateType // TODO: rename to DepthStancilStateType, and "depth_state_names" too
   {
      Disabled,
      TestAndWrite,
      TestOnly,
      WriteOnly,
      Custom,
      Invalid,
   };
   static constexpr const char* depth_state_names[] = { "Disabled", "Test and Write", "Test Only", "Write Only", "Custom" };

   DrawDispatchData draw_dispatch_data = {};

   // Vertex shader
   D3D11_PRIMITIVE_TOPOLOGY primitive_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
   std::vector<std::string> vertex_buffer_hashes;
   std::string input_layout_hash;
   std::string index_buffer_hash;
   DXGI_FORMAT index_buffer_format = DXGI_FORMAT_UNKNOWN;
   std::vector<DXGI_FORMAT> input_layouts_formats;
   UINT index_buffer_offset = 0;

   DepthStateType depth_state = DepthStateType::Disabled;
   DepthStateType stencil_state = DepthStateType::Disabled;
   bool scissors = false;
   float4 viewport_0 = {};
   // Already includes all the render targets
   D3D11_BLEND_DESC blend_desc = {};
   FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };

   bool cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
   std::string cb_hash[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {}; // Ptr hash (not content hash)
   // D3D11.1 partial buffer binding offsets (0/0 means data not available or full-buffer bind via legacy API)
   UINT cb_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
   UINT cb_num_constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};

   D3D11_FILTER samplers_filter[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
   D3D11_TEXTURE_ADDRESS_MODE samplers_address_u[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
   D3D11_TEXTURE_ADDRESS_MODE samplers_address_v[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
   D3D11_TEXTURE_ADDRESS_MODE samplers_address_w[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
   float samplers_mip_lod_bias[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};

   // Render Target (Resource+Views)
   static constexpr size_t rtvs_size = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; // Max size (we can lower it if needed) // TODO: allocate these as a vector of structs
   const ID3D11RenderTargetView* rtvs[rtvs_size] = {};                  // TODO.. find a better way, this is very hacky (though safe, as long as we read/compare it)
   DXGI_FORMAT rt_format[rtvs_size] = {};                               // The format of the resource
   DXGI_FORMAT rtv_format[rtvs_size] = {};                              // The format of the view
   uint4 rt_size[rtvs_size] = {};                                       // 3th and 4th channels are Array, MS and Mips
   uint3 rtv_size[rtvs_size] = {};
   UINT rtv_mip[rtvs_size] = {};
   std::string rt_type_name[rtvs_size] = {};
   std::string rt_hash[rtvs_size] = {};                                    // Ptr hash (not content hash)
   std::string rt_debug_name[rtvs_size] = {}; // Debug name of the texture or the view
   bool rt_is_swapchain[rtvs_size] = {};
   // Shader Resource (Resource+Views)
#if GAME_BURNOUT_PARADISE_REMASTERED // TODO: remove these hacks... Burnout Paradise crashes due to too big allocations
   static constexpr size_t srvs_size = 16;
#else
   static constexpr size_t srvs_size = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
#endif
   const ID3D11ShaderResourceView* srvs[srvs_size] = {};
   DXGI_FORMAT srv_format[srvs_size] = {};                                   // The format of the view
   DXGI_FORMAT sr_format[srvs_size] = {};                            // The format of the resource
   uint4 sr_size[srvs_size] = {};                                            // 3th and 4th channels are Array, MS and Mips
   uint3 srv_size[srvs_size] = {};
   UINT srv_mip[srvs_size] = {};
   std::string sr_type_name[srvs_size] = {};
   std::string sr_hash[srvs_size] = {};                                          // Ptr hash (not content hash)
   std::string sr_debug_name[srvs_size] = {}; // Debug name of the texture or the view
   bool sr_is_rt[srvs_size] = {};
   bool sr_is_ua[srvs_size] = {};
   // Unordered Access (Resource+Views)
#if GAME_BURNOUT_PARADISE_REMASTERED
   static constexpr size_t uavs_size = 16;
#else
   static constexpr size_t uavs_size = D3D11_1_UAV_SLOT_COUNT;
#endif
   const ID3D11UnorderedAccessView* uavs[uavs_size] = {};
   DXGI_FORMAT ua_format[uavs_size] = {};               // The format of the resource
   DXGI_FORMAT uav_format[uavs_size] = {};     // The format of the view
   uint4 ua_size[uavs_size] = {};                       // 3th and 4th channels are Array, MS and Mips
   uint3 uav_size[uavs_size] = {};
   UINT uav_mip[uavs_size] = {};
   std::string ua_type_name[uavs_size] = {};
   std::string ua_hash[uavs_size] = {};                    // Ptr hash (not content hash)
   std::string ua_debug_name[uavs_size] = {}; // Debug name of the texture or the view
   bool ua_is_rt[uavs_size] = {};
   // Depth Stencil (Resource+View)
   DXGI_FORMAT ds_format = {}; // The format of the resource
   DXGI_FORMAT dsv_format = {}; // The format of the view
   uint2 ds_size = {};
   std::string ds_hash = {}; // Ptr hash (not content hash)
   std::string ds_debug_name = {}; // Debug name of the texture or the view

   // TODO: these might not always be filled up!
   bool any_input_resources_format_upgraded = false;
   bool any_output_resources_format_upgraded = false;

   bool IsRTVValid(size_t index) const { return rtv_format[index] != DXGI_FORMAT_UNKNOWN && rtv_format[index] != DXGI_FORMAT(-1); }
   bool IsSRVValid(size_t index) const { return srv_format[index] != DXGI_FORMAT_UNKNOWN && srv_format[index] != DXGI_FORMAT(-1); }
   bool IsUAVValid(size_t index) const { return uav_format[index] != DXGI_FORMAT_UNKNOWN && uav_format[index] != DXGI_FORMAT(-1); }
   bool IsDSVValid() const { return dsv_format != DXGI_FORMAT_UNKNOWN && dsv_format != DXGI_FORMAT(-1); }

   const char* custom_name = "Unknown";
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

   reshade::api::pipeline pipeline_state_original_compute_shader = reshade::api::pipeline(0);
   reshade::api::pipeline pipeline_state_original_vertex_shader = reshade::api::pipeline(0);
   reshade::api::pipeline pipeline_state_original_pixel_shader = reshade::api::pipeline(0);

   Shader::ShaderHashesList<OneShaderPerPipeline> pipeline_state_original_graphics_shader_hashes;
   Shader::ShaderHashesList<OneShaderPerPipeline> pipeline_state_original_compute_shader_hashes;
   bool pipeline_state_has_custom_vertex_shader = false;
   bool pipeline_state_has_custom_pixel_shader = false;
   bool pipeline_state_has_custom_graphics_shader = false;
   bool pipeline_state_has_custom_compute_shader = false;

   enum ViewState
   {
      NotSet,
      // Could be null or valid
      Set,
      SetAndUpgraded,
   };

   std::array<ViewState, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> ps_srvs_state = {};
   std::array<ViewState, D3D11_1_UAV_SLOT_COUNT> ps_uavs_state = {};
   std::array<ViewState, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> cs_srvs_state = {};
   std::array<ViewState, D3D11_1_UAV_SLOT_COUNT> cs_uavs_state = {};
   bool any_upgraded_ps_srvs = false;
   bool any_upgraded_ps_uavs = false;
   bool any_upgraded_cs_srvs = false;
   bool any_upgraded_cs_uavs = false;

   void ResetUpgradedViews()
   {
      ps_srvs_state.fill(ViewState::NotSet);
      ps_uavs_state.fill(ViewState::NotSet);
      cs_srvs_state.fill(ViewState::NotSet);
      cs_uavs_state.fill(ViewState::NotSet);
      any_upgraded_ps_srvs = false;
      any_upgraded_ps_uavs = false;
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
   void UpdateUpgradedCSSRVs()
   {
      any_upgraded_cs_srvs = std::any_of(cs_srvs_state.begin(), cs_srvs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }
   void UpdateUpgradedCSUAVs()
   {
      any_upgraded_cs_uavs = std::any_of(cs_uavs_state.begin(), cs_uavs_state.end(), [](auto v) { return v == ViewState::SetAndUpgraded; });
   }

#if DEVELOPMENT
   std::shared_mutex mutex_trace;
   std::vector<TraceDrawCallData> trace_draw_calls_data;

   void AddGraphicsCaptureCustomDrawCall(const char* name, class ID3D11DeviceContext* native_device_context, class ID3D11Resource* resource = nullptr, class ID3D11View* view = nullptr);

   bool requires_join = false;

   bool any_draw_done = false;
   bool any_dispatch_done = false;

   ShaderCustomDepthStencilType temp_custom_depth_stencil = ShaderCustomDepthStencilType::None;
#endif
};

struct __declspec(uuid("cfebf6d4-d184-4e1a-ac14-09d088e560ca")) DeviceData
{
   // Only for "swapchains", "back_buffers" and "upgraded_resources" (and related) and "modified_shaders_byte_code".
   // Device object creation etc is usually single threaded anyway, except for the destructor.
   std::shared_mutex mutex;

   std::thread thread_auto_loading;
   std::atomic<bool> thread_auto_loading_running = false;

   std::unordered_set<uint64_t> upgraded_resources; // All the directly upgraded resources, excluding the swapchains backbuffers, as they are created internally by DX
#if DEVELOPMENT
   std::unordered_map<uint64_t, reshade::api::format> original_upgraded_resources_formats; // Maps the original resource to its direct upgraded format. These include the swapchain buffers too!
   std::unordered_map<uint64_t, std::pair<uint64_t, reshade::api::format>> original_upgraded_resource_views_formats; // All the views for direct upgraded resources, with the resource and the original resource view format
#endif
   std::unordered_map<uint64_t, uint64_t> original_resources_to_mirrored_upgraded_resources; // TODO: convert/copy the initial/current data from the source texture when created. Also rename to "indirect_upgraded"
   std::unordered_map<uint64_t, uint64_t> original_resource_views_to_mirrored_upgraded_resource_views;

#if ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS
   // Edited shaders byte code + size + MD5 hash by (original) shader hash.
   // We cache these in memory forever just because with ReShade handling their destruction on the spot between the pipeline (shader) creation and init function isn't "possible",
   // and it can be called from multiple threads so we need to protect it.
   std::unordered_map<uint32_t, std::tuple<std::unique_ptr<std::byte[]>, size_t, Hash::MD5::Digest>> modified_shaders_byte_code;
#endif

   std::unordered_set<reshade::api::swapchain*> swapchains;
   std::unordered_set<uint64_t> back_buffers; // From all the swapchains (whether they are upgraded or not)
   ID3D11Device* native_device = nullptr; // Doesn't need mutex, always valid given it's a ptr to itself
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
   float2 render_resolution = { 1, 1 };
   float2 previous_render_resolution = { 1, 1 };
   // Note: this is the "display"/swapchain res
   float2 output_resolution = { 1, 1 };

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
