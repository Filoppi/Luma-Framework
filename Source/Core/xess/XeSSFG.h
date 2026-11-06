#pragma once

#include "../includes/frame_generation.h"

// XeSS Frame Generation, with XeLL (Intel's low latency library, which it requires) (DX12 only, see "D3D12SwapChainProxy.h").
// Intel's signed "libxess_fg.dll" and "libxell.dll" are shipped next to the Luma addon. It runs on any GPU that supports SM 6.4 (generating one frame on non Intel GPUs).
// Opt in per game with "ENABLE_XESS_FG" (see "UseLumaXeSSFG" in "Luma.Features.props").
#if defined(_WIN64) && defined(ENABLE_XESS_FG) && ENABLE_XESS_FG && __has_include("xess_fg/xefg_swapchain_d3d12.h") && __has_include("xell/xell_d3d12.h")
#undef ENABLE_XESS_FG
#define ENABLE_XESS_FG 1
#else
#ifdef ENABLE_XESS_FG
#undef ENABLE_XESS_FG
#endif
#define ENABLE_XESS_FG 0
#endif

#if ENABLE_XESS_FG

namespace Intel
{
	class XeSSFG : public FG::FrameGenerationImpl
	{
	public:
		virtual FG::Type GetType() const override { return FG::Type::XeSS_FG; }

		virtual bool Init(FG::InstanceData*& data, IDXGIAdapter* adapter, ID3D12Device* device) override;
		virtual void Deinit(FG::InstanceData*& data) override;

		virtual HRESULT CreateSwapchain(FG::InstanceData* data, IDXGIFactory2* factory, ID3D12CommandQueue* command_queue, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain4** swapchain) override;
		virtual void DestroySwapchain(FG::InstanceData* data) override;

		virtual bool UpdateSettings(FG::InstanceData* data, const FG::SettingsData& settings_data) override;
		virtual bool NeedsSwapchainRecreation(const FG::InstanceData* data) const override;

		virtual void BeginFrame(FG::InstanceData* data, uint32_t frame_id) override;
		virtual bool PrepareFrame(FG::InstanceData* data, ID3D12GraphicsCommandList* command_list, const FG::FrameData& frame_data, bool generate) override;
		virtual HRESULT Present(FG::InstanceData* data, IDXGISwapChain4* swapchain, uint32_t frame_id, UINT sync_interval, UINT flags) override;

		virtual unsigned int GetLastPresentedFramesCount(const FG::InstanceData* data) const override;
	};
} // namespace Intel

#endif
