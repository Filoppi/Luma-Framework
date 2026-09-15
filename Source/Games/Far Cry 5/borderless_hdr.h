#pragma once

// Saved opt-in, latched at startup. Does not upgrade SDR resources or shaders.
// Native FC5 must already request its scRGB output (saved hdr=2, valid refresh).
namespace FC5::BorderlessHDR
{
   inline bool configured = false;
   inline bool active = false;
   inline bool initialized = false;
   inline bool test_override = false;
   inline void LoadConfiguration()
   {
      reshade::get_config_value(nullptr, NAME, "FC5BorderlessHDR", configured);
      if (initialized) return;
      wchar_t value[8]{};
      test_override = GetEnvironmentVariableW(L"FC5_TEST_BORDERLESS_HDR", value, 8) == 1 && value[0] == L'1';
      active = configured || test_override;
      initialized = true;
      prevent_fullscreen_state = force_borderless = active;
      reshade::log::message(reshade::log::level::info, active ?
         "[FC5 HDR] Borderless HDR startup option enabled; native scRGB required" :
         "[FC5 HDR] Borderless HDR startup option disabled");
   }
   inline bool Enabled()
   {
      return active;
   }
   inline bool OnCreate(reshade::api::device_api api, reshade::api::swapchain_desc& desc, void*)
   {
      if (!Enabled() || api != reshade::api::device_api::d3d11 ||
          desc.back_buffer.texture.format != reshade::api::format::r16g16b16a16_float ||
          desc.back_buffer.texture.samples != 1) return false;
      desc.present_mode = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      desc.back_buffer_count = (std::max)(desc.back_buffer_count, 2u);
      desc.fullscreen_state = false;
      desc.present_flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
      reshade::log::message(reshade::log::level::info, "[FC5 HDR] TEST native FP16 flip-discard requested; SDR unchanged");
      return true;
   }
   inline void OnInit(reshade::api::swapchain* swapchain, bool)
   {
      if (!Enabled() || swapchain->get_device()->get_api() != reshade::api::device_api::d3d11) return;
      auto* native = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
      DXGI_SWAP_CHAIN_DESC desc{};
      if (FAILED(native->GetDesc(&desc))) return;
      HRESULT result = E_NOINTERFACE;
      com_ptr<IDXGISwapChain3> chain3;
      if (desc.BufferDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
          (desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD || desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) &&
          SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&chain3))))
      {
         UINT support = 0;
         result = chain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support);
         if (SUCCEEDED(result) && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            result = chain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
         else if (SUCCEEDED(result)) result = DXGI_ERROR_UNSUPPORTED;
      }
      BOOL fullscreen = FALSE;
      const HRESULT fs = native->GetFullscreenState(&fullscreen, nullptr);
      char line[256];
      std::snprintf(line, sizeof(line), "[FC5 HDR] TEST actual %ux%u format=%u effect=%u buffers=%u windowed=%d fullscreen=%d fsHR=%08X scRGB_HR=%08X",
         desc.BufferDesc.Width, desc.BufferDesc.Height, UINT(desc.BufferDesc.Format), UINT(desc.SwapEffect), desc.BufferCount,
         desc.Windowed, fullscreen, UINT(fs), UINT(result));
      reshade::log::message(reshade::log::level::info, line);
   }
}
