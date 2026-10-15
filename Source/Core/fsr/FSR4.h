#pragma once

#include "FSR.h"

#include <functional>

// FSR 4 (through the FidelityFX API DX12 dlls, loaded at runtime, not shipped with Luma), run on DX11 games through DX11->DX12 interop.
// Opt in per game with "ENABLE_FSR4" (see "UseLumaFSR4" in "Luma.Features.props"). Requires the FSR 3 SDK to also be enabled.
#if defined(_WIN64) && ENABLE_FIDELITY_SK && defined(ENABLE_FSR4) && ENABLE_FSR4 && __has_include("ffx_api/ffx_upscale.h")
#undef ENABLE_FSR4
#define ENABLE_FSR4 1
#else
#ifdef ENABLE_FSR4
#undef ENABLE_FSR4
#endif
#define ENABLE_FSR4 0
#endif

#if ENABLE_FSR4

struct ID3D11ComputeShader;

namespace FidelityFX
{
	// FSR 4 SR (DX12 FidelityFX API, through interop). Only supported if the GPU (driver) offers an FSR 4 upscaler.
	class FSR4 : public SR::SuperResolutionImpl
	{
	public:
		virtual bool HasInit(const SR::InstanceData* data) const override;
		virtual bool IsSupported(const SR::InstanceData* data) const override;

		virtual bool Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter = nullptr) override;
		virtual void Deinit(SR::InstanceData*& data, ID3D11Device* optional_device = nullptr) override;

		virtual bool UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data) override;

		virtual bool Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data) override;

		virtual int GetJitterPhases(const SR::InstanceData* data) const;

		virtual bool NeedsStateRestoration() const { return true; }

		// The Luma native shader that copies the depth into a R32_FLOAT texture (FSR 4 can't read DX11 depth stencil textures).
		// The getter returns a new reference (or nullptr), as native shaders can be recompiled.
		static void SetDepthCopyShader(SR::InstanceData* data, std::function<ID3D11ComputeShader*()> get_depth_copy_shader);
	};
}

#endif
