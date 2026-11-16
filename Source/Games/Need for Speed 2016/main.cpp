// Need for Speed (2016) Luma mod (Frostbite, D3D11), based on the Mass Effect Andromeda one.
// Replaces the game's TAA resolve with super resolution without upscaling (DLSS, FSR 4, XeSS or FSR 3), sets the engine's jitter to the
// selected one's Halton sequence, sharpens textures (16x anisotropic filtering and the usual mip LOD bias), and offers the game's
// resolution scale as a setting.

#define GAME_NEED_FOR_SPEED_2016 1

// Frostbite input quirks (as in Mass Effect Andromeda): any message box breaks the game's input for good, and focus loss handling
// interferes too
#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define AVOID_INPUT_LOSS 1

#define GEOMETRY_SHADER_SUPPORT 0

// FSR 4 and XeSS run on a D3D12 device (Luma's interop), which ReShade wraps too: ignore its events
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

// For "original_draw_dispatch_func" in all builds (the depth of field resolve runs natively before SR)
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

#include <chrono>

// The mod's headers build on core's types, so they come after it
#include "about.h"
#include "camera_capture.h"
#include "diagnostics.h"
#include "engine_jitter.h"
#include "render_scale.h"
#include "super_resolution_pass.h"
#include "taa_resolve.h"

static_assert(ENABLE_SR, "The mod replaces the TAA with super resolution: build it with the SR implementations on");

// SR's frame time: the present to present interval
class FrameTimer
{
public:
   // At every present
   void Tick()
   {
      const auto now = std::chrono::steady_clock::now();
      if (last_tick != std::chrono::steady_clock::time_point{})
         seconds = std::chrono::duration<float>(now - last_tick).count();
      last_tick = now;
   }

   // < 0 until the second frame
   float GetSeconds() const
   {
      return seconds;
   }

private:
   std::chrono::steady_clock::time_point last_tick = {};
   float seconds = -1.f;
};

// What happened in the current frame
struct FrameState
{
   bool resolve_ran = false;
   SRResult sr_result = SRResult::Off;
};

// The mod's state for a device, and its work at the TAA resolve and at present
struct NeedForSpeed2016GameDeviceData final : public GameDeviceData
{
   DrawOrDispatchOverrideType ReplaceResolve(ID3D11DeviceContext* context, DeviceData& device_data, ResolveVariant variant, std::function<void()>* original_dispatch);
   void OnPresent(DeviceData& device_data);

   CameraCapture camera_capture;
   SuperResolutionPass super_resolution;
   Diagnostics diagnostics;
   FrameTimer frame_timer;
   FrameState frame;
   RenderSize render_size; // The resolve's color size

private:
   void UpdateRenderSize(const ResolveResources& resources);
   bool RunSuperResolution(ID3D11DeviceContext* context, DeviceData& device_data, const ResolveResources& resources);
   void UpdateTextureMipBias(DeviceData& device_data) const;
};

class NeedForSpeed2016 final : public Game
{
public:
   // Luma's global settings for the mod, at load
   static void SetLumaGlobals();
   static void UnregisterEvents();

   void OnInit(bool async) override;
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override;
   void OnDestroyDeviceData(DeviceData& device_data) override;
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override;
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override;
   void LoadConfigs() override;
   void DrawImGuiSettings(DeviceData& device_data) override;
#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override;
#endif
#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override;
#endif
   // Luma's About tab
   void PrintImGuiAbout() override
   {
      DrawAbout();
   }

private:
   // ReShade events, for the camera. They run for every map, destruction and binding of the game, so the cheap checks come first. The
   // SR interop's D3D12 device sends its events here too.
   static void RegisterEvents();
   static void OnBufferMapped(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data);
   static void OnResourceDestroyed(reshade::api::device* device, reshade::api::resource resource);
   static void OnDescriptorsPushed(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::pipeline_layout layout, uint32_t layout_param, const reshade::api::descriptor_table_update& update);

   // The mod's state for a device whose data Luma created (it calls the game only for those)
   static NeedForSpeed2016GameDeviceData& GetGameData(DeviceData& device_data)
   {
      return *static_cast<NeedForSpeed2016GameDeviceData*>(device_data.game);
   }
   static const NeedForSpeed2016GameDeviceData& GetGameData(const DeviceData& device_data)
   {
      return *static_cast<const NeedForSpeed2016GameDeviceData*>(device_data.game);
   }
   // Null until Luma created the device's data
   static NeedForSpeed2016GameDeviceData* FindGameData(reshade::api::device* device);
   // The game renders on the immediate context; deferred ones record command lists on its worker threads
   static bool IsImmediate(ID3D11DeviceContext* context)
   {
      return context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
   }

