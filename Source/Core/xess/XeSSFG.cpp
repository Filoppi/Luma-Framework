#include "XeSSFG.h"

#if ENABLE_XESS_FG

#include "xess_fg/xefg_swapchain.h"
#include "xess_fg/xefg_swapchain_d3d12.h"
#include "xell/xell.h"
#include "xell/xell_d3d12.h"

#include "../includes/debug.h"
#include "../utils/system.h"

#include <include/reshade.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>

namespace Intel
{
   namespace
   {
      // Shipped next to the Luma addon (see "UseLumaXeSSFG" in "Luma.Features.props")
      constexpr const wchar_t* xess_fg_dll_name = L"libxess_fg.dll";
      constexpr const wchar_t* xell_dll_name = L"libxell.dll";

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("XeSS-FG: " + message).c_str());
      }

      struct XeSSFGInstanceData : public FG::InstanceData
      {
         HMODULE xess_fg_module = nullptr;
         HMODULE xell_module = nullptr;

         decltype(&xellD3D12CreateContext) xell_create_context = nullptr;
         decltype(&xellDestroyContext) xell_destroy_context = nullptr;
         decltype(&xellSetSleepMode) xell_set_sleep_mode = nullptr;
         decltype(&xellSleep) xell_sleep = nullptr;
         decltype(&xellAddMarkerData) xell_add_marker_data = nullptr;
         decltype(&xellSetLoggingCallback) xell_set_logging_callback = nullptr;

         decltype(&xefgSwapChainD3D12CreateContext) create_context = nullptr;
         decltype(&xefgSwapChainDestroy) destroy = nullptr;
         decltype(&xefgSwapChainSetLatencyReduction) set_latency_reduction = nullptr;
         decltype(&xefgSwapChainSetLoggingCallback) set_logging_callback = nullptr;
         decltype(&xefgSwapChainGetProperties) get_properties = nullptr;
         decltype(&xefgSwapChainD3D12InitFromSwapChainDesc) init_from_swapchain_desc = nullptr;
         decltype(&xefgSwapChainD3D12GetSwapChainPtr) get_swapchain_ptr = nullptr;
         decltype(&xefgSwapChainD3D12TagFrameResource) tag_frame_resource = nullptr;
         decltype(&xefgSwapChainTagFrameConstants) tag_frame_constants = nullptr;
         decltype(&xefgSwapChainSetEnabled) set_enabled = nullptr;
         decltype(&xefgSwapChainSetPresentId) set_present_id = nullptr;
         decltype(&xefgSwapChainGetLastPresentStatus) get_last_present_status = nullptr;
         decltype(&xefgSwapChainSetNumInterpolatedFrames) set_num_interpolated_frames = nullptr;

         ID3D12Device* device = nullptr; // Owned by the caller
         xell_context_handle_t xell_context = nullptr;
         // Created once by "Init()" (to check support), and re-created for every swapchain (destroying the swapchain destroys it)
         xefg_swapchain_handle_t context = nullptr;
         bool swapchain_initialized = false;
         uint32_t init_flags = 0; // The ones the swapchain was initialized with
         bool needs_swapchain_recreation = false;
         bool enabled = false;
         uint32_t interpolated_frames = 0; // The ones currently set (0 means the default, which is the max)

         uint32_t frame_id = 0;
         uint32_t next_marker = XELL_SIMULATION_START; // The next XeLL marker that needs to be sent for the current frame (they need to be in order)

         unsigned int last_presented_frames = 1;
         unsigned int logged_errors = 0;
      };

      void LogError(XeSSFGInstanceData& data, const std::string& message)
      {
         if (data.logged_errors++ < 10) // Don't spam the log
            Log(reshade::log::level::error, message);
      }

      // Positive results are warnings, the call still succeeded
      bool Failed(xefg_swapchain_result_t result)
      {
         return result < XEFG_SWAPCHAIN_RESULT_SUCCESS;
      }

      template<typename T>
      bool GetFunction(HMODULE module, const char* name, T& function)
      {
         function = reinterpret_cast<T>(GetProcAddress(module, name));
         return function != nullptr;
      }

      uint32_t GetInitFlags(const FG::SettingsData& settings_data)
      {
         uint32_t init_flags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
         if (settings_data.inverted_depth)
            init_flags |= XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH;
         if (settings_data.mvs_jittered)
            init_flags |= XEFG_SWAPCHAIN_INIT_FLAG_JITTERED_MV;
         if (!settings_data.ui_premultiplied_alpha)
            init_flags |= XEFG_SWAPCHAIN_INIT_FLAG_UITEXTURE_NOT_PREMUL_ALPHA;
         return init_flags;
      }

      bool CreateContext(XeSSFGInstanceData& data)
      {
         if (data.context)
            return true;
         const xefg_swapchain_result_t result = data.create_context(data.device, &data.context);
         if (Failed(result))
         {
            data.context = nullptr;
            Log(reshade::log::level::info, std::format("XeSS-FG is not supported on this GPU, error = {}", int(result)));
            return false;
         }
#if DEVELOPMENT || TEST
         data.set_logging_callback(data.context, XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING, [](const char* message, xefg_swapchain_logging_level_t level, void*)
            { Log((level == XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR ? reshade::log::level::error : reshade::log::level::warning), (message ? message : "")); }, nullptr);
#else
         data.set_logging_callback(data.context, XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR, [](const char* message, xefg_swapchain_logging_level_t level, void*)
            { Log(reshade::log::level::error, (message ? message : "")); }, nullptr);
#endif
         // The XeLL context needs to be connected before the swapchain is initialized
         if (Failed(data.set_latency_reduction(data.context, data.xell_context)))
         {
            Log(reshade::log::level::error, "xefgSwapChainSetLatencyReduction failed");
            data.destroy(data.context);
            data.context = nullptr;
            return false;
         }
         return true;
      }

      // Sends the latency markers that are still missing (in order) up to the given one (included)
      void SendMarkers(XeSSFGInstanceData& data, uint32_t frame_id, xell_latency_marker_type_t last_marker)
      {
         if (data.frame_id != frame_id)
         {
            data.frame_id = frame_id;
            data.next_marker = XELL_SIMULATION_START;
         }
         // We don't know when the DX11 game simulates and renders, so the markers we don't receive are sent at the latest possible time (in front of the present)
         for (; data.next_marker <= uint32_t(last_marker); data.next_marker++)
            data.xell_add_marker_data(data.xell_context, frame_id, xell_latency_marker_type_t(data.next_marker));
      }
   }

   bool XeSSFG::Init(FG::InstanceData*& data, IDXGIAdapter* adapter, ID3D12Device* device)
   {
      if (data)
         Deinit(data); // This will also null the pointer
      if (!device)
         return false;

      auto* custom_data = new XeSSFGInstanceData();
      data = custom_data;
      custom_data->device = device;

      // From next to Luma, by full path, so the XeSS dlls the game ships (possibly older versions) are separate modules
      HMODULE luma_module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Log), &luma_module);
      const std::filesystem::path luma_folder = System::GetModulePath(luma_module).parent_path();
      custom_data->xess_fg_module = LoadLibraryExW((luma_folder / xess_fg_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      custom_data->xell_module = LoadLibraryExW((luma_folder / xell_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!custom_data->xess_fg_module || !custom_data->xell_module)
      {
         Log(reshade::log::level::info, "libxess_fg.dll or libxell.dll weren't found next to Luma, XeSS-FG won't be available");
         return false;
      }

      HMODULE xell = custom_data->xell_module;
      HMODULE xess_fg = custom_data->xess_fg_module;
      if (!GetFunction(xell, "xellD3D12CreateContext", custom_data->xell_create_context)
         || !GetFunction(xell, "xellDestroyContext", custom_data->xell_destroy_context)
         || !GetFunction(xell, "xellSetSleepMode", custom_data->xell_set_sleep_mode)
         || !GetFunction(xell, "xellSleep", custom_data->xell_sleep)
         || !GetFunction(xell, "xellAddMarkerData", custom_data->xell_add_marker_data)
         || !GetFunction(xell, "xellSetLoggingCallback", custom_data->xell_set_logging_callback)
         || !GetFunction(xess_fg, "xefgSwapChainD3D12CreateContext", custom_data->create_context)
         || !GetFunction(xess_fg, "xefgSwapChainDestroy", custom_data->destroy)
         || !GetFunction(xess_fg, "xefgSwapChainSetLatencyReduction", custom_data->set_latency_reduction)
         || !GetFunction(xess_fg, "xefgSwapChainSetLoggingCallback", custom_data->set_logging_callback)
         || !GetFunction(xess_fg, "xefgSwapChainGetProperties", custom_data->get_properties)
         || !GetFunction(xess_fg, "xefgSwapChainD3D12InitFromSwapChainDesc", custom_data->init_from_swapchain_desc)
         || !GetFunction(xess_fg, "xefgSwapChainD3D12GetSwapChainPtr", custom_data->get_swapchain_ptr)
         || !GetFunction(xess_fg, "xefgSwapChainD3D12TagFrameResource", custom_data->tag_frame_resource)
         || !GetFunction(xess_fg, "xefgSwapChainTagFrameConstants", custom_data->tag_frame_constants)
         || !GetFunction(xess_fg, "xefgSwapChainSetEnabled", custom_data->set_enabled)
         || !GetFunction(xess_fg, "xefgSwapChainSetPresentId", custom_data->set_present_id)
         || !GetFunction(xess_fg, "xefgSwapChainGetLastPresentStatus", custom_data->get_last_present_status)
         || !GetFunction(xess_fg, "xefgSwapChainSetNumInterpolatedFrames", custom_data->set_num_interpolated_frames))
      {
         Log(reshade::log::level::error, "The XeSS-FG or XeLL dlls are missing exports");
         return false;
      }

      // XeLL is required on all GPUs
      if (custom_data->xell_create_context(device, &custom_data->xell_context) != XELL_RESULT_SUCCESS || !custom_data->xell_context)
      {
         custom_data->xell_context = nullptr;
         Log(reshade::log::level::info, "XeLL is not supported on this GPU, XeSS-FG won't be available");
         return false;
      }
#if DEVELOPMENT || TEST
      custom_data->xell_set_logging_callback(custom_data->xell_context, XELL_LOGGING_LEVEL_WARNING, [](const char* message, xell_logging_level_t level)
         { Log((level == XELL_LOGGING_LEVEL_ERROR ? reshade::log::level::error : reshade::log::level::warning), ("XeLL: " + std::string(message ? message : ""))); });
#endif
      xell_sleep_params_t sleep_params = {};
      sleep_params.bLowLatencyMode = 1;
      if (custom_data->xell_set_sleep_mode(custom_data->xell_context, &sleep_params) != XELL_RESULT_SUCCESS)
         Log(reshade::log::level::warning, "xellSetSleepMode failed");

      // Creating a context is the support check
      if (!CreateContext(*custom_data))
         return false;
      xefg_swapchain_properties_t properties = {};
      if (!Failed(custom_data->get_properties(custom_data->context, &properties)))
         custom_data->max_generated_frames = (std::max)(properties.maxSupportedInterpolations, 1u);
      custom_data->min_resolution = 32;

      custom_data->is_supported = true;
      Log(reshade::log::level::info, std::format("Initialized (up to {}x)", custom_data->max_generated_frames + 1));
      return true;
   }

   void XeSSFG::Deinit(FG::InstanceData*& data)
   {
      auto& custom_data = reinterpret_cast<XeSSFGInstanceData*&>(data);
      if (!custom_data)
         return;
      // XeSS-FG needs to be destroyed before XeLL
      if (custom_data->context && Failed(custom_data->destroy(custom_data->context)))
         Log(reshade::log::level::warning, "xefgSwapChainDestroy failed");
      if (custom_data->xell_context && custom_data->xell_destroy_context(custom_data->xell_context) != XELL_RESULT_SUCCESS)
         Log(reshade::log::level::warning, "xellDestroyContext failed");
      HMODULE xess_fg_module = custom_data->xess_fg_module;
      HMODULE xell_module = custom_data->xell_module;
      delete custom_data;
      custom_data = nullptr;
      if (xess_fg_module)
         FreeLibrary(xess_fg_module);
      if (xell_module)
         FreeLibrary(xell_module);
   }

   HRESULT XeSSFG::CreateSwapchain(FG::InstanceData* data, IDXGIFactory2* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain4** swapchain)
   {
      auto* custom_data = static_cast<XeSSFGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || custom_data->swapchain_initialized || !CreateContext(*custom_data))
         return E_FAIL;

      xefg_swapchain_d3d12_init_params_t init_params = {};
      init_params.initFlags = GetInitFlags(custom_data->settings_data);
      init_params.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
      init_params.uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO;
      // The window can't have any other swapchain, which is why the game one is moved to a hidden window
      xefg_swapchain_result_t result = custom_data->init_from_swapchain_desc(custom_data->context, hwnd, &desc, nullptr, command_queue, factory, &init_params);
      if (!Failed(result))
         result = custom_data->get_swapchain_ptr(custom_data->context, IID_PPV_ARGS(swapchain));
      if (Failed(result))
      {
         LogError(*custom_data, std::format("Failed to create the swapchain, error = {}", int(result)));
         // Start from a clean slate next time
         custom_data->destroy(custom_data->context);
         custom_data->context = nullptr;
         return E_FAIL;
      }
      custom_data->swapchain_initialized = true;
      custom_data->init_flags = init_params.initFlags;
      custom_data->needs_swapchain_recreation = false;
      custom_data->enabled = false; // It's disabled by default
      custom_data->interpolated_frames = 0;
      return S_OK;
   }

   void XeSSFG::DestroySwapchain(FG::InstanceData* data)
   {
      auto* custom_data = static_cast<XeSSFGInstanceData*>(data);
      if (!custom_data || !custom_data->context)
         return;
      // This releases the native swapchain too (all the references to the proxy swapchain have already been released)
      if (Failed(custom_data->destroy(custom_data->context)))
         Log(reshade::log::level::warning, "xefgSwapChainDestroy failed");
      custom_data->context = nullptr;
      custom_data->swapchain_initialized = false;
      custom_data->enabled = false;
   }

   bool XeSSFG::UpdateSettings(FG::InstanceData* data, const FG::SettingsData& settings_data)
   {
      auto* custom_data = static_cast<XeSSFGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported)
         return false;
      custom_data->settings_data = settings_data;
      // Some settings can only be set when initializing the swapchain
      custom_data->needs_swapchain_recreation = custom_data->swapchain_initialized && GetInitFlags(settings_data) != custom_data->init_flags;
      // XeSS-FG only supports HDR10 for HDR (not scRGB), which is checked by the swapchain proxy
      return settings_data.output_width >= custom_data->min_resolution && settings_data.output_height >= custom_data->min_resolution;
   }

   bool XeSSFG::NeedsSwapchainRecreation(const FG::InstanceData* data) const
   {
      const auto* custom_data = static_cast<const XeSSFGInstanceData*>(data);
      return custom_data && custom_data->needs_swapchain_recreation;
   }

   void XeSSFG::BeginFrame(FG::InstanceData* data, uint32_t frame_id)
   {
      auto* custom_data = static_cast<XeSSFGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || !custom_data->xell_context)
         return;
      // XeLL needs to sleep (and receive all the markers) every frame, even if frame generation is off. The sleep needs to be the first call of the frame.
      custom_data->xell_sleep(custom_data->xell_context, frame_id);
      custom_data->frame_id = frame_id;
      custom_data->next_marker = XELL_SIMULATION_START;
      SendMarkers(*custom_data, frame_id, XELL_SIMULATION_START);
   }

   bool XeSSFG::PrepareFrame(FG::InstanceData* data, ID3D12GraphicsCommandList* command_list, const FG::FrameData& frame_data, bool generate)
   {
      auto* custom_data = static_cast<XeSSFGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || !custom_data->xell_context)
         return false;
      SendMarkers(*custom_data, frame_data.frame_id, XELL_RENDERSUBMIT_END);
      if (!custom_data->context || !custom_data->swapchain_initialized)
         return false;

      const FG::SettingsData& settings_data = custom_data->settings_data;
      if (generate)
      {
         const uint32_t interpolated_frames = std::clamp(settings_data.generated_frames, 1u, custom_data->max_generated_frames);
         if (interpolated_frames != custom_data->interpolated_frames)
         {
            // This can be slow (it might compile shaders), but it only happens when the user changes the setting
            const xefg_swapchain_result_t result = custom_data->set_num_interpolated_frames(custom_data->context, interpolated_frames);
            if (Failed(result))
               LogError(*custom_data, std::format("xefgSwapChainSetNumInterpolatedFrames failed, error = {}", int(result)));
            custom_data->interpolated_frames = interpolated_frames;
         }

         // The resources are only valid now (they are shared with DX11), so XeSS-FG copies them on our command list
         constexpr xefg_swapchain_resource_type_t resource_types[size_t(FG::InputType::Count)] = { XEFG_SWAPCHAIN_RES_DEPTH, XEFG_SWAPCHAIN_RES_MOTION_VECTOR, XEFG_SWAPCHAIN_RES_HUDLESS_COLOR, XEFG_SWAPCHAIN_RES_UI };
         for (size_t i = 0; i < size_t(FG::InputType::Count); i++)
         {
            const FG::Resource& input = frame_data.inputs[i];
            if (!input.resource)
               continue;
            xefg_swapchain_d3d12_resource_data_t resource_data = {};
            resource_data.type = resource_types[i];
            resource_data.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
            resource_data.resourceBase = { input.left, input.top };
            resource_data.resourceSize = { input.width, input.height };
            resource_data.pResource = input.resource;
            resource_data.incomingState = input.state;
            const xefg_swapchain_result_t result = custom_data->tag_frame_resource(custom_data->context, command_list, frame_data.frame_id, &resource_data);
            if (Failed(result))
            {
               LogError(*custom_data, std::format("xefgSwapChainD3D12TagFrameResource failed, error = {}", int(result)));
               generate = false;
            }
         }

         const FG::FrameConstants& frame_constants = frame_data.constants;
         xefg_swapchain_frame_constant_data_t constants = {};
         std::memcpy(constants.viewMatrix, frame_constants.world_to_view, sizeof(constants.viewMatrix));
         if (frame_constants.has_projection_matrix || frame_constants.vert_fov <= 0.f)
         {
            std::memcpy(constants.projectionMatrix, frame_constants.view_to_clip, sizeof(constants.projectionMatrix));
         }
         else
         {
            // DX style (left handed, row vectors) perspective projection
            float near_plane = frame_constants.near_plane;
            float far_plane = frame_constants.far_plane;
            if (settings_data.inverted_depth)
               std::swap(near_plane, far_plane);
            const float y_scale = 1.f / std::tan(frame_constants.vert_fov * 0.5f);
            const float z_range = far_plane / (far_plane - near_plane);
            constants.projectionMatrix[0] = y_scale / frame_constants.aspect_ratio;
            constants.projectionMatrix[5] = y_scale;
            constants.projectionMatrix[10] = z_range;
            constants.projectionMatrix[11] = 1.f;
            constants.projectionMatrix[14] = -near_plane * z_range;
         }
         constants.jitterOffsetX = frame_constants.jitter_x;
         constants.jitterOffsetY = frame_constants.jitter_y;
         // In pixels, from the current position to the previous one
         constants.motionVectorScaleX = frame_constants.mvs_x_scale;
         constants.motionVectorScaleY = frame_constants.mvs_y_scale;
         constants.resetHistory = frame_constants.reset ? 1 : 0;
         constants.frameRenderTime = frame_constants.frame_time_ms;
         const xefg_swapchain_result_t result = custom_data->tag_frame_constants(custom_data->context, frame_data.frame_id, &constants);
         if (Failed(result))
         {
            LogError(*custom_data, std::format("xefgSwapChainTagFrameConstants failed, error = {}", int(result)));
            generate = false;
         }
      }

      if (generate != custom_data->enabled)
      {
         // Disabling it also resets its history
         const xefg_swapchain_result_t result = custom_data->set_enabled(custom_data->context, generate ? 1 : 0);
         if (Failed(result))
         {
            LogError(*custom_data, std::format("xefgSwapChainSetEnabled failed, error = {}", int(result)));
            generate = false;
         }
         custom_data->enabled = generate;
      }
      // The present id needs to match the XeLL frame id
      custom_data->set_present_id(custom_data->context, frame_data.frame_id);
      return generate;
   }

   HRESULT XeSSFG::Present(FG::InstanceData* data, IDXGISwapChain4* swapchain, uint32_t frame_id, UINT sync_interval, UINT flags)
   {
      auto* custom_data = static_cast<XeSSFGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || !custom_data->xell_context)
         return swapchain->Present(sync_interval, flags);

      SendMarkers(*custom_data, frame_id, XELL_PRESENT_START);
      const HRESULT hr = swapchain->Present(sync_interval, flags);
      SendMarkers(*custom_data, frame_id, XELL_PRESENT_END);

      custom_data->last_presented_frames = 1;
      xefg_swapchain_present_status_t present_status = {};
      if (custom_data->context && !Failed(custom_data->get_last_present_status(custom_data->context, &present_status)))
      {
         custom_data->last_presented_frames = (std::max)(present_status.framesPresented, 1u);
         if (present_status.isFrameGenEnabled && Failed(present_status.frameGenResult))
            LogError(*custom_data, std::format("Frame generation failed, error = {}", int(present_status.frameGenResult)));
      }
      return hr;
   }

   unsigned int XeSSFG::GetLastPresentedFramesCount(const FG::InstanceData* data) const
   {
      const auto* custom_data = static_cast<const XeSSFGInstanceData*>(data);
      return custom_data ? custom_data->last_presented_frames : 1;
   }
} // namespace Intel

#endif
