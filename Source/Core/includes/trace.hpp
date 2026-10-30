#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <d3d11.h>
#include <d3d11shader.h>
#include <include/reshade.hpp>

#include "com_ptr.h"
#include "shader_types.h"
#include "shaders.h"

struct CommandListData; // instance_data.h
struct DeviceData;      // instance_data.h

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
   // Already includes all the render targets
   D3D11_BLEND_DESC1 blend_desc = {};
   FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };

   bool cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
   // The views and buffers bound to every slot (only compared, never dereferenced), including the slots the shader doesn't use, see "Trace::UpdateDraw()"
   const ID3D11Buffer* cb_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
   const ID3D11SamplerState* sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
   const ID3D11DepthStencilView* dsv = nullptr;
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

   // Full depth/stencil and rasterizer state (pixel shader entries only). Without a bound state object D3D11 uses its defaults.
   bool has_depth_stencil_state = false;
   D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = {};
   UINT stencil_ref = 0;
   bool has_rasterizer_state = false;
   D3D11_RASTERIZER_DESC rasterizer_desc = {};
   UINT viewport_count = 0;
   D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
   UINT scissor_count = 0;
   D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};

   // Copy entries (source in "sr_*[0]", destination in "rt_*[0]")
   enum class CopyKind : uint8_t
   {
      Resource,
      Region,
      Resolve,
      BufferRegion,
   };
   struct CopyInfo
   {
      CopyKind kind = CopyKind::Resource;
      UINT source_subresource = 0;
      UINT dest_subresource = 0;
      uint3 dest_offset = {}; // Bytes in "x" for buffer copies
      bool has_box = false;
      D3D11_BOX box = {}; // Source region (bytes in "left"/"right" for buffer copies)
      DXGI_FORMAT resolve_format = DXGI_FORMAT_UNKNOWN;
   };
   CopyInfo copy_info;

   // Free text game code attached to this entry during the capture (see "Trace::Annotate")
   std::string note;
   // What the shader that ran was built from ("None" for the original), after the draw callbacks picked the variant (see "Shader::UsePipelineVariant()")
   Shader::CloneOrigin drawn_origin = Shader::CloneOrigin::None;
   // The draw callbacks didn't let the draw through: they cancelled it, or drew something else in its place
   bool cancelled_or_replaced = false;
   // Slots a draw callback changed ("Trace::UpdateDraw()"), their new state is in "replaced_state" (at the same slot)
   enum class SlotKind : uint8_t
   {
      RTV,
      SRV,
      UAV,
      DSV,
      CB,
      Sampler,
   };
   enum class SlotChange : uint8_t
   {
      Added,
      Replaced,
      Unbound,
   };
   struct ReplacedSlot
   {
      SlotKind kind;
      uint32_t slot;
      SlotChange change;
   };
   std::vector<ReplacedSlot> replaced_slots;
   std::shared_ptr<const TraceDrawCallData> replaced_state;

   // TODO: these might not always be filled up!
   bool any_input_resources_format_upgraded = false;
   bool any_output_resources_format_upgraded = false;
   bool any_input_resources_scaled = false;
   bool any_output_resources_scaled = false;

   bool IsRTVValid(size_t index) const { return rtv_format[index] != DXGI_FORMAT_UNKNOWN && rtv_format[index] != DXGI_FORMAT(-1); }
   bool IsSRVValid(size_t index) const { return srv_format[index] != DXGI_FORMAT_UNKNOWN && srv_format[index] != DXGI_FORMAT(-1); }
   bool IsUAVValid(size_t index) const { return uav_format[index] != DXGI_FORMAT_UNKNOWN && uav_format[index] != DXGI_FORMAT(-1); }
   bool IsDSVValid() const { return dsv_format != DXGI_FORMAT_UNKNOWN && dsv_format != DXGI_FORMAT(-1); }

   const char* custom_name = "Unknown";
   // Luma passes ("Trace::CustomDraw()"): the stage whose state was recorded (none for markers), and its "native_shaders_definitions" key
   reshade::api::shader_stage custom_stage = reshade::api::shader_stage(0);
   uint32_t custom_shader_hash = 0;
};

