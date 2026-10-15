#include "FSR4.h"

#if ENABLE_FSR4

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

#include "ffx_api/ffx_api.h"
#include "ffx_api/ffx_upscale.h"
#include "ffx_api/dx12/ffx_api_dx12.h"

#include "../includes/debug.h"

#include <include/reshade.hpp>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace FidelityFX
{
   using Microsoft::WRL::ComPtr;

   namespace
   {
      constexpr int fsr4_default_phase_count = 16;
      // Max number of DX12 command lists in flight. Note that we might run more than one SR pass per frame.
      constexpr size_t fsr4_command_allocators_count = 8;
      // Max CPU wait on the DX12 GPU work (these waits are rare), to never hang the game if the GPU is stuck
      constexpr DWORD fsr4_fence_timeout_ms = 2000;

      // The FidelityFX API "loader" dll, which then loads the actual effect dlls (e.g. "amd_fidelityfx_upscaler_dx12.dll") from its folder
      constexpr const wchar_t* fsr4_loader_dll_names[] = { L"amd_fidelityfx_dx12.dll", L"amd_fidelityfx_loader_dx12.dll" };

      std::mutex interop_device_mutex;
      std::atomic<bool> creating_interop_device = false;
      ID3D12Device* interop_native_device = nullptr; // Captured from the ReShade "init_device" event, if ReShade wrapped our device

      template<typename... Args>
      void Log(reshade::log::level level, const char* format, Args... args)
      {
         char buffer[1024];
         snprintf(buffer, sizeof(buffer), format, args...);
         reshade::log::message(level, buffer);
      }

      void FFXMessageCallback(uint32_t type, const wchar_t* message)
      {
         char buffer[512] = {};
         size_t converted = 0;
         wcstombs_s(&converted, buffer, message ? message : L"", sizeof(buffer) - 1);
         if (type == FFX_API_MESSAGE_TYPE_ERROR)
         {
            Log(reshade::log::level::error, "FSR4: FidelityFX error: %s", buffer);
         }
#if DEVELOPMENT || TEST
         else
         {
            Log(reshade::log::level::warning, "FSR4: FidelityFX warning: %s", buffer);
         }
#endif
      }

      // Parses "4.1.1" like strings into a comparable number (skipping any text prefix, e.g. "FSR 4.0.2", and ignoring anything after the numbers)
      uint64_t ParseVersion(const char* name)
      {
         uint64_t parts[3] = {};
         int part = 0;
         while (name && *name && (*name < '0' || *name > '9'))
            ++name;
         for (const char* c = name; c && *c && part < 3; ++c)
         {
            if (*c >= '0' && *c <= '9')
               parts[part] = parts[part] * 10 + uint64_t(*c - '0');
            else if (*c == '.')
               ++part;
            else
               break;
         }
         return (parts[0] << 40) | (parts[1] << 20) | parts[2];
      }

      // Shared textures need a typed format for FSR to create views on them (copies work within the same typeless family).
      // sRGB is mapped to its UNORM equivalent: copies between them are raw bit copies (no conversion), so the input is passed through FSR in its encoded (gamma) space,
      // and the output is copied back with the same bits, so the game reads it with the encoding it expects (no double/missing gamma).
      DXGI_FORMAT GetTypedFormat(DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
         case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
         case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
         case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
         case DXGI_FORMAT_R8G8B8A8_TYPELESS:
         case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
         case DXGI_FORMAT_B8G8R8A8_TYPELESS:
         case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
         case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
         case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
         case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
         case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
         default: return format;
         }
      }

      // The format to read the depth of a depth stencil (typeless) texture
      DXGI_FORMAT GetDepthSRVFormat(DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_R24G8_TYPELESS:
         case DXGI_FORMAT_D24_UNORM_S8_UINT:
         case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
         case DXGI_FORMAT_R32G8X24_TYPELESS:
         case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
         case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
         case DXGI_FORMAT_R32_TYPELESS:
         case DXGI_FORMAT_D32_FLOAT:
         case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
         case DXGI_FORMAT_R16_TYPELESS:
         case DXGI_FORMAT_D16_UNORM:
         case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_UNORM;
         default: return GetTypedFormat(format);
         }
      }

      // Copies any depth format into a R32_FLOAT texture (FSR wants 32bit depth, and depth stencil formats can't be shared with simultaneous access)
      constexpr char depth_copy_cs_code[] = R"(
