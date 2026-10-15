#include "FSR.h"

#if ENABLE_FIDELITY_SK

#include "FidelityFX/host/backends/dx11/ffx_dx11.h"
#include "FidelityFX/host/ffx_fsr3.h"
#include "FidelityFX/host/ffx_fsr3upscaler.h"

#include "../includes/debug.h"

#include <cstring>
#include <cassert>
#include <unordered_set>
#include <wrl/client.h>
#include <d3d11.h>

namespace FidelityFX
{
   constexpr int default_phase_count = 16;

   struct FSRInstanceData : public SR::InstanceData
   {
      FfxFsr3Context context = {};
      bool has_context = false;
      void* scratch_buffer = nullptr;
      int phase_count = default_phase_count;
   };

   // TODO: delete
   double MillisecondsNow()
   {
      static LARGE_INTEGER s_frequency;
      static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
      double milliseconds = 0;
      if (s_use_qpc)
      {
         LARGE_INTEGER now;
         QueryPerformanceCounter(&now);
         milliseconds = double(1000.0 * now.QuadPart) / s_frequency.QuadPart;
      }
      else
      {
         milliseconds = double(GetTickCount64());
      }
      return milliseconds;
   }
   double GetDeltaTime()
   {
      static double last_frame_time = MillisecondsNow();
      double currentTime = MillisecondsNow();
      double deltaTime = (currentTime - last_frame_time);
      last_frame_time = currentTime;
      return deltaTime;
   }

   // Calling this function in the library ("ffxGetResourceDX1") gives a linking error for some reason
   FfxResource ffxGetResourceDX11_local(ID3D11Resource* dx11Resource,
      FfxResourceDescription ffxResDescription,
      wchar_t const* ffxResName,
      FfxResourceStates state = FFX_RESOURCE_STATE_COMPUTE_READ)
   {
      FfxResource resource = {};
      resource.resource = reinterpret_cast<void*>(const_cast<ID3D11Resource*>(dx11Resource));
      resource.state = state;
      resource.description = ffxResDescription;

#if DEVELOPMENT
      if (ffxResName)
      {
         wcscpy_s(resource.name, ffxResName);
      }
#endif

      return resource;
   }

