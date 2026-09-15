#define GAME_FAR_CRY_5 1
#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_SWAPCHAIN_FLIP_MODEL 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1
#include "../../Core/core.hpp"
#include "validation.h"
#include "perf_probe.h"
#include "upload_probe.h"
#include "test_control.h"
#include "camera_probe.h"
#include "image_probe.h"
#include "borderless_hdr.h"
#include "gpu_probe.h"
#include "reporter_control.h"

namespace FC5
{
   inline bool configured_enable = true, configured_upscale = true;
   inline bool gpu_timing = false;
   inline bool configured_suspend_reporter = false;
   constexpr uint32_t ShaderTAA = 0x902DA714, ShaderTAAScope = 0x76DF16DF;
   constexpr uint32_t ShaderSharpen = 0xF73CAD14, ShaderMotionVectorCS = 0x47C5C6CD;
   inline bool IsTemporalResolve(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      // ADS with some weapon sights uses a TAA permutation with the same
      // scene-color, motion-vector and output bindings as the main resolve.
      return hashes.Contains(ShaderTAA, reshade::api::shader_stage::pixel) ||
         hashes.Contains(ShaderTAAScope, reshade::api::shader_stage::pixel);
   }
   struct Device final : GameDeviceData
   {
      CameraProbe camera;
      DXGI_FORMAT native_output_format = DXGI_FORMAT_UNKNOWN;
      PerfCounter sr_cpu, prepare_cpu, settings_cpu, ngx_cpu, handoff_cpu;
      ThreadWorkCounter ngx_thread;
      ReporterControl reporter;
      bool reporter_startup_attempted = false;
      GPUProbe gpu;
      TestControl test_control;
      uint32_t image_wait = 0;
      bool capture_image = false;
      com_ptr<ID3D11Resource> pending_native_capture;
      uint32_t scaling_frames = 0;
      bool scaling_frame = false;
      com_ptr<ID3D11Resource> depth, encoded_motion;
      com_ptr<ID3D11Buffer> staging_cb;
      com_ptr<ID3D11Texture2D> motion, resolve, input_color, input_depth, composite;
      com_ptr<ID3D11UnorderedAccessView> motion_uav, composite_uav, input_color_uav;
      com_ptr<ID3D11ShaderResourceView> resolve_srv;
      DXGI_FORMAT depth_view_format = DXGI_FORMAT_UNKNOWN;
      uint2 size = { 0, 0 };
      uint2 output_size = { 0, 0 };
      bool upscale = false, evaluated_this_frame = false;
      com_ptr<ID3D11Resource> pending_taa_target;
      uint64_t routed = 0;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
      float4 jitter_raw = {};
      float2 jitter_scale = { 1.f, 1.f }, mv_scale = { 1.f, 1.f };
      // Explicit opt-in until jitter, color encoding and rendering are verified.
      bool enable_dlss = false, inverted_depth = true, auto_exposure = true;
      bool linear_hdr_input = false, mvs_jittered = false;
      bool jitter_valid = false, drawn_this_frame = false, probe_frame = false;
      uint32_t capture_samples = 32;
      uint64_t frame = 0, cs_count = 0, taa_count = 0, attempts = 0, successes = 0;
      const char* status = "Capture only; experimental DLAA disabled";
   };

   // Protect preparation and implicit hazard unbindings on all exit paths.
   struct ScopedState
   {
      ID3D11DeviceContext* context;
      UINT uav_count;
      DrawStateStack<DrawStateStackType::FullGraphics> graphics;
      DrawStateStack<DrawStateStackType::Compute> compute;
      ScopedState(ID3D11DeviceContext* ctx, UINT count) : context(ctx), uav_count(count)
      {
         graphics.Cache(ctx, count); compute.Cache(ctx, count);
      }
      ~ScopedState()
      {
         ID3D11ShaderResourceView* srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
         ID3D11UnorderedAccessView* uavs[D3D11_1_UAV_SLOT_COUNT] = {};
         context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
         context->CSSetUnorderedAccessViews(0, uav_count, uavs, nullptr);
         compute.Restore(context); graphics.Restore(context);
      }
   };