// Frame capture entries (DEVELOPMENT only, the calls compile to nothing otherwise), implemented in "trace.inl".
// The capture scheduling and the dev UI trace view are in "core.hpp".
namespace Trace
{
#if DEVELOPMENT
   // Needs to match in shader
   enum class DebugDrawTextureOptionsMask : uint32_t
   {
      None = 0,
      Fullscreen = 1 << 0,
      RenderResolutionScale = 1 << 1,
      ShowAlpha = 1 << 2,
      PreMultiplyAlpha = 1 << 3,
      InvertColors = 1 << 4,
      LinearToGamma = 1 << 5,
      GammaToLinear = 1 << 6,
      FlipY = 1 << 7,
      Saturate = 1 << 8,
      RedOnly = 1 << 9,
      BackgroundPassthrough = 1 << 10,
      TextureMultiSample = 1 << 11,
      TextureArray = 1 << 12,
      // If this is true and Texture3D is also true, the texture is a cube. If both are false it's 1D.
      Texture2D = 1 << 13,
      Texture3D = 1 << 14,
      Abs = 1 << 15,
      Zoom4x = 1 << 16,
      Bilinear = 1 << 17,
      Tonemap = 1 << 18,
      SRGB = 1 << 19,
      UVToPixelSpace = 1 << 20,
      Denormalize = 1 << 21,
   };
   enum class DebugDrawMode : uint32_t
   {
      Custom,
      // TODO: rename all of these to "View", or not
      RenderTarget,
      UnorderedAccessView,
      ShaderResource,
      Depth,
      Stencil,
   };
   inline constexpr const char* debug_draw_mode_strings[] = {
       "Custom",
       "Render Target",
       "Unordered Access View",
       "Shader Resource",
       "Depth",
       "Stencil",
   };

   // A capture is scheduled for the next present and runs until the one after. "running" is atomic so hot paths can check it without "s_mutex"
   // (and recheck it under the lock).
   inline std::shared_mutex s_mutex; // For "running", "scheduled" and "captured_count"
   inline std::atomic<bool> running = false; // For this frame
   inline bool scheduled = false; // For next frame
   inline uint32_t captured_count = 0; // Not exactly necessary but... it might help

   // The dev UI capture list filters
   inline bool show_command_list_info = false;
   inline bool ignore_vertex_shaders = true;
   inline bool ignore_buffer_writes = true;
   inline bool ignore_bindings = true;
   inline bool ignore_custom_draws = true; // Luma passes ("Trace::CustomDraw()")
   inline bool ignore_non_bound_shader_referenced_resources = true;
   inline std::string highlighted_resource; // The resource the capture list and the textures tab highlight

   // Textures debug drawing
   inline uint32_t debug_draw_shader_hash = 0;
   inline char debug_draw_shader_hash_string[8 + 1] = {}; // A shader hash and its terminator
   inline uint64_t debug_draw_pipeline = 0; // In DX11 shaders and pipelines are the same thing so this is kinda useless (excluding the rare case where multiple pipelines (compiled shader objects) were based on the same shader, and hence had the same hash)
   inline int32_t debug_draw_pipeline_target_instance = -1;
   inline std::thread::id debug_draw_pipeline_target_thread = std::thread::id(); // Filter target instances by thread, which would hopefully reliably match the command lists, over multiple frames
   inline uint32_t debug_draw_custom_pass = 0; // A Luma pass instead of a game pipeline, by the hash of its "Trace::CustomDraw()" name

   inline std::atomic<int32_t> debug_draw_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases
   inline std::atomic<int32_t> debug_draw_custom_pass_instance = 0;

   inline DebugDrawMode debug_draw_mode = DebugDrawMode::Custom;
   inline bool debug_draw_freeze_inputs = false; // Allows freezing the inputs/state of this pass, allowing you to edit the shader and seeing the results live. This is bundled with the textures debug draw feature because generally you don't need to freeze the inputs of any prior pass if not the very same one you are analyzing (or iterating upon). If the game stopped drawing the shader, debugging would also stop.
   inline int32_t debug_draw_view_index = 0;
   inline bool debug_draw_disable_blends = false; // Allows disabling HW blends so we can debug the raw pixel shader output
   inline uint32_t debug_draw_options = (uint32_t)DebugDrawTextureOptionsMask::Fullscreen | (uint32_t)DebugDrawTextureOptionsMask::BackgroundPassthrough | (uint32_t)DebugDrawTextureOptionsMask::Tonemap;
   inline int32_t debug_draw_mip = 0;
   inline bool debug_draw_pipeline_instance_filter_by_thread = false; // Not true by default as it seems like quite a few games set up multiple render threads, so this conflicts between frames
   inline bool debug_draw_auto_clear_texture = false;
   inline bool debug_draw_replaced_pass = false; // Whether we print the debugging of the original or replaced pass (the resources bindings etc might be different, though this won't forcefully run the original pass if it was skipped by the game's mod custom code, so this isn't fully reliable)
   inline bool debug_draw_auto_gamma = true;

