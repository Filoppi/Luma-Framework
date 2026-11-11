#include "SRBridge.h"

#if ENABLE_SR_BRIDGE

#include "SRBridgeProtocol.h"
#include "DepthCopyCS.h"
#include "../utils/system.h"

#include <include/reshade.hpp>

#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace SRBridge
{
   using Microsoft::WRL::ComPtr;
   using namespace SRBridgeProtocol;

   namespace
   {
      void Log(reshade::log::level level, const std::string& message)
      {
         reshade::log::message(level, ("[SR Bridge] " + message).c_str());
      }

      // The protocol's "<settings>"
      std::string FormatSettings(const SR::SettingsData& settings)
      {
         return std::format("{} {} {} {} {} {} {} {} {} {} {} {}", settings.render_width, settings.render_height,
            settings.output_width, settings.output_height, int(settings.hdr), int(settings.inverted_depth),
            int(settings.mvs_jittered), int(settings.auto_exposure), int(settings.dynamic_resolution),
            settings.mvs_x_scale, settings.mvs_y_scale, settings.render_preset);
      }

      // The format a depth texture is read with, UNKNOWN if it isn't a depth format
      DXGI_FORMAT GetDepthReadFormat(DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_R24G8_TYPELESS:
         case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
         case DXGI_FORMAT_R32G8X24_TYPELESS:
         case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
         case DXGI_FORMAT_R32_TYPELESS:
         case DXGI_FORMAT_D32_FLOAT:
            return DXGI_FORMAT_R32_FLOAT;
         case DXGI_FORMAT_R16_TYPELESS:
         case DXGI_FORMAT_D16_UNORM:
            return DXGI_FORMAT_R16_UNORM;
         default:
            return DXGI_FORMAT_UNKNOWN;
         }
      }

      constexpr const char* RESOURCE_NAMES[RESOURCE_COUNT] = {"color", "motion vectors", "depth", "bias mask", "transparency", "exposure", "output"};
   } // namespace

   // The helper works on the game's textures when they can be used as they are (zero copy), else on copies made each frame.
   // See "SRBridgeProtocol.h" for the fences.
   struct BridgeInstanceData : public SR::InstanceData
   {
      // "ReleaseResources()" comes from the overlay, possibly during a "Draw()" on another thread
      static inline std::shared_mutex mutex;
      // One helper at a time, see "Start()"
      static inline BridgeInstanceData* running_instance = nullptr;

      SR::Type type = SR::Type::None;
      LUID luid = {};

      // The running helper
      HANDLE job = nullptr; // Ends the helper with the game
      HANDLE process = nullptr;
      HANDLE pipe = nullptr;         // Its stdin
      bool settings_pending = false; // Changed since the helper started: sent before the next frame
      bool failed = false;

      ComPtr<ID3D11Device5> device;
      ComPtr<ID3D11DeviceContext4> context;
      ComPtr<ID3D11Fence> fence_in;
      ComPtr<ID3D11Fence> fence_out;
      // The game's resources, only compared. Referenced so a recreated one can't reuse the address.
      ComPtr<ID3D11Resource> sources[RESOURCE_COUNT];
      ComPtr<ID3D11Texture2D> shared[RESOURCE_COUNT]; // What the helper opens
      bool copied[RESOURCE_COUNT] = {};
      DXGI_FORMAT source_formats[RESOURCE_COUNT] = {}; // Of the game's textures
      uint64_t frame = 0;                              // 0 until the upscaler is ready, then the last frame sent (from READY_FENCE_VALUE)

      // Depth formats can't be NT-handle shared: such a depth is copied into an R32_FLOAT texture by "DEPTH_COPY_CS"
      ComPtr<ID3D11ComputeShader> depth_copy_cs;
      ComPtr<ID3D11UnorderedAccessView> depth_copy_uav;
      ComPtr<ID3D11ShaderResourceView> depth_copy_srv; // Of "sources[RESOURCE_DEPTH]"

      ~BridgeInstanceData()
      {
         Stop();
      }

      void Stop()
      {
         if (pipe)
         {
            CloseHandle(pipe); // EOF: the helper ends by itself
         }
         if (process)
         {
            WaitForSingleObject(process, 500);
            CloseHandle(process);
         }
         if (job)
         {
            CloseHandle(job); // Ends it if it's still running
         }
         job = process = pipe = nullptr;
         fence_in.Reset();
         fence_out.Reset();
         for (int i = 0; i < RESOURCE_COUNT; i++)
         {
            sources[i].Reset();
            shared[i].Reset();
            copied[i] = false;
            source_formats[i] = DXGI_FORMAT_UNKNOWN;
         }
         depth_copy_uav.Reset();
         depth_copy_srv.Reset();
         frame = 0;
         settings_pending = false;
         if (running_instance == this)
         {
            running_instance = nullptr;
         }
      }

      bool Fail(const std::string& reason)
      {
         Stop();
         failed = true;
         Log(reshade::log::level::warning, reason);
         return false;
      }

      bool Send(const std::string& line)
      {
         DWORD written = 0;
         if (WriteFile(pipe, line.data(), DWORD(line.size()), &written, nullptr))
         {
            return true;
         }
         return Fail("the helper's pipe broke");
      }

      // Shares the textures (or copies of them) and creates the fences. "handles" has RESOURCE_COUNT + 2 entries, the textures' (0: none)
      // then the fences'. The caller closes them, also after an error. Returns the error, empty if fine.
      std::string CreateSharedResources(ID3D11DeviceContext* command_list, ID3D11Resource* const* resources, HANDLE* handles)
      {
         if (FAILED(command_list->QueryInterface(IID_PPV_ARGS(&context))))
         {
            return "no D3D11 fences on this device context";
         }
         if (!resources[RESOURCE_COLOR] || !resources[RESOURCE_MOTION] || !resources[RESOURCE_DEPTH] || !resources[RESOURCE_OUTPUT])
         {
            return "missing color, motion vectors, depth or output";
         }
         for (int i = 0; i < RESOURCE_COUNT; i++)
         {
            sources[i] = resources[i];
            if (!resources[i])
            {
               continue;
            }
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(resources[i]->QueryInterface(IID_PPV_ARGS(&texture))))
            {
               return std::format("resource {} isn't a 2D texture", i);
            }
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            // The helper binds the output as a UAV and the inputs as SRVs: a shared texture without that is copied too
            const UINT required_bind = (i == RESOURCE_OUTPUT ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_SHADER_RESOURCE);
            copied[i] = (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) == 0 || (desc.BindFlags & required_bind) == 0;
            source_formats[i] = desc.Format;
            if (copied[i])
            {
               const bool depth_copy = i == RESOURCE_DEPTH && GetDepthReadFormat(desc.Format) != DXGI_FORMAT_UNKNOWN;
               desc.Usage = D3D11_USAGE_DEFAULT;
               desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | ((i == RESOURCE_OUTPUT || depth_copy) ? D3D11_BIND_UNORDERED_ACCESS : 0);
               desc.CPUAccessFlags = 0;
               desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
               if (depth_copy)
               {
                  desc.Format = DXGI_FORMAT_R32_FLOAT;
                  desc.MipLevels = 1;
                  desc.ArraySize = 1;
               }
               if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)))
               {
                  return std::format("resource {} (format {}) can't be shared", i, int(desc.Format));
               }
               if (depth_copy && ((!depth_copy_cs && FAILED(device->CreateComputeShader(DEPTH_COPY_CS, sizeof(DEPTH_COPY_CS), nullptr, &depth_copy_cs))) ||
                                    FAILED(device->CreateUnorderedAccessView(texture.Get(), nullptr, &depth_copy_uav))))
               {
                  return std::format("the depth (format {}) can't be copied", int(source_formats[i]));
               }
            }
            shared[i] = texture;
            ComPtr<IDXGIResource1> dxgi_resource;
            if (FAILED(texture.As(&dxgi_resource)) ||
                FAILED(dxgi_resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handles[i])))
            {
               return std::format("resource {} has no shared handle", i);
            }
         }
         const auto create_fence = [&](ComPtr<ID3D11Fence>* fence, HANDLE* handle)
         {
            return SUCCEEDED(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(fence->GetAddressOf()))) &&
                   SUCCEEDED((*fence)->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, handle));
         };
         if (!create_fence(&fence_in, &handles[RESOURCE_COUNT]) || !create_fence(&fence_out, &handles[RESOURCE_COUNT + 1]))
         {
            return "no shared fences";
         }
         return {};
      }

      // The first input that needs a restart, -1 if none. A copied input can come from another texture of the same description
      // (a game that alternates two targets for one pass): only the source changes.
      int FindNewInput(ID3D11Resource* const* resources)
      {
         for (int i = 0; i < RESOURCE_COUNT; i++)
         {
            if (resources[i] == sources[i].Get())
            {
               continue;
            }
            ComPtr<ID3D11Texture2D> texture;
            if (i == RESOURCE_OUTPUT || !copied[i] || !resources[i] || FAILED(resources[i]->QueryInterface(IID_PPV_ARGS(&texture))))
            {
               return i;
            }
            D3D11_TEXTURE2D_DESC desc, shared_desc;
            texture->GetDesc(&desc);
            shared[i]->GetDesc(&shared_desc);
            if (desc.Format != source_formats[i] || desc.Width != shared_desc.Width || desc.Height != shared_desc.Height ||
                desc.ArraySize != shared_desc.ArraySize || desc.MipLevels != shared_desc.MipLevels ||
                desc.SampleDesc.Count != shared_desc.SampleDesc.Count)
            {
               return i;
            }
            sources[i] = resources[i];
            if (i == RESOURCE_DEPTH)
            {
               depth_copy_srv.Reset();
            }
         }
         return -1;
      }

      // The color as it is, at the top left if it's smaller. Nothing is copied if the formats differ: reporting a failure would make the
      // game stop drawing the upscaler for good.
      void CopyColorToOutput(ID3D11DeviceContext* command_list, const SR::SuperResolutionImpl::DrawData& draw_data) const
      {
         ComPtr<ID3D11Texture2D> source, output;
         if (SUCCEEDED(draw_data.source_color->QueryInterface(IID_PPV_ARGS(&source))) &&
             SUCCEEDED(draw_data.output_color->QueryInterface(IID_PPV_ARGS(&output))))
         {
            D3D11_TEXTURE2D_DESC source_desc, output_desc;
            source->GetDesc(&source_desc);
            output->GetDesc(&output_desc);
            const D3D11_BOX box = {
               .left = 0,
               .top = 0,
               .front = 0,
               .right = (std::min)(source_desc.Width, output_desc.Width),
               .bottom = (std::min)(source_desc.Height, output_desc.Height),
               .back = 1};
            if (source_desc.Format == output_desc.Format)
            {
               command_list->CopySubresourceRegion(output.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
            }
         }
      }

      // Returns the frame number signaled on "in", 0 if the depth can't be read
      uint64_t Submit(ID3D11DeviceContext* command_list, ID3D11Resource* const* resources, UINT render_width, UINT render_height)
      {
         for (int i = 0; i < RESOURCE_COUNT; i++)
         {
            if (i == RESOURCE_OUTPUT || !copied[i])
            {
               continue;
            }
            // The upscaler only reads the render area (a render scale draws into the top left of larger targets)
            D3D11_TEXTURE2D_DESC desc;
            shared[i]->GetDesc(&desc);
            const UINT width = (std::min)(render_width, desc.Width), height = (std::min)(render_height, desc.Height);
            if (i == RESOURCE_DEPTH && depth_copy_uav)
            {
               if (!CopyDepth(command_list, resources[i], width, height))
               {
                  return 0;
               }
               continue;
            }
            const D3D11_BOX render_area = {.left = 0, .top = 0, .front = 0, .right = width, .bottom = height, .back = 1};
            command_list->CopySubresourceRegion(shared[i].Get(), 0, 0, 0, 0, resources[i], 0, &render_area);
         }
         const uint64_t n = ++frame;
         context->Signal(fence_in.Get(), n);
         command_list->Flush();
         return n;
      }

      // Copies the render area into the shared R32_FLOAT texture, restoring the compute state it touches. False if the depth can't be read.
      bool CopyDepth(ID3D11DeviceContext* command_list, ID3D11Resource* source, UINT width, UINT height)
      {
         if (!depth_copy_srv)
         {
            const D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
               .Format = GetDepthReadFormat(source_formats[RESOURCE_DEPTH]),
               .ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
               .Texture2D = {.MostDetailedMip = 0, .MipLevels = 1}};
            if (FAILED(device->CreateShaderResourceView(source, &srv_desc, &depth_copy_srv)))
            {
               return false;
            }
         }

         ComPtr<ID3D11ComputeShader> previous_cs;
         command_list->CSGetShader(&previous_cs, nullptr, nullptr);
         ComPtr<ID3D11ShaderResourceView> previous_srv;
         command_list->CSGetShaderResources(0, 1, &previous_srv);
         ComPtr<ID3D11UnorderedAccessView> previous_uav;
         command_list->CSGetUnorderedAccessViews(0, 1, &previous_uav);

         command_list->CSSetShader(depth_copy_cs.Get(), nullptr, 0);
         command_list->CSSetShaderResources(0, 1, depth_copy_srv.GetAddressOf());
         command_list->CSSetUnorderedAccessViews(0, 1, depth_copy_uav.GetAddressOf(), nullptr);
         command_list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

         command_list->CSSetShader(previous_cs.Get(), nullptr, 0);
         command_list->CSSetShaderResources(0, 1, previous_srv.GetAddressOf());
         command_list->CSSetUnorderedAccessViews(0, 1, previous_uav.GetAddressOf(), nullptr);
         return true;
      }

      bool Start(ID3D11DeviceContext* command_list, ID3D11Resource* const* resources)
      {
         if (running_instance && running_instance != this)
         {
            running_instance->Stop();
         }
         Stop();
         HANDLE handles[RESOURCE_COUNT + 2] = {};
         const auto fail = [&](const std::string& reason)
         {
            for (HANDLE handle : handles)
            {
               if (handle)
               {
                  CloseHandle(handle);
               }
            }
            return Fail(reason);
         };
         if (const std::string error = CreateSharedResources(command_list, resources, handles); !error.empty())
            return fail(error);

         job = CreateJobObjectW(nullptr, nullptr);
         JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {.BasicLimitInformation = {.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE}};
         if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            return fail("no job object");

         // The helper next to the exe: its stdin a pipe, its output the log file there
         const std::filesystem::path directory = System::GetModulePath().parent_path();
         const std::filesystem::path helper_path = directory / HELPER_NAME;
         SECURITY_ATTRIBUTES inherit = {.nLength = sizeof(inherit), .lpSecurityDescriptor = nullptr, .bInheritHandle = TRUE};
         HANDLE pipe_read = nullptr;
         const HANDLE log_file = CreateFileW((directory / LOG_NAME).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS, 0, nullptr);
         PROCESS_INFORMATION process_info = {};
         bool started = false;
         if (log_file != INVALID_HANDLE_VALUE && CreatePipe(&pipe_read, &pipe, &inherit, 0))
         {
            SetHandleInformation(pipe, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOW startup = {.cb = sizeof(startup), .dwFlags = STARTF_USESTDHANDLES, .hStdInput = pipe_read, .hStdOutput = log_file, .hStdError = log_file};
            std::wstring command = L"\"" + helper_path.wstring() + L"\"";
            // Suspended until it's in the job, so it can't outlive the game even if it's killed right away
            started = CreateProcessW(helper_path.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, directory.c_str(), &startup, &process_info);
            if (started)
            {
               process = process_info.hProcess;
               started = AssignProcessToJobObject(job, process) && ResumeThread(process_info.hThread) != DWORD(-1);
               CloseHandle(process_info.hThread);
            }
         }
         const DWORD error = GetLastError();
         if (!started && process)
         {
            TerminateProcess(process, 0); // Created, but it didn't start in the job
         }
         for (HANDLE handle : {pipe_read, log_file})
         {
            if (handle && handle != INVALID_HANDLE_VALUE)
            {
               CloseHandle(handle);
            }
         }
         if (!started)
            return fail(std::format("the helper couldn't start (error {}): {}", error, helper_path.string()));

         std::string message = std::format("{} {} {} {} {}", PROTOCOL_VERSION, int(type), luid.LowPart, luid.HighPart, FormatSettings(settings_data));
         for (HANDLE handle : handles)
         {
            HANDLE remote = nullptr;
            if (handle)
            {
               DuplicateHandle(GetCurrentProcess(), handle, process, &remote, 0, FALSE, DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE);
            }
            message += std::format(" {}", uint64_t(uintptr_t(remote)));
         }
         message += "\n";
         if (!Send(message))
            return false;
         running_instance = this;
         Log(reshade::log::level::info, std::format("started the helper for {} ({}x{} -> {}x{})", SR::GetTypeName(type), settings_data.render_width,
                                           settings_data.render_height, settings_data.output_width, settings_data.output_height));
         return true;
      }
   };

   bool Bridge::HasInit(const SR::InstanceData* data) const
   {
      return data != nullptr;
   }

   bool Bridge::IsSupported(const SR::InstanceData* data) const
   {
      return data && data->is_supported;
   }

   // No process yet (Core initializes every type at device creation): it's supported if the helper and the upscaler's dlls are next to
   // the exe, DLSS also needs an NVIDIA GPU.
   bool Bridge::Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter)
   {
      if (data)
      {
         Deinit(data);
      }
      auto* const custom_data = new BridgeInstanceData();
      data = custom_data;
      custom_data->type = type;
      custom_data->min_resolution = 32;
      custom_data->requires_unordered_access_output_texture = false; // A game output without a UAV is copied
      if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&custom_data->device))))
         return false;
      DXGI_ADAPTER_DESC adapter_desc = {};
      if (!adapter || FAILED(adapter->GetDesc(&adapter_desc)))
         return false;
      custom_data->luid = adapter_desc.AdapterLuid;
      const std::filesystem::path directory = System::GetModulePath().parent_path();
      const auto file_present = [&](const wchar_t* name)
      {
         std::error_code error;
         return std::filesystem::exists(directory / name, error);
      };
      bool files_present = false;
      switch (type)
      {
      case SR::Type::DLSS:
         files_present = adapter_desc.VendorId == 0x10DE && file_present(L"nvngx_dlss.dll");
         break;
      case SR::Type::FSR:
         files_present = true; // Linked into the helper
         break;
      case SR::Type::FSR_4:
         custom_data->auto_selectable = adapter_desc.VendorId == 0x1002;
         files_present = file_present(L"amd_fidelityfx_loader_dx12.dll") && file_present(L"amd_fidelityfx_upscaler_dx12.dll");
         break;
      case SR::Type::XeSS:
         custom_data->auto_selectable = adapter_desc.VendorId == 0x8086;
         files_present = file_present(L"libxess.dll");
         break;
      }
      custom_data->is_supported = files_present && file_present(HELPER_NAME);
      return custom_data->is_supported;
   }

   void Bridge::Deinit(SR::InstanceData*& data, ID3D11Device* optional_device)
   {
      delete static_cast<BridgeInstanceData*>(data);
      data = nullptr;
   }

   void Bridge::ReleaseResources(SR::InstanceData* data)
   {
      if (auto* const bridge = static_cast<BridgeInstanceData*>(data))
      {
         const std::unique_lock lock(BridgeInstanceData::mutex);
         bridge->Stop();
         // A new pick starts over after a failure (games stop drawing a failed upscaler until it's picked again)
         bridge->failed = false;
      }
   }

   // The helper starts on the next draw with these, or a running one recreates the upscaler with them
   bool Bridge::UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data)
   {
      auto* const custom_data = static_cast<BridgeInstanceData*>(data);
      if (!custom_data || !custom_data->is_supported || !command_list)
         return false;
      if (settings_data == custom_data->settings_data)
         return !custom_data->failed;
      custom_data->settings_data = settings_data;
      custom_data->settings_pending = custom_data->process != nullptr;
      custom_data->failed = false;
      return true;
   }

   bool Bridge::Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data)
   {
      const std::unique_lock lock(BridgeInstanceData::mutex);
      auto& custom_data = *const_cast<BridgeInstanceData*>(static_cast<const BridgeInstanceData*>(data));
      // Until picked again ("ReleaseResources") or new settings
      if (custom_data.failed)
         return false;
      ID3D11Resource* const resources[RESOURCE_COUNT] = {draw_data.source_color, draw_data.motion_vectors, draw_data.depth_buffer, draw_data.bias_mask,
         draw_data.transparency_alpha, draw_data.exposure, draw_data.output_color};
      if (custom_data.process)
      {
         // A dead process's fences read UINT64_MAX, so no GPU wait is left hanging
         if (WaitForSingleObject(custom_data.process, 0) == WAIT_OBJECT_0 || custom_data.fence_out->GetCompletedValue() == UINT64_MAX)
            return custom_data.Fail(std::format("the helper exited (see {} next to the game's exe)", std::filesystem::path(LOG_NAME).string()));
         if (const int changed = custom_data.FindNewInput(resources); changed >= 0)
         {
            // The helper restarts on it. Named so that a game alternating an input shows up as the cause of a restart loop.
            Log(reshade::log::level::info, std::format("restarting the helper: a new {} texture", RESOURCE_NAMES[changed]));
            custom_data.Stop();
         }
      }
      if (!custom_data.process && !custom_data.Start(command_list, resources))
         return false;

      if (custom_data.frame == 0)
      {
         if (custom_data.fence_out->GetCompletedValue() < READY_FENCE_VALUE)
         {
            // Still creating the upscaler
            custom_data.CopyColorToOutput(command_list, draw_data);
            return true;
         }
         custom_data.frame = READY_FENCE_VALUE;
         Log(reshade::log::level::info, std::format("running {}", SR::GetTypeName(custom_data.type)));
      }
      if (custom_data.settings_pending)
      {
         // Sent once ready, so changes made while it's starting become one
         if (!custom_data.Send("settings " + FormatSettings(custom_data.settings_data) + "\n"))
            return false;
         custom_data.settings_pending = false;
         Log(reshade::log::level::info, "new settings for the helper: " + FormatSettings(custom_data.settings_data));
      }

      const UINT render_width = (draw_data.render_width ? draw_data.render_width : custom_data.settings_data.render_width);
      const UINT render_height = (draw_data.render_height ? draw_data.render_height : custom_data.settings_data.render_height);
      const uint64_t frame = custom_data.Submit(command_list, resources, render_width, render_height);
      if (frame == 0)
      {
         return custom_data.Fail("the depth can't be read");
      }
      if (!custom_data.Send(std::format("frame {} {} {} {} {} {} {} {} {} {} {} {} {}\n", frame, draw_data.jitter_x, draw_data.jitter_y, draw_data.reset ? 1 : 0,
             render_width, render_height, draw_data.pre_exposure, draw_data.user_sharpness, draw_data.near_plane, draw_data.far_plane, draw_data.vert_fov,
             draw_data.time_delta, draw_data.fsr_velocity_factor)))
         return false;
      custom_data.context->Wait(custom_data.fence_out.Get(), frame);
      if (custom_data.copied[RESOURCE_OUTPUT])
      {
         command_list->CopyResource(draw_data.output_color, custom_data.shared[RESOURCE_OUTPUT].Get());
      }
      return true;
   }

   bool Bridge::IsReady(const SR::InstanceData* data) const
   {
      return data && static_cast<const BridgeInstanceData*>(data)->frame != 0;
   }

   // Each upscaler's own formula, as they aren't loaded in this process
   int Bridge::GetJitterPhases(const SR::InstanceData* data) const
   {
      if (!data || data->settings_data.render_width == 0)
         return SR::GetDefaultJitterPhases();
      const SR::SettingsData& settings = data->settings_data;
      const float base_phases = float(SR::GetDefaultJitterPhases());
      const float width_scale = float(settings.output_width) / float(settings.render_width);
      switch (type)
      {
      case SR::Type::DLSS:
         return std::lrintf(base_phases * std::pow((std::max)(float(settings.output_height) / float(settings.render_height), 1.f), 2.f));
      case SR::Type::XeSS:
         return int(std::ceil(base_phases * width_scale * width_scale));
      default:
         return int(base_phases * std::pow(width_scale, 2.f)); // "ffxFsr3GetJitterPhaseCount()", FSR 4 returns the same
      }
   }
} // namespace SRBridge

#endif