   bool Describe(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& desc)
   {
      com_ptr<ID3D11Texture2D> texture;
      if (!resource || FAILED(resource->QueryInterface(&texture))) return false;
      texture->GetDesc(&desc);
      return true;
   }
   void LogTexture(const char* name, ID3D11Resource* resource, uint64_t frame)
   {
      D3D11_TEXTURE2D_DESC d{};
      if (!Describe(resource, d)) return;
      char line[300];
      std::snprintf(line, sizeof(line),
         "[FC5 probe] frame=%llu %s=%p %ux%u fmt=%u mips=%u array=%u samples=%u bind=0x%X",
         frame, name, resource, d.Width, d.Height, d.Format, d.MipLevels,
         d.ArraySize, d.SampleDesc.Count, d.BindFlags);
      reshade::log::message(reshade::log::level::info, line);
   }
   void ReadTemporalCB(ID3D11Device* device, ID3D11DeviceContext* ctx, Device& fc)
   {
      fc.jitter_valid = false;
      com_ptr<ID3D11Buffer> cb;
      com_ptr<ID3D11DeviceContext1> ctx1;
      UINT first = 0, count = 4096;
      if (SUCCEEDED(ctx->QueryInterface(&ctx1)))
         ctx1->CSGetConstantBuffers1(0, 1, &cb, &first, &count);
      else ctx->CSGetConstantBuffers(0, 1, &cb);
      if (!cb) return;
      D3D11_BUFFER_DESC desc{};
      cb->GetDesc(&desc);
      const uint64_t offset = uint64_t(first) * 16;
      if (count < 11 || offset + 176 > desc.ByteWidth) return;
      if (!fc.staging_cb)
      {
         D3D11_BUFFER_DESC staging{};
         staging.ByteWidth = 176; staging.Usage = D3D11_USAGE_STAGING;
         staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         if (FAILED(device->CreateBuffer(&staging, nullptr, &fc.staging_cb))) return;
      }
      D3D11_BOX box{ UINT(offset), 0, 0, UINT(offset + 176), 1, 1 };
      ctx->CopySubresourceRegion(fc.staging_cb.get(), 0, 0, 0, 0, cb.get(), 0, &box);
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (FAILED(ctx->Map(fc.staging_cb.get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
      float values[44];
      std::memcpy(values, mapped.pData, sizeof(values));
      ctx->Unmap(fc.staging_cb.get(), 0);
      // c5 is unused garbage. Keep it in probe logs only, never in live jitter UI.
      if (fc.probe_frame)
      {
         char line[300];
         for (UINT row = 0; row < 11; ++row)
         {
            std::snprintf(line, sizeof(line), "[FC5 probe] frame=%llu temporal.c%u=%.9g,%.9g,%.9g,%.9g",
               fc.frame, row, values[row * 4], values[row * 4 + 1], values[row * 4 + 2], values[row * 4 + 3]);
            reshade::log::message(reshade::log::level::info, line);
         }
      }
   }
   void LogScalingPass(ID3D11Device* device, ID3D11DeviceContext* ctx, Device& fc, uint32_t hash, UINT output_width, UINT output_height)
   {
      char line[256];
      D3D11_VIEWPORT viewport{}; UINT count = 1; ctx->RSGetViewports(&count, &viewport);
      std::snprintf(line, sizeof(line), "[FC5 scaling] frame=%llu PS=%08X output=%ux%u viewport=(%.0f,%.0f,%.0f,%.0f) count=%u",
         fc.frame, hash, output_width, output_height, viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height, count);
      reshade::log::message(reshade::log::level::info, line);
      com_ptr<ID3D11ShaderResourceView> srvs[5]; ctx->PSGetShaderResources(0, 5, &srvs[0]);
      for (UINT i = 0; i < 5; ++i) if (srvs[i])
      {
         com_ptr<ID3D11Resource> resource; srvs[i]->GetResource(&resource);
         D3D11_SHADER_RESOURCE_VIEW_DESC v{}; srvs[i]->GetDesc(&v);
         std::snprintf(line, sizeof(line), "PS%08X.t%u.view%u", hash, i, v.Format);
         LogTexture(line, resource.get(), fc.frame);
      }
      com_ptr<ID3D11RenderTargetView> rtvs[8]; ctx->OMGetRenderTargets(8, &rtvs[0], nullptr);
      for (UINT i = 0; i < 8; ++i) if (rtvs[i])
      {
         com_ptr<ID3D11Resource> resource; rtvs[i]->GetResource(&resource);
         D3D11_RENDER_TARGET_VIEW_DESC v{}; rtvs[i]->GetDesc(&v);
         std::snprintf(line, sizeof(line), "PS%08X.rtv%u.view%u", hash, i, v.Format);
         LogTexture(line, resource.get(), fc.frame);
      }
      com_ptr<ID3D11Buffer> cb; com_ptr<ID3D11DeviceContext1> ctx1;
      UINT first = 0, constants = 4096;
      if (SUCCEEDED(ctx->QueryInterface(&ctx1))) ctx1->PSGetConstantBuffers1(0, 1, &cb, &first, &constants);
      else ctx->PSGetConstantBuffers(0, 1, &cb);
      if (!cb) return;
      D3D11_BUFFER_DESC bd{}; cb->GetDesc(&bd);
      const uint64_t offset = uint64_t(first)*16;
      if (constants < 11 || offset+176 > bd.ByteWidth) return;
      if (!fc.staging_cb)
      {
         D3D11_BUFFER_DESC s{}; s.ByteWidth=176; s.Usage=D3D11_USAGE_STAGING; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
         if (FAILED(device->CreateBuffer(&s, nullptr, &fc.staging_cb))) return;
      }
      D3D11_BOX box{UINT(offset),0,0,UINT(offset+176),1,1};
      ctx->CopySubresourceRegion(fc.staging_cb.get(),0,0,0,0,cb.get(),0,&box);
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (FAILED(ctx->Map(fc.staging_cb.get(),0,D3D11_MAP_READ,0,&mapped))) return;
      float values[44]; std::memcpy(values,mapped.pData,sizeof(values)); ctx->Unmap(fc.staging_cb.get(),0);
      for (UINT row=6; row<11; ++row)
      {
         std::snprintf(line,sizeof(line),"[FC5 scaling] frame=%llu PS=%08X cb0.c%u=%.9g,%.9g,%.9g,%.9g",
            fc.frame,hash,row,values[row*4],values[row*4+1],values[row*4+2],values[row*4+3]);
         reshade::log::message(reshade::log::level::info,line);
      }
   }
   bool Allocate(ID3D11Device* device, Device& fc, const D3D11_TEXTURE2D_DESC& color, UINT ow, UINT oh)
   {
      if (fc.motion && fc.motion_uav && fc.resolve && fc.size.x == color.Width &&
         fc.size.y == color.Height && fc.format == color.Format && fc.output_size.x == ow && fc.output_size.y == oh) return true;
      // Transactional allocation: never treat partial creation as a valid cache.
      com_ptr<ID3D11Texture2D> motion, resolve, input_color, input_depth, composite;
      com_ptr<ID3D11UnorderedAccessView> motion_uav, composite_uav, input_color_uav;
      com_ptr<ID3D11ShaderResourceView> resolve_srv;
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = color.Width; desc.Height = color.Height;
      desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
      desc.Format = DXGI_FORMAT_R16G16_FLOAT; desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &motion)) ||
         FAILED(device->CreateUnorderedAccessView(motion.get(), nullptr, &motion_uav))) return false;
      desc.Format = color.Format;
      desc.Width = ow; desc.Height = oh;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &resolve)) ||
         FAILED(device->CreateShaderResourceView(resolve.get(), nullptr, &resolve_srv)) ||
         FAILED(device->CreateTexture2D(&desc, nullptr, &composite)) ||
         FAILED(device->CreateUnorderedAccessView(composite.get(), nullptr, &composite_uav))) return false;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      if (color.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
      desc.Width = color.Width; desc.Height = color.Height;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &input_color))) return false;
      if (color.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
         FAILED(device->CreateUnorderedAccessView(input_color.get(), nullptr, &input_color_uav))) return false;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.Format = DXGI_FORMAT_R32_FLOAT;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, &input_depth))) return false;
      fc.motion = std::move(motion); fc.motion_uav = std::move(motion_uav);
      fc.resolve = std::move(resolve); fc.size = { desc.Width, desc.Height }; fc.format = color.Format;
      fc.input_color = std::move(input_color); fc.input_depth = std::move(input_depth);
      fc.input_color_uav = std::move(input_color_uav);
      fc.composite = std::move(composite); fc.composite_uav = std::move(composite_uav);
      fc.resolve_srv = std::move(resolve_srv);
      fc.output_size = { ow, oh };
      return true;
   }
}

