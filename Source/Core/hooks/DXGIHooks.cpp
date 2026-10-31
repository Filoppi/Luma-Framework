#include "DXGIHooks.h"

#if ENABLE_SAFETYHOOK

#include <dxgi1_6.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <wrl/client.h>

#include "../utils/display.hpp" // "IsRealDisplayQueryActive()"

namespace DXGIHooks
{
   namespace
   {
      constexpr size_t adapter_get_desc1_index = 10; // IUnknown (3) + IDXGIObject (4) + IDXGIAdapter (3)
      constexpr size_t output_get_desc1_index = 27; // IUnknown (3) + IDXGIObject (4) + IDXGIOutput (12) + IDXGIOutput1 (4) + IDXGIOutput2..5 (4)
      constexpr size_t swapchain_check_color_space_support_index = 37; // IUnknown (3) + IDXGIObject (4) + IDXGIDeviceSubObject (1) + IDXGISwapChain (10) + IDXGISwapChain1 (11) + IDXGISwapChain2 (7) + GetCurrentBackBufferIndex (1)
      constexpr float sdr_max_luminance = 80.f; // Windows sRGB standard luminance

      std::mutex init_mutex;
      safetyhook::InlineHook adapter_get_desc1_hook;
      std::atomic<HMODULE> vendor_id_override_module = nullptr;
      std::atomic<UINT> vendor_id_override = 0;
      safetyhook::InlineHook output_get_desc1_hook;
      safetyhook::InlineHook swapchain_check_color_space_support_hook;
      std::atomic<bool> sdr_display_spoof = false;

      bool IsHDRColorSpace(DXGI_COLOR_SPACE_TYPE color_space)
      {
         switch (color_space)
         {
         case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:
         case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020:
         case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709: // scRGB
         case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_LEFT_P2020:
         case DXGI_COLOR_SPACE_YCBCR_STUDIO_G2084_TOPLEFT_P2020:
         case DXGI_COLOR_SPACE_YCBCR_STUDIO_GHLG_TOPLEFT_P2020:
         case DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020:
            return true;
         default:
            return false;
         }
      }

      HRESULT STDMETHODCALLTYPE HookedAdapterGetDesc1(IDXGIAdapter1* adapter, DXGI_ADAPTER_DESC1* desc)
      {
         const HRESULT hr = adapter_get_desc1_hook.stdcall<HRESULT>(adapter, desc);
         if (SUCCEEDED(hr) && desc && Hooks::IsAddressInModule(_ReturnAddress(), vendor_id_override_module))
            desc->VendorId = vendor_id_override;
         return hr;
      }

      HRESULT STDMETHODCALLTYPE HookedOutputGetDesc1(IDXGIOutput6* output, DXGI_OUTPUT_DESC1* desc)
      {
         const HRESULT hr = output_get_desc1_hook.stdcall<HRESULT>(output, desc);
         if (SUCCEEDED(hr) && desc && !Display::IsRealDisplayQueryActive() && IsHDRColorSpace(desc->ColorSpace))
         {
            desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            desc->MaxLuminance = (std::min)(desc->MaxLuminance, sdr_max_luminance);
            desc->MaxFullFrameLuminance = (std::min)(desc->MaxFullFrameLuminance, sdr_max_luminance);
         }
         return hr;
      }

      HRESULT STDMETHODCALLTYPE HookedSwapChainCheckColorSpaceSupport(IDXGISwapChain3* swapchain, DXGI_COLOR_SPACE_TYPE color_space, UINT* color_space_support)
      {
         const HRESULT hr = swapchain_check_color_space_support_hook.stdcall<HRESULT>(swapchain, color_space, color_space_support);
         if (SUCCEEDED(hr) && color_space_support && !Display::IsRealDisplayQueryActive() && IsHDRColorSpace(color_space))
            *color_space_support = 0;
         return hr;
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

   bool InitDisplayCapability(IDXGIOutput* output)
   {
      const std::lock_guard lock(init_mutex);
      if (output_get_desc1_hook)
         return true;
      Microsoft::WRL::ComPtr<IDXGIOutput6> output_6;
      if (!output || FAILED(output->QueryInterface(IID_PPV_ARGS(&output_6))))
         return false;
      auto hook = safetyhook::InlineHook::create(Hooks::GetVirtualFunction(output_6.Get(), output_get_desc1_index), reinterpret_cast<void*>(&HookedOutputGetDesc1), safetyhook::InlineHook::StartDisabled);
      if (!hook)
         return false;
      output_get_desc1_hook = std::move(*hook);
      return !sdr_display_spoof || Hooks::SetEnabled(output_get_desc1_hook, true);
   }

   bool InitDisplayCapability(IDXGISwapChain3* swapchain)
   {
      const std::lock_guard lock(init_mutex);
      if (swapchain_check_color_space_support_hook)
         return true;
      if (!swapchain)
         return false;
      auto hook = safetyhook::InlineHook::create(Hooks::GetVirtualFunction(swapchain, swapchain_check_color_space_support_index), reinterpret_cast<void*>(&HookedSwapChainCheckColorSpaceSupport), safetyhook::InlineHook::StartDisabled);
      if (!hook)
         return false;
      swapchain_check_color_space_support_hook = std::move(*hook);
      return !sdr_display_spoof || Hooks::SetEnabled(swapchain_check_color_space_support_hook, true);
   }

   bool SetSDRDisplaySpoof(bool enabled)
   {
      const std::lock_guard lock(init_mutex);
      sdr_display_spoof = enabled;
      bool success = true;
      if (output_get_desc1_hook)
         success &= Hooks::SetEnabled(output_get_desc1_hook, enabled);
      if (swapchain_check_color_space_support_hook)
         success &= Hooks::SetEnabled(swapchain_check_color_space_support_hook, enabled);
      return success;
   }
}

#endif