   bool FidelityFX::FSR::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data); // This will also null the pointer
      }

      auto& custom_data = reinterpret_cast<FSRInstanceData*&>(data);

      if (device->GetFeatureLevel() == D3D_FEATURE_LEVEL_11_0)
      {
         ASSERT_ONCE(false); // FSR isn't support on DX11.0, disable it for this game if possible, there's no point in bundling it

         return false;
      }
      else
      {
         custom_data = new FSRInstanceData();
         custom_data->is_supported = true;
         custom_data->automatically_restores_pipeline_state = false; // TODO: does it actually not do it? What states does it change? Does DLSS properly do it?
         custom_data->min_resolution = 32; // Not 100% accurate but should be a decent start
      }

      // NOTE: the init for FSR is the same as the context creation. There's no separate global dll init like there is for DLSS,
      // and given our implementation is flexible and doesn't know the settings until the first drawn frame,
      // we delay the init there. There's no reason why FSR wouldn't run unless the user deleted the dll.
      // Here we could check if the dll is present, but that wouldn't be the right check to do in all cases, so we avoid it (note: the current DX11 version of FSR has no dll, it's statically linked).

      return custom_data != nullptr && custom_data->is_supported;
   }

   void FidelityFX::FSR::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
   {
      auto& custom_data = reinterpret_cast<FSRInstanceData*&>(data);

      if (custom_data->has_context)
      {
         FfxErrorCode err_code = ffxFsr3ContextDestroy(&custom_data->context);
         if (err_code != FFX_OK)
         {
            printf_s("FSR3: ffxFsr3ContextDestroy failed, error = %d\n", static_cast<int>(err_code));
         }

         free(custom_data->scratch_buffer);
      }

      delete custom_data;
      custom_data = nullptr;
   }

   bool FidelityFX::FSR::HasInit(const SR::InstanceData* data) const
   {
      return true;
   }

   bool FidelityFX::FSR::IsSupported(const SR::InstanceData* data) const
   {
      return data->is_supported;
   }

   bool FidelityFX::FSR::UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data)
   {
      auto& custom_data = reinterpret_cast<FSRInstanceData*&>(data);

      // Early exit if FSR is not supported by hardware or driver.
      if (!command_list || !custom_data || !custom_data->is_supported)
         return false;

      // No need to re-instantiate FSR "features" if all the params are the same
      if (memcmp(&settings_data, &custom_data->settings_data, sizeof(SR::SettingsData)) == 0 && custom_data->has_context)
      {
         return true;
      }

      const size_t scratch_buffer_size = ffxGetScratchMemorySizeDX11(1);
      void* scratch_buffer = calloc(scratch_buffer_size, 1);

      Microsoft::WRL::ComPtr<ID3D11Device> device;
      command_list->GetDevice(device.GetAddressOf());

      FfxFsr3ContextDescription context_desc{};

      FfxErrorCode err_code = ffxGetInterfaceDX11(&context_desc.backendInterfaceUpscaling, ffxGetDeviceDX11(device.Get()), scratch_buffer, scratch_buffer_size, 1);
      if (err_code != FFX_OK)
      {
         printf_s("FSR3: ffxGetInterfaceDX11 failed, error = %d\n", static_cast<int>(err_code));
         ASSERT_ONCE(false);
         free(scratch_buffer);
         return false;
      }

      context_desc.displaySize.width = settings_data.output_width;
      context_desc.displaySize.height = settings_data.output_height;
      context_desc.maxUpscaleSize.width = settings_data.output_width;
      context_desc.maxUpscaleSize.height = settings_data.output_height;
      if (settings_data.dynamic_resolution)
      {
         context_desc.maxRenderSize.width = settings_data.output_width;
         context_desc.maxRenderSize.height = settings_data.output_height;
      }
      else
      {
         context_desc.maxRenderSize.width = settings_data.render_width;
         context_desc.maxRenderSize.height = settings_data.render_height;
      }

      context_desc.flags |= FFX_FSR3_ENABLE_UPSCALING_ONLY; // No FG for now (there's none in DX11 anyway)
      if (settings_data.auto_exposure)
      {
         context_desc.flags |= FFX_FSR3_ENABLE_AUTO_EXPOSURE;
      }
      if (settings_data.hdr)
      {
         context_desc.flags |= FFX_FSR3_ENABLE_HIGH_DYNAMIC_RANGE;
      }
      if (settings_data.dynamic_resolution)
      {
         context_desc.flags |= FFX_FSR3_ENABLE_DYNAMIC_RESOLUTION;
      }
      if (settings_data.inverted_depth)
      {
         context_desc.flags |= FFX_FSR3_ENABLE_DEPTH_INVERTED | FFX_FSR3_ENABLE_DEPTH_INFINITE;
      }
      if (settings_data.mvs_jittered)
      {
         context_desc.flags |= FFX_FSR3_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
      }

      context_desc.backBufferFormat = FfxSurfaceFormat::FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT; // Just guessed for now, for Frame Gen only. Luma classic format hardcoded.

#if DEVELOPMENT
      if (true) // For now we always do this in dev mode
      {
         auto LogCallback = [](FfxMsgType type, const wchar_t* message)
         {
            // Convert wide to narrow
            char buffer[512] = {};
            std::size_t converted = std::wcstombs(buffer, message ? message : L"", sizeof(buffer) - 1);

            if (converted == static_cast<std::size_t>(-1))
            {
               return;
            }

            switch (type)
            {
            case FFX_MESSAGE_TYPE_ERROR:
               printf_s("FSR3 Error: %s\n", buffer); // TODO: use "OutputDebugStringA", this doesn't work in a dll
               ASSERT_MSGF(false, "FSR3 Error: %s\n", buffer);
               break;
            case FFX_MESSAGE_TYPE_WARNING:
               printf_s("FSR3 Warning: %s\n", buffer);
               break;
            }
         };
         context_desc.fpMessage = LogCallback;
         context_desc.flags |= FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
      }
#endif

      // Destroy any possible previously created context
      if (custom_data->has_context)
      {
         err_code = ffxFsr3ContextDestroy(&custom_data->context);
         if (err_code != FFX_OK)
         {
            printf_s("FSR3: ffxFsr3ContextDestroy failed, error = %d\n", static_cast<int>(err_code));
         }
         //custom_data->context = {}; // Probably not very useful
         memset(&custom_data->context, 0, sizeof(FfxFsr3Context));
         custom_data->has_context = false;

         free(custom_data->scratch_buffer);
      }

      err_code = ffxFsr3ContextCreate(&custom_data->context, &context_desc);
      if (err_code != FFX_OK)
      {
         // TODO: set FSR as non compatible if this failed with a "non compatible" or "dll missing" error? Or try again?
         printf_s("FSR3: ffxFsr3ContextCreate failed, error = %d\n", static_cast<int>(err_code));
         ASSERT_ONCE_MSG(false, "Couldn't create FSR3 context");
         free(scratch_buffer);
         return false;
      }

      custom_data->settings_data = settings_data;
      custom_data->scratch_buffer = scratch_buffer;
      custom_data->has_context = true;

      custom_data->phase_count = ffxFsr3GetJitterPhaseCount(settings_data.render_width, settings_data.output_width);

      return true;
   }

   int FidelityFX::FSR::GetJitterPhases(const SR::InstanceData* data) const
   {
      auto& custom_data = reinterpret_cast<const FSRInstanceData*&>(data);
      return custom_data->phase_count;
   }

   bool FidelityFX::FSR::Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data)
   {
      auto& custom_data = reinterpret_cast<const FSRInstanceData*&>(data);

      FfxFsr3DispatchUpscaleDescription dispatch_upscale{};

      dispatch_upscale.commandList = ffxGetCommandListDX11(command_list);
      dispatch_upscale.color = ffxGetResourceDX11_local(draw_data.source_color, GetFfxResourceDescriptionDX11(draw_data.source_color), L"FSR3_color");
      dispatch_upscale.upscaleOutput = ffxGetResourceDX11_local(draw_data.output_color, GetFfxResourceDescriptionDX11(draw_data.output_color), L"FSR3_upscaleOutput");
      dispatch_upscale.depth = ffxGetResourceDX11_local(draw_data.depth_buffer, GetFfxResourceDescriptionDX11(draw_data.depth_buffer), L"FSR3_depth");
      dispatch_upscale.motionVectors = ffxGetResourceDX11_local(draw_data.motion_vectors, GetFfxResourceDescriptionDX11(draw_data.motion_vectors), L"FSR3_motionVectors");
      if (draw_data.exposure)
      {
         dispatch_upscale.exposure = ffxGetResourceDX11_local(draw_data.exposure, GetFfxResourceDescriptionDX11(draw_data.exposure), L"FSR3_exposure");
      }
      if (draw_data.bias_mask)
      {
         dispatch_upscale.reactive = ffxGetResourceDX11_local(draw_data.bias_mask, GetFfxResourceDescriptionDX11(draw_data.bias_mask), L"FSR3_reactive");
      }
      if (draw_data.transparency_alpha)
      {
         dispatch_upscale.transparencyAndComposition = ffxGetResourceDX11_local(draw_data.transparency_alpha, GetFfxResourceDescriptionDX11(draw_data.transparency_alpha), L"FSR3_transparencyAndComposition");
      }

#if DEVELOPMENT
      D3D11_TEXTURE2D_DESC output_desc;
      ((ID3D11Texture2D*)draw_data.output_color)->GetDesc(&output_desc);
      ASSERT_ONCE(draw_data.output_color && custom_data->settings_data.output_width == (int)output_desc.Width && custom_data->settings_data.output_height == (int)output_desc.Height);
#endif
      dispatch_upscale.upscaleSize.width = custom_data->settings_data.output_width;
      dispatch_upscale.upscaleSize.height = custom_data->settings_data.output_height;
      dispatch_upscale.renderSize.width = draw_data.render_width;
      dispatch_upscale.renderSize.height = draw_data.render_height;
      dispatch_upscale.jitterOffset.x = draw_data.jitter_x;
      dispatch_upscale.jitterOffset.y = draw_data.jitter_y;
      dispatch_upscale.motionVectorScale.x = custom_data->settings_data.mvs_x_scale;
      dispatch_upscale.motionVectorScale.y = custom_data->settings_data.mvs_y_scale;

      dispatch_upscale.preExposure = draw_data.pre_exposure == 0.f ? 1.f : draw_data.pre_exposure;

      dispatch_upscale.cameraFovAngleVertical = draw_data.vert_fov;
      if (custom_data->settings_data.inverted_depth)
      {
         dispatch_upscale.cameraFar = draw_data.near_plane;
         dispatch_upscale.cameraNear = draw_data.far_plane;
      }
      else
      {
         dispatch_upscale.cameraFar = draw_data.far_plane;
         dispatch_upscale.cameraNear = draw_data.near_plane;
      }
      //dispatch_upscale.cameraFar = draw_data.near_plane; // TODO: why does FSR complain depth is too small with inverse depth? Even with 100000
      //dispatch_upscale.cameraNear = draw_data.far_plane;
      dispatch_upscale.viewSpaceToMetersFactor = 1.f; // This can be left at zero and still work. Most engines units should be meters.

      dispatch_upscale.reset = draw_data.reset;

      dispatch_upscale.enableSharpening = draw_data.user_sharpness >= 0.f;
      dispatch_upscale.sharpness = dispatch_upscale.enableSharpening ? draw_data.user_sharpness : 0.f;

      float time_delta = draw_data.time_delta > 0.f ? draw_data.time_delta : (1.f / 60.f);
      dispatch_upscale.frameTimeDelta = static_cast<float>(time_delta * 1000.f); // FSR expects milliseconds. Unused by upscaling.
      dispatch_upscale.frameID = draw_data.frame_index; // This is ignored so it doesn't matter (it's for FG)

      dispatch_upscale.flags = 0;
#if DEVELOPMENT && !defined(NDEBUG)
      if (true) // For now we always do this in development // TODO: expose a toggle
      {
         dispatch_upscale.flags |= FFX_FSR3_UPSCALER_FLAG_DRAW_DEBUG_VIEW;
      }
#endif

      FfxErrorCode err_code = ffxFsr3ContextDispatchUpscale(const_cast<FfxFsr3Context*>(&custom_data->context), &dispatch_upscale);
      if (err_code != FFX_OK)
      {
         printf_s("FSR3: ffxFsr3ContextDispatchUpscale failed, error = %d\n", static_cast<int>(err_code));
      }
      return err_code == FFX_OK;
   }
}

#endif