Texture2D<float> source_depth : register(t0);
RWTexture2D<float> target_depth : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
   target_depth[id.xy] = source_depth.Load(int3(id.xy, 0));
}
)";
   }

   // A texture created on DX12 and opened on DX11, used to pass data between the two
   struct FSR4SharedTexture
   {
      ComPtr<ID3D12Resource> resource_12;
      ComPtr<ID3D11Texture2D> resource_11;
      UINT width = 0;
      UINT height = 0;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   };

   // Per context (settings) data, each has its own history and resources (e.g. different SR passes in the same frame)
   struct FSR4ContextData
   {
      ffxContext context = nullptr;
      int phase_count = fsr4_default_phase_count;
      UINT64 last_used_fence_value = 0;

      FSR4SharedTexture color;
      FSR4SharedTexture depth;
      FSR4SharedTexture motion_vectors;
      FSR4SharedTexture exposure;
      FSR4SharedTexture reactive;
      FSR4SharedTexture transparency;
      FSR4SharedTexture output;

      ComPtr<ID3D11UnorderedAccessView> depth_uav;
      ComPtr<ID3D11Resource> depth_source; // Kept alive to make sure the pointer can't be reused by another resource while we cache its view
      ComPtr<ID3D11ShaderResourceView> depth_source_srv;
   };

   struct FSR4InstanceData : public SR::InstanceData
   {
      HMODULE ffx_module = nullptr;
      PfnFfxCreateContext ffx_create_context = nullptr;
      PfnFfxDestroyContext ffx_destroy_context = nullptr;
      PfnFfxConfigure ffx_configure = nullptr;
      PfnFfxQuery ffx_query = nullptr;
      PfnFfxDispatch ffx_dispatch = nullptr;

      uint64_t version_id = 0;
      std::string version_name;
      bool has_native_fsr4 = false;

      ComPtr<ID3D11Device> device_11;
      ComPtr<ID3D11Device1> device_11_1;
      ComPtr<ID3D11Device5> device_11_5;
      ID3D11DeviceContext* last_command_list = nullptr;
      ComPtr<ID3D11DeviceContext4> command_list_11_4;
      ComPtr<ID3D11ComputeShader> depth_copy_cs;

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
      UINT64 fence_value = 0;
      HANDLE fence_event = nullptr; // For the rare CPU waits (e.g. on resources destruction)

      SR::SettingsCache<FSR4ContextData, 2> cached_contexts; // Smaller than the others, as each context also has its own shared textures
      FSR4ContextData* current = nullptr;

      unsigned int logged_errors = 0;

      bool CanLogError()
      {
         return logged_errors++ < 10; // Don't spam the log
      }

      // Returns false if the GPU didn't reach the value in time (e.g. device removed/hung), so that we never hang the game
      bool WaitForFence(UINT64 value)
      {
         if (!fence_12 || value == 0 || fence_12->GetCompletedValue() >= value)
            return true;
         if (SUCCEEDED(fence_12->SetEventOnCompletion(value, fence_event)) && WaitForSingleObject(fence_event, fsr4_fence_timeout_ms) == WAIT_OBJECT_0)
            return true;
         if (fence_12->GetCompletedValue() >= value)
            return true;
         const HRESULT removed_reason_11 = device_11 ? device_11->GetDeviceRemovedReason() : S_OK;
         const HRESULT removed_reason_12 = device_12 ? device_12->GetDeviceRemovedReason() : S_OK;
         if (CanLogError())
            Log(reshade::log::level::error, "FSR4: Timed out waiting for the DX12 GPU work (fence %llu, completed %llu), DX11 device removed reason = 0x%08X, DX12 device removed reason = 0x%08X",
               static_cast<unsigned long long>(value), static_cast<unsigned long long>(fence_12->GetCompletedValue()), unsigned(removed_reason_11), unsigned(removed_reason_12));
         return false;
      }

      void DestroyContext(FSR4ContextData& context_data)
      {
         // Make sure the GPU isn't using it anymore. If it timed out on a live device, leak the context instead of risking a crash, if the device was removed it's safe to destroy.
         const bool gpu_done = WaitForFence(context_data.last_used_fence_value) || (device_12 && FAILED(device_12->GetDeviceRemovedReason()));
         if (!gpu_done)
         {
            Log(reshade::log::level::warning, "FSR4: Leaking a context (and its resources) that might still be in use by the GPU");
            new FSR4ContextData(std::move(context_data)); // Intentionally leaked
            context_data = FSR4ContextData();
            return;
         }
         if (context_data.context)
         {
            ffxReturnCode_t ret = ffx_destroy_context(&context_data.context, nullptr);
            if (ret != FFX_API_RETURN_OK)
            {
               Log(reshade::log::level::warning, "FSR4: ffxDestroyContext failed, error = %u", ret);
            }
            context_data.context = nullptr;
         }
         context_data = FSR4ContextData();
      }

      bool CreateSharedTexture(FSR4SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access)
      {
         texture = FSR4SharedTexture();

         D3D12_HEAP_PROPERTIES heap_properties = {};
         heap_properties.Type = D3D12_HEAP_TYPE_DEFAULT;
         D3D12_RESOURCE_DESC desc = {};
         desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
         desc.Width = width;
         desc.Height = height;
         desc.DepthOrArraySize = 1;
         desc.MipLevels = 1;
         desc.Format = format;
         desc.SampleDesc.Count = 1;
         desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
         // Simultaneous access allows both APIs to access it without explicit layout transitions (we sync them with fences)
         desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | (unordered_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);

         HRESULT hr = device_12->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture.resource_12));
         if (FAILED(hr))
         {
            if (CanLogError())
               Log(reshade::log::level::error, "FSR4: Failed to create shared DX12 texture %ux%u format %u, hr = 0x%08X", width, height, unsigned(format), unsigned(hr));
            texture = FSR4SharedTexture();
            return false;
         }

         HANDLE shared_handle = nullptr;
         hr = device_12->CreateSharedHandle(texture.resource_12.Get(), nullptr, GENERIC_ALL, nullptr, &shared_handle);
         if (SUCCEEDED(hr))
         {
            hr = device_11_1->OpenSharedResource1(shared_handle, IID_PPV_ARGS(&texture.resource_11));
            CloseHandle(shared_handle);
         }
         if (FAILED(hr))
         {
            if (CanLogError())
               Log(reshade::log::level::error, "FSR4: Failed to share DX12 texture with DX11 (%ux%u format %u), hr = 0x%08X", width, height, unsigned(format), unsigned(hr));
            texture = FSR4SharedTexture();
            return false;
         }

         texture.width = width;
         texture.height = height;
         texture.format = format;
         return true;
      }

      // (Re)creates the shared texture if the source changed size or format
      bool UpdateSharedTexture(FSR4ContextData& context_data, FSR4SharedTexture& texture, ID3D11Resource* source, bool unordered_access, DXGI_FORMAT forced_format = DXGI_FORMAT_UNKNOWN)
      {
         ComPtr<ID3D11Texture2D> source_texture;
         if (!source || FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))))
         {
            if (CanLogError())
               Log(reshade::log::level::error, "FSR4: An input/output resource is not a 2D texture");
            return false;
         }
         D3D11_TEXTURE2D_DESC source_desc;
         source_texture->GetDesc(&source_desc);
         const DXGI_FORMAT format = forced_format != DXGI_FORMAT_UNKNOWN ? forced_format : GetTypedFormat(source_desc.Format);
         if (texture.resource_11 && texture.width == source_desc.Width && texture.height == source_desc.Height && texture.format == format)
         {
            return true;
         }
         if (!WaitForFence(context_data.last_used_fence_value)) // The previous one might still be in use
            return false;
         return CreateSharedTexture(texture, source_desc.Width, source_desc.Height, format, unordered_access);
      }
   };

   bool FidelityFX::FSR4::IsCreatingInteropDevice()
   {
      return creating_interop_device.load();
   }

   void FidelityFX::FSR4::OnInteropDeviceInit(void* native_device)
   {
      if (creating_interop_device.load() && native_device && !interop_native_device)
      {
         interop_native_device = reinterpret_cast<ID3D12Device*>(native_device);
         interop_native_device->AddRef();
      }
   }

   bool FidelityFX::FSR4::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data); // This will also null the pointer
      }

      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);

      // Find the FidelityFX API loader dll, it's expected to be in the game executable folder (or the addon one), we don't ship it
      HMODULE ffx_module = nullptr;
      {
         std::vector<std::wstring> search_folders;
         wchar_t path[MAX_PATH] = {};
         if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0)
         {
            std::wstring folder = path;
            search_folders.push_back(folder.substr(0, folder.find_last_of(L"\\/") + 1));
         }
         HMODULE addon_module = nullptr;
         if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&FSR4::IsCreatingInteropDevice), &addon_module)
            && GetModuleFileNameW(addon_module, path, MAX_PATH) > 0)
         {
            std::wstring folder = path;
            search_folders.push_back(folder.substr(0, folder.find_last_of(L"\\/") + 1));
         }
         for (const wchar_t* dll_name : fsr4_loader_dll_names)
         {
            for (const std::wstring& folder : search_folders)
            {
               const std::wstring dll_path = folder + dll_name;
               // Altered search path so the loader finds the effect dlls next to it
               ffx_module = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
               if (ffx_module)
               {
                  char narrow_path[MAX_PATH * 2] = {};
                  size_t converted = 0;
                  wcstombs_s(&converted, narrow_path, dll_path.c_str(), sizeof(narrow_path) - 1);
                  Log(reshade::log::level::info, "FSR4: Loaded FidelityFX API from \"%s\"", narrow_path);
                  break;
               }
            }
            if (ffx_module)
               break;
         }
      }
      if (!ffx_module)
      {
         Log(reshade::log::level::info, "FSR4: FidelityFX API DX12 dll (amd_fidelityfx_dx12.dll) not found next to the game executable, FSR 4 won't be available");
         return false;
      }

      custom_data = new FSR4InstanceData();
      custom_data->is_supported = false;
      custom_data->automatically_restores_pipeline_state = false;
      custom_data->min_resolution = 32; // Not 100% accurate but should be a decent start
      custom_data->ffx_module = ffx_module;
      custom_data->ffx_create_context = reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(ffx_module, "ffxCreateContext"));
      custom_data->ffx_destroy_context = reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(ffx_module, "ffxDestroyContext"));
      custom_data->ffx_configure = reinterpret_cast<PfnFfxConfigure>(GetProcAddress(ffx_module, "ffxConfigure"));
      custom_data->ffx_query = reinterpret_cast<PfnFfxQuery>(GetProcAddress(ffx_module, "ffxQuery"));
      custom_data->ffx_dispatch = reinterpret_cast<PfnFfxDispatch>(GetProcAddress(ffx_module, "ffxDispatch"));
      if (!custom_data->ffx_create_context || !custom_data->ffx_destroy_context || !custom_data->ffx_query || !custom_data->ffx_dispatch)
      {
         Log(reshade::log::level::error, "FSR4: The FidelityFX API dll is missing exports");
         return false;
      }

      // DX11 interop interfaces (Windows 10 1703+)
      custom_data->device_11 = device;
      if (FAILED(device->QueryInterface(IID_PPV_ARGS(&custom_data->device_11_1))) || FAILED(device->QueryInterface(IID_PPV_ARGS(&custom_data->device_11_5))))
      {
         Log(reshade::log::level::error, "FSR4: ID3D11Device5 is not supported, can't share fences between DX11 and DX12");
         return false;
      }

      // Create the DX12 device on the same adapter as the DX11 one
      ComPtr<IDXGIAdapter> dxgi_adapter = adapter;
      if (!dxgi_adapter)
      {
         ComPtr<IDXGIDevice> dxgi_device;
         if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))))
         {
            dxgi_device->GetAdapter(&dxgi_adapter);
         }
      }
      if (!dxgi_adapter)
      {
         Log(reshade::log::level::error, "FSR4: Couldn't retrieve the DX11 device adapter");
         return false;
      }
      DXGI_ADAPTER_DESC adapter_desc = {};
      dxgi_adapter->GetDesc(&adapter_desc);

      HMODULE d3d12_module = GetModuleHandleW(L"d3d12.dll");
      if (!d3d12_module)
         d3d12_module = LoadLibraryW(L"d3d12.dll"); // Never freed, it's a system dll that is likely to be used by other things anyway
      PFN_D3D12_CREATE_DEVICE d3d12_create_device = d3d12_module ? reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d12_module, "D3D12CreateDevice")) : nullptr;
      if (!d3d12_create_device)
      {
         Log(reshade::log::level::error, "FSR4: Couldn't load d3d12.dll");
         return false;
      }

      {
         const std::lock_guard lock(interop_device_mutex);
         interop_native_device = nullptr;
         creating_interop_device = true;
         ComPtr<ID3D12Device> device_12;
         HRESULT hr = d3d12_create_device(dxgi_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_12));
         creating_interop_device = false;
         if (FAILED(hr) || !device_12)
         {
            if (interop_native_device)
               interop_native_device->Release();
            interop_native_device = nullptr;
            Log(reshade::log::level::error, "FSR4: D3D12CreateDevice failed, hr = 0x%08X", unsigned(hr));
            return false;
         }
         if (interop_native_device)
         {
            // ReShade wrapped it, use the native one directly
            custom_data->device_12_wrapped = device_12;
            custom_data->device_12.Attach(interop_native_device);
            interop_native_device = nullptr;
         }
         else
         {
            custom_data->device_12 = device_12;
         }
      }
      const LUID device_12_luid = custom_data->device_12->GetAdapterLuid();
      if (device_12_luid.LowPart != adapter_desc.AdapterLuid.LowPart || device_12_luid.HighPart != adapter_desc.AdapterLuid.HighPart)
      {
         Log(reshade::log::level::error, "FSR4: The DX12 device adapter doesn't match the DX11 one");
         return false;
      }

      D3D12_COMMAND_QUEUE_DESC queue_desc = {};
      queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
      HRESULT hr = custom_data->device_12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&custom_data->command_queue));
      for (size_t i = 0; SUCCEEDED(hr) && i < fsr4_command_allocators_count; i++)
      {
         hr = custom_data->device_12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&custom_data->command_allocators[i]));
      }
      if (SUCCEEDED(hr))
      {
         hr = custom_data->device_12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, custom_data->command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&custom_data->command_list_12));
      }
      if (SUCCEEDED(hr))
      {
         hr = custom_data->command_list_12->Close();
      }
      if (SUCCEEDED(hr))
      {
         hr = custom_data->device_12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&custom_data->fence_12));
      }
      if (SUCCEEDED(hr))
      {
         HANDLE shared_handle = nullptr;
         hr = custom_data->device_12->CreateSharedHandle(custom_data->fence_12.Get(), nullptr, GENERIC_ALL, nullptr, &shared_handle);
         if (SUCCEEDED(hr))
         {
            hr = custom_data->device_11_5->OpenSharedFence(shared_handle, IID_PPV_ARGS(&custom_data->fence_11));
            CloseHandle(shared_handle);
         }
      }
      if (FAILED(hr))
      {
         Log(reshade::log::level::error, "FSR4: Failed to create the DX12 queue/command lists/shared fence, hr = 0x%08X", unsigned(hr));
         return false;
      }
      custom_data->fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

      // Depth conversion shader
      {
         HMODULE d3d_compiler = LoadLibraryW(L"d3dcompiler_47.dll");
         pD3DCompile d3d_compile = d3d_compiler ? reinterpret_cast<pD3DCompile>(GetProcAddress(d3d_compiler, "D3DCompile")) : nullptr;
         ComPtr<ID3DBlob> code_blob;
         ComPtr<ID3DBlob> error_blob;
         hr = d3d_compile ? d3d_compile(depth_copy_cs_code, sizeof(depth_copy_cs_code) - 1, "FSR4DepthCopyCS", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code_blob, &error_blob) : E_FAIL;
         if (SUCCEEDED(hr))
         {
            hr = device->CreateComputeShader(code_blob->GetBufferPointer(), code_blob->GetBufferSize(), nullptr, &custom_data->depth_copy_cs);
         }
         if (d3d_compiler)
            FreeLibrary(d3d_compiler);
         if (FAILED(hr))
         {
            Log(reshade::log::level::error, "FSR4: Failed to compile the depth copy shader, hr = 0x%08X %s", unsigned(hr), error_blob ? static_cast<const char*>(error_blob->GetBufferPointer()) : "");
            return false;
         }
      }

      // Find the available upscaler versions, and pick the highest one (FSR 4.x if the GPU supports it)
      {
         uint64_t versions_count = 0;
         ffxQueryDescGetVersions versions_desc = {};
         versions_desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
         versions_desc.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
         versions_desc.device = custom_data->device_12.Get();
         versions_desc.outputCount = &versions_count;
         ffxReturnCode_t ret = custom_data->ffx_query(nullptr, &versions_desc.header);
         if (ret != FFX_API_RETURN_OK || versions_count == 0)
         {
            Log(reshade::log::level::error, "FSR4: No FidelityFX upscaler versions available (error = %u). Is amd_fidelityfx_upscaler_dx12.dll next to amd_fidelityfx_dx12.dll?", ret);
            return false;
         }
         std::vector<uint64_t> version_ids(versions_count);
         std::vector<const char*> version_names(versions_count);
         versions_desc.versionIds = version_ids.data();
         versions_desc.versionNames = version_names.data();
         ret = custom_data->ffx_query(nullptr, &versions_desc.header);
         if (ret != FFX_API_RETURN_OK)
         {
            Log(reshade::log::level::error, "FSR4: Failed to query the FidelityFX upscaler versions, error = %u", ret);
            return false;
         }
         uint64_t best_version = 0;
         for (uint64_t i = 0; i < versions_count; i++)
         {
            const char* name = version_names[i] ? version_names[i] : "";
            Log(reshade::log::level::info, "FSR4: Available upscaler version: \"%s\" (id %llu)", name, static_cast<unsigned long long>(version_ids[i]));
            const uint64_t version = ParseVersion(name);
            if (custom_data->version_name.empty() || version > best_version)
            {
               best_version = version;
               custom_data->version_id = version_ids[i];
               custom_data->version_name = name;
            }
         }
         custom_data->has_native_fsr4 = (best_version >> 40) >= 4;
         Log(custom_data->has_native_fsr4 ? reshade::log::level::info : reshade::log::level::warning, "FSR4: Selected upscaler version \"%s\"%s", custom_data->version_name.c_str(), custom_data->has_native_fsr4 ? "" : " (FSR 4 isn't available on this GPU/driver)");
      }

      custom_data->is_supported = true;
      Log(reshade::log::level::info, "FSR4: Initialized DX11->DX12 interop on adapter \"%ls\"%s", adapter_desc.Description, custom_data->device_12_wrapped ? " (unwrapped the ReShade DX12 device)" : "");

      return custom_data != nullptr && custom_data->is_supported;
   }

   void FidelityFX::FSR4::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
   {
      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);

      if (!custom_data)
         return;

      // Wait for all the work to be done before destroying anything (this shouldn't take long).
      // If the GPU is stuck (but the device wasn't removed), leak everything instead of hanging or risking a crash.
      if (!custom_data->WaitForFence(custom_data->fence_value) && !(custom_data->device_12 && FAILED(custom_data->device_12->GetDeviceRemovedReason())))
      {
         Log(reshade::log::level::warning, "FSR4: Leaking the DX12 interop resources, as the GPU might still be using them");
         custom_data = nullptr;
         return;
      }

      custom_data->cached_contexts.Clear([&](FSR4ContextData& context_data) { custom_data->DestroyContext(context_data); });
      custom_data->current = nullptr;

      HMODULE ffx_module = custom_data->ffx_module;
      if (custom_data->fence_event)
      {
         CloseHandle(custom_data->fence_event);
         custom_data->fence_event = nullptr;
      }

      delete custom_data;
      custom_data = nullptr;

      if (ffx_module)
      {
         FreeLibrary(ffx_module);
      }
   }

   bool FidelityFX::FSR4::HasInit(const SR::InstanceData* data) const
   {
      return data != nullptr;
   }

   bool FidelityFX::FSR4::IsSupported(const SR::InstanceData* data) const
   {
      return data && data->is_supported;
   }

   bool FidelityFX::FSR4::HasNativeFSR4(const SR::InstanceData* data)
   {
      return data && data->is_supported && static_cast<const FSR4InstanceData*>(data)->has_native_fsr4;
   }

   bool FidelityFX::FSR4::UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data)
   {
      auto& custom_data = reinterpret_cast<FSR4InstanceData*&>(data);

      if (!command_list || !custom_data || !custom_data->is_supported)
         return false;

      // No need to re-instantiate FSR contexts if all the params are the same
      if (custom_data->current && custom_data->current->context && settings_data == custom_data->settings_data)
      {
         return true;
      }

      // Re-use a previously created context with the same settings, this also keeps its history
      if (FSR4ContextData* cached_context = custom_data->cached_contexts.Find(settings_data); cached_context && cached_context->context)
      {
         custom_data->settings_data = settings_data;
         custom_data->current = cached_context;
         return true;
      }

      auto DestroyContext = [&](FSR4ContextData& context_data) { custom_data->DestroyContext(context_data); };
      custom_data->current = nullptr;
      if (FSR4ContextData* failed_context = custom_data->cached_contexts.Find(settings_data))
      {
         custom_data->cached_contexts.Remove(failed_context, DestroyContext);
      }
      custom_data->cached_contexts.EvictIfFull(DestroyContext);

      ffxCreateContextDescUpscale context_desc = {};
      context_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
      context_desc.maxUpscaleSize.width = settings_data.output_width;
      context_desc.maxUpscaleSize.height = settings_data.output_height;
      if (settings_data.dynamic_resolution)
      {
         context_desc.maxRenderSize.width = settings_data.output_width;
         context_desc.maxRenderSize.height = settings_data.output_height;
      }
      else
      {
         context_desc.maxRenderSize.width = settings_data.render_width;
         context_desc.maxRenderSize.height = settings_data.render_height;
      }
      if (settings_data.auto_exposure)
      {
         context_desc.flags |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
      }
      if (settings_data.hdr)
      {
         context_desc.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
      }
      if (settings_data.dynamic_resolution)
      {
         context_desc.flags |= FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION;
      }
      if (settings_data.inverted_depth)
      {
         context_desc.flags |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
      }
      if (settings_data.mvs_jittered)
      {
         context_desc.flags |= FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
      }
#if DEVELOPMENT
      context_desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
#if !defined(NDEBUG)
      context_desc.flags |= FFX_UPSCALE_ENABLE_DEBUG_VISUALIZATION;
#endif
#endif
      context_desc.fpMessage = FFXMessageCallback;

      ffxCreateBackendDX12Desc backend_desc = {};
      backend_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
      backend_desc.device = custom_data->device_12.Get();

      ffxOverrideVersion override_version_desc = {};
      override_version_desc.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
      override_version_desc.versionId = custom_data->version_id;

      ffxCreateContextDescUpscaleVersion api_version_desc = {};
      api_version_desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
      api_version_desc.version = FFX_UPSCALER_VERSION;

      context_desc.header.pNext = &backend_desc.header;
      backend_desc.header.pNext = &override_version_desc.header;
      override_version_desc.header.pNext = &api_version_desc.header;

      FSR4ContextData& context_data = custom_data->cached_contexts.Add(settings_data);
      ffxReturnCode_t ret = custom_data->ffx_create_context(&context_data.context, &context_desc.header, nullptr);
      if (ret != FFX_API_RETURN_OK)
      {
         // Try again without the optional descs, in case the dll didn't like them
         override_version_desc.header.pNext = nullptr;
         context_data.context = nullptr;
         ret = custom_data->ffx_create_context(&context_data.context, &context_desc.header, nullptr);
      }
      if (ret != FFX_API_RETURN_OK)
      {
         // Last resort, let the dll pick its default version
         backend_desc.header.pNext = nullptr;
         context_data.context = nullptr;
         ret = custom_data->ffx_create_context(&context_data.context, &context_desc.header, nullptr);
         if (ret == FFX_API_RETURN_OK)
         {
            Log(reshade::log::level::warning, "FSR4: Couldn't create a context with the \"%s\" upscaler version, fell back to the default one", custom_data->version_name.c_str());
         }
      }
      if (ret != FFX_API_RETURN_OK)
      {
         if (custom_data->CanLogError())
            Log(reshade::log::level::error, "FSR4: ffxCreateContext failed (render %ux%u, output %ux%u), error = %u", settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height, ret);
         ASSERT_ONCE_MSG(false, "Couldn't create FSR4 context");
         context_data.context = nullptr;
         custom_data->cached_contexts.Remove(&context_data, DestroyContext);
         return false;
      }

      ffxQueryGetProviderVersion provider_version_desc = {};
      provider_version_desc.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
      custom_data->ffx_query(&context_data.context, &provider_version_desc.header);

      int32_t phase_count = 0;
      ffxQueryDescUpscaleGetJitterPhaseCount phase_count_desc = {};
      phase_count_desc.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
      phase_count_desc.renderWidth = settings_data.render_width;
      phase_count_desc.displayWidth = settings_data.output_width;
      phase_count_desc.pOutPhaseCount = &phase_count;
      if (custom_data->ffx_query(&context_data.context, &phase_count_desc.header) == FFX_API_RETURN_OK && phase_count > 0)
      {
         context_data.phase_count = phase_count;
      }

      Log(reshade::log::level::info, "FSR4: Created context (render %ux%u, output %ux%u, HDR %d, inverted depth %d, auto exposure %d), provider \"%s\", cached contexts: %zu",
         settings_data.render_width, settings_data.render_height, settings_data.output_width, settings_data.output_height,
         int(settings_data.hdr), int(settings_data.inverted_depth), int(settings_data.auto_exposure),
         provider_version_desc.versionName ? provider_version_desc.versionName : "unknown", custom_data->cached_contexts.entries.size());

      custom_data->settings_data = settings_data;
      custom_data->current = &context_data;

      return true;
   }

   int FidelityFX::FSR4::GetJitterPhases(const SR::InstanceData* data) const
   {
      auto& custom_data = reinterpret_cast<const FSR4InstanceData*&>(data);
      return custom_data->current ? custom_data->current->phase_count : fsr4_default_phase_count;
   }

   bool FidelityFX::FSR4::Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data)
   {
      // We need to update our sync state and resources even if this is "const"
      auto* custom_data = const_cast<FSR4InstanceData*>(static_cast<const FSR4InstanceData*>(data));

      if (!custom_data || !custom_data->is_supported || !custom_data->current || !custom_data->current->context || !command_list)
         return false;
      if (!draw_data.source_color || !draw_data.output_color || !draw_data.depth_buffer || !draw_data.motion_vectors)
         return false;

      FSR4ContextData& context_data = *custom_data->current;

      if (custom_data->last_command_list != command_list)
      {
         custom_data->command_list_11_4 = nullptr;
         custom_data->last_command_list = nullptr;
         if (FAILED(command_list->QueryInterface(IID_PPV_ARGS(&custom_data->command_list_11_4))))
         {
            if (custom_data->CanLogError())
               Log(reshade::log::level::error, "FSR4: ID3D11DeviceContext4 not supported");
            return false;
         }
         custom_data->last_command_list = command_list;
      }
      ID3D11DeviceContext4* command_list_11_4 = custom_data->command_list_11_4.Get();

      // Make sure all the shared resources are up to date with the game ones
      bool resources_valid = true;
      resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.color, draw_data.source_color, false);
      resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.motion_vectors, draw_data.motion_vectors, false);
      resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.output, draw_data.output_color, true);
      resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.depth, draw_data.depth_buffer, true, DXGI_FORMAT_R32_FLOAT);
      if (draw_data.exposure)
         resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.exposure, draw_data.exposure, false);
      if (draw_data.bias_mask)
         resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.reactive, draw_data.bias_mask, false);
      if (draw_data.transparency_alpha)
         resources_valid &= custom_data->UpdateSharedTexture(context_data, context_data.transparency, draw_data.transparency_alpha, false);
      if (!resources_valid)
         return false;

      // Views for the depth conversion (the UAV keeps a reference to the texture it was created for, so comparing the pointers is safe)
      ComPtr<ID3D11Resource> depth_uav_resource;
      if (context_data.depth_uav)
         context_data.depth_uav->GetResource(&depth_uav_resource);
      if (!context_data.depth_uav || depth_uav_resource.Get() != context_data.depth.resource_11.Get())
      {
         context_data.depth_uav = nullptr;
         D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
         uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
         uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
         HRESULT hr = custom_data->device_11->CreateUnorderedAccessView(context_data.depth.resource_11.Get(), &uav_desc, &context_data.depth_uav);
         if (FAILED(hr))
         {
            if (custom_data->CanLogError())
               Log(reshade::log::level::error, "FSR4: Failed to create the shared depth UAV, hr = 0x%08X", unsigned(hr));
            return false;
         }
      }
      if (!context_data.depth_source_srv || context_data.depth_source.Get() != draw_data.depth_buffer)
      {
         context_data.depth_source_srv = nullptr;
         context_data.depth_source = draw_data.depth_buffer;
         D3D11_TEXTURE2D_DESC depth_desc;
         static_cast<ID3D11Texture2D*>(draw_data.depth_buffer)->GetDesc(&depth_desc); // Already verified to be a 2D texture
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = GetDepthSRVFormat(depth_desc.Format);
         srv_desc.ViewDimension = depth_desc.SampleDesc.Count > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MostDetailedMip = 0;
         srv_desc.Texture2D.MipLevels = 1;
         HRESULT hr = depth_desc.SampleDesc.Count > 1 ? E_INVALIDARG : custom_data->device_11->CreateShaderResourceView(draw_data.depth_buffer, &srv_desc, &context_data.depth_source_srv);
         if (FAILED(hr))
         {
            context_data.depth_source = nullptr;
            if (custom_data->CanLogError())
               Log(reshade::log::level::error, "FSR4: Failed to create the depth SRV (format %u), hr = 0x%08X", unsigned(depth_desc.Format), unsigned(hr));
            return false;
         }
      }

      // DX11: copy the inputs into the shared textures
      auto CopyToShared = [&](const FSR4SharedTexture& target, ID3D11Resource* source)
      {
         command_list->CopySubresourceRegion(target.resource_11.Get(), 0, 0, 0, 0, source, 0, nullptr);
      };
      CopyToShared(context_data.color, draw_data.source_color);
      CopyToShared(context_data.motion_vectors, draw_data.motion_vectors);
      if (draw_data.exposure)
         CopyToShared(context_data.exposure, draw_data.exposure);
      if (draw_data.bias_mask)
         CopyToShared(context_data.reactive, draw_data.bias_mask);
      if (draw_data.transparency_alpha)
         CopyToShared(context_data.transparency, draw_data.transparency_alpha);
      {
         // If the depth is still bound as depth stencil, DX11 would null its SRV, so temporarily unbind it (keeping the render targets and UAVs)
         ID3D11RenderTargetView* rtvs_raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
         ComPtr<ID3D11DepthStencilView> dsv;
         command_list->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_raw, &dsv);
         ComPtr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // Release the references on scope exit
         UINT rtvs_count = 0;
         for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
         {
            rtvs[i].Attach(rtvs_raw[i]);
            if (rtvs_raw[i])
               rtvs_count = i + 1;
         }
         ComPtr<ID3D11Resource> dsv_resource;
         if (dsv)
            dsv->GetResource(&dsv_resource);
         const bool unbind_dsv = dsv && dsv_resource.Get() == draw_data.depth_buffer;
         if (unbind_dsv)
            command_list->OMSetRenderTargetsAndUnorderedAccessViews(rtvs_count, rtvs_raw, nullptr, D3D11_KEEP_UNORDERED_ACCESS_VIEWS, 0, nullptr, nullptr);

         ID3D11ShaderResourceView* const srv = context_data.depth_source_srv.Get();
         ID3D11UnorderedAccessView* const uav = context_data.depth_uav.Get();
         command_list->CSSetShader(custom_data->depth_copy_cs.Get(), nullptr, 0);
         command_list->CSSetShaderResources(0, 1, &srv);
         command_list->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
         command_list->Dispatch((context_data.depth.width + 7) / 8, (context_data.depth.height + 7) / 8, 1);
         ID3D11ShaderResourceView* const null_srv = nullptr;
         ID3D11UnorderedAccessView* const null_uav = nullptr;
         command_list->CSSetShaderResources(0, 1, &null_srv);
         command_list->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
         command_list->CSSetShader(nullptr, nullptr, 0);

         if (unbind_dsv)
            command_list->OMSetRenderTargetsAndUnorderedAccessViews(rtvs_count, rtvs_raw, dsv.Get(), D3D11_KEEP_UNORDERED_ACCESS_VIEWS, 0, nullptr, nullptr);
      }

      // DX11 -> DX12 sync. Flush to make sure DX11 submits its work (and the signal) before DX12 waits on it.
      const UINT64 inputs_ready_fence_value = ++custom_data->fence_value;
      command_list_11_4->Signal(custom_data->fence_11.Get(), inputs_ready_fence_value);
      command_list->Flush();

      // DX12: record the FSR dispatch
      const size_t allocator_index = custom_data->command_allocator_index;
      custom_data->command_allocator_index = (custom_data->command_allocator_index + 1) % fsr4_command_allocators_count;
      ID3D12CommandAllocator* command_allocator = custom_data->command_allocators[allocator_index].Get();
      ID3D12GraphicsCommandList* command_list_12 = custom_data->command_list_12.Get();
      HRESULT hr = custom_data->WaitForFence(custom_data->command_allocators_fence_values[allocator_index]) ? S_OK : HRESULT_FROM_WIN32(WAIT_TIMEOUT); // Usually already done, as DX11 waited on it
      if (SUCCEEDED(hr))
      {
         hr = command_allocator->Reset();
      }
      if (SUCCEEDED(hr))
      {
         hr = command_list_12->Reset(command_allocator, nullptr);
      }
      if (FAILED(hr))
      {
         if (custom_data->CanLogError())
            Log(reshade::log::level::error, "FSR4: Failed to reset the DX12 command list, hr = 0x%08X", unsigned(hr));
         // Keep the fence values progression valid anyway
         custom_data->command_queue->Wait(custom_data->fence_12.Get(), inputs_ready_fence_value);
         return false;
      }

      // Simultaneous access resources are in the common state at the beginning and end of every command list, transition them to what FSR expects and back
      std::vector<D3D12_RESOURCE_BARRIER> barriers;
      auto AddBarrier = [&](const FSR4SharedTexture& texture, D3D12_RESOURCE_STATES state)
      {
         if (!texture.resource_12)
            return;
         D3D12_RESOURCE_BARRIER barrier = {};
         barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
         barrier.Transition.pResource = texture.resource_12.Get();
         barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
         barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
         barrier.Transition.StateAfter = state;
         barriers.push_back(barrier);
      };
      constexpr D3D12_RESOURCE_STATES input_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      AddBarrier(context_data.color, input_state);
      AddBarrier(context_data.depth, input_state);
      AddBarrier(context_data.motion_vectors, input_state);
      if (draw_data.exposure)
         AddBarrier(context_data.exposure, input_state);
      if (draw_data.bias_mask)
         AddBarrier(context_data.reactive, input_state);
      if (draw_data.transparency_alpha)
         AddBarrier(context_data.transparency, input_state);
      AddBarrier(context_data.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      command_list_12->ResourceBarrier(UINT(barriers.size()), barriers.data());

      // Mirrors the FSR 3 implementation
      ffxDispatchDescUpscale dispatch_upscale = {};
      dispatch_upscale.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
      dispatch_upscale.commandList = command_list_12;
      dispatch_upscale.color = ffxApiGetResourceDX12(context_data.color.resource_12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
      dispatch_upscale.depth = ffxApiGetResourceDX12(context_data.depth.resource_12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
      dispatch_upscale.motionVectors = ffxApiGetResourceDX12(context_data.motion_vectors.resource_12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
      if (draw_data.exposure)
      {
         dispatch_upscale.exposure = ffxApiGetResourceDX12(context_data.exposure.resource_12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
      }
      if (draw_data.bias_mask)
      {
         dispatch_upscale.reactive = ffxApiGetResourceDX12(context_data.reactive.resource_12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
      }
      if (draw_data.transparency_alpha)
      {
         dispatch_upscale.transparencyAndComposition = ffxApiGetResourceDX12(context_data.transparency.resource_12.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
      }
      dispatch_upscale.output = ffxApiGetResourceDX12(context_data.output.resource_12.Get(), FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

      ASSERT_ONCE(custom_data->settings_data.output_width == context_data.output.width && custom_data->settings_data.output_height == context_data.output.height);
      dispatch_upscale.upscaleSize.width = custom_data->settings_data.output_width;
      dispatch_upscale.upscaleSize.height = custom_data->settings_data.output_height;
      dispatch_upscale.renderSize.width = draw_data.render_width != 0 ? draw_data.render_width : custom_data->settings_data.render_width;
      dispatch_upscale.renderSize.height = draw_data.render_height != 0 ? draw_data.render_height : custom_data->settings_data.render_height;
      dispatch_upscale.jitterOffset.x = draw_data.jitter_x;
      dispatch_upscale.jitterOffset.y = draw_data.jitter_y;
      dispatch_upscale.motionVectorScale.x = custom_data->settings_data.mvs_x_scale;
      dispatch_upscale.motionVectorScale.y = custom_data->settings_data.mvs_y_scale;

      dispatch_upscale.preExposure = draw_data.pre_exposure == 0.f ? 1.f : draw_data.pre_exposure;

      dispatch_upscale.cameraFovAngleVertical = draw_data.vert_fov;
      if (custom_data->settings_data.inverted_depth)
      {
         dispatch_upscale.cameraFar = draw_data.near_plane;
         dispatch_upscale.cameraNear = draw_data.far_plane;
      }
      else
      {
         dispatch_upscale.cameraFar = draw_data.far_plane;
         dispatch_upscale.cameraNear = draw_data.near_plane;
      }
      dispatch_upscale.viewSpaceToMetersFactor = 1.f;

      dispatch_upscale.reset = draw_data.reset;

      dispatch_upscale.enableSharpening = draw_data.user_sharpness >= 0.f;
      dispatch_upscale.sharpness = dispatch_upscale.enableSharpening ? draw_data.user_sharpness : 0.f;

      float time_delta = draw_data.time_delta > 0.f ? draw_data.time_delta : (1.f / 60.f);
      dispatch_upscale.frameTimeDelta = static_cast<float>(time_delta * 1000.f); // FSR expects milliseconds

      dispatch_upscale.flags = 0;
#if DEVELOPMENT && !defined(NDEBUG)
      dispatch_upscale.flags |= FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW;
#endif

      const ffxReturnCode_t ret = custom_data->ffx_dispatch(&context_data.context, &dispatch_upscale.header);
      if (ret != FFX_API_RETURN_OK && custom_data->CanLogError())
      {
         Log(reshade::log::level::error, "FSR4: ffxDispatch failed, error = %u", ret);
      }

      for (D3D12_RESOURCE_BARRIER& barrier : barriers)
      {
         std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      }
      command_list_12->ResourceBarrier(UINT(barriers.size()), barriers.data());
      hr = command_list_12->Close();

      // DX12: wait for DX11 inputs, run FSR, and signal back
      custom_data->command_queue->Wait(custom_data->fence_12.Get(), inputs_ready_fence_value);
      if (SUCCEEDED(hr))
      {
         ID3D12CommandList* const command_lists[] = { command_list_12 };
         custom_data->command_queue->ExecuteCommandLists(1, command_lists);
      }
      else if (custom_data->CanLogError())
      {
         Log(reshade::log::level::error, "FSR4: Failed to close the DX12 command list, hr = 0x%08X", unsigned(hr));
      }
      const UINT64 output_ready_fence_value = ++custom_data->fence_value;
      custom_data->command_queue->Signal(custom_data->fence_12.Get(), output_ready_fence_value);
      custom_data->command_allocators_fence_values[allocator_index] = output_ready_fence_value;
      context_data.last_used_fence_value = output_ready_fence_value;

      // DX11: wait for DX12 (on GPU) and copy back the output
      command_list_11_4->Wait(custom_data->fence_11.Get(), output_ready_fence_value);
      const bool succeeded = SUCCEEDED(hr) && ret == FFX_API_RETURN_OK;
      if (succeeded)
      {
         command_list->CopySubresourceRegion(draw_data.output_color, 0, 0, 0, 0, context_data.output.resource_11.Get(), 0, nullptr);
      }
      return succeeded;
   }
}

#endif