class FarCry5 final : public Game
{
   static FC5::Device& Data(DeviceData& data) { return *static_cast<FC5::Device*>(data.game); }
public:
   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "FC5EnableDLSS", FC5::configured_enable);
      reshade::get_config_value(nullptr, NAME, "FC5MatchDisplay", FC5::configured_upscale);
      reshade::get_config_value(nullptr, NAME, "FC5SuspendReporter", FC5::configured_suspend_reporter);
      FC5::BorderlessHDR::LoadConfiguration();
      wchar_t timing[8]{};
      FC5::gpu_timing = GetEnvironmentVariableW(L"FC5_GPU_TIMING", timing, 8) == 1 && timing[0] == L'1';
   }
   void OnInit(bool async) override
   {
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Inputs"),
         ShaderDefinition{ "Luma_PrepareInputs", reshade::api::pipeline_subobject_type::compute_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("Resolve DLAA"),
         ShaderDefinition{ "Luma_ResolveDLAA", reshade::api::pipeline_subobject_type::compute_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare HDR Color"),
         ShaderDefinition{ "Luma_PrepareHDRColor", reshade::api::pipeline_subobject_type::compute_shader });
      native_shaders_definitions.emplace(CompileTimeStringHash("Resolve HDR"),
         ShaderDefinition{ "Luma_ResolveHDR", reshade::api::pipeline_subobject_type::compute_shader });
      luma_settings_cbuffer_index = luma_data_cbuffer_index = luma_ui_cbuffer_index = -1;
   }
   void OnCreateDevice(ID3D11Device* device, DeviceData& data) override
   {
      data.game = new FC5::Device;
      Data(data).enable_dlss = FC5::configured_enable;
      Data(data).upscale = FC5::configured_upscale;
      // Core initializes NGX AFTER this callback. Do not override user selection.
      reshade::log::message(reshade::log::level::info,
         "[FC5] DLSS/DLAA initialized from saved settings; invalid inputs retain native TAA.");
   }
   void OnDestroyDeviceData(DeviceData& data) override
   {
      delete static_cast<FC5::Device*>(data.game); data.game = nullptr;
   }
   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      if (swapchain->get_device()->get_api() != reshade::api::device_api::d3d11) return;
      auto* data = swapchain->get_device()->get_private_data<DeviceData>();
      DXGI_SWAP_CHAIN_DESC desc{};
      auto* native = reinterpret_cast<IDXGISwapChain*>(swapchain->get_native());
      if (data && data->game && SUCCEEDED(native->GetDesc(&desc)))
      {
         Data(*data).native_output_format = desc.BufferDesc.Format;
         data->force_reset_sr = true;
      }
   }
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* device,
      ID3D11DeviceContext* ctx, CommandListData& cmd, DeviceData& data,
      reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& hashes,
      bool custom, bool& updated, std::function<void()>* original) override
   {
      // CPU readback cannot run on deferred contexts; need replay support first.
      if (custom || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return DrawOrDispatchOverrideType::None;
      auto& fc = Data(data);
      if (fc.scaling_frames && !fc.scaling_frame &&
         (hashes.Contains(FC5::ShaderMotionVectorCS, reshade::api::shader_stage::compute) ||
          FC5::IsTemporalResolve(hashes)))
      {
         fc.scaling_frame = true; --fc.scaling_frames;
      }
      if (fc.scaling_frame)
      {
         for (const uint32_t hash : {FC5::ShaderTAA, FC5::ShaderSharpen, 0x3470C30Eu, 0x459A77B4u, 0x76DF16DFu, 0x8A10C346u})
            if (hashes.Contains(hash, reshade::api::shader_stage::pixel))
               FC5::LogScalingPass(device, ctx, fc, hash, UINT(data.output_resolution.x), UINT(data.output_resolution.y));
      }
#if ENABLE_SR
      if (fc.pending_native_capture && hashes.Contains(FC5::ShaderSharpen, reshade::api::shader_stage::pixel))
      {
         // Original TAA has already executed. Read only its exact same-frame
         // output at the verified consumer; no Release original-draw callback.
         com_ptr<ID3D11ShaderResourceView> srv;
         com_ptr<ID3D11Resource> source;
         ctx->PSGetShaderResources(0, 1, &srv);
         if (srv) srv->GetResource(&source);
         if (!fc.enable_dlss && fc.capture_image && source.get() == fc.pending_native_capture.get())
         {
            SaveProbe(device, ctx, fc, source.get(), "native-taa");
            fc.capture_image = false;
         }
         fc.pending_native_capture.reset();
      }
      if (fc.pending_taa_target && hashes.Contains(FC5::ShaderSharpen, reshade::api::shader_stage::pixel))
      {
         // Verified route: TAA RTV becomes sharpening t0; sharpening RTV is full resolution.
         // Native TAA still runs, so every rejected handoff retains a complete native image.
         com_ptr<ID3D11ShaderResourceView> srv; ctx->PSGetShaderResources(0, 1, &srv);
         com_ptr<ID3D11RenderTargetView> rtvs[8]; ctx->OMGetRenderTargets(8, &rtvs[0], nullptr);
         com_ptr<ID3D11Resource> source, target;
         if (srv) srv->GetResource(&source);
         if (rtvs[0]) rtvs[0]->GetResource(&target);
         D3D11_TEXTURE2D_DESC s{}, t{};
         D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
         D3D11_RENDER_TARGET_VIEW_DESC tv{};
         if (srv) srv->GetDesc(&sv);
         if (rtvs[0]) rtvs[0]->GetDesc(&tv);
         bool single = true;
         for (UINT i = 1; i < 8; ++i) if (rtvs[i]) single = false;
         D3D11_VIEWPORT viewport{}; UINT count = 1; ctx->RSGetViewports(&count, &viewport);
         if (single && FC5::Describe(source.get(), s) && FC5::Describe(target.get(), t) &&
            FC5::CanRouteUpscale(s, t, sv.Format, tv.Format, fc.size.x, fc.size.y,
               fc.output_size.x, fc.output_size.y, source.get() == fc.pending_taa_target.get(),
               FC5::BorderlessHDR::Enabled() && fc.native_output_format == DXGI_FORMAT_R16G16B16A16_FLOAT) &&
            sv.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && !sv.Texture2D.MostDetailedMip &&
            tv.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D && !tv.Texture2D.MipSlice &&
            count == 1 && FC5::IsFullViewport(viewport, t) && target.get() != source.get())
         {
            FC5::PerfScope handoff_timer(fc.handoff_cpu);
            FC5::ScopedState state(ctx, data.uav_max_count);
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->CopyResource(target.get(), fc.composite.get());
            if (fc.capture_image)
            {
               if (fc.image_wait) --fc.image_wait;
               else
               {
                  SaveProbe(device, ctx, fc, fc.input_color.get(), "dlss-input");
                  // Native TAA still runs during upscale: capture its completed
                  // same-frame output for exposure/quality comparisons.
                  SaveProbe(device, ctx, fc, source.get(), "native-taa-reference");
                  SaveProbe(device, ctx, fc, target.get(), "dlss-output");
                  fc.capture_image = false;
               }
            }
            fc.pending_taa_target.reset();
            fc.drawn_this_frame = data.has_drawn_sr = true; data.force_reset_sr = false;
            ++fc.routed;
            if (fc.routed == 1)
            {
               char line[200];
               std::snprintf(line, sizeof(line), "[FC5 DLSS route] first handoff %ux%u -> %ux%u at F73CAD14 frame=%llu",
                  fc.size.x, fc.size.y, fc.output_size.x, fc.output_size.y, fc.frame);
               reshade::log::message(reshade::log::level::info, line);
            }
            fc.status = "DLSS routed to full-resolution output; visual validation required";
            return DrawOrDispatchOverrideType::Replaced;
         }
         fc.status = "DLSS handoff mismatch; native upscaling retained";
      }
#endif
      if ((fc.enable_dlss || fc.capture_image || fc.capture_samples || fc.probe_frame) && !fc.evaluated_this_frame)
      {
         const bool log = fc.capture_samples || fc.probe_frame;
         fc.camera.Capture(device, ctx, hashes.vertex_shaders[0], false, fc.frame, log,
            UINT(data.output_resolution.x), UINT(data.output_resolution.y));
         if (log) fc.camera.Capture(device, ctx, hashes.pixel_shaders[0], true, fc.frame);
      }
      if (hashes.Contains(FC5::ShaderMotionVectorCS, reshade::api::shader_stage::compute))
      {
         ++fc.cs_count;
         if (fc.capture_samples && !fc.probe_frame) { fc.probe_frame = true; --fc.capture_samples; }
         fc.depth.reset(); fc.encoded_motion.reset(); fc.jitter_valid = false;
         com_ptr<ID3D11ShaderResourceView> srv;
         com_ptr<ID3D11UnorderedAccessView> uav;
         ctx->CSGetShaderResources(0, 1, &srv); ctx->CSGetUnorderedAccessViews(0, 1, &uav);
         if (srv) srv->GetResource(&fc.depth);
         fc.depth_view_format = DXGI_FORMAT_UNKNOWN;
         if (srv) { D3D11_SHADER_RESOURCE_VIEW_DESC view{}; srv->GetDesc(&view); fc.depth_view_format = view.Format; }
         if (uav) uav->GetResource(&fc.encoded_motion);
         if (fc.probe_frame) FC5::ReadTemporalCB(device, ctx, fc);
         if (fc.probe_frame)
         {
            FC5::LogTexture("depth", fc.depth.get(), fc.frame);
            FC5::LogTexture("mv-producer", fc.encoded_motion.get(), fc.frame);
         }
         return DrawOrDispatchOverrideType::None;
      }
      if (FC5::IsTemporalResolve(hashes))
      {
         ++fc.taa_count;
         if (fc.capture_samples && !fc.probe_frame) { fc.probe_frame = true; --fc.capture_samples; }
         com_ptr<ID3D11ShaderResourceView> srvs[3];
         ctx->PSGetShaderResources(0, 3, &srvs[0]);
         com_ptr<ID3D11Resource> color, motion, target;
         if (srvs[0]) srvs[0]->GetResource(&motion);
         if (srvs[2]) srvs[2]->GetResource(&color);
         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         ctx->OMGetRenderTargets(ARRAYSIZE(rtvs), &rtvs[0], nullptr);
         if (rtvs[0]) rtvs[0]->GetResource(&target);
         if (fc.probe_frame)
         {
            FC5::LogTexture("taa-color", color.get(), fc.frame);
            FC5::LogTexture("taa-mv", motion.get(), fc.frame);
            FC5::LogTexture("taa-target", target.get(), fc.frame);
         }
#if ENABLE_SR
         auto fallback = [&](const char* reason) {
            fc.status = reason; data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         };
         if (!fc.enable_dlss)
         {
            fc.status = "Native TAA (DLAA disabled)"; data.force_reset_sr = true;
            fc.pending_native_capture.reset();
            if (fc.capture_image && target && fc.camera.candidates.size())
            {
               if (fc.image_wait) --fc.image_wait;
               else
               {
                  fc.pending_native_capture = target;
               }
            }
            return DrawOrDispatchOverrideType::None;
         }
         auto* instance = data.GetSRInstanceData();
         if (data.sr_type != SR::Type::DLSS || data.sr_suppressed || !instance ||
            !sr_implementations.contains(SR::Type::DLSS)) return fallback("Select supported DLSS in Luma settings");
         if (fc.evaluated_this_frame)
         {
            fc.pending_taa_target.reset();
            return fallback("Additional TAA view: native fallback");
         }
         if (!color || !motion || !target || !fc.depth ||
            motion.get() != fc.encoded_motion.get()) return fallback("Missing current-frame input or MV producer mismatch");
         for (UINT i = 1; i < ARRAYSIZE(rtvs); ++i)
            if (rtvs[i]) return fallback("Multiple render targets unsupported");
         D3D11_TEXTURE2D_DESC c{}, m{}, d{}, t{};
         if (!FC5::Describe(color.get(), c) || !FC5::Describe(motion.get(), m) ||
            !FC5::Describe(fc.depth.get(), d) || !FC5::Describe(target.get(), t) ||
            !FC5::CanCopyWhole(c, t) || !FC5::IsSingleSurface(m) || !FC5::IsSingleSurface(d) ||
            d.Width != c.Width || d.Height != c.Height || color.get() == target.get())
            return fallback("Unsupported input dimensions/format/subresources");
         const UINT ow = fc.upscale ? UINT(data.output_resolution.x) : c.Width;
         const UINT oh = fc.upscale ? UINT(data.output_resolution.y) : c.Height;
         const auto route = FC5::SelectTemporalRoute(c.Width, c.Height, ow, oh);
         if (route == FC5::TemporalRoute::Invalid)
            return fallback("DLSS requires matching aspect and render size <= output; native retained");
         D3D11_SHADER_RESOURCE_VIEW_DESC mv_view{}, color_view{};
         srvs[0]->GetDesc(&mv_view); srvs[2]->GetDesc(&color_view);
         D3D11_RENDER_TARGET_VIEW_DESC target_view{};
         rtvs[0]->GetDesc(&target_view);
         // FC5 native scRGB tonemappers (also RenoDX) output linear BT709 nits/80.
         // This is a game-specific path, not an inference that all FLOAT is linear.
         // Native HDR10 is PQ before TAA and is not supported by this integration.
         if (fc.native_output_format == DXGI_FORMAT_R10G10B10A2_UNORM)
            return fallback("HDR10 temporal encoding unsupported; use native scRGB HDR");
         const bool hdr_input = FC5::BorderlessHDR::Enabled() &&
            fc.native_output_format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
            color_view.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
         if (fc.probe_frame || fc.attempts == 0)
         {
            char line[180];
            std::snprintf(line, sizeof(line), "[FC5 views] color=%u target=%u motion=%u depth=%u cameras=%zu",
               color_view.Format, target_view.Format, mv_view.Format, fc.depth_view_format, fc.camera.candidates.size());
            if (fc.probe_frame || fc.frame % 300 == 0) reshade::log::message(reshade::log::level::info, line);
         }
         if (mv_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            color_view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
            target_view.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
            mv_view.Texture2D.MostDetailedMip || color_view.Texture2D.MostDetailedMip ||
            target_view.Texture2D.MipSlice || color_view.Format != target_view.Format ||
            !FC5::IsTemporalColorView(c.Format, color_view.Format, hdr_input) ||
            !FC5::IsTemporalColorView(t.Format, target_view.Format, hdr_input) || mv_view.Format != DXGI_FORMAT_R8G8_UNORM ||
            fc.depth_view_format != DXGI_FORMAT_R32_FLOAT ||
            (d.Format != DXGI_FORMAT_R32_TYPELESS && d.Format != DXGI_FORMAT_R32_FLOAT))
            return fallback("Unsupported typed view or subresource");
         fc.jitter_valid = false;
         for (const auto& candidate : fc.camera.candidates)
         {
            if (candidate.width != c.Width || candidate.height != c.Height) continue;
            if (fc.jitter_valid && (std::abs(fc.jitter_raw.x - candidate.x) > 0.001f ||
               std::abs(fc.jitter_raw.y - candidate.y) > 0.001f)) return fallback("Conflicting main-camera jitter");
            fc.jitter_raw = { candidate.x, candidate.y, 0, 0 }; fc.jitter_valid = true;
         }
         if (!fc.jitter_valid) return fallback("No validated current-frame main-camera jitter");
         D3D11_VIEWPORT viewport{};
         UINT viewport_count = 1;
         ctx->RSGetViewports(&viewport_count, &viewport);
         if (viewport_count != 1 || !FC5::IsFullViewport(viewport, t)) return fallback("Partial viewport unsupported");
         auto prep = data.native_compute_shaders.find(CompileTimeStringHash("Prepare Inputs"));
         auto finish = data.native_compute_shaders.find(hdr_input ? CompileTimeStringHash("Resolve HDR") : CompileTimeStringHash("Resolve DLAA"));
         auto hdr_prepare = data.native_compute_shaders.find(CompileTimeStringHash("Prepare HDR Color"));
         if (prep == data.native_compute_shaders.end() || !prep->second) return fallback("Motion preparation shader unavailable");
         if (finish == data.native_compute_shaders.end() || !finish->second) return fallback("DLAA composite shader unavailable");
         if (hdr_input && (hdr_prepare == data.native_compute_shaders.end() || !hdr_prepare->second))
            return fallback("HDR working-color shader unavailable");
         auto typed = c; typed.Format = color_view.Format;
         const bool resized = fc.size.x != c.Width || fc.size.y != c.Height || fc.format != typed.Format ||
            fc.output_size.x != ow || fc.output_size.y != oh;
         if (!FC5::Allocate(device, fc, typed, ow, oh)) return fallback("DLSS/DLAA texture allocation failed");
         if (resized) data.force_reset_sr = true;
         const float jx = fc.jitter_raw.x * fc.jitter_scale.x, jy = fc.jitter_raw.y * fc.jitter_scale.y;
         if (!std::isfinite(jx) || !std::isfinite(jy) || std::abs(jx) > 1.f || std::abs(jy) > 1.f)
            return fallback("Jitter outside experimental pixel-space range");
         FC5::PerfScope sr_timer(fc.sr_cpu);
         FC5::GPUProbe::Scope gpu_timer(fc.gpu, device, ctx, FC5::gpu_timing && fc.frame % 16 == 0);
         auto prep_start = FC5::PerfCounter::Clock::now();
         FC5::ScopedState state(ctx, data.uav_max_count);
         if (hdr_input)
         {
            ID3D11ShaderResourceView* source = srvs[2].get();
            ID3D11UnorderedAccessView* destination = fc.input_color_uav.get();
            ctx->CSSetShader(hdr_prepare->second.get(), nullptr, 0);
            ctx->CSSetShaderResources(0, 1, &source);
            ctx->CSSetUnorderedAccessViews(0, 1, &destination, nullptr);
            ctx->Dispatch((c.Width + 7) / 8, (c.Height + 7) / 8, 1);
            ID3D11UnorderedAccessView* empty = nullptr;
            ctx->CSSetUnorderedAccessViews(0, 1, &empty, nullptr);
         }
         else ctx->CopyResource(fc.input_color.get(), color.get());
         ctx->CopyResource(fc.input_depth.get(), fc.depth.get());
         ID3D11ShaderResourceView* inputs[] = { srvs[0].get() };
         ID3D11UnorderedAccessView* outputs[] = { fc.motion_uav.get() };
         ctx->CSSetShader(prep->second.get(), nullptr, 0);
         ctx->CSSetShaderResources(0, 1, inputs); ctx->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
         ctx->Dispatch((c.Width + 7) / 8, (c.Height + 7) / 8, 1);
         ID3D11UnorderedAccessView* null_uav = nullptr;
         ID3D11ShaderResourceView* null_srv = nullptr;
         ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr); ctx->CSSetShaderResources(0, 1, &null_srv);
         fc.prepare_cpu.Add(prep_start);
         gpu_timer.Mark(1);
         SR::SettingsData settings{};
         settings.render_width = c.Width; settings.render_height = c.Height;
         settings.output_width = ow; settings.output_height = oh;
         settings.hdr = hdr_input || fc.linear_hdr_input;
         settings.inverted_depth = fc.inverted_depth; settings.mvs_jittered = fc.mvs_jittered;
         settings.mvs_x_scale = fc.mv_scale.x; settings.mvs_y_scale = fc.mv_scale.y;
         settings.auto_exposure = fc.auto_exposure; settings.render_preset = dlss_render_preset;
         auto& impl = sr_implementations.at(SR::Type::DLSS);
         bool settings_ok;
         { FC5::PerfScope timer(fc.settings_cpu); settings_ok = impl->UpdateSettings(instance, ctx, settings); }
         if (!settings_ok) return fallback("NGX feature/settings creation failed");
         SR::SuperResolutionImpl::DrawData draw{};
         draw.source_color = fc.input_color.get(); draw.output_color = fc.resolve.get();
         draw.motion_vectors = fc.motion.get(); draw.depth_buffer = fc.input_depth.get();
         draw.render_width = c.Width; draw.render_height = c.Height;
         draw.jitter_x = jx; draw.jitter_y = jy; draw.reset = data.force_reset_sr; draw.frame_index = fc.frame;
         ++fc.attempts;
         bool draw_ok;
         { FC5::PerfScope timer(fc.ngx_cpu); FC5::ThreadWorkScope thread_timer(fc.ngx_thread);
           draw_ok = impl->Draw(instance, ctx, draw); }
         gpu_timer.Mark(2);
         if (!draw_ok) return fallback("NGX evaluation failed; native TAA retained");
         fc.evaluated_this_frame = true; ++fc.successes;
         // Use the original target captured and validated BEFORE NGX.
         ctx->OMSetRenderTargets(0, nullptr, nullptr);
         inputs[0] = fc.resolve_srv.get(); outputs[0] = fc.composite_uav.get();
         ctx->CSSetShader(finish->second.get(), nullptr, 0);
         ctx->CSSetShaderResources(0, 1, inputs); ctx->CSSetUnorderedAccessViews(0, 1, outputs, nullptr);
         ctx->Dispatch((ow + 7) / 8, (oh + 7) / 8, 1);
         ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr); ctx->CSSetShaderResources(0, 1, &null_srv);
         if (route == FC5::TemporalRoute::Upscale)
         {
            fc.pending_taa_target = target;
            fc.status = "NGX upscaled; awaiting verified full-resolution handoff";
            // State is restored before original TAA. Preserve its history and fallback output.
            return DrawOrDispatchOverrideType::None;
         }
         ctx->CopyResource(target.get(), fc.composite.get());
         ++fc.routed;
         if (fc.capture_image)
         {
            if (fc.image_wait) --fc.image_wait;
            else
            {
               SaveProbe(device, ctx, fc, fc.input_color.get(), "dlaa-input");
               SaveProbe(device, ctx, fc, fc.composite.get(), "dlaa-output");
               fc.capture_image = false;
            }
         }
         fc.drawn_this_frame = data.has_drawn_sr = true; data.force_reset_sr = false;
         fc.status = "DLAA delivered at render resolution";
         return DrawOrDispatchOverrideType::Replaced;
#endif
      }
      // All unmatched passes remain native.
      return DrawOrDispatchOverrideType::None;
   }
   void OnPresent(ID3D11Device* device, DeviceData& data) override
   {
      auto& fc = Data(data);
      if (!fc.drawn_this_frame) data.force_reset_sr = true;
      if (fc.pending_taa_target) fc.status = "DLSS output not handed off; native frame retained";
      if (FC5::gpu_timing)
      {
         com_ptr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
         fc.gpu.Poll(context.get());
         if (fc.frame % 300 == 0 && fc.gpu.samples)
         {
            char line[256];
            std::snprintf(line, sizeof(line), "[FC5 GPU] cumulative n=%llu dropped=%llu invalid=%llu prepare_ms=%.4f NGX_ms=%.4f finish_ms=%.4f",
               fc.gpu.samples, fc.gpu.dropped, fc.gpu.invalid, fc.gpu.milliseconds[0] / fc.gpu.samples,
               fc.gpu.milliseconds[1] / fc.gpu.samples, fc.gpu.milliseconds[2] / fc.gpu.samples);
            reshade::log::message(reshade::log::level::info, line);
         }
      }
      if (fc.frame % 300 == 0)
      {
         char line[500];
         std::snprintf(line, sizeof(line),
            "[FC5] frame=%llu CS=%llu TAA=%llu NGX=%llu ok=%llu routed=%llu jitter=(%.9g,%.9g,%.9g,%.9g) status=%s",
            fc.frame, fc.cs_count, fc.taa_count, fc.attempts, fc.successes, fc.routed,
            fc.jitter_raw.x, fc.jitter_raw.y, fc.jitter_raw.z, fc.jitter_raw.w, fc.status);
         reshade::log::message(reshade::log::level::info, line);
         fc.camera.readback_cpu.Log("camera-copy-map", fc.frame);
         fc.camera.detection_cpu.Log("camera-detection-includes-readback", fc.frame);
         FC5::UploadProbe::Log(device, fc.frame);
         fc.sr_cpu.Log("SR-total-includes-prepare-settings-evaluate-composite-state", fc.frame);
         fc.prepare_cpu.Log("prepare-includes-state-cache", fc.frame);
         fc.settings_cpu.Log("NGX-settings", fc.frame);
         fc.ngx_cpu.Log("NGX-evaluate", fc.frame);
         fc.ngx_thread.Log(fc.frame);
         fc.handoff_cpu.Log("handoff-includes-state", fc.frame);
      }
      // Apply the saved preference once after scene temporal rendering starts,
      // not during DLL loading. Pause() verifies live code and thread identity.
      if (FC5::configured_suspend_reporter && !fc.reporter_startup_attempted && fc.taa_count >= 300)
      {
         fc.reporter_startup_attempted = true; // No recurring scan/stall on failure.
         fc.reporter.Pause();
         reshade::log::message(reshade::log::level::info, fc.reporter.Status());
      }
      fc.depth.reset(); fc.encoded_motion.reset();
      fc.pending_native_capture.reset();
      fc.pending_taa_target.reset(); fc.evaluated_this_frame = false;
      fc.jitter_valid = fc.drawn_this_frame = fc.probe_frame = false;
      fc.camera.sampled.clear();
      fc.camera.candidates.clear();
      FC5::UploadProbe::AdvanceFrame(device);
      fc.scaling_frame = false;
      if (fc.frame % 30 == 0)
      {
         const auto command = fc.test_control.Poll(FC5::ControlDirectory() / "test-control.txt");
         if (command == FC5::TestCommand::ReporterPause || command == FC5::TestCommand::ReporterResume)
         {
            if (command == FC5::TestCommand::ReporterPause) fc.reporter.Pause(); else fc.reporter.Resume();
            reshade::log::message(reshade::log::level::info, fc.reporter.Status());
         }
         else if (command != FC5::TestCommand::Invalid)
         {
            fc.enable_dlss = command != FC5::TestCommand::Native && command != FC5::TestCommand::CaptureNative && command != FC5::TestCommand::ProbeScaling;
            fc.scaling_frames = command == FC5::TestCommand::ProbeScaling ? 128 : 0;
            fc.upscale = command == FC5::TestCommand::DLSS || command == FC5::TestCommand::CaptureDLSS;
            fc.mvs_jittered = command == FC5::TestCommand::JitteredMV;
            fc.jitter_scale = command == FC5::TestCommand::FlippedJitter ? float2{-1.f,-1.f} : float2{1.f,1.f};
            fc.capture_image = command == FC5::TestCommand::CaptureNative || command == FC5::TestCommand::CaptureDLAA || command == FC5::TestCommand::CaptureDLSS;
            fc.image_wait = 32; data.force_reset_sr = true;
            fc.capture_samples = fc.capture_image ? 64 : 0;
            char line[180];
            std::snprintf(line, sizeof(line), "[FC5 test] command=%d enabled=%d mvJittered=%d jitterScale=%.0f capture=%d",
               int(command), fc.enable_dlss, fc.mvs_jittered, fc.jitter_scale.x, fc.capture_image);
            reshade::log::message(reshade::log::level::info, line);
         }
      }
      if (fc.frame % 30 == 0 && fc.camera.Requested())
      {
         fc.capture_samples = 32;
         reshade::log::message(reshade::log::level::info, "[FC5 probe] Camera capture request accepted");
      }
      ++fc.frame;
   }
   void DrawImGuiSettings(DeviceData& data) override
   {
      auto& fc = Data(data);
      ImGui::SeparatorText("Far Cry 5 DLSS / DLAA");
      ImGui::TextWrapped("Keep TAA enabled in the game and select Auto or DLSS above. Resolution scale controls quality: 100% = DLAA, lower values = DLSS upscaling.");
      bool changed = ImGui::Checkbox("Enable DLSS / DLAA", &fc.enable_dlss);
      changed |= ImGui::Checkbox("Match display resolution (DLAA at 100% game scale)", &fc.upscale);
      if (changed)
      {
         FC5::configured_enable = fc.enable_dlss; FC5::configured_upscale = fc.upscale;
         reshade::set_config_value(nullptr, NAME, "FC5EnableDLSS", fc.enable_dlss);
         reshade::set_config_value(nullptr, NAME, "FC5MatchDisplay", fc.upscale);
      }
      ImGui::SeparatorText("Borderless HDR / RenoDX");
      if (ImGui::Checkbox("Enable borderless HDR (restart required)", &FC5::BorderlessHDR::configured))
         reshade::set_config_value(nullptr, NAME, "FC5BorderlessHDR", FC5::BorderlessHDR::configured);
      ImGui::TextWrapped("Enable Windows HDR and native game scRGB HDR. Keep RenoDX installed for HDR color processing. This option keeps native HDR in a borderless window; it does not convert SDR to HDR.");
      if (FC5::BorderlessHDR::configured != FC5::BorderlessHDR::Enabled() && !FC5::BorderlessHDR::test_override)
         ImGui::TextUnformatted("Restart Far Cry 5 to apply the borderless HDR change.");
      if (FC5::BorderlessHDR::test_override)
         ImGui::TextUnformatted("Test launch flag overrides this option for this session.");
      ImGui::Text("Startup option: %s | Native output: %s", FC5::BorderlessHDR::Enabled() ? "Enabled" : "Disabled",
         fc.native_output_format == DXGI_FORMAT_R16G16B16A16_FLOAT ? "FP16 / scRGB candidate" :
         fc.native_output_format == DXGI_FORMAT_R10G10B10A2_UNORM ? "HDR10/PQ (DLSS unsupported)" : "SDR / other");
      ImGui::SeparatorText("Optional FC5 reporting thread");
      if (ImGui::Checkbox("Suspend FC5 reporting thread (remember choice)", &FC5::configured_suspend_reporter))
      {
         reshade::set_config_value(nullptr, NAME, "FC5SuspendReporter", FC5::configured_suspend_reporter);
         fc.reporter_startup_attempted = true;
         if (FC5::configured_suspend_reporter) fc.reporter.Pause(); else fc.reporter.Resume();
         reshade::log::message(reshade::log::level::info, fc.reporter.Status());
      }
      if (fc.reporter.Suspended() && ImGui::Button("Resume reporting worker now"))
      {
         FC5::configured_suspend_reporter = false;
         reshade::set_config_value(nullptr, NAME, "FC5SuspendReporter", false);
         fc.reporter_startup_attempted = true;
         fc.reporter.Resume();
      }
      ImGui::TextWrapped("Stops a verified Far Cry 5 gameplay-reporting worker until unchecked or the game exits. This may interfere with reporting or hang the game. The choice is saved; startup verifies the code and live thread again before suspending it. If identification fails, toggle off and on to retry.");
      ImGui::Text("Actual worker state: %s", fc.reporter.Suspended() ? "Suspended by Luma" : "Not suspended by Luma");
      ImGui::TextWrapped("%s", fc.reporter.Status());
#if DEVELOPMENT
      if (ImGui::TreeNode("Developer diagnostics (not saved)"))
      {
      ImGui::TextWrapped("Testing only: leave these defaults unchanged for normal play. Wrong depth, jitter or motion-vector settings can cause ghosting. Native scRGB HDR is detected automatically; the HDR override below is NOT an HDR enable switch.");
      changed |= ImGui::Checkbox("Inverted depth", &fc.inverted_depth);
      changed |= ImGui::Checkbox("Force linear HDR input (diagnostic override)", &fc.linear_hdr_input);
      changed |= ImGui::Checkbox("Auto exposure", &fc.auto_exposure);
      changed |= ImGui::Checkbox("Motion vectors include jitter", &fc.mvs_jittered);
      changed |= ImGui::DragFloat2("Jitter scale", &fc.jitter_scale.x, 0.05f, -10.f, 10.f);
      changed |= ImGui::DragFloat2("Motion vector scale", &fc.mv_scale.x, 0.05f, -10.f, 10.f);
         ImGui::TreePop();
      }
      if (changed) data.force_reset_sr = true;
      if (ImGui::Button("Log next 32 temporal samples")) fc.capture_samples = 32;
      ImGui::Text("Raw jitter %.9g %.9g %.9g %.9g", fc.jitter_raw.x, fc.jitter_raw.y, fc.jitter_raw.z, fc.jitter_raw.w);
      ImGui::Text("CS %llu | TAA %llu | NGX %llu | success %llu", fc.cs_count, fc.taa_count, fc.attempts, fc.successes);
#endif
      ImGui::Text("Input %u x %u; output %u x %u; routed %llu", fc.size.x, fc.size.y, fc.output_size.x, fc.output_size.y, fc.routed);
      ImGui::TextWrapped("%s", fc.status);
   }
   void PrintImGuiAbout() override
   {
      ImGui::TextUnformatted("Far Cry 5 DLSS / DLAA by HenDGS.");
      ImGui::TextUnformatted("Built with Luma Framework by Filippo Tarpini and contributors.");
      ImGui::TextUnformatted("Far Cry 5 shader reference: Musa Haji / RenoDX.");
   }
