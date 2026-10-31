#pragma once

#include "../includes/super_resolution.h"
#include "../interop/D3D12Interop.h"

#include <functional>

// XeSS for DX11 games, run through the DX11->DX12 interop with Intel's signed DX12 "libxess.dll", which is shipped with Luma.
// The DX11 XeSS only runs on Intel GPUs, the DX12 one on any GPU with SM 6.4 and DP4a.
// Opt in per game with "ENABLE_XESS" (see "UseLumaXeSS" in "Luma.Features.props"), which also enables the interop.
#if defined(_WIN64) && defined(ENABLE_XESS) && ENABLE_XESS && ENABLE_D3D12_INTEROP && __has_include("xess/xess_d3d12.h")
#undef ENABLE_XESS
#define ENABLE_XESS 1
#else
#ifdef ENABLE_XESS
#undef ENABLE_XESS
#endif
#define ENABLE_XESS 0
#endif

#if ENABLE_XESS

struct ID3D11ComputeShader;

namespace Intel
{
   class XeSS : public SR::SuperResolutionImpl
   {
   public:
      virtual bool HasInit(const SR::InstanceData* data) const override;
      virtual bool IsSupported(const SR::InstanceData* data) const override;

      virtual bool Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter = nullptr) override;
      virtual void Deinit(SR::InstanceData*& data, ID3D11Device* optional_device = nullptr) override;

      virtual bool UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data) override;

      virtual bool Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data) override;

      virtual int GetJitterPhases(const SR::InstanceData* data) const override;

      virtual bool NeedsStateRestoration() const override
      {
         return true;
      }

      // See "FidelityFX::FSR4::SetDepthCopyShader()"
      static void SetDepthCopyShader(SR::InstanceData* data, std::function<ID3D11ComputeShader*()> get_depth_copy_shader);
   };
} // namespace Intel

#endif
