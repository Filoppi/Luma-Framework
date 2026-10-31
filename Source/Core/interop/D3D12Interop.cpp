#include "D3D12Interop.h"

#if ENABLE_D3D12_INTEROP

#include <include/reshade.hpp>

#include <format>
#include <string>

namespace D3D12Interop
{
   using Microsoft::WRL::ComPtr;

   namespace
   {
      // Max CPU wait on the DX12 GPU work (these waits are rare), to never hang the game if the GPU is stuck
      constexpr ULONGLONG fence_timeout_ms = 2000;

      // ReShade's interface ID to query the original object from its proxies (see "com_utils.hpp")
      constexpr GUID IID_UnwrappedObject = {0x7f2c9a11, 0x3b4e, 0x4d6a, {0x81, 0x2f, 0x5e, 0x9c, 0xd3, 0x7a, 0x1b, 0x42}};

      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("D3D12 Interop: " + message).c_str());
      }

      void LogError(Context& context, const std::string& message)
      {
         if (context.logged_errors++ < 10) // Don't spam the log
            Log(reshade::log::level::error, message);
      }

      // Returns false if the GPU didn't reach the value in time (and the device wasn't removed)
      bool WaitForFence(Context& context, UINT64 value)
      {
         const ULONGLONG deadline = GetTickCount64() + fence_timeout_ms;
         while (context.fence_12->GetCompletedValue() < value)
         {
            if (FAILED(context.device_12->GetDeviceRemovedReason()))
               return true; // Nothing will run anymore
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline || FAILED(context.fence_12->SetEventOnCompletion(value, context.fence_event)))
            {
               LogError(context, std::format("Timed out waiting for the DX12 GPU work (fence {}, completed {})", value, context.fence_12->GetCompletedValue()));
               return false;
            }
            WaitForSingleObject(context.fence_event, DWORD(deadline - now)); // Loop to check the value, the event might have been signaled by a previous (timed out) wait
         }
         return true;
      }

      bool CreateSharedTexture(Context& context, SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access)
      {
         // Created on DX11 with a legacy (non NT) shared handle, and opened on DX12.
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
         HRESULT hr = context.device_11->CreateTexture2D(&desc, nullptr, &texture.resource_11);
         if (SUCCEEDED(hr))
            hr = texture.resource_11.As(&dxgi_resource);
         if (SUCCEEDED(hr))
            hr = dxgi_resource->GetSharedHandle(&shared_handle);
         if (SUCCEEDED(hr))
            hr = context.device_12->OpenSharedHandle(shared_handle, IID_PPV_ARGS(&texture.resource_12));
         if (SUCCEEDED(hr) && unordered_access)
            hr = context.device_11->CreateUnorderedAccessView(texture.resource_11.Get(), nullptr, &texture.uav_11);
         if (FAILED(hr))
         {
            LogError(context, std::format("Failed to share a DX11 texture with DX12 ({}x{} format {}), hr = {:#010x}", width, height, int(format), unsigned(hr)));
            texture = {};
            return false;
         }
         texture.width = width;
         texture.height = height;
         texture.format = format;
         return true;
      }
   }

   bool Init(Context& context, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      // DX11 fences sharing requires Windows 10 1703+
      context.device_11 = device;
      ComPtr<ID3D11Device5> device_11_5;
      if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device_11_5))))
      {
         Log(reshade::log::level::error, "ID3D11Device5 is not supported, can't share fences between DX11 and DX12");
         return false;
      }

      context.adapter = adapter;
      ComPtr<IDXGIDevice> dxgi_device;
      if (!context.adapter && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi_device))))
         dxgi_device->GetAdapter(&context.adapter);
      const auto d3d12_create_device = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(LoadLibraryW(L"d3d12.dll"), "D3D12CreateDevice")); // Never freed, it's a system dll
      if (!context.adapter || FAILED(context.adapter->GetDesc(&context.adapter_desc)) || !d3d12_create_device)
      {
         Log(reshade::log::level::error, "Couldn't retrieve the DX11 adapter or load DX12");
         return false;
      }
      const HRESULT create_hr = d3d12_create_device(context.adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&context.device_12));
      if (FAILED(create_hr))
      {
         Log(reshade::log::level::error, std::format("D3D12CreateDevice failed, hr = {:#010x}", unsigned(create_hr)));
         return false;
      }
      // ReShade wraps any DX12 device created in the process, use the original one, so none of our DX12 work goes through ReShade (and its addons)
      ComPtr<ID3D12Device> unwrapped_device_12;
      if (SUCCEEDED(context.device_12->QueryInterface(IID_UnwrappedObject, reinterpret_cast<void**>(unwrapped_device_12.GetAddressOf()))))
      {
         context.device_12_wrapped = std::move(context.device_12);
         context.device_12 = std::move(unwrapped_device_12);
      }
      const LUID device_12_luid = context.device_12->GetAdapterLuid();
      if (device_12_luid.LowPart != context.adapter_desc.AdapterLuid.LowPart || device_12_luid.HighPart != context.adapter_desc.AdapterLuid.HighPart)
      {
         Log(reshade::log::level::error, "The DX12 device adapter doesn't match the DX11 one");
         return false;
      }

      D3D12_COMMAND_QUEUE_DESC queue_desc = {};
      queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
      HRESULT hr = context.device_12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&context.command_queue));
      for (auto& command_allocator : context.command_allocators)
      {
         if (SUCCEEDED(hr))
            hr = context.device_12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator));
      }
      if (SUCCEEDED(hr))
         hr = context.device_12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.command_allocators[0].Get(), nullptr, IID_PPV_ARGS(&context.command_list));
      if (SUCCEEDED(hr))
         hr = context.command_list->Close();
      // The fence is created on DX12 and opened on DX11 (through an NT handle)
      HANDLE fence_handle = nullptr;
      if (SUCCEEDED(hr))
         hr = context.device_12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&context.fence_12));
      if (SUCCEEDED(hr))
         hr = context.device_12->CreateSharedHandle(context.fence_12.Get(), nullptr, GENERIC_ALL, nullptr, &fence_handle);
      if (SUCCEEDED(hr))
      {
         hr = device_11_5->OpenSharedFence(fence_handle, IID_PPV_ARGS(&context.fence_11));
         CloseHandle(fence_handle);
      }
      if (SUCCEEDED(hr))
      {
         context.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
         hr = context.fence_event ? S_OK : HRESULT_FROM_WIN32(GetLastError());
      }
      if (FAILED(hr))
      {
         Log(reshade::log::level::error, std::format("Failed to create the DX12 queue/command list/shared fence, hr = {:#010x}", unsigned(hr)));
         return false;
      }

      // Make sure textures can actually be shared
      SharedTexture test_texture;
      if (!CreateSharedTexture(context, test_texture, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, true))
         return false;

      Log(reshade::log::level::info, std::format("Initialized{}", context.device_12_wrapped ? " (unwrapped the ReShade DX12 device)" : ""));
      return true;
   }

   bool WaitForIdle(Context& context)
   {
      return !context.poisoned && (!context.fence_12 || WaitForFence(context, context.fence_value));
   }

   void Deinit(Context& context)
   {
      if (context.fence_event)
         CloseHandle(context.fence_event);
      context = {};
   }

   bool UpdateSharedTexture(Context& context, SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access)
   {
      if (texture.resource_11 && texture.width == width && texture.height == height && texture.format == format && (texture.uav_11 != nullptr) == unordered_access)
         return true;
      // The previous one might still be in use by DX12
      return WaitForFence(context, context.fence_value) && CreateSharedTexture(context, texture, width, height, format, unordered_access);
   }

   bool UpdateSharedTextureFrom(Context& context, SharedTexture& texture, ID3D11Resource* source, bool is_depth, bool unordered_access)
   {
      ComPtr<ID3D11Texture2D> source_texture;
      if (FAILED(source->QueryInterface(IID_PPV_ARGS(&source_texture))))
      {
         LogError(context, "An input/output resource is not a 2D texture");
         return false;
      }
      D3D11_TEXTURE2D_DESC source_desc;
      source_texture->GetDesc(&source_desc);
      const auto source_format = reshade::api::format(source_desc.Format);
      const bool is_srgb = reshade::api::format_to_default_typed(source_format, 1) == source_format && reshade::api::format_to_default_typed(source_format, 0) != source_format;
      if (!is_depth && (source_desc.MipLevels != 1 || source_desc.ArraySize != 1 || source_desc.SampleDesc.Count != 1 || is_srgb))
      {
         LogError(context, std::format("Unsupported input/output texture (mips {}, array size {}, samples {}, format {})", source_desc.MipLevels, source_desc.ArraySize, source_desc.SampleDesc.Count, int(source_desc.Format)));
         return false;
      }
      // DX12 libraries need typed formats to create views
      const DXGI_FORMAT format = (is_depth ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT(reshade::api::format_to_default_typed(source_format)));
      return UpdateSharedTexture(context, texture, source_desc.Width, source_desc.Height, format, unordered_access);
   }

   bool CopyDepth(Context& context, SharedTexture& texture, ID3D11DeviceContext* device_context, ID3D11Resource* source)
   {
      ComPtr<ID3D11ComputeShader> depth_copy_cs;
      if (context.get_depth_copy_shader)
      {
         depth_copy_cs.Attach(context.get_depth_copy_shader());
      }
      if (!depth_copy_cs)
         return false;

      if (!context.depth_source_srv || context.depth_source.Get() != source)
      {
         context.depth_source_srv = nullptr;
         context.depth_source = source;
         D3D11_TEXTURE2D_DESC desc;
         static_cast<ID3D11Texture2D*>(source)->GetDesc(&desc); // Already verified to be a 2D texture by "UpdateSharedTextureFrom()"
         // The depth (not stencil) view of the typeless depth formats ("format_to_default_typed()" would pick float for 16 bit)
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = (desc.Format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT(reshade::api::format_to_default_typed(reshade::api::format(desc.Format))));
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MipLevels = 1;
         HRESULT hr = E_INVALIDARG; // MSAA depth isn't supported
         if (desc.SampleDesc.Count == 1)
         {
            hr = context.device_11->CreateShaderResourceView(source, &srv_desc, &context.depth_source_srv);
         }
         if (FAILED(hr))
         {
            LogError(context, std::format("Failed to create the depth SRV (format {}, samples {}), hr = {:#010x}", int(desc.Format), desc.SampleDesc.Count, unsigned(hr)));
            context.depth_source = nullptr;
            return false;
         }
      }

      device_context->OMSetRenderTargets(0, nullptr, nullptr); // The depth might still be bound as depth stencil, which would null its SRV
      ID3D11ShaderResourceView* const depth_srv = context.depth_source_srv.Get();
      ID3D11UnorderedAccessView* const depth_uav = texture.uav_11.Get();
      device_context->CSSetShader(depth_copy_cs.Get(), nullptr, 0);
      device_context->CSSetShaderResources(0, 1, &depth_srv);
      device_context->CSSetUnorderedAccessViews(0, 1, &depth_uav, nullptr);
      device_context->Dispatch((texture.width + 7) / 8, (texture.height + 7) / 8, 1);
      return true;
   }

   ID3D12GraphicsCommandList* BeginCommandList(Context& context, ID3D11DeviceContext* device_context)
   {
      ComPtr<ID3D11DeviceContext4> device_context_4;
      HRESULT hr = device_context->QueryInterface(IID_PPV_ARGS(&device_context_4));
      // Flush to make sure DX11 submits its work (and the signal) before DX12 waits on it
      if (SUCCEEDED(hr))
         hr = device_context_4->Signal(context.fence_11.Get(), context.fence_value + 1);
      if (FAILED(hr))
      {
         LogError(context, std::format("Failed to signal the DX11 fence, hr = {:#010x}", unsigned(hr)));
         return nullptr;
      }
      context.fence_value++;
      device_context->Flush();

      context.command_allocator_index = (context.command_allocator_index + 1) % command_allocators_count;
      ID3D12CommandAllocator* const command_allocator = context.command_allocators[context.command_allocator_index].Get();
      hr = WaitForFence(context, context.command_allocators_fence_values[context.command_allocator_index]) ? S_OK : HRESULT_FROM_WIN32(WAIT_TIMEOUT); // Usually already done, as DX11 waited on it
      if (SUCCEEDED(hr))
         hr = command_allocator->Reset();
      if (SUCCEEDED(hr))
         hr = context.command_list->Reset(command_allocator, nullptr);
      if (FAILED(hr))
      {
         LogError(context, std::format("Failed to reset the DX12 command list, hr = {:#010x}", unsigned(hr)));
         return nullptr;
      }
      return context.command_list.Get();
   }

   bool ExecuteCommandList(Context& context, ID3D11DeviceContext* device_context)
   {
      const UINT64 inputs_ready_fence_value = context.fence_value;
      const UINT64 output_ready_fence_value = inputs_ready_fence_value + 1;
      HRESULT hr = context.command_list->Close();
      if (SUCCEEDED(hr))
         hr = context.command_queue->Wait(context.fence_12.Get(), inputs_ready_fence_value);
      if (FAILED(hr))
      {
         LogError(context, std::format("Failed to submit the DX12 work, hr = {:#010x}", unsigned(hr)));
         return false;
      }
      ID3D12CommandList* const command_lists[] = {context.command_list.Get()};
      context.command_queue->ExecuteCommandLists(1, command_lists);
      hr = context.command_queue->Signal(context.fence_12.Get(), output_ready_fence_value);
      ComPtr<ID3D11DeviceContext4> device_context_4;
      if (SUCCEEDED(hr))
      {
         context.fence_value = output_ready_fence_value;
         context.command_allocators_fence_values[context.command_allocator_index] = output_ready_fence_value;
         hr = device_context->QueryInterface(IID_PPV_ARGS(&device_context_4));
      }
      if (SUCCEEDED(hr))
         hr = device_context_4->Wait(context.fence_11.Get(), output_ready_fence_value);
      if (FAILED(hr))
      {
         LogError(context, std::format("Failed to sync the DX12 work with DX11, hr = {:#010x}", unsigned(hr)));
         context.poisoned = true;
         return false;
      }
      return true;
   }
}

#endif
