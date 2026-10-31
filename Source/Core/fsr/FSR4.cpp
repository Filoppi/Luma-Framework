#include "FSR4.h"

#if ENABLE_FSR4

#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

#include "../includes/debug.h"
#include "../utils/system.h"
#include "../interop/D3D12Interop.h"
#include "../hooks/FidelityFXHooks.h"

#include <include/reshade.hpp>

#include <format>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace FidelityFX
{
   using Microsoft::WRL::ComPtr;

   namespace
   {
      constexpr int fsr4_default_phase_count = 16;
      constexpr UINT vendor_id_amd = 0x1002;
      // Shipped next to the Luma addon (see "UseLumaFSR4" in "Luma.Features.props"), the loader then loads the upscaler
      constexpr const wchar_t* ffx_loader_dll_name = L"amd_fidelityfx_loader_dx12.dll";
      constexpr const wchar_t* ffx_upscaler_dll_name = L"amd_fidelityfx_upscaler_dx12.dll";

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("FSR4: " + message).c_str());
      }

      void FFXMessageCallback(uint32_t type, const wchar_t* message)
      {
#if !DEVELOPMENT && !TEST
         if (type != FFX_API_MESSAGE_TYPE_ERROR)
            return;
#endif
         char buffer[512] = {};
         size_t converted = 0;
         wcstombs_s(&converted, buffer, message ? message : L"", sizeof(buffer) - 1);
         Log(type == FFX_API_MESSAGE_TYPE_ERROR ? reshade::log::level::error : reshade::log::level::warning, std::string("FidelityFX: ") + buffer);
      }

      struct FSR4InstanceData : public SR::InstanceData
      {
         // The shared textures, in the order of "DrawData"
         enum TextureIndex : size_t
         {
            Color,
            Depth,
            MotionVectors,
            Exposure,
            Reactive,
            Transparency,
            Output,
            TexturesCount
         };

         HMODULE ffx_loader_module = nullptr;
         HMODULE ffx_upscaler_module = nullptr;
         PfnFfxCreateContext ffx_create_context = nullptr;
         PfnFfxDestroyContext ffx_destroy_context = nullptr;
         PfnFfxQuery ffx_query = nullptr;
         PfnFfxDispatch ffx_dispatch = nullptr;
         ffxContext context = nullptr;
         int phase_count = fsr4_default_phase_count;
         bool spoof_amd = false; // Whether the FFX dlls need to see an AMD GPU (the real one isn't)

         D3D12Interop::Context interop;
         D3D12Interop::SharedTexture textures[TexturesCount];

         unsigned int logged_errors = 0;
      };

      void LogError(FSR4InstanceData& data, const std::string& message)
      {
         if (data.logged_errors++ < 10) // Don't spam the log
            Log(reshade::log::level::error, message);
      }

      // The FFX dlls only check the GPU when queried for the upscaler versions and on context creation.
      // Returns whether the spoofing started (and thus needs to be ended).
      bool BeginAMDSpoofing(const FSR4InstanceData& data)
      {
         if (!data.spoof_amd)
            return false;
         const bool spoofing = FidelityFXHooks::BeginAMDSpoofing(data.ffx_upscaler_module);
         if (!spoofing)
            Log(reshade::log::level::error, "Failed to spoof an AMD GPU");
         return spoofing;
      }

      void EndAMDSpoofing(bool spoofing)
      {
         if (spoofing)
            FidelityFXHooks::EndAMDSpoofing();
      }

      void DestroyContext(FSR4InstanceData& data)
      {
         if (data.context && data.ffx_destroy_context(&data.context, nullptr) != FFX_API_RETURN_OK)
            Log(reshade::log::level::warning, "ffxDestroyContext failed");
         data.context = nullptr;
      }

      // Returns the name of the newest FSR 4 upscaler version, or an empty string if there's none
      std::string QueryFSR4Version(FSR4InstanceData& data)
      {
         uint64_t versions_count = 0;
         ffxQueryDescGetVersions versions_desc = {};
         versions_desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
         versions_desc.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
         versions_desc.device = data.interop.device_12.Get();
         versions_desc.outputCount = &versions_count;
         std::vector<uint64_t> version_ids;
         std::vector<const char*> version_names;
         const bool spoofing = BeginAMDSpoofing(data);
         if (data.ffx_query(nullptr, &versions_desc.header) == FFX_API_RETURN_OK && versions_count > 0)
         {
            version_ids.resize(versions_count);
            version_names.resize(versions_count);
            versions_desc.versionIds = version_ids.data();
            versions_desc.versionNames = version_names.data();
            if (data.ffx_query(nullptr, &versions_desc.header) != FFX_API_RETURN_OK)
               version_names.clear();
         }
         EndAMDSpoofing(spoofing);

         // Sorted from the newest
         std::string available_versions;
         for (const char* name : version_names)
         {
            if (name && name[0] == '4' && name[1] == '.')
               return name;
            available_versions += std::format("{}{}", available_versions.empty() ? "" : ", ", name ? name : "(null)");
         }
         Log(reshade::log::level::info, std::format("No FSR 4 upscaler available on this GPU (available versions: [{}])", available_versions));
         return {};
      }

      bool InitInstance(FSR4InstanceData& data, ID3D11Device* device, IDXGIAdapter* adapter)
      {
         data.ffx_create_context = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(data.ffx_loader_module, "ffxCreateContext"));
         data.ffx_destroy_context = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(data.ffx_loader_module, "ffxDestroyContext"));
         data.ffx_query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(data.ffx_loader_module, "ffxQuery"));
         data.ffx_dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(data.ffx_loader_module, "ffxDispatch"));
         if (!data.ffx_create_context || !data.ffx_destroy_context || !data.ffx_query || !data.ffx_dispatch)
         {
            Log(reshade::log::level::error, "The FidelityFX loader dll is missing exports");
            return false;
         }

         if (!D3D12Interop::Init(data.interop, device, adapter))
            return false;

         // The FFX dlls only offer FSR 4 on AMD GPUs, on the others they get to offer the INT8 model if they see an AMD RDNA 3 GPU
         if (data.interop.adapter_desc.VendorId != vendor_id_amd)
         {
            data.spoof_amd = FidelityFXHooks::Init(data.interop.adapter.Get());
            if (!data.spoof_amd)
               Log(reshade::log::level::error, "Failed to create the hooks to spoof an AMD GPU");
         }

         const std::string fsr4_version = QueryFSR4Version(data);
         if (fsr4_version.empty())
            return false;
         Log(reshade::log::level::info, std::format("Initialized FSR {}{}", fsr4_version, data.spoof_amd ? std::format(" (spoofing an AMD GPU, the real vendor id is {:#06x})", data.interop.adapter_desc.VendorId) : ""));
         return true;
      }
   }

   void FidelityFX::FSR4::SetDepthCopyShader(SR::InstanceData* data, std::function<ID3D11ComputeShader*()> get_depth_copy_shader)
   {
      if (data)
         static_cast<FSR4InstanceData*>(data)->interop.get_depth_copy_shader = std::move(get_depth_copy_shader);
   }

   bool FidelityFX::FSR4::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data); // This will also null the pointer
      }

      HMODULE luma_module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&FFXMessageCallback), &luma_module);
      const std::filesystem::path luma_folder = System::GetModulePath(luma_module).parent_path();
      // The upscaler is loaded before the loader would, so we know its module (the loader then gets the same one)
      HMODULE ffx_upscaler_module = LoadLibraryExW((luma_folder / ffx_upscaler_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      HMODULE ffx_loader_module = ffx_upscaler_module ? LoadLibraryExW((luma_folder / ffx_loader_dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) : nullptr;
      if (!ffx_loader_module)
      {
         if (ffx_upscaler_module)
            FreeLibrary(ffx_upscaler_module);
         Log(reshade::log::level::info, "The FidelityFX DX12 dlls weren't found next to Luma, FSR 4 won't be available");
         return false;
      }

      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);
      custom_data = new FSR4InstanceData();
      custom_data->automatically_restores_pipeline_state = false;
      custom_data->min_resolution = 32; // Not 100% accurate but should be a decent start
      custom_data->ffx_loader_module = ffx_loader_module;
      custom_data->ffx_upscaler_module = ffx_upscaler_module;
      custom_data->is_supported = InitInstance(*custom_data, device, adapter);
      return custom_data->is_supported;
   }

   void FidelityFX::FSR4::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
   {
      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);
      if (!custom_data)
         return;

      // If the GPU is stuck (but the device wasn't removed), leak everything instead of hanging or risking a crash
      if (!D3D12Interop::WaitForIdle(custom_data->interop))
      {
         Log(reshade::log::level::warning, "Leaking the DX12 interop resources, as the GPU might still be using them");
         custom_data = nullptr;
         return;
      }

      DestroyContext(*custom_data);
      D3D12Interop::Deinit(custom_data->interop);
      HMODULE ffx_loader_module = custom_data->ffx_loader_module;
      HMODULE ffx_upscaler_module = custom_data->ffx_upscaler_module;
      delete custom_data;
      custom_data = nullptr;
      FreeLibrary(ffx_loader_module);
      FreeLibrary(ffx_upscaler_module);
   }

   bool FidelityFX::FSR4::HasInit(const SR::InstanceData* data) const
   {
      return data != nullptr;
   }

   bool FidelityFX::FSR4::IsSupported(const SR::InstanceData* data) const
   {
      return data && data->is_supported;
   }

   bool FidelityFX::FSR4::UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data)
   {
      auto* custom_data = static_cast<FSR4InstanceData*>(data);
      if (!command_list || !custom_data || !custom_data->is_supported)
         return false;

      // No need to re-instantiate the FSR context if all the params are the same
      if (custom_data->context && settings_data == custom_data->settings_data)
         return true;

      // The previous context might still be in use by DX12
      if (!D3D12Interop::WaitForIdle(custom_data->interop))
         return false;
      DestroyContext(*custom_data);

      ffxCreateContextDescUpscale context_desc = {};
      context_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
      context_desc.maxUpscaleSize.width = settings_data.output_width;
      context_desc.maxUpscaleSize.height = settings_data.output_height;
      context_desc.maxRenderSize.width = settings_data.dynamic_resolution ? settings_data.output_width : settings_data.render_width;
      context_desc.maxRenderSize.height = settings_data.dynamic_resolution ? settings_data.output_height : settings_data.render_height;
      if (settings_data.auto_exposure)
         context_desc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
      if (settings_data.hdr)
         context_desc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
      if (settings_data.dynamic_resolution)
         context_desc.flags |= FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
      if (settings_data.inverted_depth)
         context_desc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
      if (settings_data.mvs_jittered)
         context_desc.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
#if DEVELOPMENT
      context_desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING | FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION; // The latter is needed by "SR::debug_view"
#endif
      context_desc.fpMessage = FFXMessageCallback;

      ffxCreateBackendDX12Desc backend_desc = {};
      backend_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
      backend_desc.device = custom_data->interop.device_12.Get();
      context_desc.header.pNext = &backend_desc.header;

      // Required for ABI compatibility with the dll
      ffxCreateContextDescUpscaleVersion api_version_desc = {};
      api_version_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
      api_version_desc.version = FFX_UPSCALER_VERSION;
      backend_desc.header.pNext = &api_version_desc.header;

      const bool spoofing = BeginAMDSpoofing(*custom_data);
      const ffxReturnCode_t ret = custom_data->ffx_create_context(&custom_data->context, &context_desc.header, nullptr);
      EndAMDSpoofing(spoofing);
      if (ret != FFX_API_RETURN_OK)
      {
         LogError(*custom_data, std::format("ffxCreateContext failed (render {}x{}, output {}x{}), error = {}", settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height, ret));
         ASSERT_ONCE_MSG(false, "Couldn't create FSR4 context");
         custom_data->context = nullptr;
         return false;
      }

      ffxQueryGetProviderVersion provider_version_desc = {};
      provider_version_desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
      custom_data->ffx_query(&custom_data->context, &provider_version_desc.header);

      custom_data->phase_count = fsr4_default_phase_count;
      ffxQueryDescUpscaleGetJitterPhaseCount phase_count_desc = {};
      phase_count_desc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
      phase_count_desc.renderWidth = settings_data.render_width;
      phase_count_desc.displayWidth = settings_data.output_width;
      phase_count_desc.pOutPhaseCount = &custom_data->phase_count;
      custom_data->ffx_query(&custom_data->context, &phase_count_desc.header);

      Log(reshade::log::level::info, std::format("Created context (render {}x{}, output {}x{}), provider \"{}\"", settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height,
         provider_version_desc.versionName ? provider_version_desc.versionName : "unknown"));

      custom_data->settings_data = settings_data;
      return true;
   }

   int FidelityFX::FSR4::GetJitterPhases(const SR::InstanceData* data) const
   {
      return data ? static_cast<const FSR4InstanceData*>(data)->phase_count : fsr4_default_phase_count;
   }

   bool FidelityFX::FSR4::Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data)
   {
      using T = FSR4InstanceData;
      // We need to update our sync state and resources even if this is "const"
      auto* custom_data = const_cast<T*>(static_cast<const T*>(data));

      if (!custom_data || !custom_data->is_supported || !custom_data->context || !draw_data.source_color || !draw_data.output_color || !draw_data.depth_buffer || !draw_data.motion_vectors)
         return false;

      // Make sure all the shared textures are up to date with the game ones (the optional ones are skipped)
      ID3D11Resource* const sources[T::TexturesCount] = {draw_data.source_color, draw_data.depth_buffer, draw_data.motion_vectors, draw_data.exposure, draw_data.bias_mask, draw_data.transparency_alpha, draw_data.output_color};
      for (size_t i = 0; i < T::TexturesCount; i++)
      {
         if (sources[i] && !D3D12Interop::UpdateSharedTextureFrom(custom_data->interop, custom_data->textures[i], sources[i], i == T::Depth, i == T::Depth || i == T::Output))
            return false;
      }

      // DX11: convert the depth and copy the other inputs into the shared textures (the pipeline state is restored by the caller)
      if (!D3D12Interop::CopyDepth(custom_data->interop, custom_data->textures[T::Depth], command_list, draw_data.depth_buffer))
         return false;
      for (size_t i : {T::Color, T::MotionVectors, T::Exposure, T::Reactive, T::Transparency})
      {
         if (sources[i])
            command_list->CopyResource(custom_data->textures[i].resource_11.Get(), sources[i]);
      }

      ID3D12GraphicsCommandList* const command_list_12 = D3D12Interop::BeginCommandList(custom_data->interop, command_list);
      if (!command_list_12)
         return false;

      // Mirrors the FSR 3 implementation
      ffxDispatchDescUpscale dispatch_upscale = {};
      dispatch_upscale.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
      dispatch_upscale.commandList = command_list_12;

      // Transition the shared textures from the common state to what FSR expects, and back
      FfxApiResource* const dispatch_resources[T::TexturesCount] = {&dispatch_upscale.color, &dispatch_upscale.depth, &dispatch_upscale.motionVectors, &dispatch_upscale.exposure, &dispatch_upscale.reactive, &dispatch_upscale.transparencyAndComposition, &dispatch_upscale.output};
      D3D12_RESOURCE_BARRIER barriers[T::TexturesCount] = {};
      UINT barriers_count = 0;
      for (size_t i = 0; i < T::TexturesCount; i++)
      {
         if (!sources[i])
            continue;
         const bool is_output = i == T::Output;
         ID3D12Resource* const resource = custom_data->textures[i].resource_12.Get();
         *dispatch_resources[i] = ffxApiGetResourceDX12(resource, is_output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ);
         D3D12_RESOURCE_BARRIER& barrier = barriers[barriers_count++];
         barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
         barrier.Transition.pResource = resource;
         barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
         barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
         barrier.Transition.StateAfter = is_output ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      }
      command_list_12->ResourceBarrier(barriers_count, barriers);

      const auto& settings_data = custom_data->settings_data;
      ASSERT_ONCE(settings_data.output_width == custom_data->textures[T::Output].width && settings_data.output_height == custom_data->textures[T::Output].height);
      dispatch_upscale.upscaleSize.width = settings_data.output_width;
      dispatch_upscale.upscaleSize.height = settings_data.output_height;
      dispatch_upscale.renderSize.width = draw_data.render_width != 0 ? draw_data.render_width : settings_data.render_width;
      dispatch_upscale.renderSize.height = draw_data.render_height != 0 ? draw_data.render_height : settings_data.render_height;
      dispatch_upscale.jitterOffset.x = draw_data.jitter_x;
      dispatch_upscale.jitterOffset.y = draw_data.jitter_y;
      dispatch_upscale.motionVectorScale.x = settings_data.mvs_x_scale;
      dispatch_upscale.motionVectorScale.y = settings_data.mvs_y_scale;
      dispatch_upscale.preExposure = draw_data.pre_exposure == 0.f ? 1.f : draw_data.pre_exposure;
      dispatch_upscale.cameraFovAngleVertical = draw_data.vert_fov;
      dispatch_upscale.cameraNear = settings_data.inverted_depth ? draw_data.far_plane : draw_data.near_plane;
      dispatch_upscale.cameraFar = settings_data.inverted_depth ? draw_data.near_plane : draw_data.far_plane;
      dispatch_upscale.viewSpaceToMetersFactor = 1.f;
      dispatch_upscale.reset = draw_data.reset;
      dispatch_upscale.enableSharpening = draw_data.user_sharpness >= 0.f;
      dispatch_upscale.sharpness = dispatch_upscale.enableSharpening ? draw_data.user_sharpness : 0.f;
      dispatch_upscale.frameTimeDelta = (draw_data.time_delta > 0.f ? draw_data.time_delta : (1.f / 60.f)) * 1000.f; // FSR expects milliseconds
#if DEVELOPMENT
      if (SR::debug_view)
         dispatch_upscale.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
#endif

      const ffxReturnCode_t ret = custom_data->ffx_dispatch(&custom_data->context, &dispatch_upscale.header);
      if (ret != FFX_API_RETURN_OK)
         LogError(*custom_data, std::format("ffxDispatch failed, error = {}", ret));

      for (UINT i = 0; i < barriers_count; i++)
         std::swap(barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
      command_list_12->ResourceBarrier(barriers_count, barriers);

      // Still submitted if FSR failed, to keep the DX11/DX12 sync consistent
      if (!D3D12Interop::ExecuteCommandList(custom_data->interop, command_list))
      {
         if (custom_data->interop.poisoned)
            custom_data->is_supported = false;
         return false;
      }
      if (ret != FFX_API_RETURN_OK)
         return false;
      command_list->CopyResource(draw_data.output_color, custom_data->textures[T::Output].resource_11.Get());
      return true;
   }
}

#endif
