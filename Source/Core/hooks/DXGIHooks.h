#pragma once

#include "Hooks.h"

struct IDXGIAdapter;
struct IDXGIOutput;
struct IDXGISwapChain3;

namespace DXGIHooks
{
#if ENABLE_SAFETYHOOK
   // Creates the (disabled) hooks on the methods of the adapter class. Safe to call more than once.
   bool Init(IDXGIAdapter* adapter);

   // "IDXGIAdapter1::GetDesc1()" calls made by "caller_module" return "vendor_id" instead of the real one. Pass a null module to stop.
   bool SetAdapterVendorIdOverride(HMODULE caller_module, UINT vendor_id = 0);

   // Creates the (disabled) hook on "IDXGIOutput6::GetDesc1()", from any output (the method is shared by all the outputs). Safe to call more than once.
   bool InitDisplayCapability(IDXGIOutput* output);
   // Creates the (disabled) hook on "IDXGISwapChain3::CheckColorSpaceSupport()", from any swapchain (the method is shared by all the swapchains). Safe to call more than once.
   bool InitDisplayCapability(IDXGISwapChain3* swapchain);

   // While enabled, the display reports not being in HDR and HDR color spaces as unsupported to everyone, except the code under a "Display::ScopedRealDisplayQuery".
   // This makes games without an HDR toggle pick their SDR path (hooks that haven't been created yet will be enabled on creation).
   bool SetSDRDisplaySpoof(bool enabled);
#endif
}
