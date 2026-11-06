#pragma once

#include "../includes/frame_generation.h"

// DLSS Frame Generation through Streamline (DX12 only, see "D3D12SwapChainProxy.h").
// Streamline is loaded dynamically from next to the Luma addon ("sl.interposer.dll", and its "sl.*.dll" plugins and "nvngx_dlssg.dll"), after verifying its signature.
// It's integrated with manual hooking, so Streamline only ever sees our DX12 objects (not the game's DX11 ones).
// Opt in per game with "ENABLE_DLSSG" (see "UseLumaDLSSG" in "Luma.Features.props").
#if defined(_WIN64) && defined(ENABLE_DLSSG) && ENABLE_DLSSG && __has_include("sl_dlss_g.h")
#undef ENABLE_DLSSG
#define ENABLE_DLSSG 1
#else
#ifdef ENABLE_DLSSG
#undef ENABLE_DLSSG
#endif
#define ENABLE_DLSSG 0
#endif

#if ENABLE_DLSSG

namespace Streamline
{
	class DLSSG : public FG::FrameGenerationImpl
	{
	public:
		virtual FG::Type GetType() const override { return FG::Type::DLSS_G; }

		virtual bool Init(FG::InstanceData*& data, IDXGIAdapter* adapter, ID3D12Device* device) override;
		virtual void Deinit(FG::InstanceData*& data) override;

		virtual HRESULT CreateCommandQueue(FG::InstanceData* data, ID3D12Device* device, const D3D12_COMMAND_QUEUE_DESC& desc, ID3D12CommandQueue** command_queue) override;
		virtual HRESULT CreateSwapchain(FG::InstanceData* data, IDXGIFactory2* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain4** swapchain) override;
		virtual void DestroySwapchain(FG::InstanceData* data) override;
		virtual void OnSwapchainResize(FG::InstanceData* data, bool before) override;

		virtual bool UpdateSettings(FG::InstanceData* data, const FG::SettingsData& settings_data) override;

		virtual void BeginFrame(FG::InstanceData* data, uint32_t frame_id) override;
		virtual bool PrepareFrame(FG::InstanceData* data, ID3D12GraphicsCommandList* command_list, const FG::FrameData& frame_data, bool generate) override;
		virtual HRESULT Present(FG::InstanceData* data, IDXGISwapChain4* swapchain, uint32_t frame_id, UINT sync_interval, UINT flags) override;

		virtual unsigned int GetLastPresentedFramesCount(const FG::InstanceData* data) const override;
	};
}

#endif