   void UpdateEngineSettings(const DeviceData& device_data, const NeedForSpeed2016GameDeviceData& data);

   // The engine's settings are process wide, like the game object
   bool engine_settings_searched = false;
   Frostbite::SettingsLookup settings_lookup;
   EngineJitter engine_jitter;
   RenderScale render_scale;
};

// Replaces the resolve with SR (or the Development TAA bypass), else lets the game's run
DrawOrDispatchOverrideType NeedForSpeed2016GameDeviceData::ReplaceResolve(ID3D11DeviceContext* context, DeviceData& device_data, ResolveVariant variant, std::function<void()>* original_dispatch)
{
   frame.resolve_ran = true;
   const bool sr_selected = IsSRSelected(device_data);

   // The depth of field variant's second layer is only resolved by the game's shader, so it runs first. If SR then falls back, its
   // color output stays.
   const bool original_ran = variant == ResolveVariant::DepthOfField && (sr_selected || diagnostics.IsBypassOn()) && original_dispatch;
   if (original_ran)
      (*original_dispatch)();

   const ResolveResources resources = GetResolveResources(context);
   UpdateRenderSize(resources);

   if (diagnostics.TryBypassTAA(context, resources))
      return DrawOrDispatchOverrideType::Replaced;
   if (sr_selected && RunSuperResolution(context, device_data, resources))
      return DrawOrDispatchOverrideType::Replaced;
   // The game's resolve already ran if it was the depth of field one: core mustn't run it again
   if (original_ran)
      return DrawOrDispatchOverrideType::Replaced;
   return DrawOrDispatchOverrideType::None;
}

// The end of the frame. SR's history restarts after any frame SR didn't draw (menus, loading, FXAA, None or another SR type picked), as
// in Mass Effect Andromeda: core doesn't reset it, and the SR implementations keep their history while not selected.
void NeedForSpeed2016GameDeviceData::OnPresent(DeviceData& device_data)
{
   frame_timer.Tick();
   UpdateTextureMipBias(device_data);
   device_data.force_reset_sr = !device_data.has_drawn_sr;
   device_data.has_drawn_sr = false;
   device_data.has_drawn_main_post_processing = false;

   diagnostics.OnFrameEnd(frame.resolve_ran, frame.sr_result, camera_capture.GetCamera(), render_size);
   frame = {};
   camera_capture.EndFrame();
}

// The color input's size. The resolve also runs at 1280x720 at boot, before the swapchain is resized.
void NeedForSpeed2016GameDeviceData::UpdateRenderSize(const ResolveResources& resources)
{
   const RenderSize color_size = GetTextureSize(resources.color.get());
   if (color_size.width > 0)
      render_size = color_size;
}

// SR in place of the resolve. False if the game's resolve has to run instead.
bool NeedForSpeed2016GameDeviceData::RunSuperResolution(ID3D11DeviceContext* context, DeviceData& device_data, const ResolveResources& resources)
{
   const Camera* camera = camera_capture.GetCamera();
   frame.sr_result = super_resolution.Draw(context, device_data, resources, camera, render_size, frame_timer.GetSeconds());
   diagnostics.OnSRResult(frame.sr_result, device_data.sr_type, camera, render_size, resources);
   if (frame.sr_result != SRResult::Drawn)
      return false;
   device_data.has_drawn_sr = true;
   device_data.has_drawn_main_post_processing = true;
   return true;
}

// As in Mass Effect Andromeda: SR's own bias without upscaling (-1 for DLSS and FSR, 0 for XeSS), -1 for the game's TAA, and none without
// them (FXAA, menus). The resolution scale shifts it (rendering at twice the screen's resolution samples textures a mip finer), never
// above 0. Unless a custom bias is set in the dev settings.
void NeedForSpeed2016GameDeviceData::UpdateTextureMipBias(DeviceData& device_data) const
{
   if (!enable_samplers_upgrade || custom_texture_mip_lod_bias_offset)
      return;
   float bias = 0.f;
   if (frame.resolve_ran && render_size.height > 0)
   {
      const float height = float(render_size.height);
      float taa_bias = -1.f;
      if (frame.sr_result == SRResult::Drawn)
         taa_bias = SR::GetMipLODBias(device_data.sr_type, height, height);
      const float scale = height / (std::max)(device_data.output_resolution.y, 1.f);
      bias = (std::min)(taa_bias + std::log2(scale), 0.f);
   }

   // Core reads it under a shared lock from any thread, and only this thread writes it
   if (device_data.texture_mip_lod_bias_offset == bias)
      return;
   const std::unique_lock lock(s_mutex_samplers);
   device_data.texture_mip_lod_bias_offset = bias;
}