   // Constant Buffers tracking
   inline uint64_t track_buffer_pipeline = 0; // Can be any type of shader
   inline uint32_t track_buffer_custom_pass = 0; // A Luma pass instead of a game pipeline, like "debug_draw_custom_pass"
   inline int32_t track_buffer_pipeline_target_instance = -1;
   inline int32_t track_buffer_index = 0;

   inline std::atomic<int32_t> track_buffer_pipeline_instance = 0; // Theoretically should be within "CommandListData" but this should work for most cases
   inline std::atomic<int32_t> track_buffer_custom_pass_instance = 0;

   // Core: at the ReShade present, ends the running capture or starts the scheduled one (in the immediate command list)
   void OnPresent(CommandListData& cmd_list_data);
   // Copies the view of the pass that just drew (still bound) selected by "debug_draw_mode" and "debug_draw_view_index" into "DeviceData::debug_draw_texture"
   bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, ID3D11DeviceContext* native_device_context, DeviceData& device_data, bool is_dispatch);
   bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, reshade::api::command_list* cmd_list, bool is_dispatch = false);
   // Reads a constant buffer back (immediate contexts only) or copies it to read it later ("CopyBuffer()"), for the constant buffer tracking
   bool MapBufferData(com_ptr<ID3D11Buffer>& cb, ID3D11DeviceContext* native_device_context, std::vector<float>& buffer_data, UINT byte_width);
   bool CopyBuffer(com_ptr<ID3D11Buffer> cb, ID3D11DeviceContext* native_device_context, std::vector<float>& buffer_data, com_ptr<ID3D11Buffer>& cb_copy);

   // Records a draw (or dispatch) the mod issued itself (a Luma pass) like a game draw: the views, cbuffers, samplers and render state bound for it,
   // and its arguments. Call it right after the "Draw()" or "Dispatch()", before unbinding or restoring anything: the debug draw (and MCP
   // readbacks) of the pass copy its output here too.
   // "name" is kept by the capture (a string literal), without one the pass goes by its shader's name. The bound pixel (or compute) shader is recorded
   // if it's one of Luma's ("native_shaders_definitions"), with the slots it uses.
   // From a draw callback, the entry goes before the game draw's ones if the draw went through (it ran after), after them otherwise (the pass replaced it).
   void CustomDraw(ID3D11DeviceContext* native_device_context, const char* name, UINT vertex_count);
   void CustomDispatch(ID3D11DeviceContext* native_device_context, const char* name, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z);
   // An entry with a name (and a target view or resource) only: a phase marker, or a pass without state of its own (e.g. SR)
   void Marker(ID3D11DeviceContext* native_device_context, const char* name, ID3D11DeviceChild* target = nullptr);
   // From a draw callback that changed what the game draw binds (e.g. a texture swapped for its own) and lets it draw: records the views, cbuffers
   // and samplers that were added, replaced or unbound on the draw's entries, next to the ones the game bound.
   void UpdateDraw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context);
   // Attaches a note to the draw being processed in the current capture (e.g. the game's motion vector decision).
   // Call it from "Game::OnDrawOrDispatch", where the shader entries of the current draw are the last ones of "cmd_list_data".
   // "value" is appended as "text=value" (formatted only while capturing, so it can be called on every draw).
   void Annotate(CommandListData& cmd_list_data, std::string_view text, std::optional<int64_t> value = std::nullopt);

   // Core: reads the slots a shader reads or writes from its reflections (RTVs, SRVs, UAVs, cbuffers and samplers), the ones a draw's entry records.
   // Game shaders are reflected at creation, Luma's when a pass is recorded.
   void ReflectShaderSlots(ID3D11ShaderReflection* shader_reflector, const D3D11_SHADER_DESC& shader_desc, Shader::CachedShader& cached_shader, bool& found_any_rtvs, bool& found_any_other_bindings);
   // Core: the entries of events that aren't draws, with the resource they read ("source", in "sr_*[0]") and the one they write ("target", in "rt_*[0]")
   void AddEvent(CommandListData& cmd_list_data, ID3D11DeviceContext* command_list, TraceDrawCallData::TraceDrawCallType type, ID3D11Resource* source = nullptr, ID3D11Resource* target = nullptr);
   // "values" are stored in "blend_factor" (depth and stencil in the first two for depth clears)
   void AddClear(reshade::api::command_list* cmd_list, reshade::api::resource_view view, std::initializer_list<float> values);
   void AddCopy(CommandListData& cmd_list_data, ID3D11DeviceContext* command_list, ID3D11Resource* source, ID3D11Resource* target, const TraceDrawCallData::CopyInfo& copy_info);
   // Core: a deferred command list created or appended into "cmd_list_data", with the entries it recorded
   void JoinCommandList(CommandListData& cmd_list_data, CommandListData& secondary_cmd_list_data, ID3D11DeviceContext* command_list, bool is_finish_command_list);
   // Core: the entries of a game draw (its vertex and pixel shaders) or dispatch, before its draw callbacks run (they might change the state)
   void Draw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context, const DrawDispatchData& draw_dispatch_data);
   void Dispatch(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context, const DrawDispatchData& draw_dispatch_data);
   // Core: on every way out of the draw callbacks. Stamps the variant the draw's shaders ran with ("Shader::UsePipelineVariant()"), and moves the passes
   // recorded meanwhile into the trace: before the draw if it went through (they ran first), after it otherwise (they replaced it).
   void FinishDrawCallbacks(CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, bool draw_went_through);

   // Core: the debug draw and constant buffer tracking of a game draw or dispatch, "BeginGameDraw()" before its draw callbacks and "EndGameDraw()" after them
   struct GameDraw
   {
      bool is_dispatch = false;
      bool debug_draw = false; // The debug draw targets its pipeline (maybe not this instance)
      std::shared_ptr<void> pre_draw_state; // Its "DrawStateStack" before the callbacks, to read back the original pass if they replaced it
   };
   GameDraw BeginGameDraw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context, bool is_dispatch);
   // Flags the draw's entries if the callbacks didn't let it through ("EndDraw()"), and reads the pass back for the debug draw and constant buffer tracking.
   // Returns whether it issued the draw itself ("draw") to read it back, then the hook mustn't let it through.
   bool EndGameDraw(const GameDraw& game_draw, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::command_list* cmd_list, bool cancelled_or_replaced, const std::function<void()>& draw);
   // Core: at the end of every frame
   void EndFrame(DeviceData& device_data);

   // A pass the debug draw and the constant buffer tracking can target: a game pipeline, or a Luma pass by name ("CustomDraw()")
   struct PassTarget
   {
      uint64_t pipeline = 0;
      uint32_t shader_hash = 0; // The pipeline's (first) shader, or the Luma pass's native shader
      uint32_t custom_pass = 0;
      reshade::api::shader_stage stage = reshade::api::shader_stage(0);
      int32_t instance = 0; // Among the passes with the same identity in a frame, like the runtime counters
      std::thread::id thread; // Set if the instances were counted on its thread only
   };
   // The pass of a captured entry (a game draw with a live pipeline, or a Luma pass), counting its instance among the previous entries (the ones
   // recorded on its thread only, if "by_thread"). Needs "s_mutex_generic" and the command list's "mutex_trace" locked.
   std::optional<PassTarget> FindPassTarget(const DeviceData& device_data, const CommandListData& cmd_list_data, size_t index, bool by_thread);
   bool IsDebugDrawTarget(const PassTarget& target);
   // Debug draws a pass, from its captured "entry". The view type follows the pass (render target, UAV, or SRV), unless "keep_mode" and the pass has the view.
   void SetDebugDrawTarget(DeviceData& device_data, const PassTarget& target, const TraceDrawCallData& entry, bool keep_mode);
   void ClearDebugDrawTarget(DeviceData& device_data);
   bool IsTrackBufferTarget(const PassTarget& target);
   void SetTrackBufferTarget(DeviceData& device_data, const PassTarget& target);
   void ClearTrackBufferTarget(DeviceData& device_data);

   // Core: moves the entries queued on this thread for "native_device_context" (Luma passes and markers) into its trace, outside of the draw hooks
   // (the other contexts' are dropped). Without a command list, they are dropped.
   void FlushQueued(CommandListData* cmd_list_data, ID3D11DeviceContext* native_device_context);
   // Core: pre-allocates the entries of a command list's captures
   void InitCommandList(CommandListData& cmd_list_data);
   // Core: called by the draw hooks with what the draw callbacks returned, flags the entries of the draw ("trace_current_draw") if they didn't let it through
   void EndDraw(CommandListData& cmd_list_data, bool cancelled_or_replaced);
#else
   inline void CustomDraw(ID3D11DeviceContext*, const char*, UINT) {}
   inline void CustomDispatch(ID3D11DeviceContext*, const char*, UINT, UINT, UINT) {}
   inline void Marker(ID3D11DeviceContext*, const char*, ID3D11DeviceChild* = nullptr) {}
   inline void UpdateDraw(CommandListData&, DeviceData&, ID3D11DeviceContext*) {}
   inline void Annotate(CommandListData&, std::string_view, std::optional<int64_t> = std::nullopt) {}
#endif
} // namespace Trace
