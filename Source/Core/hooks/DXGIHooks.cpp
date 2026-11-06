#include "DXGIHooks.h"

#if ENABLE_SAFETYHOOK

#include <dxgi1_2.h>
#include <intrin.h>
#include <atomic>
#include <mutex>
#include <wrl/client.h>

namespace DXGIHooks
{
   namespace
   {
      constexpr size_t adapter_get_desc1_index = 10; // IUnknown (3) + IDXGIObject (4) + IDXGIAdapter (3)
      constexpr size_t factory_create_swapchain_index = 10; // IUnknown (3) + IDXGIObject (4) + IDXGIFactory (3)
      constexpr size_t factory_create_swapchain_for_hwnd_index = 15; // IUnknown (3) + IDXGIObject (4) + IDXGIFactory (5) + IDXGIFactory1 (2) + IDXGIFactory2 (1)

      std::mutex init_mutex;
      safetyhook::InlineHook adapter_get_desc1_hook;
      std::atomic<HMODULE> vendor_id_override_module = nullptr;
      std::atomic<UINT> vendor_id_override = 0;

      safetyhook::InlineHook factory_create_swapchain_hook;
      safetyhook::InlineHook factory_create_swapchain_for_hwnd_hook;
      std::atomic<SwapChainCreationOverride*> swapchain_creation_override = nullptr;
      // Swapchains created from within the override (e.g. the replacement ones) aren't overridden
      thread_local bool in_swapchain_creation_override = false;

      HRESULT STDMETHODCALLTYPE HookedAdapterGetDesc1(IDXGIAdapter1* adapter, DXGI_ADAPTER_DESC1* desc)
      {
         const HRESULT hr = adapter_get_desc1_hook.stdcall<HRESULT>(adapter, desc);
         if (SUCCEEDED(hr) && desc && Hooks::IsAddressInModule(_ReturnAddress(), vendor_id_override_module))
            desc->VendorId = vendor_id_override;
         return hr;
      }

      HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChain(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain)
      {
         SwapChainCreationOverride* const swapchain_override = swapchain_creation_override;
         if (swapchain_override && !in_swapchain_creation_override && device && desc && swapchain)
         {
            in_swapchain_creation_override = true;
            HRESULT hr = S_OK;
            const bool handled = swapchain_override->CreateSwapChain(factory, device, *desc, swapchain, hr);
            in_swapchain_creation_override = false;
            if (handled)
               return hr;
         }
         return factory_create_swapchain_hook.stdcall<HRESULT>(factory, device, desc, swapchain);
      }

      HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* restrict_to_output, IDXGISwapChain1** swapchain)
      {
         SwapChainCreationOverride* const swapchain_override = swapchain_creation_override;
         if (swapchain_override && !in_swapchain_creation_override && device && desc && swapchain)
         {
            in_swapchain_creation_override = true;
            HRESULT hr = S_OK;
            const bool handled = swapchain_override->CreateSwapChainForHwnd(factory, device, hwnd, *desc, fullscreen_desc, restrict_to_output, swapchain, hr);
            in_swapchain_creation_override = false;
            if (handled)
               return hr;
         }
         return factory_create_swapchain_for_hwnd_hook.stdcall<HRESULT>(factory, device, hwnd, desc, fullscreen_desc, restrict_to_output, swapchain);
      }
   }

   bool Init(IDXGIAdapter* adapter)
   {
      const std::lock_guard lock(init_mutex);
      if (adapter_get_desc1_hook)
         return true;
      Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter_1;
      if (!adapter || FAILED(adapter->QueryInterface(IID_PPV_ARGS(&adapter_1))))
         return false;
      auto hook = safetyhook::InlineHook::create(Hooks::GetVirtualFunction(adapter_1.Get(), adapter_get_desc1_index), reinterpret_cast<void*>(&HookedAdapterGetDesc1), safetyhook::InlineHook::StartDisabled);
      if (!hook)
         return false;
      adapter_get_desc1_hook = std::move(*hook);
      return true;
   }

   bool SetAdapterVendorIdOverride(HMODULE caller_module, UINT vendor_id)
   {
      vendor_id_override = vendor_id;
      vendor_id_override_module = caller_module;
      return Hooks::SetEnabled(adapter_get_desc1_hook, caller_module != nullptr);
   }

   bool InitFactory(IDXGIFactory* factory)
   {
      const std::lock_guard lock(init_mutex);
      if (factory_create_swapchain_hook && factory_create_swapchain_for_hwnd_hook)
         return true;
      Microsoft::WRL::ComPtr<IDXGIFactory2> factory_2;
      if (!factory || FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory_2))))
         return false;
      if (!factory_create_swapchain_hook)
      {
         auto hook = safetyhook::InlineHook::create(Hooks::GetVirtualFunction(factory_2.Get(), factory_create_swapchain_index), reinterpret_cast<void*>(&HookedFactoryCreateSwapChain), safetyhook::InlineHook::StartDisabled);
         if (!hook)
            return false;
         factory_create_swapchain_hook = std::move(*hook);
      }
      if (!factory_create_swapchain_for_hwnd_hook)
      {
         auto hook = safetyhook::InlineHook::create(Hooks::GetVirtualFunction(factory_2.Get(), factory_create_swapchain_for_hwnd_index), reinterpret_cast<void*>(&HookedFactoryCreateSwapChainForHwnd), safetyhook::InlineHook::StartDisabled);
         if (!hook)
            return false;
         factory_create_swapchain_for_hwnd_hook = std::move(*hook);
      }
      return true;
   }

   bool SetSwapChainCreationOverride(SwapChainCreationOverride* swapchain_override)
   {
      swapchain_creation_override = swapchain_override;
      const bool enabled = swapchain_override != nullptr;
      const bool succeeded = Hooks::SetEnabled(factory_create_swapchain_hook, enabled);
      return Hooks::SetEnabled(factory_create_swapchain_for_hwnd_hook, enabled) && succeeded;
   }

   HRESULT CreateSwapChainOriginal(IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain)
   {
      if (!factory_create_swapchain_hook)
         return factory->CreateSwapChain(device, desc, swapchain);
      return factory_create_swapchain_hook.stdcall<HRESULT>(factory, device, desc, swapchain);
   }

   HRESULT CreateSwapChainForHwndOriginal(IDXGIFactory2* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen_desc, IDXGIOutput* restrict_to_output, IDXGISwapChain1** swapchain)
   {
      if (!factory_create_swapchain_for_hwnd_hook)
         return factory->CreateSwapChainForHwnd(device, hwnd, desc, fullscreen_desc, restrict_to_output, swapchain);
      return factory_create_swapchain_for_hwnd_hook.stdcall<HRESULT>(factory, device, hwnd, desc, fullscreen_desc, restrict_to_output, swapchain);
   }
}

#endif
