#include "XeSS.h"

#if ENABLE_XESS

#include "xess/xess_d3d11.h"
#include "xess/xess_d3d12.h"

#include "../includes/debug.h"
#include "../utils/system.h"

#include <include/reshade.hpp>

#include <dxgi.h>
#include <wrl/client.h>

#include <climits>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>

namespace Intel
{
   namespace
   {
      // Shipped next to the Luma addon (see "UseLumaXeSS" in "Luma.Features.props")
      constexpr const wchar_t* xess_dll_name = L"libxess.dll";
      constexpr const wchar_t* xess_dx11_dll_name = L"libxess_dx11.dll";
      constexpr UINT vendor_id_intel = 0x8086;

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("XeSS: " + message).c_str());
      }

      struct XeSSInstanceData : public SR::InstanceData
      {
         // The shared textures, in the order of "DrawData"
         enum TextureIndex : size_t
         {
            Color,
            Depth,
            MotionVectors,
            Output,
            TexturesCount
         };

         HMODULE module = nullptr;
         bool native_dx11 = false; // Whether "module" is the DX11 dll, otherwise we go through the DX12 interop
         decltype(&xessD3D11Init) init_11 = nullptr;
         decltype(&xessD3D11Execute) execute_11 = nullptr;
         decltype(&xessD3D12Init) init_12 = nullptr;
         decltype(&xessD3D12Execute) execute_12 = nullptr;
         decltype(&xessDestroyContext) destroy_context = nullptr;
         decltype(&xessGetOptimalInputResolution) get_optimal_input_resolution = nullptr;
         decltype(&xessSetVelocityScale) set_velocity_scale = nullptr;
         // Created once by "InitInstance()", and (re)initialized for the settings by "UpdateSettings()"
         xess_context_handle_t context = nullptr;
         bool context_initialized = false;

         // Empty with "native_dx11", so waiting for it to be idle is a no-op
         D3D12Interop::Context interop;
         D3D12Interop::SharedTexture textures[TexturesCount];

         unsigned int logged_errors = 0;
      };

      void LogError(XeSSInstanceData& data, const std::string& message)
      {
         if (data.logged_errors++ < 10) // Don't spam the log
         {
            Log(reshade::log::level::error, message);
         }
      }

      // Positive results are warnings (e.g. an old driver), the call still succeeded
      bool Failed(xess_result_t result)
      {
         return result < XESS_RESULT_SUCCESS;
      }

      // The DX11 and DX12 dlls export the API agnostic functions with the same names
      bool InitInstance(XeSSInstanceData& data, ID3D11Device* device, IDXGIAdapter* adapter)
      {
         data.destroy_context = reinterpret_cast<decltype(&xessDestroyContext)>(GetProcAddress(data.module, "xessDestroyContext"));
         data.get_optimal_input_resolution = reinterpret_cast<decltype(&xessGetOptimalInputResolution)>(GetProcAddress(data.module, "xessGetOptimalInputResolution"));
         data.set_velocity_scale = reinterpret_cast<decltype(&xessSetVelocityScale)>(GetProcAddress(data.module, "xessSetVelocityScale"));
         const auto set_logging_callback = reinterpret_cast<decltype(&xessSetLoggingCallback)>(GetProcAddress(data.module, "xessSetLoggingCallback"));
         const auto get_version = reinterpret_cast<decltype(&xessGetVersion)>(GetProcAddress(data.module, "xessGetVersion"));
         const auto is_optimal_driver = reinterpret_cast<decltype(&xessIsOptimalDriver)>(GetProcAddress(data.module, "xessIsOptimalDriver"));
         if (!data.destroy_context || !data.get_optimal_input_resolution || !data.set_velocity_scale || !set_logging_callback || !get_version || !is_optimal_driver)
         {
            Log(reshade::log::level::error, "The XeSS dll is missing exports");
            return false;
         }

         xess_version_t version = {};
         get_version(&version);
         const char* const path_name = (data.native_dx11 ? "DX11" : "DX12 interop");

         // Creating a context is the only support check (on DX12 it fails without SM 6.4, on DX11 without an Intel GPU)
         xess_result_t result = XESS_RESULT_ERROR_UNKNOWN;
         if (data.native_dx11)
         {
            const auto create_context = reinterpret_cast<decltype(&xessD3D11CreateContext)>(GetProcAddress(data.module, "xessD3D11CreateContext"));
            data.init_11 = reinterpret_cast<decltype(&xessD3D11Init)>(GetProcAddress(data.module, "xessD3D11Init"));
            data.execute_11 = reinterpret_cast<decltype(&xessD3D11Execute)>(GetProcAddress(data.module, "xessD3D11Execute"));
            if (!create_context || !data.init_11 || !data.execute_11)
            {
               Log(reshade::log::level::error, "The XeSS DX11 dll is missing exports");
               return false;
            }
            result = create_context(device, &data.context);
         }
         else
         {
            const auto create_context = reinterpret_cast<decltype(&xessD3D12CreateContext)>(GetProcAddress(data.module, "xessD3D12CreateContext"));
            data.init_12 = reinterpret_cast<decltype(&xessD3D12Init)>(GetProcAddress(data.module, "xessD3D12Init"));
            data.execute_12 = reinterpret_cast<decltype(&xessD3D12Execute)>(GetProcAddress(data.module, "xessD3D12Execute"));
            if (!create_context || !data.init_12 || !data.execute_12)
            {
               Log(reshade::log::level::error, "The XeSS DX12 dll is missing exports");
               return false;
            }
            if (!D3D12Interop::Init(data.interop, device, adapter))
               return false;
            result = create_context(data.interop.device_12.Get(), &data.context);
         }
         if (Failed(result))
         {
            Log(reshade::log::level::info, std::format("XeSS {}.{}.{} ({}) is not supported on this GPU, error = {}", version.major, version.minor, version.patch, path_name, int(result)));
            data.context = nullptr;
            return false;
         }
         if (is_optimal_driver(data.context) == XESS_RESULT_WARNING_OLD_DRIVER)
         {
            Log(reshade::log::level::warning, "The GPU driver is older than XeSS recommends");
         }
#if DEVELOPMENT || TEST
         set_logging_callback(data.context, XESS_LOGGING_LEVEL_WARNING, [](const char* message, xess_logging_level_t level)
            { Log((level == XESS_LOGGING_LEVEL_ERROR ? reshade::log::level::error : reshade::log::level::warning), (message ? message : "")); });
#endif

         Log(reshade::log::level::info, std::format("Initialized XeSS {}.{}.{} ({})", version.major, version.minor, version.patch, path_name));
         return true;
      }
   } // namespace

   void Intel::XeSS::SetDepthCopyShader(SR::InstanceData* data, std::function<ID3D11ComputeShader*()> get_depth_copy_shader)
   {
      if (data)
      {
         static_cast<XeSSInstanceData*>(data)->interop.get_depth_copy_shader = std::move(get_depth_copy_shader);
      }
   }

   bool Intel::XeSS::IsNativeDX11(const SR::InstanceData* data)
   {
      return data && static_cast<const XeSSInstanceData*>(data)->native_dx11;
   }

   bool Intel::XeSS::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data); // This will also null the pointer
      }

      // From next to Luma, by full path, so a XeSS dll the game ships (possibly an older version) is a separate module
      HMODULE luma_module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Log), &luma_module);
      const std::filesystem::path luma_folder = System::GetModulePath(luma_module).parent_path();

      auto& custom_data = reinterpret_cast<XeSSInstanceData*&>(data);
      const auto init_instance = [&](HMODULE module, bool native_dx11)
      {
         custom_data = new XeSSInstanceData();
         custom_data->automatically_restores_pipeline_state = false;
         custom_data->min_resolution = 32;
         custom_data->module = module;
         custom_data->native_dx11 = native_dx11;
         custom_data->is_supported = InitInstance(*custom_data, device, adapter);
         return custom_data->is_supported;
      };

      // Prefer the native DX11 XeSS on Intel GPUs, falling back on the DX12 one (see "XeSS.h")
      DXGI_ADAPTER_DESC adapter_desc = {};
      if (adapter && SUCCEEDED(adapter->GetDesc(&adapter_desc)) && adapter_desc.VendorId == vendor_id_intel)
      {
         if (HMODULE module = LoadLibraryExW((luma_folder / xess_dx11_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
         {
            if (init_instance(module, true))
               return true;
            Deinit(data);
         }
      }

      HMODULE module = LoadLibraryExW((luma_folder / xess_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!module)
      {
         Log(reshade::log::level::info, "libxess.dll wasn't found next to Luma, XeSS won't be available");
         return false;
      }
      return init_instance(module, false);
   }

   void Intel::XeSS::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
   {
      auto& custom_data = reinterpret_cast<XeSSInstanceData*&>(data);
      if (!custom_data)
         return;

      // If the GPU is stuck (but the device wasn't removed), leak everything instead of hanging or risking a crash
      if (!D3D12Interop::WaitForIdle(custom_data->interop))
      {
         Log(reshade::log::level::warning, "Leaking the DX12 interop resources, as the GPU might still be using them");
         custom_data = nullptr;
         return;
      }

      if (custom_data->context && Failed(custom_data->destroy_context(custom_data->context)))
      {
         Log(reshade::log::level::warning, "xessDestroyContext failed");
      }
      D3D12Interop::Deinit(custom_data->interop);
      HMODULE module = custom_data->module;
      delete custom_data;
      custom_data = nullptr;
      FreeLibrary(module);
   }

   bool Intel::XeSS::HasInit(const SR::InstanceData* data) const
   {
      return data != nullptr;
   }

   bool Intel::XeSS::IsSupported(const SR::InstanceData* data) const
   {
      return data && data->is_supported;
   }

   bool Intel::XeSS::UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data)
   {
      auto* custom_data = static_cast<XeSSInstanceData*>(data);
      if (!command_list || !custom_data || !custom_data->is_supported)
         return false;

      // No need to re-initialize the XeSS context if all the params are the same
      if (custom_data->context_initialized && settings_data == custom_data->settings_data)
         return true;

      // Re-initializing needs the context not to be in use by the GPU anymore
      if (custom_data->native_dx11)
      {
         // XeSS DX11 runs on the immediate context, so we wait on an event query there, which also flushes it
         Microsoft::WRL::ComPtr<ID3D11Device> device;
         command_list->GetDevice(&device);
         Microsoft::WRL::ComPtr<ID3D11Query> query;
         const D3D11_QUERY_DESC query_desc = {.Query = D3D11_QUERY_EVENT};
         if (FAILED(device->CreateQuery(&query_desc, &query)))
            return false;
         command_list->End(query.Get());
         const ULONGLONG deadline = GetTickCount64() + 2000; // Same timeout as the DX12 interop
         BOOL done = FALSE;
         while (command_list->GetData(query.Get(), &done, sizeof(done), 0) == S_FALSE)
         {
            if (GetTickCount64() >= deadline)
            {
               LogError(*custom_data, "Timed out waiting for the GPU before re-initializing XeSS");
               return false;
            }
            Sleep(0);
         }
      }
      else if (!D3D12Interop::WaitForIdle(custom_data->interop))
      {
         return false;
      }
      custom_data->context_initialized = false;

      const xess_2d_t output_resolution = {.x = settings_data.output_width, .y = settings_data.output_height};
      // The preset sets the model's upscaling range. We use native AA when nothing is upscaled, and ultra performance for dynamic resolution, as the render size can drop that far.
      // Otherwise we pick the preset whose optimal render size is the closest to ours.
      xess_quality_settings_t quality = XESS_QUALITY_SETTING_AA;
      if (settings_data.dynamic_resolution)
      {
         quality = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE;
      }
      else if (settings_data.render_width < settings_data.output_width || settings_data.render_height < settings_data.output_height)
      {
         unsigned int best_delta = UINT_MAX;
         for (int i = XESS_QUALITY_SETTING_ULTRA_PERFORMANCE; i < XESS_QUALITY_SETTING_AA; i++)
         {
            xess_2d_t optimal = {}, min = {}, max = {};
            if (Failed(custom_data->get_optimal_input_resolution(custom_data->context, &output_resolution, xess_quality_settings_t(i), &optimal, &min, &max)))
               continue;
            const unsigned int delta = unsigned(std::abs(int(optimal.x) - int(settings_data.render_width)) + std::abs(int(optimal.y) - int(settings_data.render_height)));
            if (delta < best_delta)
            {
               best_delta = delta;
               quality = xess_quality_settings_t(i);
            }
         }
      }

      // We don't pass an exposure texture or responsive mask, as XeSS needs their flags on init and "SettingsData" doesn't say whether the game provides them
      uint32_t init_flags = 0;
      if (settings_data.inverted_depth)
      {
         init_flags |= XESS_INIT_FLAG_INVERTED_DEPTH;
      }
      if (settings_data.mvs_jittered)
      {
         init_flags |= XESS_INIT_FLAG_JITTERED_MV;
      }
      if (settings_data.auto_exposure)
      {
         init_flags |= XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE;
      }
      if (!settings_data.hdr)
      {
         init_flags |= XESS_INIT_FLAG_LDR_INPUT_COLOR;
      }
      // Compiles its pipelines synchronously (a few hundred ms, once per settings change)
      xess_result_t result = XESS_RESULT_ERROR_UNKNOWN;
      if (custom_data->native_dx11)
      {
         const xess_d3d11_init_params_t init_params = {
            .outputResolution = output_resolution,
            .qualitySetting = quality,
            .initFlags = init_flags,
         };
         result = custom_data->init_11(custom_data->context, &init_params);
      }
      else
      {
         const xess_d3d12_init_params_t init_params = {
            .outputResolution = output_resolution,
            .qualitySetting = quality,
            .initFlags = init_flags,
         };
         result = custom_data->init_12(custom_data->context, &init_params);
      }
      if (Failed(result))
      {
         LogError(*custom_data, std::format("{} failed, error = {} (render {}x{}, output {}x{})", (custom_data->native_dx11 ? "xessD3D11Init" : "xessD3D12Init"), int(result), settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height));
         return false;
      }
      // Both in pixels, from the current position to the previous one, as FSR
      custom_data->set_velocity_scale(custom_data->context, settings_data.mvs_x_scale, settings_data.mvs_y_scale);

      Log(reshade::log::level::info, std::format("Initialized the context (render {}x{}, output {}x{}, quality preset {})", settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height, int(quality)));

      custom_data->context_initialized = true;
      custom_data->settings_data = settings_data;
      return true;
   }

   // The minimum the XeSS guide asks for: 8 phases times the upscaling area ratio
   int Intel::XeSS::GetJitterPhases(const SR::InstanceData* data) const
   {
      if (!data || data->settings_data.render_width == 0)
         return SR::GetDefaultJitterPhases();
      const float scale = float(data->settings_data.output_width) / float(data->settings_data.render_width);
      return int(std::ceil(float(SR::GetDefaultJitterPhases()) * scale * scale));
   }

   bool Intel::XeSS::Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data)
   {
      using T = XeSSInstanceData;
      // We need to update our sync state and resources even if this is "const"
      auto* custom_data = const_cast<T*>(static_cast<const T*>(data));
      if (!custom_data || !custom_data->is_supported || !custom_data->context_initialized || !draw_data.source_color || !draw_data.output_color || !draw_data.depth_buffer || !draw_data.motion_vectors)
         return false;

      const auto& settings_data = custom_data->settings_data;
      const auto set_common_execute_params = [&](auto* execute_params)
      {
         execute_params->jitterOffsetX = draw_data.jitter_x;
         execute_params->jitterOffsetY = draw_data.jitter_y;
         // XeSS applies it to the input and undoes it on the output, so a pre-exposed input gets its exposure back
         execute_params->exposureScale = (draw_data.pre_exposure == 0.f ? 1.f : (1.f / draw_data.pre_exposure));
         execute_params->resetHistory = uint32_t(draw_data.reset);
         execute_params->inputWidth = (draw_data.render_width != 0 ? draw_data.render_width : settings_data.render_width);
         execute_params->inputHeight = (draw_data.render_height != 0 ? draw_data.render_height : settings_data.render_height);
      };

      if (custom_data->native_dx11)
      {
         // XeSS records into the immediate context of the device it was created with, so it would run out of order with a deferred one
         if (command_list->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         {
            ASSERT_ONCE_MSG(false, "XeSS DX11 needs to run on the immediate context");
            return false;
         }
         xess_d3d11_execute_params_t execute_params = {
            .pColorTexture = draw_data.source_color,
            .pVelocityTexture = draw_data.motion_vectors,
            .pDepthTexture = draw_data.depth_buffer,
            .pOutputTexture = draw_data.output_color,
         };
         set_common_execute_params(&execute_params);
         const xess_result_t result = custom_data->execute_11(custom_data->context, &execute_params);
         if (Failed(result))
         {
            LogError(*custom_data, std::format("xessD3D11Execute failed, error = {}", int(result)));
            return false;
         }
         return true;
      }

      ID3D11Resource* const sources[T::TexturesCount] = {draw_data.source_color, draw_data.depth_buffer, draw_data.motion_vectors, draw_data.output_color};
      for (size_t i = 0; i < T::TexturesCount; i++)
      {
         if (!D3D12Interop::UpdateSharedTextureFrom(custom_data->interop, custom_data->textures[i], sources[i], i == T::Depth, i == T::Depth || i == T::Output))
            return false;
      }

      // DX11: convert the depth and copy the other inputs into the shared textures (the pipeline state is restored by the caller)
      if (!D3D12Interop::CopyDepth(custom_data->interop, custom_data->textures[T::Depth], command_list, draw_data.depth_buffer))
         return false;
      command_list->CopyResource(custom_data->textures[T::Color].resource_11.Get(), sources[T::Color]);
      command_list->CopyResource(custom_data->textures[T::MotionVectors].resource_11.Get(), sources[T::MotionVectors]);

      ID3D12GraphicsCommandList* const command_list_12 = D3D12Interop::BeginCommandList(custom_data->interop, command_list);
      if (!command_list_12)
         return false;

      // XeSS reads the inputs in the non pixel shader resource state and writes the output in the unordered access one, without barriers of its own
      D3D12_RESOURCE_BARRIER barriers[T::TexturesCount] = {};
      for (size_t i = 0; i < T::TexturesCount; i++)
      {
         D3D12_RESOURCE_BARRIER& barrier = barriers[i];
         barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
         barrier.Transition.pResource = custom_data->textures[i].resource_12.Get();
         barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
         barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
         barrier.Transition.StateAfter = (i == T::Output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      }
      command_list_12->ResourceBarrier(T::TexturesCount, barriers);

      ASSERT_ONCE(settings_data.output_width == custom_data->textures[T::Output].width && settings_data.output_height == custom_data->textures[T::Output].height);
      xess_d3d12_execute_params_t execute_params = {
         .pColorTexture = custom_data->textures[T::Color].resource_12.Get(),
         .pVelocityTexture = custom_data->textures[T::MotionVectors].resource_12.Get(),
         .pDepthTexture = custom_data->textures[T::Depth].resource_12.Get(),
         .pOutputTexture = custom_data->textures[T::Output].resource_12.Get(),
      };
      set_common_execute_params(&execute_params);
      const xess_result_t result = custom_data->execute_12(custom_data->context, command_list_12, &execute_params);
      const bool execute_failed = Failed(result);
      if (execute_failed)
      {
         LogError(*custom_data, std::format("xessD3D12Execute failed, error = {}", int(result)));
      }

      for (D3D12_RESOURCE_BARRIER& barrier : barriers)
      {
         std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      }
      command_list_12->ResourceBarrier(T::TexturesCount, barriers);

      // Still submitted if XeSS failed, to keep the DX11/DX12 sync consistent
      if (!D3D12Interop::ExecuteCommandList(custom_data->interop, command_list))
      {
         if (custom_data->interop.poisoned)
         {
            custom_data->is_supported = false;
         }
         return false;
      }
      if (execute_failed)
         return false;
      command_list->CopyResource(draw_data.output_color, custom_data->textures[T::Output].resource_11.Get());
      return true;
   }
} // namespace Intel

#endif