private:
   static void SaveProbe(ID3D11Device* device, ID3D11DeviceContext* ctx, FC5::Device& fc,
      ID3D11Resource* resource, const char* label)
   {
      char filename[160];
      std::snprintf(filename, sizeof(filename), "%s-%lu-%llu.bmp", label, GetCurrentProcessId(), fc.frame);
      D3D11_TEXTURE2D_DESC desc{};
      if (FC5::Describe(resource, desc) && (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS))
         std::snprintf(filename, sizeof(filename), "%s-%lu-%llu-%ux%u.rgba16f", label, GetCurrentProcessId(), fc.frame, desc.Width, desc.Height);
      const auto path = FC5::ControlDirectory() / filename;
      const bool ok = FC5::SaveImage(device, ctx, resource, path);
      char line[240]; std::snprintf(line, sizeof(line), "[FC5 image] %s %s", ok ? "saved" : "FAILED", filename);
      reshade::log::message(reshade::log::level::info, line);
   }
};

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
   if (reason == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals("Far Cry 5", "DLSS, DLAA and borderless scRGB HDR", "", 1);
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      force_disable_display_composition = true;
      prevent_fullscreen_state = false; force_borderless = false;
      enable_samplers_upgrade = false;
#if DEVELOPMENT
      forced_shader_names.emplace(FC5::ShaderTAA, "FC5 Temporal Resolve");
      forced_shader_names.emplace(FC5::ShaderTAAScope, "FC5 Temporal Resolve (ADS)");
      forced_shader_names.emplace(FC5::ShaderSharpen, "FC5 Temporal Sharpen");
      forced_shader_names.emplace(FC5::ShaderMotionVectorCS, "FC5 Motion Vectors Compute");
#endif
      game = new FarCry5;
   }
   if (reason == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::create_swapchain>(FC5::BorderlessHDR::OnCreate);
      reshade::unregister_event<reshade::addon_event::init_swapchain>(FC5::BorderlessHDR::OnInit);
      reshade::unregister_event<reshade::addon_event::init_pipeline>(FC5::CameraProbe::OnPipeline);
      FC5::UploadProbe::Unregister(reserved != nullptr);
   }
   const BOOL result = CoreMain(module, reason, reserved);
   if (reason == DLL_PROCESS_ATTACH && result)
   {
      reshade::register_event<reshade::addon_event::create_swapchain>(FC5::BorderlessHDR::OnCreate);
      reshade::register_event<reshade::addon_event::init_swapchain>(FC5::BorderlessHDR::OnInit);
      FC5::UploadProbe::Register();
      reshade::register_event<reshade::addon_event::init_pipeline>(FC5::CameraProbe::OnPipeline);
   }
   return result;
}
