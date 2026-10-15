#include "FSR4.h"

#if ENABLE_FSR4

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

#include "../includes/debug.h"
#include "../utils/system.h"

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
      // Max number of DX12 command lists in flight
      constexpr size_t fsr4_command_allocators_count = 4;
      // Max CPU wait on the DX12 GPU work (these waits are rare), to never hang the game if the GPU is stuck
      constexpr ULONGLONG fsr4_fence_timeout_ms = 2000;

      // ReShade's interface ID to query the original object from its proxies (see "com_utils.hpp")
      constexpr GUID IID_UnwrappedObject = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("FSR4: " + message).c_str());
      }

      void FFXMessageCallback(uint32_t type, const wchar_t* message)
      {
         char buffer[512] = {};
         size_t converted = 0;
         wcstombs_s(&converted, buffer, message ? message : L"", sizeof(buffer) - 1);
#if !DEVELOPMENT && !TEST
         if (type != FFX_API_MESSAGE_TYPE_ERROR)
            return;
#endif
         Log(type == FFX_API_MESSAGE_TYPE_ERROR ? reshade::log::level::error : reshade::log::level::warning, std::string("FidelityFX: ") + buffer);
      }
   }

   // A texture created on DX11 and opened on DX12, used to pass data between the two
   struct FSR4SharedTexture
   {
      ComPtr<ID3D11Texture2D> resource_11;
      ComPtr<ID3D12Resource> resource_12;
      UINT width = 0;
      UINT height = 0;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   };

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

      HMODULE ffx_module = nullptr;
      PfnFfxCreateContext ffx_create_context = nullptr;
      PfnFfxDestroyContext ffx_destroy_context = nullptr;
      PfnFfxQuery ffx_query = nullptr;
      PfnFfxDispatch ffx_dispatch = nullptr;
      ffxContext context = nullptr;
      int phase_count = fsr4_default_phase_count;

      ComPtr<ID3D11Device> device_11;
      ComPtr<ID3D12Device> device_12; // Native (not wrapped by ReShade)
      ComPtr<ID3D12Device> device_12_wrapped; // Kept alive just in case, if ReShade wrapped our device
      ComPtr<ID3D12CommandQueue> command_queue;
      ComPtr<ID3D12CommandAllocator> command_allocators[fsr4_command_allocators_count];
      UINT64 command_allocators_fence_values[fsr4_command_allocators_count] = {};
      size_t command_allocator_index = 0;
      ComPtr<ID3D12GraphicsCommandList> command_list_12;

      // Shared between DX11 and DX12, to sync their GPU work without CPU waits
      ComPtr<ID3D12Fence> fence_12;
      ComPtr<ID3D11Fence> fence_11;
      UINT64 fence_value = 0; // The last signaled one
      HANDLE fence_event = nullptr; // For the rare CPU waits (e.g. on resources destruction)

      FSR4SharedTexture textures[TexturesCount];
      ComPtr<ID3D11UnorderedAccessView> depth_uav;
      ComPtr<ID3D11Resource> depth_source; // Kept alive to make sure the pointer can't be reused by another resource while we cache its view
      ComPtr<ID3D11ShaderResourceView> depth_source_srv;
      std::function<ID3D11ComputeShader*()> get_depth_copy_shader;

      unsigned int logged_errors = 0;
      bool poisoned = false; // The GPU sync broke, so we can't know when the DX12 work completes, never use or release the resources again

      void Poison()
      {
         poisoned = true;
         is_supported = false;
      }

      void LogError(const std::string& message)
      {
         if (logged_errors++ < 10) // Don't spam the log
            Log(reshade::log::level::error, message);
      }

      // Returns false if the GPU didn't reach the value in time (and the device wasn't removed), in which case the resources it uses can't be safely released
      bool WaitForFence(UINT64 value)
      {
         const ULONGLONG deadline = GetTickCount64() + fsr4_fence_timeout_ms;
         while (fence_12->GetCompletedValue() < value)
         {
            if (FAILED(device_12->GetDeviceRemovedReason()))
               return true; // Nothing will run anymore
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline || FAILED(fence_12->SetEventOnCompletion(value, fence_event)))
            {
               LogError(std::format("Timed out waiting for the DX12 GPU work (fence {}, completed {})", value, fence_12->GetCompletedValue()));
               return false;
            }
            WaitForSingleObject(fence_event, DWORD(deadline - now)); // Loop to check the value, the event might have been signaled by a previous (timed out) wait
         }
         return true;
      }

      void DestroyContext()
      {
         if (context && ffx_destroy_context(&context, nullptr) != FFX_API_RETURN_OK)
            Log(reshade::log::level::warning, "ffxDestroyContext failed");
         context = nullptr;
      }

      bool CreateSharedTexture(FSR4SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access)
      {
         // Created on DX11 (the game device) with a legacy (non NT) shared handle, and opened on DX12.
         // DX12 created textures failed to open on DX11 in some setups (E_INVALIDARG), legacy handles are also the most compatible (e.g. with the ReShade proxy).
         texture = {};
         D3D11_TEXTURE2D_DESC desc = {};
         desc.Width = width;
         desc.Height = height;
         desc.MipLevels = 1;
         desc.ArraySize = 1;
         desc.Format = format;
         desc.SampleDesc.Count = 1;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (unordered_access ? D3D11_BIND_UNORDERED_ACCESS : 0);
         desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
         ComPtr<IDXGIResource> dxgi_resource;
         HANDLE shared_handle = nullptr; // Legacy handles aren't real handles and must not be closed
         HRESULT hr = device_11->CreateTexture2D(&desc, nullptr, &texture.resource_11);
         if (SUCCEEDED(hr))
            hr = texture.resource_11.As(&dxgi_resource);
         if (SUCCEEDED(hr))
            hr = dxgi_resource->GetSharedHandle(&shared_handle);
         if (SUCCEEDED(hr))
            hr = device_12->OpenSharedHandle(shared_handle, IID_PPV_ARGS(&texture.resource_12));
         if (FAILED(hr))
         {
            LogError(std::format("Failed to share a DX11 texture with DX12 ({}x{} format {}), hr = {:#010x}", width, height, int(format), unsigned(hr)));
            texture = {};
            return false;
         }
         texture.width = width;
         texture.height = height;
         texture.format = format;
         return true;
      }

      // (Re)creates the shared texture if the source changed size or format
      bool UpdateSharedTexture(TextureIndex index, ID3D11Resource* source)
      {
         ComPtr<ID3D11Texture2D> source_texture;
         if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))))
         {
            LogError("An input/output resource is not a 2D texture");
            return false;
         }
         D3D11_TEXTURE2D_DESC source_desc;
         source_texture->GetDesc(&source_desc);
         // The shared textures are copied to and from with whole resource copies.
         // sRGB isn't supported (it would need to be known on context creation), Luma generally feeds SR with linear colors anyway.
         const auto source_format = reshade::api::format(source_desc.Format);
         const bool is_srgb = reshade::api::format_to_default_typed(source_format, 1) == source_format && reshade::api::format_to_default_typed(source_format, 0) != source_format;
         if (index != Depth && (source_desc.MipLevels != 1 || source_desc.ArraySize != 1 || source_desc.SampleDesc.Count != 1 || is_srgb))
         {
            LogError(std::format("Unsupported input/output texture (mips {}, array size {}, samples {}, format {})", source_desc.MipLevels, source_desc.ArraySize, source_desc.SampleDesc.Count, int(source_desc.Format)));
            return false;
         }
         // Shared textures need a typed format for FSR to create views on them
         const DXGI_FORMAT format = index == Depth ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT(reshade::api::format_to_default_typed(source_format));
         FSR4SharedTexture& texture = textures[index];
         if (texture.resource_11 && texture.width == source_desc.Width && texture.height == source_desc.Height && texture.format == format)
            return true;

         // The previous one might still be in use by DX12
         const bool unordered_access = index == Depth || index == Output;
         if (!WaitForFence(fence_value) || !CreateSharedTexture(texture, source_desc.Width, source_desc.Height, format, unordered_access))
            return false;
         if (index == Depth)
         {
            depth_uav = nullptr;
            const HRESULT hr = device_11->CreateUnorderedAccessView(texture.resource_11.Get(), nullptr, &depth_uav);
            if (FAILED(hr))
            {
               LogError(std::format("Failed to create the shared depth UAV, hr = {:#010x}", unsigned(hr)));
               texture = {};
               return false;
            }
         }
         return true;
      }

      bool UpdateDepthSourceSRV(ID3D11Resource* source)
      {
         if (depth_source_srv && depth_source.Get() == source)
            return true;
         depth_source_srv = nullptr;
         depth_source = source;
         D3D11_TEXTURE2D_DESC desc;
         static_cast<ID3D11Texture2D*>(source)->GetDesc(&desc); // Already verified to be a 2D texture
         // The depth (not stencil) view of the typeless depth formats ("format_to_default_typed()" would pick float for 16 bit)
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = desc.Format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT(reshade::api::format_to_default_typed(reshade::api::format(desc.Format)));
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MipLevels = 1;
         const HRESULT hr = desc.SampleDesc.Count > 1 ? E_INVALIDARG : device_11->CreateShaderResourceView(source, &srv_desc, &depth_source_srv);
         if (FAILED(hr))
         {
            LogError(std::format("Failed to create the depth SRV (format {}, samples {}), hr = {:#010x}", int(desc.Format), desc.SampleDesc.Count, unsigned(hr)));
            depth_source = nullptr;
            return false;
         }
         return true;
      }

      bool Init(ID3D11Device* device, IDXGIAdapter* adapter)
      {
         ffx_create_context = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(ffx_module, "ffxCreateContext"));
         ffx_destroy_context = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(ffx_module, "ffxDestroyContext"));
         ffx_query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(ffx_module, "ffxQuery"));
         ffx_dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(ffx_module, "ffxDispatch"));
         if (!ffx_create_context || !ffx_destroy_context || !ffx_query || !ffx_dispatch)
         {
            Log(reshade::log::level::error, "The FidelityFX API dll is missing exports");
            return false;
         }

         // DX11 fences sharing requires Windows 10 1703+
         device_11 = device;
         ComPtr<ID3D11Device5> device_11_5;
         if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device_11_5))))
         {
            Log(reshade::log::level::error, "ID3D11Device5 is not supported, can't share fences between DX11 and DX12");
            return false;
         }

         // Create the DX12 device on the same adapter as the DX11 one
         ComPtr<IDXGIAdapter> dxgi_adapter = adapter;
         ComPtr<IDXGIDevice> dxgi_device;
         if (!dxgi_adapter && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))))
            dxgi_device->GetAdapter(&dxgi_adapter);
         DXGI_ADAPTER_DESC adapter_desc = {};
         const auto d3d12_create_device = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(LoadLibraryW(L"d3d12.dll"), "D3D12CreateDevice")); // Never freed, it's a system dll
         if (!dxgi_adapter || FAILED(dxgi_adapter->GetDesc(&adapter_desc)) || !d3d12_create_device)
         {
            Log(reshade::log::level::error, "Couldn't retrieve the DX11 adapter or load DX12");
            return false;
         }
         const HRESULT create_hr = d3d12_create_device(dxgi_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_12));
         if (FAILED(create_hr))
         {
            Log(reshade::log::level::error, std::format("D3D12CreateDevice failed, hr = {:#010x}", unsigned(create_hr)));
            return false;
         }
         // ReShade wraps any DX12 device created in the process, use the original one, so none of our DX12 work goes through ReShade (and its addons)
         ComPtr<ID3D12Device> unwrapped_device_12;
         if (SUCCEEDED(device_12->QueryInterface(IID_UnwrappedObject, reinterpret_cast<void**>(unwrapped_device_12.GetAddressOf()))))
         {
            device_12_wrapped = std::move(device_12);
            device_12 = std::move(unwrapped_device_12);
         }
         const LUID device_12_luid = device_12->GetAdapterLuid();
         if (device_12_luid.LowPart != adapter_desc.AdapterLuid.LowPart || device_12_luid.HighPart != adapter_desc.AdapterLuid.HighPart)
         {
            Log(reshade::log::level::error, "The DX12 device adapter doesn't match the DX11 one");
            return false;
         }

         D3D12_COMMAND_QUEUE_DESC queue_desc = {};
         queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
         HRESULT hr = device_12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));
         for (auto& command_allocator : command_allocators)
         {
            if (SUCCEEDED(hr))
               hr = device_12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));
         }
         if (SUCCEEDED(hr))
            hr = device_12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&command_list_12));
         if (SUCCEEDED(hr))
            hr = command_list_12->Close();
         // The fence is created on DX12 and opened on DX11 (through an NT handle)
         HANDLE fence_handle = nullptr;
         if (SUCCEEDED(hr))
            hr = device_12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence_12));
         if (SUCCEEDED(hr))
            hr = device_12->CreateSharedHandle(fence_12.Get(), nullptr, GENERIC_ALL, nullptr, &fence_handle);
         if (SUCCEEDED(hr))
         {
            hr = device_11_5->OpenSharedFence(fence_handle, IID_PPV_ARGS(&fence_11));
            CloseHandle(fence_handle);
         }
         if (SUCCEEDED(hr))
         {
            fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            hr = fence_event ? S_OK : HRESULT_FROM_WIN32(GetLastError());
         }
         if (FAILED(hr))
         {
            Log(reshade::log::level::error, std::format("Failed to create the DX12 queue/command list/shared fence, hr = {:#010x}", unsigned(hr)));
            return false;
         }

         // Only claim support if the GPU (driver) offers an FSR 4 upscaler, otherwise FSR 3 is better used directly. The FidelityFX loader picks the newest version by default.
         uint64_t versions_count = 0;
         ffxQueryDescGetVersions versions_desc = {};
         versions_desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
         versions_desc.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
         versions_desc.device = device_12.Get();
         versions_desc.outputCount = &versions_count;
         std::vector<uint64_t> version_ids;
         std::vector<const char*> version_names;
         if (ffx_query(nullptr, &versions_desc.header) == FFX_API_RETURN_OK && versions_count > 0)
         {
            version_ids.resize(versions_count);
            version_names.resize(versions_count);
            versions_desc.versionIds = version_ids.data();
            versions_desc.versionNames = version_names.data();
            if (ffx_query(nullptr, &versions_desc.header) != FFX_API_RETURN_OK)
               version_names.clear();
         }
         const auto fsr4_version = std::find_if(version_names.begin(), version_names.end(), [](const char* name) { return name && name[0] == '4' && name[1] == '.'; });
         if (fsr4_version == version_names.end())
         {
            Log(reshade::log::level::info, "No FSR 4 upscaler available on this GPU/driver (is \"amd_fidelityfx_upscaler_dx12.dll\" next to the loader dll?)");
            return false;
         }

         // Make sure textures can actually be shared (otherwise FSR 3 will be automatically used instead)
         FSR4SharedTexture test_texture;
         if (!CreateSharedTexture(test_texture, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, true))
            return false;

         Log(reshade::log::level::info, std::format("Initialized DX11->DX12 interop with FSR {}{}", *fsr4_version, device_12_wrapped ? " (unwrapped the ReShade DX12 device)" : ""));
         return true;
      }
   };

   void FidelityFX::FSR4::SetDepthCopyShader(SR::InstanceData* data, std::function<ID3D11ComputeShader*()> get_depth_copy_shader)
   {
      if (data)
         static_cast<FSR4InstanceData*>(data)->get_depth_copy_shader = std::move(get_depth_copy_shader);
   }

   bool FidelityFX::FSR4::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data); // This will also null the pointer
      }

      // The FidelityFX API loader dll (which then loads the upscaler dll next to it) isn't shipped with Luma, it's expected to be in the game executable folder
      const std::filesystem::path game_folder = System::GetModulePath().parent_path();
      HMODULE ffx_module = nullptr;
      for (const wchar_t* dll_name : {L"amd_fidelityfx_loader_dx12.dll", L"amd_fidelityfx_dx12.dll"})
      {
         ffx_module = LoadLibraryExW((game_folder / dll_name).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
         if (ffx_module)
            break;
      }
      if (!ffx_module)
      {
         Log(reshade::log::level::info, "FidelityFX API DX12 loader dll not found next to the game executable, FSR 4 won't be available");
         return false;
      }

      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);
      custom_data = new FSR4InstanceData();
      custom_data->automatically_restores_pipeline_state = false;
      custom_data->min_resolution = 32; // Not 100% accurate but should be a decent start
      custom_data->ffx_module = ffx_module;
      custom_data->is_supported = custom_data->Init(device, adapter);
      return custom_data->is_supported;
   }

   void FidelityFX::FSR4::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
   {
      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);
      if (!custom_data)
         return;

      // If the GPU is stuck (but the device wasn't removed), leak everything instead of hanging or risking a crash
      if (custom_data->poisoned || (custom_data->fence_12 && !custom_data->WaitForFence(custom_data->fence_value)))
      {
         Log(reshade::log::level::warning, "Leaking the DX12 interop resources, as the GPU might still be using them");
         custom_data = nullptr;
         return;
      }

      custom_data->DestroyContext();
      if (custom_data->fence_event)
         CloseHandle(custom_data->fence_event);
      HMODULE ffx_module = custom_data->ffx_module;
      delete custom_data;
      custom_data = nullptr;
      FreeLibrary(ffx_module);
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
      if (!custom_data->WaitForFence(custom_data->fence_value))
         return false;
      custom_data->DestroyContext();

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
      context_desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
