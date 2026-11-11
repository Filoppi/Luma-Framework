#pragma once

#include "../includes/super_resolution.h"

// DLSS, FSR and XeSS for 32-bit games: all are x64 only, so they run in "Luma-Upscaler.exe" next to the game's exe
// (Source/Tools/SR Bridge Helper) on shared textures. Games opt in with "UseLumaSRBridge" (their package then
// ships the helper and the upscalers' dlls), and use it like the in-process implementations.
#if defined(_WIN64) || !defined(ENABLE_SR_BRIDGE)
#undef ENABLE_SR_BRIDGE
#define ENABLE_SR_BRIDGE 0
#endif

#if ENABLE_SR_BRIDGE

#include <d3d11.h>

namespace SRBridge
{
   // A texture the bridge hands to its helper as is (NT handle shared), else a plain one (the bridge copies those every frame)
   inline HRESULT CreateSharableTexture(ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ID3D11Texture2D** texture)
   {
      desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
      if (SUCCEEDED(native_device->CreateTexture2D(&desc, nullptr, texture)))
         return S_OK;
      desc.MiscFlags &= ~(D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
      return native_device->CreateTexture2D(&desc, nullptr, texture);
   }

   // One per SR type, registered as that type's implementation
   class Bridge : public SR::SuperResolutionImpl
   {
   public:
      explicit Bridge(SR::Type type) : type(type)
      {
      }

      virtual bool HasInit(const SR::InstanceData* data) const override;
      virtual bool IsSupported(const SR::InstanceData* data) const override;

      virtual bool Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter = nullptr) override;
      virtual void Deinit(SR::InstanceData*& data, ID3D11Device* optional_device = nullptr) override;
      // Stops the helper and drops the bridge's copies, the next draw starts a new one
      virtual void ReleaseResources(SR::InstanceData* data) override;

      virtual bool UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data) override;

      // Until the helper is ready, the color is copied to the output (so the game doesn't treat it as a failure)
      virtual bool Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data) override;

      virtual int GetJitterPhases(const SR::InstanceData* data) const override;

      // True once the helper created the upscaler
      virtual bool IsReady(const SR::InstanceData* data) const override;

   private:
      SR::Type type;
   };
} // namespace SRBridge

#endif