// Anti-aliasing only (as in Mass Effect Andromeda), with sharper textures
void NeedForSpeed2016::SetLumaGlobals()
{
   // SetGlobals() copies every string, so none can be null (the dll would fail to load)
   Globals::SetGlobals(PROJECT_NAME, "Need for Speed (2016) Luma mod", "", 1);
   Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

   // No HDR or format upgrades, and no display composition pass (it darkens the game's SDR swapchain)
   swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
   swapchain_upgrade_type = SwapchainUpgradeType::None;
   texture_format_upgrades_type = TextureFormatUpgradesType::None;
   force_disable_display_composition = true;

   // Exclusive fullscreen stays allowed, as the game was tested with (preventing it also adds a waitable object to the swapchain)
   prevent_fullscreen_state = false;

   auto_dump = false;

   // 16x anisotropic filtering, plus an additive mip LOD bias (mode 4, set every frame at present)
   enable_samplers_upgrade = true;
   samplers_upgrade_mode = 4;
}

// Once, when Luma starts. No shaders are replaced, so no Luma constant buffers are needed.
void NeedForSpeed2016::OnInit(bool async)
{
   luma_settings_cbuffer_index = -1;
   luma_data_cbuffer_index = -1;
   luma_ui_cbuffer_index = -1;
   RegisterEvents();
}

// The mod's state for the game's device
void NeedForSpeed2016::OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data)
{
   device_data.game = new NeedForSpeed2016GameDeviceData;
}

// GameDeviceData has no virtual destructor, so it's deleted as the mod's type
void NeedForSpeed2016::OnDestroyDeviceData(DeviceData& device_data)
{
   delete static_cast<NeedForSpeed2016GameDeviceData*>(device_data.game);
   device_data.game = nullptr;
}

// Every draw and dispatch of the game: only the TAA resolve's are replaced
DrawOrDispatchOverrideType NeedForSpeed2016::OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func)
{
   const ResolveVariant variant = GetResolveVariant(original_shader_hashes);
   if (variant == ResolveVariant::None)
      return DrawOrDispatchOverrideType::None;
   // For the SR status icon in Luma's settings
   device_data.taa_detected = true;

   NeedForSpeed2016GameDeviceData& data = GetGameData(device_data);
   // The game dispatches the resolve on the immediate context, where the camera is captured
   if (!IsImmediate(native_device_context))
   {
      data.diagnostics.OnDeferredResolve();
      return DrawOrDispatchOverrideType::None;
   }
   return data.ReplaceResolve(native_device_context, device_data, variant, original_draw_dispatch_func);
}

// The engine's settings first: the device's frame end forgets this frame's camera, which the first lookup waits for
void NeedForSpeed2016::OnPresent(ID3D11Device* native_device, DeviceData& device_data)
{
   NeedForSpeed2016GameDeviceData& data = GetGameData(device_data);
   UpdateEngineSettings(device_data, data);
   data.OnPresent(device_data);
}

// The mod's saved settings, when Luma loads its own
void NeedForSpeed2016::LoadConfigs()
{
   render_scale.LoadConfig();
}

// Below Luma's Super Resolution setting
void NeedForSpeed2016::DrawImGuiSettings(DeviceData& device_data)
{
   render_scale.DrawSetting();
}

#if DEVELOPMENT
// Luma's dev settings, Development builds only
void NeedForSpeed2016::DrawImGuiDevSettings(DeviceData& device_data)
{
   if (device_data.game)
      GetGameData(device_data).diagnostics.DrawDevSettings();
}
#endif

#if DEVELOPMENT || TEST
// Luma's Info tab, Development and Test builds
void NeedForSpeed2016::PrintImGuiInfo(const DeviceData& device_data)
{
   if (device_data.game)
      GetGameData(device_data).diagnostics.DrawInfo(device_data.texture_mip_lod_bias_offset, engine_jitter.Describe());
}
#endif

