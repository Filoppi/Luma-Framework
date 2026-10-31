#include "XeSS.h"

#if ENABLE_XESS

#include "xess/xess_d3d12.h"

#include "../includes/debug.h"
#include "../utils/system.h"

#include <include/reshade.hpp>

#include <climits>
#include <cmath>
#include <cstdlib>
#include <format>
#include <string>

namespace Intel
{
   namespace
   {
      // Shipped next to the Luma addon (see "UseLumaXeSS" in "Luma.Features.props")
      constexpr const wchar_t* xess_dll_name = L"libxess.dll";

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
         decltype(&xessD3D12Init) init = nullptr;
         decltype(&xessD3D12Execute) execute = nullptr;
         decltype(&xessDestroyContext) destroy_context = nullptr;
         decltype(&xessGetOptimalInputResolution) get_optimal_input_resolution = nullptr;
         decltype(&xessSetVelocityScale) set_velocity_scale = nullptr;
         // Created once by "InitInstance()", and (re)initialized for the settings by "UpdateSettings()"
         xess_context_handle_t context = nullptr;
         bool context_initialized = false;

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

      bool InitInstance(XeSSInstanceData& data, ID3D11Device* device, IDXGIAdapter* adapter)
      {
         const auto create_context = reinterpret_cast<decltype(&xessD3D12CreateContext)>(GetProcAddress(data.module, "xessD3D12CreateContext"));
         data.init = reinterpret_cast<decltype(&xessD3D12Init)>(GetProcAddress(data.module, "xessD3D12Init"));
         data.execute = reinterpret_cast<decltype(&xessD3D12Execute)>(GetProcAddress(data.module, "xessD3D12Execute"));
         data.destroy_context = reinterpret_cast<decltype(&xessDestroyContext)>(GetProcAddress(data.module, "xessDestroyContext"));
         data.get_optimal_input_resolution = reinterpret_cast<decltype(&xessGetOptimalInputResolution)>(GetProcAddress(data.module, "xessGetOptimalInputResolution"));
         data.set_velocity_scale = reinterpret_cast<decltype(&xessSetVelocityScale)>(GetProcAddress(data.module, "xessSetVelocityScale"));
         const auto set_logging_callback = reinterpret_cast<decltype(&xessSetLoggingCallback)>(GetProcAddress(data.module, "xessSetLoggingCallback"));
         const auto get_version = reinterpret_cast<decltype(&xessGetVersion)>(GetProcAddress(data.module, "xessGetVersion"));
         const auto is_optimal_driver = reinterpret_cast<decltype(&xessIsOptimalDriver)>(GetProcAddress(data.module, "xessIsOptimalDriver"));
         if (!create_context || !data.init || !data.execute || !data.destroy_context || !data.get_optimal_input_resolution || !data.set_velocity_scale || !set_logging_callback || !get_version || !is_optimal_driver)
         {
            Log(reshade::log::level::error, "The XeSS dll is missing exports");
            return false;
         }

         if (!D3D12Interop::Init(data.interop, device, adapter))
            return false;

         xess_version_t version = {};
         get_version(&version);

         // Creating a context is the only support check (it fails without SM 6.4)
         const xess_result_t result = create_context(data.interop.device_12.Get(), &data.context);
         if (Failed(result))
         {
            Log(reshade::log::level::info, std::format("XeSS {}.{}.{} is not supported on this GPU, error = {}", version.major, version.minor, version.patch, int(result)));
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

         Log(reshade::log::level::info, std::format("Initialized XeSS {}.{}.{}", version.major, version.minor, version.patch));
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

   bool Intel::XeSS::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data); // This will also null the pointer
      }

      // From next to Luma, by full path, so a "libxess.dll" the game ships (possibly an older version) is a separate module
      HMODULE luma_module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&Log), &luma_module);
      HMODULE module = LoadLibraryExW((System::GetModulePath(luma_module).parent_path() / xess_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!module)
      {
         Log(reshade::log::level::info, "libxess.dll wasn't found next to Luma, XeSS won't be available");
         return false;
      }

      auto& custom_data = reinterpret_cast<XeSSInstanceData*&>(data);
      custom_data = new XeSSInstanceData();
      custom_data->automatically_restores_pipeline_state = false;
      custom_data->min_resolution = 32;
      custom_data->module = module;
      custom_data->is_supported = InitInstance(*custom_data, device, adapter);
      return custom_data->is_supported;
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

      // Re-initializing needs the context not to be in use by DX12
      if (!D3D12Interop::WaitForIdle(custom_data->interop))
         return false;
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
      const xess_d3d12_init_params_t init_params = {
         .outputResolution = output_resolution,
         .qualitySetting = quality,
         .initFlags = init_flags,
      };
      // Compiles its pipelines synchronously (a few hundred ms, once per settings change)
      if (Failed(custom_data->init(custom_data->context, &init_params)))
      {
         LogError(*custom_data, std::format("xessD3D12Init failed (render {}x{}, output {}x{})", settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height));
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

      const auto& settings_data = custom_data->settings_data;
      ASSERT_ONCE(settings_data.output_width == custom_data->textures[T::Output].width && settings_data.output_height == custom_data->textures[T::Output].height);
      const xess_d3d12_execute_params_t execute_params = {
         .pColorTexture = custom_data->textures[T::Color].resource_12.Get(),
         .pVelocityTexture = custom_data->textures[T::MotionVectors].resource_12.Get(),
         .pDepthTexture = custom_data->textures[T::Depth].resource_12.Get(),
         .pOutputTexture = custom_data->textures[T::Output].resource_12.Get(),
         .jitterOffsetX = draw_data.jitter_x,
         .jitterOffsetY = draw_data.jitter_y,
         // XeSS applies it to the input and undoes it on the output, so a pre-exposed input gets its exposure back
         .exposureScale = (draw_data.pre_exposure == 0.f ? 1.f : (1.f / draw_data.pre_exposure)),
         .resetHistory = uint32_t(draw_data.reset),
         .inputWidth = (draw_data.render_width != 0 ? draw_data.render_width : settings_data.render_width),
         .inputHeight = (draw_data.render_height != 0 ? draw_data.render_height : settings_data.render_height),
      };
      const xess_result_t result = custom_data->execute(custom_data->context, command_list_12, &execute_params);
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
