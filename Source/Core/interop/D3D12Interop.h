#pragma once

// Runs DX12 work (e.g. DX12 only libraries) on the GPU of a DX11 device: the DX12 device is created on the same adapter,
// textures are created on DX11 and opened on DX12, and the two queues are synchronized on the GPU through a shared fence.
// Every feature owns its own context (the DX12 runtime returns the same device for the same adapter anyway).
// A frame: DX11 copies into the shared textures -> "BeginCommandList()" -> DX12 commands -> "ExecuteCommandList()" -> DX11 copies from the shared textures.
#ifndef ENABLE_D3D12_INTEROP
#define ENABLE_D3D12_INTEROP 0
#endif

#if ENABLE_D3D12_INTEROP

#include <d3d11_4.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <functional>

namespace D3D12Interop
{
   constexpr size_t command_allocators_count = 4; // Max number of DX12 command lists in flight

   // Shared textures are in the common state at the beginning and end of every DX12 command list
   struct SharedTexture
   {
      Microsoft::WRL::ComPtr<ID3D11Texture2D> resource_11;
      Microsoft::WRL::ComPtr<ID3D12Resource> resource_12;
      Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav_11; // Only if created with unordered access
      UINT width = 0;
      UINT height = 0;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   };

   struct Context
   {
      Microsoft::WRL::ComPtr<ID3D11Device> device_11;
      Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
      DXGI_ADAPTER_DESC adapter_desc = {};
      Microsoft::WRL::ComPtr<ID3D12Device> device_12; // Native (not wrapped by ReShade)
      Microsoft::WRL::ComPtr<ID3D12Device> device_12_wrapped; // Kept alive just in case, if ReShade wrapped our device
      Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue;
      Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocators[command_allocators_count];
      UINT64 command_allocators_fence_values[command_allocators_count] = {};
      size_t command_allocator_index = 0;
      Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;

      Microsoft::WRL::ComPtr<ID3D12Fence> fence_12;
      Microsoft::WRL::ComPtr<ID3D11Fence> fence_11;
      UINT64 fence_value = 0; // The last signaled one
      HANDLE fence_event = nullptr; // For the rare CPU waits (e.g. on resources destruction)

      // See "CopyDepth()". The getter returns a new reference (or nullptr), as native shaders can be recompiled.
      std::function<ID3D11ComputeShader*()> get_depth_copy_shader;
      Microsoft::WRL::ComPtr<ID3D11Resource> depth_source; // Kept alive to make sure the pointer can't be reused by another resource while we cache its view
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depth_source_srv;

      unsigned int logged_errors = 0;
      bool poisoned = false; // The GPU sync broke, so we can't know when the DX12 work completes, the resources can't be used or released anymore
   };

   // "adapter" is optional (it's retrieved from the device otherwise)
   bool Init(Context& context, ID3D11Device* device, IDXGIAdapter* adapter = nullptr);
   // Returns false if the GPU might still be using the resources (they should be leaked then)
   bool WaitForIdle(Context& context);
   void Deinit(Context& context);

   // (Re)creates the texture if it doesn't match the size and format (waiting for the GPU to be done with the previous one)
   bool UpdateSharedTexture(Context& context, SharedTexture& texture, UINT width, UINT height, DXGI_FORMAT format, bool unordered_access);
   // Same, but mirroring a DX11 2D texture, copied to and from with whole resource copies. Mips, arrays, MSAA and sRGB aren't supported (the latter would need sRGB views).
   // A depth texture is mirrored as R32_FLOAT, filled by "CopyDepth()", as DX12 libraries can't read DX11 depth stencil textures.
   bool UpdateSharedTextureFrom(Context& context, SharedTexture& texture, ID3D11Resource* source, bool is_depth, bool unordered_access);
   // Converts the depth into its shared texture (mirrored with "is_depth" and unordered access) with "get_depth_copy_shader" (Luma's "Copy Depth CS").
   // This unbinds the render targets and changes the compute shader state, the caller is expected to restore the pipeline state.
   bool CopyDepth(Context& context, SharedTexture& texture, ID3D11DeviceContext* device_context, ID3D11Resource* source);

   // Makes DX12 wait for the DX11 work submitted until now, and returns a DX12 command list to record into (or null if it failed)
   ID3D12GraphicsCommandList* BeginCommandList(Context& context, ID3D11DeviceContext* device_context);
   // Submits the DX12 command list, and makes DX11 wait for it. If this fails, the context might be poisoned.
   bool ExecuteCommandList(Context& context, ID3D11DeviceContext* device_context);
}

#endif