#endif
      context_desc.fpMessage = FFXMessageCallback;

      ffxCreateBackendDX12Desc backend_desc = {};
      backend_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
      backend_desc.device = custom_data->device_12.Get();
      context_desc.header.pNext = &backend_desc.header;

      // Required for ABI compatibility with the dll
      ffxCreateContextDescUpscaleVersion api_version_desc = {};
      api_version_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
      api_version_desc.version = FFX_UPSCALER_VERSION;
      backend_desc.header.pNext = &api_version_desc.header;

      const ffxReturnCode_t ret = custom_data->ffx_create_context(&custom_data->context, &context_desc.header, nullptr);
      if (ret != FFX_API_RETURN_OK)
      {
         custom_data->LogError(std::format("ffxCreateContext failed (render {}x{}, output {}x{}), error = {}", settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height, ret));
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

      ComPtr<ID3D11DeviceContext4> command_list_11_4;
      ComPtr<ID3D11ComputeShader> depth_copy_cs;
      if (custom_data && custom_data->get_depth_copy_shader)
         depth_copy_cs.Attach(custom_data->get_depth_copy_shader());
      if (!custom_data || !custom_data->is_supported || !custom_data->context || !depth_copy_cs || !draw_data.source_color || !draw_data.output_color || !draw_data.depth_buffer || !draw_data.motion_vectors
         || FAILED(command_list->QueryInterface(IID_PPV_ARGS(&command_list_11_4))))
      {
         return false;
      }

      // Make sure all the shared textures are up to date with the game ones (the optional ones are skipped)
      ID3D11Resource* const sources[T::TexturesCount] = {draw_data.source_color, draw_data.depth_buffer, draw_data.motion_vectors, draw_data.exposure, draw_data.bias_mask, draw_data.transparency_alpha, draw_data.output_color};
      for (size_t i = 0; i < T::TexturesCount; i++)
      {
         if (sources[i] && !custom_data->UpdateSharedTexture(T::TextureIndex(i), sources[i]))
            return false;
      }
      if (!custom_data->UpdateDepthSourceSRV(draw_data.depth_buffer))
         return false;

      // DX11: copy the inputs into the shared textures, and convert the depth (the pipeline state is restored by the caller)
      for (size_t i : {T::Color, T::MotionVectors, T::Exposure, T::Reactive, T::Transparency})
      {
         if (sources[i])
            command_list->CopyResource(custom_data->textures[i].resource_11.Get(), sources[i]);
      }
      command_list->OMSetRenderTargets(0, nullptr, nullptr); // The depth might still be bound as depth stencil, which would null its SRV
      ID3D11ShaderResourceView* const depth_srv = custom_data->depth_source_srv.Get();
      ID3D11UnorderedAccessView* const depth_uav = custom_data->depth_uav.Get();
      command_list->CSSetShader(depth_copy_cs.Get(), nullptr, 0);
      command_list->CSSetShaderResources(0, 1, &depth_srv);
      command_list->CSSetUnorderedAccessViews(0, 1, &depth_uav, nullptr);
      command_list->Dispatch((custom_data->textures[T::Depth].width + 7) / 8, (custom_data->textures[T::Depth].height + 7) / 8, 1);

      // DX11 -> DX12 sync. Flush to make sure DX11 submits its work (and the signal) before DX12 waits on it.
      const UINT64 inputs_ready_fence_value = custom_data->fence_value + 1;
      HRESULT hr = command_list_11_4->Signal(custom_data->fence_11.Get(), inputs_ready_fence_value);
      if (FAILED(hr))
      {
         custom_data->LogError(std::format("Failed to signal the DX11 fence, hr = {:#010x}", unsigned(hr)));
         return false;
      }
      custom_data->fence_value = inputs_ready_fence_value;
      command_list->Flush();

      // DX12: record the FSR dispatch
      const size_t allocator_index = custom_data->command_allocator_index;
      custom_data->command_allocator_index = (allocator_index + 1) % fsr4_command_allocators_count;
      ID3D12CommandAllocator* const command_allocator = custom_data->command_allocators[allocator_index].Get();
      ID3D12GraphicsCommandList* const command_list_12 = custom_data->command_list_12.Get();
      hr = custom_data->WaitForFence(custom_data->command_allocators_fence_values[allocator_index]) ? S_OK : HRESULT_FROM_WIN32(WAIT_TIMEOUT); // Usually already done, as DX11 waited on it
      if (SUCCEEDED(hr))
         hr = command_allocator->Reset();
      if (SUCCEEDED(hr))
         hr = command_list_12->Reset(command_allocator, nullptr);
      if (FAILED(hr))
      {
         custom_data->LogError(std::format("Failed to reset the DX12 command list, hr = {:#010x}", unsigned(hr)));
         return false;
      }

      // Mirrors the FSR 3 implementation
      ffxDispatchDescUpscale dispatch_upscale = {};
      dispatch_upscale.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
      dispatch_upscale.commandList = command_list_12;

      // Simultaneous access resources are in the common state at the beginning and end of every command list, transition them to what FSR expects and back
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

      const ffxReturnCode_t ret = custom_data->ffx_dispatch(&custom_data->context, &dispatch_upscale.header);
      if (ret != FFX_API_RETURN_OK)
         custom_data->LogError(std::format("ffxDispatch failed, error = {}", ret));

      for (UINT i = 0; i < barriers_count; i++)
         std::swap(barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);
      command_list_12->ResourceBarrier(barriers_count, barriers);
      hr = command_list_12->Close();

      // DX12: wait for the DX11 inputs, run FSR, and signal back
      const UINT64 output_ready_fence_value = custom_data->fence_value + 1;
      if (SUCCEEDED(hr))
         hr = custom_data->command_queue->Wait(custom_data->fence_12.Get(), inputs_ready_fence_value);
      if (SUCCEEDED(hr))
      {
         ID3D12CommandList* const command_lists[] = {command_list_12};
         custom_data->command_queue->ExecuteCommandLists(1, command_lists);
         hr = custom_data->command_queue->Signal(custom_data->fence_12.Get(), output_ready_fence_value);
      }
      else
      {
         custom_data->LogError(std::format("Failed to submit the DX12 work, hr = {:#010x}", unsigned(hr)));
         return false;
      }
      if (SUCCEEDED(hr))
      {
         custom_data->fence_value = output_ready_fence_value;
         custom_data->command_allocators_fence_values[allocator_index] = output_ready_fence_value;
         // DX11: wait for DX12 (on the GPU) and copy back the output
         hr = command_list_11_4->Wait(custom_data->fence_11.Get(), output_ready_fence_value);
      }
      if (FAILED(hr))
      {
         custom_data->LogError(std::format("Failed to sync the DX12 work with DX11, hr = {:#010x}", unsigned(hr)));
         custom_data->Poison();
         return false;
      }
      if (ret != FFX_API_RETURN_OK)
         return false;
      command_list->CopyResource(draw_data.output_color, custom_data->textures[T::Output].resource_11.Get());
      return true;
   }
}

#endif
