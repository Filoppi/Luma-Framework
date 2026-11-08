// x64 helper for 32-bit Luma mods: DLSS, FSR and XeSS are x64 only, so the game's SR bridge ("Source/Core/sr_bridge/SRBridge.cpp")
// starts this next to the game's exe. It runs Core's upscalers on its own device of the same adapter. Protocol: "SRBridgeProtocol.h".
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "../../Core/sr_bridge/SRBridgeProtocol.h"
#include "../../Core/sr_bridge/DepthCopyCS.h"
#include "../../Core/dlss/DLSS.h"
#include "../../Core/fsr/FSR.h"
#include "../../Core/fsr/FSR4.h"
#include "../../Core/xess/XeSS.h"

#include <cstdio>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace SRBridgeProtocol;

// Core logs through ReShade, which isn't loaded here (the helper is built with RESHADE_API_LIBRARY_EXPORT and BUILTIN_ADDON): the log is stdout
extern "C" __declspec(dllexport) void ReShadeLogMessage(void* module, int level, const char* message)
{
   printf("[Luma Upscaler] %s\n", message);
   fflush(stdout);
}

// Only FSR 4's DXGI hooks need this here, and nothing in this process queries the display for real
namespace Display
{
   bool IsRealDisplayQueryActive()
   {
      return false;
   }
} // namespace Display

