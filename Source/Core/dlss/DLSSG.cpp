#include "DLSSG.h"

#if ENABLE_DLSSG

#include "sl.h"
#include "sl_consts.h"
#include "sl_hooks.h"
#include "sl_dlss_g.h"
#include "sl_reflex.h"
#include "sl_pcl.h"
#include "sl_matrix_helpers.h"
#include "sl_security.h" // Has function definitions, it can only be included in one translation unit
#undef GetProc // Defined by "sl_security.h"

#include "../includes/debug.h"
#include "../utils/system.h"

#include <include/reshade.hpp>

#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>

namespace Streamline
{
   using Microsoft::WRL::ComPtr;

   namespace
   {
      // Shipped next to the Luma addon (see "UseLumaDLSSG" in "Luma.Features.props"), with the other Streamline plugins
      constexpr const wchar_t* interposer_dll_name = L"sl.interposer.dll";
      // A unique GUID generated for Luma (the same as the DLSS one), it's not registered by NVIDIA
      constexpr const char* project_id = "d8238c51-1f2f-438d-a309-38c16e33c716";
      constexpr const char* engine_version = "1.0";

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("DLSS-G: " + message).c_str());
      }

      // Streamline can only be initialized once per process
      std::atomic<bool> streamline_initialized = false;

      struct Functions
      {
         PFun_slInit* init = nullptr;
         PFun_slShutdown* shutdown = nullptr;
         PFun_slIsFeatureSupported* is_feature_supported = nullptr;
         PFun_slSetD3DDevice* set_d3d_device = nullptr;
         PFun_slUpgradeInterface* upgrade_interface = nullptr;
         PFun_slGetNativeInterface* get_native_interface = nullptr;
         PFun_slGetFeatureFunction* get_feature_function = nullptr;
         PFun_slGetNewFrameToken* get_new_frame_token = nullptr;
         PFun_slSetConstants* set_constants = nullptr;
         PFun_slSetTagForFrame* set_tag_for_frame = nullptr;
         PFun_slFreeResources* free_resources = nullptr;
         // Feature functions (available after the device is set)
         PFun_slDLSSGGetState* dlssg_get_state = nullptr;
         PFun_slDLSSGSetOptions* dlssg_set_options = nullptr;
         PFun_slReflexSetOptions* reflex_set_options = nullptr;
         PFun_slReflexSleep* reflex_sleep = nullptr;
         PFun_slPCLSetMarker* pcl_set_marker = nullptr;
      };

      struct DLSSGInstanceData : public FG::InstanceData
      {
         HMODULE module = nullptr;
         Functions functions;
         bool initialized = false; // Whether "slInit()" succeeded (and thus "slShutdown()" is needed)

         // The native DX12 device and Streamline proxies (for the functions it needs to hook, with manual hooking)
         ComPtr<ID3D12Device> device;
         ComPtr<ID3D12Device> device_proxy;
         ComPtr<ID3D12CommandQueue> command_queue_proxy;
         ComPtr<IDXGIFactory2> factory_proxy;

         sl::ViewportHandle viewport = sl::ViewportHandle(0u);
         sl::DLSSGOptions options = {}; // The last ones set
         bool options_set = false;
         bool runtime_error = false; // DLSS-G reported an invalid state, it's disabled

         sl::FrameToken* frame_token = nullptr;
         uint32_t frame_token_id = 0;
         uint32_t next_marker = 0; // The next PCL marker that needs to be sent for the current frame (they need to be in order)

         unsigned int last_presented_frames = 1;
         unsigned int logged_errors = 0;
      };

      void LogError(DLSSGInstanceData& data, const std::string& message)
      {
         if (data.logged_errors++ < 10) // Don't spam the log
            Log(reshade::log::level::error, message);
      }

      void LogMessageCallback(sl::LogType type, const char* message)
      {
         if (!message)
            return;
         std::string text = message;
         while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
#if !DEVELOPMENT && !TEST
         if (type == sl::LogType::eInfo)
            return;
#endif
         reshade::log::message(type == sl::LogType::eError ? reshade::log::level::error : (type == sl::LogType::eWarn ? reshade::log::level::warning : reshade::log::level::info), ("Streamline: " + text).c_str());
      }

      template<typename T>
      bool GetFunction(HMODULE module, const char* name, T*& function)
      {
         function = reinterpret_cast<T*>(GetProcAddress(module, name));
         return function != nullptr;
      }

      template<typename T>
      bool GetFeatureFunction(const Functions& functions, sl::Feature feature, const char* name, T*& function)
      {
         void* address = nullptr;
         if (functions.get_feature_function(feature, name, address) != sl::Result::eOk)
            return false;
         function = reinterpret_cast<T*>(address);
         return function != nullptr;
      }

      sl::float4x4 ToSL(const float matrix[16])
      {
         sl::float4x4 result;
         for (uint32_t i = 0; i < 4; i++)
            result.setRow(i, sl::float4(matrix[i * 4 + 0], matrix[i * 4 + 1], matrix[i * 4 + 2], matrix[i * 4 + 3]));
         return result;
      }

      sl::float4x4 Identity()
      {
         sl::float4x4 result;
         for (uint32_t i = 0; i < 4; i++)
            result.setRow(i, sl::float4(i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f, i == 3 ? 1.f : 0.f));
         return result;
      }

      // DX style (left handed, row vectors) perspective projection
      sl::float4x4 MakeProjection(float vert_fov, float aspect_ratio, float near_plane, float far_plane, bool inverted_depth)
      {
         if (inverted_depth)
            std::swap(near_plane, far_plane);
         const float y_scale = 1.f / std::tan(vert_fov * 0.5f);
         const float x_scale = y_scale / aspect_ratio;
         const float z_range = far_plane / (far_plane - near_plane);
         sl::float4x4 result;
         result.setRow(0, sl::float4(x_scale, 0.f, 0.f, 0.f));
         result.setRow(1, sl::float4(0.f, y_scale, 0.f, 0.f));
         result.setRow(2, sl::float4(0.f, 0.f, z_range, 1.f));
         result.setRow(3, sl::float4(0.f, 0.f, -near_plane * z_range, 0.f));
         return result;
      }

      void FillConstants(sl::Constants& constants, const FG::FrameData& frame_data, const FG::SettingsData& settings_data)
      {
         const FG::FrameConstants& frame_constants = frame_data.constants;
         const FG::Resource& motion_vectors = frame_data.inputs[size_t(FG::InputType::MotionVectors)];
         const float mvs_width = float(motion_vectors.width != 0 ? motion_vectors.width : settings_data.render_width);
         const float mvs_height = float(motion_vectors.height != 0 ? motion_vectors.height : settings_data.render_height);

         if (frame_constants.has_projection_matrix)
         {
            constants.cameraViewToClip = ToSL(frame_constants.view_to_clip);
         }
         else if (frame_constants.vert_fov > 0.f)
         {
            constants.cameraViewToClip = MakeProjection(frame_constants.vert_fov, frame_constants.aspect_ratio, frame_constants.near_plane, frame_constants.far_plane, settings_data.inverted_depth);
         }
         else
         {
            constants.cameraViewToClip = Identity();
         }
         sl::matrixFullInvert(constants.clipToCameraView, constants.cameraViewToClip);
         constants.clipToLensClip = Identity();

         sl::float4x4 camera_view_to_world = Identity();
         sl::float4x4 camera_view_to_world_prev = Identity();
         if (frame_constants.has_view_matrix)
         {
            sl::matrixFullInvert(camera_view_to_world, ToSL(frame_constants.world_to_view));
            sl::matrixFullInvert(camera_view_to_world_prev, ToSL(frame_constants.has_prev_matrices ? frame_constants.prev_world_to_view : frame_constants.world_to_view));
         }
         const sl::float4x4 camera_view_to_clip_prev = frame_constants.has_projection_matrix && frame_constants.has_prev_matrices ? ToSL(frame_constants.prev_view_to_clip) : constants.cameraViewToClip;
         sl::float4x4 camera_view_to_prev_camera_view;
         sl::calcCameraToPrevCamera(camera_view_to_prev_camera_view, camera_view_to_world, camera_view_to_world_prev);
         sl::float4x4 clip_to_prev_camera_view;
         sl::matrixMul(clip_to_prev_camera_view, constants.clipToCameraView, camera_view_to_prev_camera_view);
         sl::matrixMul(constants.clipToPrevClip, clip_to_prev_camera_view, camera_view_to_clip_prev);
         sl::matrixFullInvert(constants.prevClipToClip, constants.clipToPrevClip);

         constants.jitterOffset = sl::float2(frame_constants.jitter_x, frame_constants.jitter_y);
         // From pixels to UV space
         constants.mvecScale = sl::float2(frame_constants.mvs_x_scale / mvs_width, frame_constants.mvs_y_scale / mvs_height);
         constants.cameraPinholeOffset = sl::float2(0.f, 0.f);
         if (frame_constants.has_camera_vectors)
         {
            constants.cameraPos = sl::float3(frame_constants.camera_pos[0], frame_constants.camera_pos[1], frame_constants.camera_pos[2]);
            constants.cameraUp = sl::float3(frame_constants.camera_up[0], frame_constants.camera_up[1], frame_constants.camera_up[2]);
            constants.cameraRight = sl::float3(frame_constants.camera_right[0], frame_constants.camera_right[1], frame_constants.camera_right[2]);
            constants.cameraFwd = sl::float3(frame_constants.camera_forward[0], frame_constants.camera_forward[1], frame_constants.camera_forward[2]);
         }
         else
         {
            constants.cameraRight = sl::float3(camera_view_to_world[0].x, camera_view_to_world[0].y, camera_view_to_world[0].z);
            constants.cameraUp = sl::float3(camera_view_to_world[1].x, camera_view_to_world[1].y, camera_view_to_world[1].z);
            constants.cameraFwd = sl::float3(camera_view_to_world[2].x, camera_view_to_world[2].y, camera_view_to_world[2].z);
            constants.cameraPos = sl::float3(camera_view_to_world[3].x, camera_view_to_world[3].y, camera_view_to_world[3].z);
         }
         constants.cameraNear = frame_constants.near_plane;
         constants.cameraFar = frame_constants.far_plane;
         constants.cameraFOV = frame_constants.vert_fov > 0.f ? frame_constants.vert_fov : (2.f * std::atan(1.f / constants.cameraViewToClip[1].y));
         constants.cameraAspectRatio = frame_constants.aspect_ratio;
         constants.depthInverted = settings_data.inverted_depth ? sl::Boolean::eTrue : sl::Boolean::eFalse;
         constants.cameraMotionIncluded = sl::Boolean::eTrue;
         constants.motionVectors3D = sl::Boolean::eFalse;
         constants.reset = frame_constants.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
         constants.orthographicProjection = sl::Boolean::eFalse;
         constants.motionVectorsDilated = sl::Boolean::eFalse;
         constants.motionVectorsJittered = settings_data.mvs_jittered ? sl::Boolean::eTrue : sl::Boolean::eFalse;
      }

      // Gets the frame token for the frame, and sends the latency markers that are still missing (in order) up to the given one (included)
      void SendMarkers(DLSSGInstanceData& data, uint32_t frame_id, sl::PCLMarker last_marker)
      {
         if (!data.frame_token || data.frame_token_id != frame_id)
         {
            data.frame_token = nullptr;
            if (data.functions.get_new_frame_token(data.frame_token, &frame_id) != sl::Result::eOk || !data.frame_token)
               return;
            data.frame_token_id = frame_id;
            data.next_marker = uint32_t(sl::PCLMarker::eSimulationStart);
         }
         // We don't know when the DX11 game simulates and renders, so the markers we don't receive are sent at the latest possible time (in front of the present)
         for (; data.next_marker <= uint32_t(last_marker); data.next_marker++)
            data.functions.pcl_set_marker(sl::PCLMarker(data.next_marker), *data.frame_token);
      }
   }

   bool DLSSG::Init(FG::InstanceData*& data, IDXGIAdapter* adapter, ID3D12Device* device)
   {
      if (data)
         Deinit(data); // This will also null the pointer
      if (!adapter || !device || streamline_initialized.exchange(true))
         return false;

      auto* custom_data = new DLSSGInstanceData();
      data = custom_data;
      custom_data->device = device;

      // From next to Luma, by full path
      HMODULE luma_module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Log), &luma_module);
      const std::filesystem::path luma_folder = System::GetModulePath(luma_module).parent_path();
      const std::filesystem::path interposer_path = luma_folder / interposer_dll_name;

      // If the game already loaded its own Streamline, we can't use another one
      std::error_code error_code;
      if (HMODULE game_interposer = GetModuleHandleW(interposer_dll_name); game_interposer && !std::filesystem::equivalent(System::GetModulePath(game_interposer), interposer_path, error_code))
      {
         Log(reshade::log::level::warning, "The game already loaded Streamline, DLSS-G won't be available");
         return false;
      }
      if (!std::filesystem::is_regular_file(interposer_path))
      {
         Log(reshade::log::level::info, "sl.interposer.dll wasn't found next to Luma, DLSS-G won't be available");
         return false;
      }
      if (!sl::security::verifyEmbeddedSignature(interposer_path.c_str()))
      {
         Log(reshade::log::level::error, "sl.interposer.dll isn't signed by NVIDIA, DLSS-G won't be available");
         return false;
      }
      custom_data->module = LoadLibraryExW(interposer_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      Functions& functions = custom_data->functions;
      if (!custom_data->module
         || !GetFunction(custom_data->module, "slInit", functions.init)
         || !GetFunction(custom_data->module, "slShutdown", functions.shutdown)
         || !GetFunction(custom_data->module, "slIsFeatureSupported", functions.is_feature_supported)
         || !GetFunction(custom_data->module, "slSetD3DDevice", functions.set_d3d_device)
         || !GetFunction(custom_data->module, "slUpgradeInterface", functions.upgrade_interface)
         || !GetFunction(custom_data->module, "slGetNativeInterface", functions.get_native_interface)
         || !GetFunction(custom_data->module, "slGetFeatureFunction", functions.get_feature_function)
         || !GetFunction(custom_data->module, "slGetNewFrameToken", functions.get_new_frame_token)
         || !GetFunction(custom_data->module, "slSetConstants", functions.set_constants)
         || !GetFunction(custom_data->module, "slSetTagForFrame", functions.set_tag_for_frame)
         || !GetFunction(custom_data->module, "slFreeResources", functions.free_resources))
      {
         Log(reshade::log::level::error, "Failed to load sl.interposer.dll");
         return false;
      }

      const std::wstring plugins_path = luma_folder.wstring();
      const wchar_t* plugins_paths[] = { plugins_path.c_str() };
      const sl::Feature features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
      sl::Preferences preferences;
      preferences.showConsole = false;
      preferences.logLevel = sl::LogLevel::eDefault;
      preferences.pathsToPlugins = plugins_paths;
      preferences.numPathsToPlugins = 1;
      preferences.logMessageCallback = &LogMessageCallback;
      // Manual hooking makes Streamline only see the objects we give to it (and the factory proxy keeps it from hooking all the DXGI factories of the process).
      // OTA updates are disabled to keep the behaviour predictable (the plugins next to Luma are used).
      preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseDXGIFactoryProxy | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
#if !DEVELOPMENT
      preferences.flags |= sl::PreferenceFlags::eDisableDebugText;
#endif
      preferences.featuresToLoad = features;
      preferences.numFeaturesToLoad = uint32_t(std::size(features));
      preferences.engine = sl::EngineType::eCustom;
      preferences.engineVersion = engine_version;
      preferences.projectId = project_id;
      preferences.renderAPI = sl::RenderAPI::eD3D12;
      sl::Result result = functions.init(preferences, sl::kSDKVersion);
      if (result != sl::Result::eOk)
      {
         Log(reshade::log::level::error, std::format("slInit failed, error = {}", int(result)));
         return false;
      }
      custom_data->initialized = true;

      result = functions.set_d3d_device(device);
      if (result != sl::Result::eOk)
      {
         Log(reshade::log::level::error, std::format("slSetD3DDevice failed, error = {}", int(result)));
         return false;
      }

      DXGI_ADAPTER_DESC adapter_desc = {};
      adapter->GetDesc(&adapter_desc);
      sl::AdapterInfo adapter_info;
      adapter_info.deviceLUID = reinterpret_cast<uint8_t*>(&adapter_desc.AdapterLuid);
      adapter_info.deviceLUIDSizeInBytes = sizeof(LUID);
      result = functions.is_feature_supported(sl::kFeatureDLSS_G, adapter_info);
      if (result != sl::Result::eOk)
      {
         Log(reshade::log::level::info, std::format("DLSS-G is not supported on this GPU/system, error = {}", int(result)));
         return false;
      }

      if (!GetFeatureFunction(functions, sl::kFeatureDLSS_G, "slDLSSGGetState", functions.dlssg_get_state)
         || !GetFeatureFunction(functions, sl::kFeatureDLSS_G, "slDLSSGSetOptions", functions.dlssg_set_options)
         || !GetFeatureFunction(functions, sl::kFeatureReflex, "slReflexSetOptions", functions.reflex_set_options)
         || !GetFeatureFunction(functions, sl::kFeatureReflex, "slReflexSleep", functions.reflex_sleep)
         || !GetFeatureFunction(functions, sl::kFeaturePCL, "slPCLSetMarker", functions.pcl_set_marker))
      {
         Log(reshade::log::level::error, "Failed to retrieve the Streamline feature functions");
         return false;
      }

      // Queues need to be created through the device proxy
      ID3D12Device* device_proxy = device;
      result = functions.upgrade_interface(reinterpret_cast<void**>(&device_proxy));
      if (result != sl::Result::eOk || !device_proxy)
      {
         Log(reshade::log::level::error, std::format("Failed to upgrade the DX12 device, error = {}", int(result)));
         return false;
      }
      custom_data->device_proxy.Attach(device_proxy); // The proxy holds its own reference to the native device

      // Reflex is required by DLSS-G
      sl::ReflexOptions reflex_options;
      reflex_options.mode = sl::ReflexMode::eLowLatency;
      result = functions.reflex_set_options(reflex_options);
      if (result != sl::Result::eOk)
         Log(reshade::log::level::warning, std::format("slReflexSetOptions failed, error = {}", int(result)));

      sl::DLSSGState state;
      if (functions.dlssg_get_state(custom_data->viewport, state, nullptr) == sl::Result::eOk)
      {
         custom_data->max_generated_frames = (std::max)(state.numFramesToGenerateMax, 1u);
         custom_data->min_resolution = (std::max)(state.minWidthOrHeight, 1u);
      }

      custom_data->is_supported = true;
      Log(reshade::log::level::info, std::format("Initialized (up to {}x)", custom_data->max_generated_frames + 1));
      return true;
   }

   void DLSSG::Deinit(FG::InstanceData*& data)
   {
      auto& custom_data = reinterpret_cast<DLSSGInstanceData*&>(data);
      if (!custom_data)
         return;

      // Proxies need to be released before shutting down
      custom_data->factory_proxy.Reset();
      custom_data->command_queue_proxy.Reset();
      custom_data->device_proxy.Reset();
      if (custom_data->initialized && custom_data->functions.shutdown() != sl::Result::eOk)
         Log(reshade::log::level::warning, "slShutdown failed");
      custom_data->device.Reset();
      HMODULE module = custom_data->module;
      delete custom_data;
      custom_data = nullptr;
      if (module)
         FreeLibrary(module);
      streamline_initialized = false;
   }

   HRESULT DLSSG::CreateCommandQueue(FG::InstanceData* data, ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC& desc, ID3D12CommandQueue** command_queue)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported)
         return E_FAIL;
      // The queue needs to be created through the device proxy, and the swapchain created with the queue proxy, but we submit our work to the native queue
      HRESULT hr = custom_data->device_proxy->CreateCommandQueue(&desc, IID_PPV_ARGS(&custom_data->command_queue_proxy));
      if (FAILED(hr))
         return hr;
      ID3D12CommandQueue* native_command_queue = nullptr;
      if (custom_data->functions.get_native_interface(custom_data->command_queue_proxy.Get(), reinterpret_cast<void**>(&native_command_queue)) != sl::Result::eOk || !native_command_queue)
         return E_FAIL;
      *command_queue = native_command_queue; // Already has a reference
      return S_OK;
   }

   HRESULT DLSSG::CreateSwapchain(FG::InstanceData* data, IDXGIFactory2* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain4** swapchain)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || !custom_data->command_queue_proxy)
         return E_FAIL;

      // A factory proxy (and not a hooked one), as we use "eUseDXGIFactoryProxy", so the game factories aren't affected
      IDXGIFactory* factory_proxy = factory;
      if (custom_data->functions.upgrade_interface(reinterpret_cast<void**>(&factory_proxy)) != sl::Result::eOk || !factory_proxy)
         return E_FAIL;
      ComPtr<IDXGIFactory> factory_proxy_base;
      factory_proxy_base.Attach(factory_proxy);
      custom_data->factory_proxy.Reset();
      HRESULT hr = factory_proxy_base.As(&custom_data->factory_proxy);
      ComPtr<IDXGISwapChain1> swapchain_1;
      if (SUCCEEDED(hr))
         hr = custom_data->factory_proxy->CreateSwapChainForHwnd(custom_data->command_queue_proxy.Get(), hwnd, &desc, nullptr, nullptr, &swapchain_1);
      if (SUCCEEDED(hr))
         hr = swapchain_1->QueryInterface(IID_PPV_ARGS(swapchain));
      custom_data->options_set = false; // Force setting them again
      custom_data->runtime_error = false;
      return hr;
   }

   void DLSSG::DestroySwapchain(FG::InstanceData* data)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data)
         return;
      custom_data->factory_proxy.Reset();
      custom_data->options_set = false;
   }

   void DLSSG::OnSwapchainResize(FG::InstanceData* data, bool before)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      // DLSS-G needs to be off while the swapchain changes
      if (custom_data && custom_data->is_supported && before && custom_data->options_set && custom_data->options.mode != sl::DLSSGMode::eOff)
      {
         custom_data->options.mode = sl::DLSSGMode::eOff;
         custom_data->functions.dlssg_set_options(custom_data->viewport, custom_data->options);
      }
   }

   bool DLSSG::UpdateSettings(FG::InstanceData* data, const FG::SettingsData& settings_data)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || custom_data->runtime_error)
         return false;
      custom_data->settings_data = settings_data;
      // DLSS-G only supports HDR10 for HDR (not scRGB), which is checked by the swapchain proxy
      return settings_data.output_width >= custom_data->min_resolution && settings_data.output_height >= custom_data->min_resolution;
   }

   void DLSSG::BeginFrame(FG::InstanceData* data, uint32_t frame_id)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported)
         return;
      // Reflex needs to sleep every frame (even if frame generation is off)
      custom_data->frame_token = nullptr;
      if (custom_data->functions.get_new_frame_token(custom_data->frame_token, &frame_id) != sl::Result::eOk || !custom_data->frame_token)
         return;
      custom_data->frame_token_id = frame_id;
      custom_data->next_marker = uint32_t(sl::PCLMarker::eSimulationStart);
      custom_data->functions.reflex_sleep(*custom_data->frame_token);
      SendMarkers(*custom_data, frame_id, sl::PCLMarker::eSimulationStart);
   }

   bool DLSSG::PrepareFrame(FG::InstanceData* data, ID3D12GraphicsCommandList* command_list, const FG::FrameData& frame_data, bool generate)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported)
         return false;
      SendMarkers(*custom_data, frame_data.frame_id, sl::PCLMarker::eRenderSubmitEnd);
      if (!custom_data->frame_token)
         generate = false;
      generate &= !custom_data->runtime_error;

      const FG::SettingsData& settings_data = custom_data->settings_data;
      sl::DLSSGOptions options = custom_data->options;
      options.mode = generate ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
      options.numFramesToGenerate = std::clamp(settings_data.generated_frames, 1u, custom_data->max_generated_frames);
      // Disabling it in menus etc shouldn't free (and later re-allocate) all its resources
      options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
      if (settings_data.dynamic_resolution)
      {
         options.flags |= sl::DLSSGFlags::eDynamicResolutionEnabled;
         options.dynamicResWidth = settings_data.render_width;
         options.dynamicResHeight = settings_data.render_height;
      }
      else
      {
         options.dynamicResWidth = 0;
         options.dynamicResHeight = 0;
      }
      options.colorBufferFormat = uint32_t(settings_data.output_format);
      const bool options_changed = !custom_data->options_set || options.mode != custom_data->options.mode || options.numFramesToGenerate != custom_data->options.numFramesToGenerate || options.flags != custom_data->options.flags
         || options.dynamicResWidth != custom_data->options.dynamicResWidth || options.dynamicResHeight != custom_data->options.dynamicResHeight || options.colorBufferFormat != custom_data->options.colorBufferFormat;
      if (options_changed)
      {
         const sl::Result result = custom_data->functions.dlssg_set_options(custom_data->viewport, options);
         if (result != sl::Result::eOk)
         {
            LogError(*custom_data, std::format("slDLSSGSetOptions failed, error = {}", int(result)));
            return false;
         }
         custom_data->options = options;
         custom_data->options_set = true;
      }
      if (!generate)
         return false;

      // The resources are only valid now (they are shared with DX11), so Streamline copies them on our command list
      constexpr sl::BufferType buffer_types[size_t(FG::InputType::Count)] = { sl::kBufferTypeDepth, sl::kBufferTypeMotionVectors, sl::kBufferTypeHUDLessColor, sl::kBufferTypeUIColorAndAlpha };
      sl::Resource resources[size_t(FG::InputType::Count)] = {
         sl::Resource(sl::ResourceType::eTex2d, nullptr),
         sl::Resource(sl::ResourceType::eTex2d, nullptr),
         sl::Resource(sl::ResourceType::eTex2d, nullptr),
         sl::Resource(sl::ResourceType::eTex2d, nullptr),
      };
      sl::ResourceTag tags[size_t(FG::InputType::Count)] = {
         sl::ResourceTag(nullptr, buffer_types[0], sl::ResourceLifecycle::eOnlyValidNow),
         sl::ResourceTag(nullptr, buffer_types[1], sl::ResourceLifecycle::eOnlyValidNow),
         sl::ResourceTag(nullptr, buffer_types[2], sl::ResourceLifecycle::eOnlyValidNow),
         sl::ResourceTag(nullptr, buffer_types[3], sl::ResourceLifecycle::eOnlyValidNow),
      };
      for (size_t i = 0; i < size_t(FG::InputType::Count); i++)
      {
         const FG::Resource& input = frame_data.inputs[i];
         // Missing optional inputs are tagged as null, to clear the previous ones
         if (input.resource)
         {
            resources[i].native = input.resource;
            resources[i].state = uint32_t(input.state);
            tags[i].resource = &resources[i];
            tags[i].extent = sl::Extent{ input.top, input.left, input.width, input.height };
         }
      }
      sl::Result result = custom_data->functions.set_tag_for_frame(*custom_data->frame_token, custom_data->viewport, tags, uint32_t(std::size(tags)), command_list);
      if (result != sl::Result::eOk)
      {
         LogError(*custom_data, std::format("slSetTagForFrame failed, error = {}", int(result)));
         return false;
      }

      sl::Constants constants;
      FillConstants(constants, frame_data, settings_data);
      result = custom_data->functions.set_constants(constants, *custom_data->frame_token, custom_data->viewport);
      if (result != sl::Result::eOk)
      {
         LogError(*custom_data, std::format("slSetConstants failed, error = {}", int(result)));
         return false;
      }
      return true;
   }

   HRESULT DLSSG::Present(FG::InstanceData* data, IDXGISwapChain4* swapchain, uint32_t frame_id, UINT sync_interval, UINT flags)
   {
      auto* custom_data = static_cast<DLSSGInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported)
         return swapchain->Present(sync_interval, flags);

      SendMarkers(*custom_data, frame_id, sl::PCLMarker::eRenderSubmitEnd);
      if (custom_data->frame_token)
         custom_data->functions.pcl_set_marker(sl::PCLMarker::ePresentStart, *custom_data->frame_token);
      const HRESULT hr = swapchain->Present(sync_interval, flags);
      if (custom_data->frame_token)
         custom_data->functions.pcl_set_marker(sl::PCLMarker::ePresentEnd, *custom_data->frame_token);

      custom_data->last_presented_frames = 1;
      sl::DLSSGState state;
      if (custom_data->functions.dlssg_get_state(custom_data->viewport, state, nullptr) == sl::Result::eOk)
      {
         custom_data->last_presented_frames = (std::max)(state.numFramesActuallyPresented, 1u);
         if (state.status != sl::DLSSGStatus::eOk && custom_data->options.mode != sl::DLSSGMode::eOff)
         {
            // It would draw a pink overlay, turn it off
            Log(reshade::log::level::error, std::format("DLSS-G reported an invalid state ({}), disabling it", uint32_t(state.status)));
            custom_data->runtime_error = true;
         }
      }
      return hr;
   }

   unsigned int DLSSG::GetLastPresentedFramesCount(const FG::InstanceData* data) const
   {
      const auto* custom_data = static_cast<const DLSSGInstanceData*>(data);
      return custom_data ? custom_data->last_presented_frames : 1;
   }
}

#endif
