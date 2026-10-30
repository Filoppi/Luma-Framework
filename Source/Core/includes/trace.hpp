#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3d11shader.h>
#include <include/reshade.hpp>

struct CommandListData;   // instance_data.h
struct DeviceData;        // instance_data.h
struct DrawDispatchData;  // instance_data.h
struct TraceDrawCallData; // instance_data.h
namespace Shader
{
   struct CachedShader; // shaders.h
} // namespace Shader

// Frame capture entries (DEVELOPMENT only, the calls compile to nothing otherwise), implemented in "trace.inl".
// The capture scheduling and the dev UI trace view are in "core.hpp".
namespace Trace
{
#if DEVELOPMENT
   // Records a draw (or dispatch) the mod issued itself (a Luma pass) like a game draw: the views, cbuffers, samplers and render state bound for it,
   // and its arguments. Call it right after the "Draw()" or "Dispatch()", before unbinding or restoring anything: the debug draw (and MCP
   // readbacks) of the pass copy its output here too.
   // "name" is kept by the capture (a string literal), without one the pass goes by its shader's name. The bound pixel (or compute) shader is recorded
   // if it's one of Luma's ("native_shaders_definitions"), with the slots it uses.
   // From a draw callback, the entry goes before the game draw's ones if the draw went through (it ran after), after them otherwise (the pass replaced it).
   void Draw(ID3D11DeviceContext* native_device_context, const char* name, UINT vertex_count);
   void Dispatch(ID3D11DeviceContext* native_device_context, const char* name, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z);
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
   // Core: a game draw's entry for one of its shaders. Expects "s_mutex_generic", "s_mutex_dumping" and "device_data.mutex" (shared) locked.
   void AddDrawCall(std::vector<TraceDrawCallData>& trace_draw_calls_data, const DeviceData& device_data, ID3D11DeviceContext* native_device_context, uint64_t pipeline_handle, const DrawDispatchData& draw_dispatch_data);
   // Core: moves the entries queued on this thread for "native_device_context" into its trace at "index" (the other contexts' are dropped),
   // and returns how many. Expects the trace mutexes locked.
   size_t MoveQueued(std::vector<TraceDrawCallData>& trace_draw_calls_data, ID3D11DeviceContext* native_device_context, size_t index = SIZE_MAX);
   // Core: called by the draw hooks with what the draw callbacks returned, flags the entries of the draw ("trace_current_draw") if they didn't let it through
   void EndDraw(CommandListData& cmd_list_data, bool cancelled_or_replaced);
   void ClearQueued();
#else
   inline void Draw(ID3D11DeviceContext*, const char*, UINT) {}
   inline void Dispatch(ID3D11DeviceContext*, const char*, UINT, UINT, UINT) {}
   inline void Marker(ID3D11DeviceContext*, const char*, ID3D11DeviceChild* = nullptr) {}
   inline void UpdateDraw(CommandListData&, DeviceData&, ID3D11DeviceContext*) {}
   inline void Annotate(CommandListData&, std::string_view, std::optional<int64_t> = std::nullopt) {}
#endif
} // namespace Trace