int main()
{
   // The protocol's "<settings>"
   const auto read_settings = [](SR::SettingsData* settings)
   {
      return bool(std::cin >> settings->render_width >> settings->render_height >> settings->output_width >> settings->output_height >> settings->hdr >>
                  settings->inverted_depth >> settings->mvs_jittered >> settings->auto_exposure >> settings->dynamic_resolution >> settings->mvs_x_scale >>
                  settings->mvs_y_scale >> settings->render_preset);
   };
   const auto fail = [](const std::string& reason)
   {
      printf("[Luma Upscaler] FAIL %s\n", reason.c_str());
      return 1;
   };

   // Only this reads stdin
   std::ios::sync_with_stdio(false);
   std::cin.tie(nullptr);
   int version = 0, upscaler = int(SR::Type::None);
   SR::SettingsData settings;
   LUID luid = {};
   uint64_t handles[RESOURCE_COUNT] = {}, fence_handles[2] = {};
   std::cin >> version;
   if (version != PROTOCOL_VERSION)
   {
      return fail("protocol " + std::to_string(version) + ", expected " + std::to_string(PROTOCOL_VERSION));
   }
   std::cin >> upscaler >> luid.LowPart >> luid.HighPart;
   read_settings(&settings);
   for (uint64_t& handle : handles)
   {
      std::cin >> handle;
   }
   std::cin >> fence_handles[0] >> fence_handles[1];
   if (!std::cin)
   {
      return fail("bad start line");
   }

   const SR::Type type = SR::Type(upscaler);
   std::unique_ptr<SR::SuperResolutionImpl> impl;
   switch (type)
   {
   case SR::Type::DLSS:
      impl = std::make_unique<NGX::DLSS>();
      break;
   case SR::Type::FSR:
      impl = std::make_unique<FidelityFX::FSR>();
      break;
   case SR::Type::FSR_4:
      impl = std::make_unique<FidelityFX::FSR4>();
      break;
   case SR::Type::XeSS:
      impl = std::make_unique<Intel::XeSS>();
      break;
   default:
      return fail("unknown upscaler " + std::to_string(upscaler));
   }

   ComPtr<IDXGIFactory4> factory;
   ComPtr<IDXGIAdapter1> adapter;
   if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter))))
   {
      return fail("no adapter with the game's LUID");
   }
   const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
   ComPtr<ID3D11Device> device;
   ComPtr<ID3D11DeviceContext> context;
   ComPtr<ID3D11Device5> device5;
   ComPtr<ID3D11DeviceContext4> context4;
   if (FAILED(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels, UINT(std::size(levels)), D3D11_SDK_VERSION, &device, nullptr, &context)) ||
       FAILED(device.As(&device5)) || FAILED(context.As(&context4)))
   {
      return fail("device");
   }

   ComPtr<ID3D11Texture2D> textures[RESOURCE_COUNT];
   for (int i = 0; i < RESOURCE_COUNT; i++)
   {
      if (handles[i] && FAILED(device5->OpenSharedResource1(HANDLE(uintptr_t(handles[i])), IID_PPV_ARGS(&textures[i]))))
      {
         return fail("open texture " + std::to_string(i));
      }
   }
   ComPtr<ID3D11Fence> fence_in, fence_out;
   if (!textures[RESOURCE_COLOR] || !textures[RESOURCE_MOTION] || !textures[RESOURCE_DEPTH] || !textures[RESOURCE_OUTPUT] || FAILED(device5->OpenSharedFence(HANDLE(uintptr_t(fence_handles[0])), IID_PPV_ARGS(&fence_in))) ||
       FAILED(device5->OpenSharedFence(HANDLE(uintptr_t(fence_handles[1])), IID_PPV_ARGS(&fence_out))))
   {
      return fail("inputs or fences");
   }

   // FSR 4 and XeSS's D3D12 path copy the depth with Luma's "Copy Depth CS", a native shader the helper can't compile, so we embed it
   ComPtr<ID3D11ComputeShader> depth_copy_cs;
   if (FAILED(device->CreateComputeShader(DEPTH_COPY_CS, sizeof(DEPTH_COPY_CS), nullptr, &depth_copy_cs)))
   {
      return fail("depth copy shader");
   }
   const auto get_depth_copy_shader = [&]() -> ID3D11ComputeShader*
   {
      depth_copy_cs->AddRef();
      return depth_copy_cs.Get();
   };

   SR::InstanceData* data = nullptr;
   // "Deinit()" is expected also when "Init()" failed
   const auto quit = [&](int exit_code)
   {
      impl->Deinit(data, device.Get());
      return exit_code;
   };
   if (!impl->Init(data, device.Get(), adapter.Get()))
   {
      fail(std::string(SR::GetTypeName(type)) + " init");
      return quit(1);
   }
   if (type == SR::Type::FSR_4)
   {
      FidelityFX::FSR4::SetDepthCopyShader(data, get_depth_copy_shader);
   }
   else if (type == SR::Type::XeSS)
   {
      Intel::XeSS::SetDepthCopyShader(data, get_depth_copy_shader);
   }
   if (!impl->UpdateSettings(data, context.Get(), settings))
   {
      fail(std::string(SR::GetTypeName(type)) + " create");
      return quit(1);
   }
   context4->Signal(fence_out.Get(), READY_FENCE_VALUE);
   context->Flush();
   printf("[Luma Upscaler] ready: %s %ux%u -> %ux%u\n", SR::GetTypeName(type), settings.render_width, settings.render_height, settings.output_width, settings.output_height);
   fflush(stdout);

   // The game's GPU may already wait for the next frame, and only a killed process's fences jump past it by themselves
   const auto fail_running = [&](const std::string& reason)
   {
      context4->Signal(fence_out.Get(), UINT64_MAX);
      context->Flush();
      fail(reason);
      return quit(1);
   };

   SR::SuperResolutionImpl::DrawData draw = {
      .output_color = textures[RESOURCE_OUTPUT].Get(),
      .source_color = textures[RESOURCE_COLOR].Get(),
      .motion_vectors = textures[RESOURCE_MOTION].Get(),
      .depth_buffer = textures[RESOURCE_DEPTH].Get(),
      .exposure = textures[RESOURCE_EXPOSURE].Get(),
      .bias_mask = textures[RESOURCE_REACTIVE].Get(),
      .transparency_alpha = textures[RESOURCE_TRANSPARENCY].Get(),
   };
   std::string message;
   while (std::cin >> message)
   {
      if (message == "settings")
      {
         if (!read_settings(&settings))
         {
            return fail_running("bad settings line");
         }
         if (!impl->UpdateSettings(data, context.Get(), settings))
         {
            return fail_running("recreate");
         }
         printf("[Luma Upscaler] recreated: %ux%u -> %ux%u, preset %u\n", settings.render_width, settings.render_height, settings.output_width, settings.output_height,
            settings.render_preset);
         fflush(stdout);
         continue;
      }
      // See "SRBridgeProtocol.h" for why this isn't read as a stream
      std::string line;
      int reset = 0;
      if (message != "frame" || !std::getline(std::cin, line) ||
          sscanf_s(line.c_str(), "%llu %f %f %d %u %u %f %f %f %f %f %f %f", &draw.frame_index, &draw.jitter_x, &draw.jitter_y, &reset, &draw.render_width,
             &draw.render_height, &draw.pre_exposure, &draw.user_sharpness, &draw.near_plane, &draw.far_plane, &draw.vert_fov, &draw.time_delta,
             &draw.fsr_velocity_factor) != 13)
      {
         return fail_running("bad line \"" + message + "\"");
      }
      draw.reset = reset != 0;
      context4->Wait(fence_in.Get(), draw.frame_index);
      const bool drawn = impl->Draw(data, context.Get(), draw);
      context4->Signal(fence_out.Get(), draw.frame_index);
      context->Flush();
      // Exiting makes the bridge fail like an in-process upscaler, so the game falls back instead of showing the last good output
      if (!drawn)
      {
         return fail_running("frame " + std::to_string(draw.frame_index) + ": the upscaler failed");
      }
   }
   printf("[Luma Upscaler] the game closed the pipe after frame %llu\n", draw.frame_index);
   return quit(0);
}
