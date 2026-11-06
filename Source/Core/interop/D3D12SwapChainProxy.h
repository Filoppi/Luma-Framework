#pragma once

#include "D3D12Interop.h"
#include "../hooks/DXGIHooks.h"
#include "../includes/frame_generation.h"

// Presents DX11 games through a DX12 swapchain (the "proxy"), so DX12 only presentation features (Frame Generation) can be used on them.
// Enabled by the Frame Generation features (see "UseLumaDLSSG" and "UseLumaXeSSFG" in "Luma.Features.props").
//
// How it works:
// -The DXGI factory swapchain creation functions are hooked ("DXGIHooks"). When the game creates its swapchain, the original (DX11) swapchain is created on a hidden child window
//  of the game window (a window can only have one flip model swapchain), and a DX12 swapchain (the Frame Generation one) is created on the game window.
//  The window is a child so that ReShade still routes the game window input to its overlay.
// -The game receives a wrapper of its swapchain, it renders into its DX11 back buffers as usual (ReShade and Luma keep working on them as well).
// -After ReShade has finished drawing on the back buffer ("reshade_present" event), the back buffer is copied to the DX12 swapchain (through the DX11->DX12 interop) and presented.
// -Frame Generation inputs (depth, motion vectors, etc) are copied into shared textures during the frame, as the game provides them.
// The swapchain format and color space are mirrored from the DX11 one.
// The approach is similar to OptiScaler's "Dx11wDx12" swapchain.
#if ENABLE_D3D12_INTEROP && ENABLE_SAFETYHOOK && defined(ENABLE_FRAME_GENERATION) && ENABLE_FRAME_GENERATION && defined(_WIN64)
#define ENABLE_D3D12_SWAPCHAIN_PROXY 1
#else
#define ENABLE_D3D12_SWAPCHAIN_PROXY 0
#endif

#if ENABLE_D3D12_SWAPCHAIN_PROXY

#include <functional>
#include <memory>

namespace D3D12SwapChainProxy
{
   // Whether the current thread is creating internal DX12/DXGI objects (e.g. the DX12 swapchain),
   // which can trigger ReShade events (e.g. "create_swapchain") that should be ignored by the addon.
   bool IsCreatingInternalObjects();

   // Returns the game window a hidden proxy window was created for, or the same window if it's not one of ours.
   HWND GetGameWindow(HWND window);

   // A proxied swapchain
   class SwapChain
   {
   public:
      // Copies the final back buffer image to the DX12 swapchain and presents it (with frame generation, if active).
      // This needs to be called after ReShade (and any other addon) finished drawing on the back buffer (so from the ReShade "reshade_present" event), before the original present,
      // from the thread that owns the immediate context. "back_buffer" is the current back buffer of the game swapchain.
      // If this isn't called during a frame, the proxy will copy and present the back buffer itself (before ReShade draws on it, if ReShade is beneath the proxy).
      virtual void PresentBackBuffer(ID3D11Resource* back_buffer) = 0;

      virtual HWND GetGameWindow() const = 0;
      virtual HWND GetHiddenWindow() const = 0;

      // Whether frames were generated during the last present.
      virtual bool IsGeneratingFrames() const = 0;
      // The amount of frames the last present displayed (including the generated ones).
      virtual unsigned int GetLastPresentedFramesCount() const = 0;
      // Whether "PresentBackBuffer()" stopped being called (e.g. ReShade's present event isn't firing), so the proxy is presenting the frames itself.
      virtual bool IsUsingFallbackPresent() const = 0;
      // Whether frame generation can't run on the current swapchain format (e.g. scRGB HDR).
      virtual bool IsFormatUnsupported() const = 0;

   protected:
      virtual ~SwapChain() = default;
   };

   // Per DX11 device proxy data (it owns the DX12 device, through the DX11->DX12 interop).
   class Device
   {
   public:
      // Creates a DX12 device on the same adapter of the given native DX11 device. Returns nullptr on failure.
      // "get_depth_copy_shader" returns a new reference to Luma's "Copy Depth CS" (see "D3D12Interop::CopyDepth()").
      static std::shared_ptr<Device> Create(ID3D11Device* native_device, std::function<ID3D11ComputeShader*()> get_depth_copy_shader);
      virtual ~Device() = default;

      virtual ID3D12Device* GetD3D12Device() const = 0;
      virtual IDXGIAdapter* GetAdapter() const = 0;

      // The frame generation implementation used by the swapchains created from now on (it's not owned by the proxy, it needs to outlive it).
      // This needs to be called (once) before the first swapchain is created, as it creates the DX12 queue. Returns false on failure.
      virtual bool SetFrameGeneration(FG::FrameGenerationImpl* implementation, FG::InstanceData* instance_data) = 0;
      // Whether the swapchains created from now on (for this device) will be proxied.
      virtual void SetProxyEnabled(bool enabled) = 0;

      // Returns the proxy swapchain matching the given swapchain (either the game facing one, or the original one it wraps), if any.
      virtual SwapChain* FindSwapChain(IDXGISwapChain* swapchain) const = 0;
      // Returns the last created proxy swapchain (still alive), if any.
      virtual SwapChain* GetSwapChain() const = 0;

      // Frame Generation settings and inputs (for the current frame, the one that will be presented next).
      // These need to be called from the thread that owns the immediate context.

      // Game/user settings. The output (back buffer) size and format are ignored, they are set by the proxy.
      // If "render_width" or "render_height" are 1 or less, they are automatically set from the size of the depth buffer.
      // "enable" can be used to temporarily stop generating frames (e.g. in menus).
      virtual void SetFrameGenerationSettings(const FG::SettingsData& settings_data, bool enable) = 0;
      // Copies the game resource into the shared texture of the input (the depth is converted to R32_FLOAT with a compute shader), with its valid region (zero sizes mean the whole texture).
      // This changes the render targets and the compute shader state, the caller is expected to restore the pipeline state. Mips, arrays and MSAA aren't supported.
      virtual bool SetFrameInput(ID3D11DeviceContext* immediate_context, FG::InputType type, ID3D11Resource* resource, uint32_t left = 0, uint32_t top = 0, uint32_t width = 0, uint32_t height = 0) = 0;
      virtual void SetFrameConstants(const FG::FrameConstants& constants) = 0;
      // Unique id of the current frame (increased after every present)
      virtual uint32_t GetFrameId() const = 0;
   };
}

#endif // ENABLE_D3D12_SWAPCHAIN_PROXY