// The camera capture's ReShade events (core registers its own)
void NeedForSpeed2016::RegisterEvents()
{
   reshade::register_event<reshade::addon_event::map_buffer_region>(OnBufferMapped);
   reshade::register_event<reshade::addon_event::destroy_resource>(OnResourceDestroyed);
   reshade::register_event<reshade::addon_event::push_descriptors>(OnDescriptorsPushed);
}

// So a reload doesn't register them twice
void NeedForSpeed2016::UnregisterEvents()
{
   reshade::unregister_event<reshade::addon_event::map_buffer_region>(OnBufferMapped);
   reshade::unregister_event<reshade::addon_event::destroy_resource>(OnResourceDestroyed);
   reshade::unregister_event<reshade::addon_event::push_descriptors>(OnDescriptorsPushed);
}

// Records the large dynamic constant buffers' CPU memory, where the camera is read when it's bound
void NeedForSpeed2016::OnBufferMapped(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
{
   const bool writes = access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard;
   if (device->get_api() != reshade::api::device_api::d3d11 || !writes || !data || !*data)
      return;
   D3D11_BUFFER_DESC desc = {};
   reinterpret_cast<ID3D11Buffer*>(resource.handle)->GetDesc(&desc);
   if (!CameraCapture::MayHoldCamera(desc))
      return;
   if (NeedForSpeed2016GameDeviceData* game_data = FindGameData(device))
      game_data->camera_capture.OnBufferMapped(resource.handle, *data, desc.ByteWidth);
}

// So a destroyed buffer's reused handle isn't taken for the old mapping
void NeedForSpeed2016::OnResourceDestroyed(reshade::api::device* device, reshade::api::resource resource)
{
   if (device->get_api() != reshade::api::device_api::d3d11)
      return;
   if (NeedForSpeed2016GameDeviceData* game_data = FindGameData(device))
      game_data->camera_capture.OnBufferDestroyed(resource.handle);
}

// The game binds the camera on the immediate context
void NeedForSpeed2016::OnDescriptorsPushed(reshade::api::command_list* cmd_list, reshade::api::shader_stage stages, reshade::api::pipeline_layout layout, uint32_t layout_param, const reshade::api::descriptor_table_update& update)
{
   const reshade::api::buffer_range* binding = CameraCapture::FindBinding(stages, update);
   reshade::api::device* device = cmd_list->get_device();
   if (!binding || device->get_api() != reshade::api::device_api::d3d11)
      return;
   if (!IsImmediate(reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native())))
      return;
   if (NeedForSpeed2016GameDeviceData* game_data = FindGameData(device))
      game_data->camera_capture.OnCameraBound(*binding, game_data->render_size);
}

// ReShade's events can come before Luma created the device's data, and from the SR interop's D3D12 device
NeedForSpeed2016GameDeviceData* NeedForSpeed2016::FindGameData(reshade::api::device* device)
{
   DeviceData* device_data = device->get_private_data<DeviceData>();
   if (!device_data || !device_data->game)
      return nullptr;
   return static_cast<NeedForSpeed2016GameDeviceData*>(device_data->game);
}

// The engine's settings are looked up at the first frame in game (one with a camera), and the jitter is then set every frame
void NeedForSpeed2016::UpdateEngineSettings(const DeviceData& device_data, const NeedForSpeed2016GameDeviceData& data)
{
   if (!engine_settings_searched && data.camera_capture.GetCamera())
   {
      engine_settings_searched = true;
      if (!settings_lookup.Find("WorldRender"))
         LogDiagnostic(reshade::log::level::warning, "the engine's settings lookup wasn't found, so the jitter and the resolution scale can't be set");
      engine_jitter.Find(settings_lookup);
      render_scale.Find(settings_lookup);
   }
   const bool sr_selected = IsSRSelected(device_data);
   uint32_t sr_phases = 0;
   if (sr_selected)
      sr_phases = GetSRJitterPhases(device_data);
   engine_jitter.Apply(sr_selected, sr_phases);
}

// The globals and the game object before core starts, and the mod's events off before core shuts down
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      NeedForSpeed2016::SetLumaGlobals();
      game = new NeedForSpeed2016();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      NeedForSpeed2016::UnregisterEvents();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);
   return TRUE;
}
