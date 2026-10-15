#pragma once

#include "FSR.h"

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

namespace FidelityFX
{
	// FSR 4 SR (DX12 FidelityFX API, through interop)
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

		// Whether the dll offers an actual FSR 4.x version (as opposed to only FSR 3.x ones). Used to prefer it on automatic SR selection.
		static bool HasNativeFSR4(const SR::InstanceData* data);

		// ReShade wraps any DX12 device created in the process (and sends events for it to all addons),
		// these allow the addon "init_device" callback to give us back the native (unwrapped) device we are creating internally,
		// so that none of our DX12 work goes through ReShade (and FSR gets the real device).
		static bool IsCreatingInteropDevice();
		static void OnInteropDeviceInit(void* native_device);
	};
}

#endif
