#pragma once

#include "Hooks.h"

#if ENABLE_SAFETYHOOK

#include <dxgi1_2.h>

namespace DXGIHooks
{
   // Creates the (disabled) hooks on the methods of the adapter class. Safe to call more than once.
   bool Init(IDXGIAdapter* adapter);

   // "IDXGIAdapter1::GetDesc1()" calls made by "caller_module" return "vendor_id" instead of the real one. Pass a null module to stop.
   bool SetAdapterVendorIdOverride(HMODULE caller_module, UINT vendor_id = 0);

   // Lets a feature replace the swapchains the game creates (e.g. to present them through DX12, see "D3D12SwapChainProxy.h").
   // Swapchains created from within the override (on the same thread) go through the original functions.
   class SwapChainCreationOverride
   {
   public:
      // Return true if the creation was handled (whether it succeeded or not, see "result"), false to create the swapchain normally
      virtual bool CreateSwapChain(IDXGIFactory* factory, IUnknown* device, const DXGI_SWAP_CHAIN_DESC& desc, IDXGISwapChain** swapchain, HRESULT& result) = 0;
      virtual bool CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* restrict_to_output, IDXGISwapChain1** swapchain, HRESULT& result) = 0;

   protected:
      ~SwapChainCreationOverride() = default;
   };

   // Creates the (disabled) hooks on the swapchain creation methods of the factory class. Safe to call more than once.
   bool InitFactory(IDXGIFactory* factory);
   // Pass null to stop overriding. The override needs to stay alive until it's removed.
   bool SetSwapChainCreationOverride(SwapChainCreationOverride* swapchain_override);
   // Call the original functions (skipping the override)
   HRESULT CreateSwapChainOriginal(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain);
   HRESULT CreateSwapChainForHwndOriginal(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* restrict_to_output, IDXGISwapChain1** swapchain);
}

#endif
