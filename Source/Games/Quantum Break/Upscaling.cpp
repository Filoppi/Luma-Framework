// Intentionally compiled through main.cpp: Core's anonymous-namespace state must stay unique.
#include "shared.h"
#include "Upscaling.hpp"
#if ENABLE_SR
#include "NativeHooks.hpp"
#endif

namespace QuantumBreakUpscaling
{
   namespace
   {
      constexpr uint32_t hash_msaa_gbuffer_reconstruction = 0x037FCE62u;
      constexpr uint32_t hash_msaa_gbuffer_depth_resolve = 0x84DD3C0Cu;
      constexpr uint32_t dlss_scene_warmup_resolve_count = 120u;
      // Metadata safety thresholds retained from the tested layouts; no bytes are captured.
      constexpr uint32_t frame_constants_min_size = 1944u;
      constexpr uint32_t ssaa_constants_min_size = 200u;
      constexpr float fsr_sharpness = 0.f;
      constexpr float far_plane = 1000.f;

      bool HasTextureShapeChanged(const D3D11_TEXTURE2D_DESC& current, const D3D11_TEXTURE2D_DESC& previous)
      {
         return current.Width != previous.Width || current.Height != previous.Height || current.Format != previous.Format ||
                current.ArraySize != previous.ArraySize || current.MipLevels != previous.MipLevels ||
                current.SampleDesc.Count != previous.SampleDesc.Count || current.SampleDesc.Quality != previous.SampleDesc.Quality;
      }

      bool UpdatePreviousTextureDesc(const D3D11_TEXTURE2D_DESC& current, D3D11_TEXTURE2D_DESC& previous, bool& has_previous)
      {
         const bool changed = has_previous && HasTextureShapeChanged(current, previous);
         previous = current;
         has_previous = true;
         return changed;
      }

      void SetRenderData(uint32_t render_width, uint32_t render_height, uint32_t output_width, uint32_t output_height,
         float jitter_x, float jitter_y, DeviceData& device_data)
      {
         const float rw = static_cast<float>(render_width);
         const float rh = static_cast<float>(render_height);
         const float ow = static_cast<float>(output_width);
         const float oh = static_cast<float>(output_height);
         auto& settings = cb_luma_global_settings.GameSettings;
         settings.RenderRes = float2{rw, rh};
         settings.InvRenderRes = float2{rw > 0.f ? 1.f / rw : 0.f, rh > 0.f ? 1.f / rh : 0.f};
         settings.OutputRes = float2{ow, oh};
         settings.InvOutputRes = float2{ow > 0.f ? 1.f / ow : 0.f, oh > 0.f ? 1.f / oh : 0.f};
         const float render_scale = oh > 0.f ? rh / oh : 1.f;
         settings.RenderScale = render_scale;
         settings.InvRenderScale = render_scale != 0.f ? 1.f / render_scale : 1.f;
         settings.JitterOffset = float2{jitter_x, jitter_y};
         device_data.cb_luma_global_settings_dirty = true;
      }

#if ENABLE_SR
      bool TryGetTextureDesc(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& desc)
      {
         if (resource == nullptr) return false;
         com_ptr<ID3D11Texture2D> texture;
         if (FAILED(resource->QueryInterface(&texture)) || texture.get() == nullptr) return false;
         texture->GetDesc(&desc);
         return true;
      }
#endif // ENABLE_SR

#if ENABLE_SR && (DEVELOPMENT || TEST)
      void RecordSelection(NativeInputUsage& usage, NativeInputFallback reason) noexcept
      {
         usage.selections[static_cast<size_t>(reason)].fetch_add(1u, std::memory_order_relaxed);
      }

      void RecordInputGate(Data& data, NativeInputFallback native_reason, bool frame_binding, bool temporal_binding,
         TemporalInputAction action, SR::Type sr_type)
      {
         auto& usage = data.native_input_usage;
         const uint32_t bindings = (frame_binding ? 1u : 0u) | (temporal_binding ? 2u : 0u);
         usage.binding_states[bindings].fetch_add(1u, std::memory_order_relaxed);
         if (bindings != 3u && native_reason == NativeInputFallback::None)
         {
            usage.native_missing_bindings.fetch_add(1u, std::memory_order_relaxed);
         }
         if (action == TemporalInputAction::NativeTAA)
         {
            usage.taa_resolves.fetch_add(1u, std::memory_order_relaxed);
         }
         const bool skip = action == TemporalInputAction::Skip;
         const uint64_t skipped = skip ? usage.transition_skips.fetch_add(1u, std::memory_order_relaxed) : 0u;
         const auto bit = static_cast<uint8_t>(1u << bindings);
         const bool first = (usage.reported_gates[static_cast<size_t>(native_reason)].fetch_or(bit, std::memory_order_relaxed) & bit) == 0u;
         // One example per native reason/binding state, plus the first three actual transition skips.
         if (!first && (!skip || skipped >= 3u)) return;
         char message[512];
         constexpr const char* action_names[] = {"native_sr", "game_taa", "skip"};
         std::snprintf(message, sizeof(message), "QB native inputs: INPUT_GATE native=%s CB0_layout=%u CB1_layout=%u action=%s sr=%u thread=%lu",
            NativeInputFallbackText(native_reason), static_cast<unsigned>(frame_binding), static_cast<unsigned>(temporal_binding),
            action_names[static_cast<size_t>(action)], static_cast<unsigned>(sr_type), GetCurrentThreadId());
         reshade::log::message(reshade::log::level::info, message);
      }

      void RecordDraw(NativeInputUsage& usage, bool success) noexcept
      {
         usage.native_draws.fetch_add(1u, std::memory_order_relaxed);
         usage.native_successes.fetch_add(success ? 1u : 0u, std::memory_order_relaxed);
      }

      void WriteUsage(Data& data, const char* reason, bool force = false)
      {
         auto& usage = data.native_input_usage;
         const uint64_t now = GetTickCount64();
         if (!force && now - usage.last_report_at.load(std::memory_order_relaxed) < 5000u) return;
         // Throttle collection too, even while menus leave the selection counts unchanged.
         usage.last_report_at.store(now, std::memory_order_relaxed);
         std::array<uint64_t, static_cast<size_t>(NativeInputFallback::Count)> counts;
         uint64_t total = 0u;
         for (size_t i = 0u; i < counts.size(); ++i)
         {
            counts[i] = usage.selections[i].load(std::memory_order_relaxed);
            total += counts[i];
         }
         if (!force && total == usage.last_total.load(std::memory_order_relaxed)) return;
         usage.last_total.store(total, std::memory_order_relaxed);
         char message[384];
         std::snprintf(message, sizeof(message), "QB native inputs: INPUT_USAGE reason=%s native_selected=%llu game_taa_resolves=%llu skipped_resolves=%llu native_sr_draws=%llu native_sr_successes=%llu",
            reason, counts[0], usage.taa_resolves.load(std::memory_order_relaxed), usage.transition_skips.load(std::memory_order_relaxed),
            usage.native_draws.load(std::memory_order_relaxed), usage.native_successes.load(std::memory_order_relaxed));
         reshade::log::message(reshade::log::level::info, message);
         std::snprintf(message, sizeof(message), "QB native inputs: GATE_TOTAL missing_both=%llu CB0_only=%llu CB1_only=%llu both_valid=%llu native_missing_bindings=%llu transition_skips=%llu",
            usage.binding_states[0].load(std::memory_order_relaxed), usage.binding_states[1].load(std::memory_order_relaxed),
            usage.binding_states[2].load(std::memory_order_relaxed), usage.binding_states[3].load(std::memory_order_relaxed),
            usage.native_missing_bindings.load(std::memory_order_relaxed), usage.transition_skips.load(std::memory_order_relaxed));
         reshade::log::message(reshade::log::level::info, message);
         for (size_t i = 1u; i < counts.size(); ++i)
         {
            if (counts[i] == 0u)
            {
               continue;
            }
            std::snprintf(message, sizeof(message), "QB native inputs: REJECTION_TOTAL reason=%s count=%llu",
               NativeInputFallbackText(static_cast<NativeInputFallback>(i)), counts[i]);
            reshade::log::message(reshade::log::level::info, message);
         }
      }
#endif
   } // namespace

   void RegisterShaderHashes()
   {
#if DEVELOPMENT
      forced_shader_names.emplace(depth_linearization_pass.hash, "QB Depth Linearization (Clip Depth -> Linear Depth)");
      forced_shader_names.emplace(history_reprojection_pass.hash, "QB History Reprojection (Motion Vectors)");
      forced_shader_names.emplace(temporal_resolve_pass.hash, "QB Temporal Resolve / TAA");
      forced_shader_names.emplace(hash_msaa_gbuffer_reconstruction, "QB MSAA GBuffer Reconstruction Mask");
      forced_shader_names.emplace(hash_msaa_gbuffer_depth_resolve, "QB MSAA GBuffer Depth Resolve");
#endif
   }

   bool MatchesPass(const ShaderHashesList<OneShaderPerPipeline>& hashes, ShaderPass pass)
   {
      return hashes.Contains(pass.hash, pass.stage);
   }

   bool IsTemporalReplacementActive(ID3D11DeviceContext* context, const CommandListData& cmd_list_data, const DeviceData& device_data)
   {
#if ENABLE_SR
      if (!custom_shaders_enabled || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
      com_ptr<ID3D11PixelShader> bound;
      context->PSGetShader(&bound, nullptr, nullptr);
      // Per-context clone handles can outlive unload/reload. Read the live cache using its existing lock,
      // after the D3D getter; release the lock before bound's COM reference or any other device call.
      const std::shared_lock lock(s_mutex_generic);
      const auto pipeline = device_data.pipeline_cache_by_pipeline_handle.find(cmd_list_data.pipeline_state_original_pixel_shader.handle);
      if (pipeline == device_data.pipeline_cache_by_pipeline_handle.end() || pipeline->second == nullptr) return false;
      const auto& cached = *pipeline->second;
      if (!cached.cloned || cached.clone_origin != Shader::CloneOrigin::File || cached.shader_hashes[0] != temporal_resolve_pass.hash) return false;
      return CanUseTemporalSRShader(custom_shaders_enabled,
         cached.pipeline.handle, cached.pipeline_clone.handle, reinterpret_cast<uintptr_t>(bound.get()));
#else
      (void)context;
      (void)cmd_list_data;
      (void)device_data;
      return false;
#endif
   }

   void OnInit()
   {
#if ENABLE_SR
      sr_game_tooltip = "Super Resolution engages during the temporal resolve pass.\n";
      native_shaders_definitions.emplace(CompileTimeStringHash("QB Pre SR Decode"), ShaderDefinition{"Luma_QB_PreSRDecode", reshade::api::pipeline_subobject_type::pixel_shader});
#endif
   }

   bool IsRequested(const DeviceData& device_data)
   {
#if ENABLE_SR
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#else
      (void)device_data;
      return false;
#endif
   }

   void OnInitDevice(DeviceData& device_data)
   {
#if ENABLE_SR
      // Backend selection is complete here, but no owning scene has begun yet.
      NativeHooks::SetActive(IsRequested(device_data));
      NativeHooks::TryInstall();
#if DEVELOPMENT || TEST
      char message[256];
      std::snprintf(message, sizeof(message), "QB native inputs: PREPARED event=INIT_DEVICE requested=%u thread=%lu hooks=%s",
         static_cast<unsigned>(IsRequested(device_data)), GetCurrentThreadId(), NativeHooks::StatusText());
      reshade::log::message(reshade::log::level::info, message);
#endif
#else
      (void)device_data;
#endif
   }

   void PrepareNativeInputs(ID3D11DeviceContext* context, const DeviceData& device_data)
   {
#if ENABLE_SR
      if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
      const bool requested = IsRequested(device_data);
      NativeHooks::SetActive(requested);
      // A module absent on boot may appear before the first immediate scene draw.
      if (requested) NativeHooks::TryInstall();
#else
      (void)context;
      (void)device_data;
#endif
   }

   void DrawSettings(DeviceData& device_data)
   {
#if ENABLE_SR
      // Synchronize Core's preceding SR selection without refreshing any captured sample.
      NativeHooks::SetActive(IsRequested(device_data));
#endif
#if DEVELOPMENT || TEST
#if ENABLE_SR
      ImGui::NewLine();
      ImGui::Text("Super Resolution");
      if (ImGui::Button("Reset SR History"))
      {
         device_data.force_reset_sr = true;
      }
#else
      (void)device_data;
      ImGui::TextDisabled("Super Resolution is disabled in this build.");
#endif
#else
      (void)device_data;
#endif
   }

   void CaptureClipDepthFromLinearizationPass(ID3D11DeviceContext* native_device_context, Data& data)
   {
#if ENABLE_SR
      if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
      {
         return;
      }
      // Read pre-linearized clip/device depth from PS SRV0, never the linear-depth RTV.
      // Keep the largest input per frame; downscaled linearization passes cannot replace it.
      com_ptr<ID3D11ShaderResourceView> clip_depth_srv;
      native_device_context->PSGetShaderResources(0, 1, &clip_depth_srv);
      if (clip_depth_srv.get() == nullptr)
      {
         return;
      }
      com_ptr<ID3D11Resource> clip_depth_resource;
      clip_depth_srv->GetResource(&clip_depth_resource);
      D3D11_TEXTURE2D_DESC clip_depth_desc = {};
      if (!TryGetTextureDesc(clip_depth_resource.get(), clip_depth_desc) || clip_depth_desc.Width == 0u || clip_depth_desc.Height == 0u)
      {
         return;
      }
      const uint64_t current_area = static_cast<uint64_t>(clip_depth_desc.Width) * clip_depth_desc.Height;
      const uint64_t previous_area = static_cast<uint64_t>(data.sr_clip_depth_desc.Width) * data.sr_clip_depth_desc.Height;
      if (data.sr_clip_depth.get() == nullptr || current_area > previous_area)
      {
         data.sr_clip_depth = clip_depth_resource;
         data.sr_clip_depth_desc = clip_depth_desc;
      }
      NativeHooks::RecordDepth(reinterpret_cast<uintptr_t>(clip_depth_resource.get()), clip_depth_desc.Width, clip_depth_desc.Height);
#else
      (void)native_device_context;
      (void)data;
#endif
   }

   void CaptureMotionVectors(ID3D11DeviceContext* native_device_context, Data& data)
   {
#if ENABLE_SR
      if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
      {
         return;
      }
      com_ptr<ID3D11ShaderResourceView> motion_vectors_srv;
      native_device_context->CSGetShaderResources(0, 1, &motion_vectors_srv);
      if (motion_vectors_srv.get() != nullptr)
      {
         data.sr_motion_vectors = nullptr;
         motion_vectors_srv->GetResource(&data.sr_motion_vectors);
         NativeHooks::RecordMotion(reinterpret_cast<uintptr_t>(data.sr_motion_vectors.get()));
      }
#else
      (void)native_device_context;
      (void)data;
#endif
   }

#if ENABLE_SR
   namespace
   {
      bool ValidCBufferBinding(ID3D11Buffer* buffer, uint32_t minimum_size)
      {
         if (buffer == nullptr)
         {
            return false;
         }
         D3D11_BUFFER_DESC desc = {};
         buffer->GetDesc(&desc);
         return desc.ByteWidth >= minimum_size && (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0u;
      }

      bool SetupOutput(ID3D11Device* native_device, DeviceData& device_data, Data& data, const SR::InstanceData& sr_instance_data,
         const D3D11_TEXTURE2D_DESC& output_desc, bool& output_changed)
      {
         bool recreated_output_texture = false;
         if (output_desc.Width < sr_instance_data.min_resolution || output_desc.Height < sr_instance_data.min_resolution)
         {
            // Reject output sizes that the selected backend cannot process.
            return false;
         }
         D3D11_TEXTURE2D_DESC sr_output_desc = output_desc;
         sr_output_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
         if (device_data.sr_output_color.get() != nullptr)
         {
            D3D11_TEXTURE2D_DESC prev_desc = {};
            device_data.sr_output_color->GetDesc(&prev_desc);
            output_changed = prev_desc.Width != sr_output_desc.Width || prev_desc.Height != sr_output_desc.Height || prev_desc.Format != sr_output_desc.Format;
         }
         if (device_data.sr_output_color.get() == nullptr || output_changed)
         {
            device_data.sr_output_color = nullptr;
            if (FAILED(native_device->CreateTexture2D(&sr_output_desc, nullptr, &device_data.sr_output_color)) ||
                device_data.sr_output_color.get() == nullptr)
            {
               return false;
            }
            recreated_output_texture = true;
         }
         if (data.sr_output_color_srv.get() == nullptr || output_changed || recreated_output_texture)
         {
            data.sr_output_color_srv = nullptr;
            if (FAILED(native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &data.sr_output_color_srv)) ||
                data.sr_output_color_srv.get() == nullptr)
            {
               return false;
            }
         }
         return true;
      }

      DXGI_FORMAT ResolveColorViewFormat(DXGI_FORMAT format)
      {
         switch (format)
         {
         case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
         case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
         case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
         case DXGI_FORMAT_R8G8B8A8_TYPELESS:
         case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
         case DXGI_FORMAT_B8G8R8A8_TYPELESS:
         case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
         case DXGI_FORMAT_B8G8R8X8_TYPELESS:
         case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8X8_UNORM;
         default:
            return format;
         }
      }

      bool SetupConversionTexture(ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, com_ptr<ID3D11Texture2D>& texture,
         com_ptr<ID3D11ShaderResourceView>& srv, com_ptr<ID3D11RenderTargetView>& rtv)
      {
         desc.Format = ResolveColorViewFormat(desc.Format);
         if (desc.Width == 0u || desc.Height == 0u || desc.Format == DXGI_FORMAT_UNKNOWN)
         {
            return false;
         }
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         desc.CPUAccessFlags = 0;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.MiscFlags = 0;
         desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1u;
         desc.SampleDesc.Quality = 0u;
         bool recreate_texture = texture.get() == nullptr;
         if (!recreate_texture)
         {
            D3D11_TEXTURE2D_DESC previous_desc = {};
            texture->GetDesc(&previous_desc);
            recreate_texture = HasTextureShapeChanged(desc, previous_desc);
         }
         if (recreate_texture)
         {
            texture = nullptr;
            srv = nullptr;
            rtv = nullptr;
            if (FAILED(native_device->CreateTexture2D(&desc, nullptr, &texture)) || texture.get() == nullptr)
            {
               return false;
            }
         }
         if (srv.get() == nullptr &&
             (FAILED(native_device->CreateShaderResourceView(texture.get(), nullptr, &srv)) || srv.get() == nullptr))
         {
            // A missing view leaves this conversion resource unavailable.
            return false;
         }
         if (rtv.get() == nullptr &&
             (FAILED(native_device->CreateRenderTargetView(texture.get(), nullptr, &rtv)) || rtv.get() == nullptr))
         {
            // Do not draw the conversion pass without a valid target.
            return false;
         }
         return true;
      }

      bool DrawConversionPass(ID3D11DeviceContext* native_device_context, DeviceData& device_data, uint32_t pixel_shader_hash,
         ID3D11ShaderResourceView* source_srv, ID3D11RenderTargetView* target_rtv, uint32_t width, uint32_t height)
      {
         const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
         const auto ps_it = device_data.native_pixel_shaders.find(pixel_shader_hash);
         if (vs_it == device_data.native_vertex_shaders.end() || vs_it->second.get() == nullptr ||
             ps_it == device_data.native_pixel_shaders.end() || ps_it->second.get() == nullptr ||
             source_srv == nullptr || target_rtv == nullptr || width == 0u || height == 0u)
         {
            // All bindings and dimensions are mandatory before touching render state.
            return false;
         }
         native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(),
            nullptr, vs_it->second.get(), ps_it->second.get(), source_srv, target_rtv, width, height, false);
         ID3D11ShaderResourceView* null_srv = nullptr;
         native_device_context->PSSetShaderResources(0, 1, &null_srv);
         ID3D11RenderTargetView* null_rtv = nullptr;
         native_device_context->OMSetRenderTargets(1, &null_rtv, nullptr);
         return true;
      }
   } // namespace
#endif // ENABLE_SR

   TemporalResolveResult RunTemporalResolve(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, Data& data, bool temporal_replacement_active)
   {
      TemporalResolveResult result = {};
      result.requested = IsRequested(device_data);
#if ENABLE_SR
#if DEVELOPMENT || TEST
      data.debug_inputs = {};
      data.debug_used_cached_clip_depth = false;
#endif // DEVELOPMENT || TEST
      if (data.previous_sr_type != device_data.sr_type)
      {
         data.previous_sr_type = device_data.sr_type;
         data.dlss_scene_warmup_remaining = device_data.sr_type == SR::Type::DLSS ? dlss_scene_warmup_resolve_count : 0u;
      }
      if (!result.requested || native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return result;

      // UI/menu resolves can use this shader without the scene color/depth/output SR needs.
      com_ptr<ID3D11ShaderResourceView> ps_shader_resources[3];
      com_ptr<ID3D11RenderTargetView> render_target_view;
      native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));
      native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);
      if (ps_shader_resources[0].get() == nullptr || ps_shader_resources[2].get() == nullptr || render_target_view.get() == nullptr) return result;

      // Check metadata even when the replacement is absent; the known unsafe draw guard remains.
      com_ptr<ID3D11Buffer> temporal_constant_buffers[2];
      native_device_context->PSGetConstantBuffers(0, ARRAYSIZE(temporal_constant_buffers), reinterpret_cast<ID3D11Buffer**>(temporal_constant_buffers));
      const bool frame_binding = ValidCBufferBinding(temporal_constant_buffers[0].get(), frame_constants_min_size);
      const bool temporal_binding = ValidCBufferBinding(temporal_constant_buffers[1].get(), ssaa_constants_min_size);

      com_ptr<ID3D11Resource> source_color_resource;
      com_ptr<ID3D11Resource> output_resource;
      com_ptr<ID3D11Resource> temporal_resolve_depth_resource;
      com_ptr<ID3D11Resource> sr_depth_resource;
      com_ptr<ID3D11Resource> motion_vectors_resource;
      D3D11_TEXTURE2D_DESC source_desc = {};
      D3D11_TEXTURE2D_DESC temporal_resolve_depth_desc = {};
      D3D11_TEXTURE2D_DESC depth_desc = {};
      D3D11_TEXTURE2D_DESC motion_vectors_desc = {};
      D3D11_TEXTURE2D_DESC output_desc = {};
      NativeSceneCamera camera;
      float native_x = 0.f, native_y = 0.f;
      auto native_reason = NativeInputFallback::TemporalShaderUnavailable;
      if (temporal_replacement_active)
      {
         ps_shader_resources[2]->GetResource(&source_color_resource);
         render_target_view->GetResource(&output_resource);
         ps_shader_resources[0]->GetResource(&temporal_resolve_depth_resource);
         motion_vectors_resource = data.sr_motion_vectors;
         native_reason = NativeInputFallback::ResourceLayoutMismatch;
         if (TryGetTextureDesc(source_color_resource.get(), source_desc) && TryGetTextureDesc(output_resource.get(), output_desc) &&
             TryGetTextureDesc(motion_vectors_resource.get(), motion_vectors_desc))
         {
            const auto cached_size = data.sr_clip_depth.get() != nullptr ? SRRenderSize{data.sr_clip_depth_desc.Width, data.sr_clip_depth_desc.Height} : SRRenderSize{};
            const auto temporal_size = TryGetTextureDesc(temporal_resolve_depth_resource.get(), temporal_resolve_depth_desc) ? SRRenderSize{temporal_resolve_depth_desc.Width, temporal_resolve_depth_desc.Height} : SRRenderSize{};
            const auto depth_source = ChooseSRDepthSource({source_desc.Width, source_desc.Height},
               {motion_vectors_desc.Width, motion_vectors_desc.Height}, cached_size, temporal_size);
            if (depth_source != SRDepthSource::Unavailable && output_desc.Width != 0u && output_desc.Height != 0u)
            {
               const bool use_cached_depth = depth_source == SRDepthSource::Cached;
               sr_depth_resource = use_cached_depth ? data.sr_clip_depth : temporal_resolve_depth_resource;
               depth_desc = use_cached_depth ? data.sr_clip_depth_desc : temporal_resolve_depth_desc;
#if DEVELOPMENT || TEST
               data.debug_used_cached_clip_depth = use_cached_depth;
#endif
               // Native eligibility and the backend now refer to exactly this same owned resource.
               native_reason = NativeHooks::CopyCurrentInputs(camera, native_x, native_y,
                  reinterpret_cast<uintptr_t>(sr_depth_resource.get()), reinterpret_cast<uintptr_t>(motion_vectors_resource.get()));
            }
         }
      }
      const auto action = SelectTemporalInputAction(native_reason, frame_binding, temporal_binding);
#if DEVELOPMENT || TEST
      data.debug_inputs.reason = GuardNativeInputBindings(native_reason, frame_binding, temporal_binding);
      RecordSelection(data.native_input_usage, data.debug_inputs.reason);
      RecordInputGate(data, native_reason, frame_binding, temporal_binding, action, device_data.sr_type);
      if (action == TemporalInputAction::NativeSR)
      {
         data.debug_inputs.camera = camera;
         data.debug_inputs.jitter_x = native_x;
         data.debug_inputs.jitter_y = native_y;
      }
#endif
      if (action != TemporalInputAction::NativeSR)
      {
         // Reset SR without reading GPU bytes or inventing camera/jitter for this draw.
         device_data.force_reset_sr = true;
         data.dlss_scene_warmup_remaining = device_data.sr_type == SR::Type::DLSS ? dlss_scene_warmup_resolve_count : 0u;
         if (cb_luma_global_settings.SRType != 0u)
         {
            cb_luma_global_settings.SRType = 0u;
            device_data.cb_luma_global_settings_dirty = true;
         }
         result.stop_processing = action == TemporalInputAction::Skip;
         return result;
      }
      if (device_data.sr_type == SR::Type::DLSS && data.dlss_scene_warmup_remaining > 0u)
      {
         // Count valid resolves, not seconds. Keep game TAA and an empty DLSS history during warmup.
         --data.dlss_scene_warmup_remaining;
         device_data.force_reset_sr = true;
         result.requested = false;
         return result;
      }

      // The preceding selection proves native scope, bindings, and equal color/depth/MV dimensions.
      auto* sr_instance_data = device_data.GetSRInstanceData();
      if (sr_instance_data == nullptr) return result;
      bool output_changed = false;
      if (!SetupOutput(native_device, device_data, data, *sr_instance_data, output_desc, output_changed)) return result;
      const uint32_t render_width = source_desc.Width;
      const uint32_t render_height = source_desc.Height;
      const uint32_t output_width = output_desc.Width;
      const uint32_t output_height = output_desc.Height;
      // Current native jitter is UV-space; convert to SR render pixels and flip Y.
      const float pixel_jitter_x = native_x * static_cast<float>(render_width);
      const float pixel_jitter_y = -native_y * static_cast<float>(render_height);
#if DEVELOPMENT || TEST
      data.debug_inputs.pixel_jitter_x = pixel_jitter_x;
      data.debug_inputs.pixel_jitter_y = pixel_jitter_y;
#endif // DEVELOPMENT || TEST
      SetRenderData(render_width, render_height, output_width, output_height, native_x, native_y, device_data);
      SR::SettingsData settings_data = {};
      settings_data.output_width = output_width;
      settings_data.output_height = output_height;
      settings_data.render_width = render_width;
      settings_data.render_height = render_height;
      settings_data.dynamic_resolution = false;
      settings_data.hdr = true;
      settings_data.auto_exposure = false;
      settings_data.inverted_depth = false;
      settings_data.mvs_jittered = false;
      settings_data.mvs_x_scale = static_cast<float>(render_width);
      settings_data.mvs_y_scale = static_cast<float>(render_height);
      settings_data.render_preset = dlss_render_preset;
      D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc = {};
      ps_shader_resources[2]->GetDesc(&source_srv_desc);
      D3D11_TEXTURE2D_DESC sr_linear_input_desc = source_desc;
      sr_linear_input_desc.Format = source_srv_desc.Format != DXGI_FORMAT_UNKNOWN ? source_srv_desc.Format : source_desc.Format;
      if (!SetupConversionTexture(native_device, sr_linear_input_desc,
             data.sr_linear_input_color, data.sr_linear_input_color_srv, data.sr_linear_input_color_rtv)) return result;
      if (!sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data)) return result;

      const bool source_changed = UpdatePreviousTextureDesc(source_desc, data.previous_source_desc, data.has_previous_source_desc);
      const bool depth_changed = UpdatePreviousTextureDesc(depth_desc, data.previous_depth_desc, data.has_previous_depth_desc);
      const bool motion_vectors_changed = UpdatePreviousTextureDesc(motion_vectors_desc, data.previous_motion_vectors_desc, data.has_previous_motion_vectors_desc);
#if DEVELOPMENT || TEST
      data.debug_render_size = {render_width, render_height};
      data.debug_output_size = {output_width, output_height};
#endif // DEVELOPMENT || TEST
      const bool reset_sr = ConsumeSRResetRequest(device_data.force_reset_sr,
         output_changed || source_changed || depth_changed || motion_vectors_changed);
      SR::SuperResolutionImpl::DrawData draw_data = {};
      draw_data.source_color = data.sr_linear_input_color.get();
      draw_data.output_color = device_data.sr_output_color.get();
      draw_data.motion_vectors = motion_vectors_resource.get();
      draw_data.depth_buffer = sr_depth_resource.get();
      draw_data.pre_exposure = 1.f;
      draw_data.jitter_x = pixel_jitter_x;
      draw_data.jitter_y = pixel_jitter_y;
      draw_data.vert_fov = camera.vertical_fov;
      draw_data.near_plane = camera.near_plane;
      draw_data.far_plane = far_plane;
      draw_data.reset = reset_sr;
      draw_data.render_width = render_width;
      draw_data.render_height = render_height;
      draw_data.user_sharpness = device_data.sr_type == SR::Type::FSR ? fsr_sharpness : -1.f;
      DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
      DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
      draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
      compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
      const bool pre_sr_encoded = DrawConversionPass(native_device_context, device_data, CompileTimeStringHash("QB Pre SR Decode"),
         ps_shader_resources[2].get(), data.sr_linear_input_color_rtv.get(), source_desc.Width, source_desc.Height);
      bool sr_drawn = false;
      if (pre_sr_encoded)
      {
         sr_drawn = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
      }
      result.succeeded = pre_sr_encoded && sr_drawn;
#if DEVELOPMENT || TEST
      if (pre_sr_encoded)
      {
         RecordDraw(data.native_input_usage, sr_drawn);
      }
#endif
      ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
      native_device_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
      native_device_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
      ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
      native_device_context->CSSetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, null_uavs, nullptr);
      ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);
      draw_state_stack.Restore(native_device_context);
      compute_state_stack.Restore(native_device_context);
      if (result.succeeded)
      {
         device_data.has_drawn_sr = true;
      }
      else
      {
         device_data.force_reset_sr = true;
      }
#else
      (void)native_device;
      (void)native_device_context;
      (void)data;
      (void)temporal_replacement_active;
#endif
      return result;
   }

   void SetSRTypeForTemporalResolve(DeviceData& device_data, bool sr_succeeded)
   {
#if ENABLE_SR
      const uint32_t sr_type_for_pass = sr_succeeded ? static_cast<uint32_t>(device_data.sr_type) + 1u : 0u;
#else
      const uint32_t sr_type_for_pass = 0u;
      (void)sr_succeeded;
#endif
      if (cb_luma_global_settings.SRType != sr_type_for_pass)
      {
         cb_luma_global_settings.SRType = sr_type_for_pass;
         device_data.cb_luma_global_settings_dirty = true;
      }
   }

   void ForceResetIfRequestedAndFailed(DeviceData& device_data, const TemporalResolveResult& result)
   {
#if ENABLE_SR
      if (!result.succeeded && result.requested)
      {
         device_data.force_reset_sr = true;
      }
#else
      (void)device_data;
      (void)result;
#endif
   }

   void BindOutputToTemporalResolve(ID3D11DeviceContext* native_device_context, Data& data, bool sr_succeeded)
   {
#if ENABLE_SR
      if (sr_succeeded)
      {
         ID3D11ShaderResourceView* sr_output_srv = data.sr_output_color_srv.get();
         native_device_context->PSSetShaderResources(2, 1, &sr_output_srv);
      }
#else
      (void)native_device_context;
      (void)data;
      (void)sr_succeeded;
#endif
   }

   void CleanResources(DeviceData& device_data, Data& data)
   {
#if ENABLE_SR
      NativeHooks::Reset();
      device_data.force_reset_sr = true;
      device_data.has_drawn_sr = false;
      data.sr_clip_depth = nullptr;
      data.sr_motion_vectors = nullptr;
      data.sr_linear_input_color = nullptr;
      data.sr_linear_input_color_srv = nullptr;
      data.sr_linear_input_color_rtv = nullptr;
      data.sr_output_color_srv = nullptr;
#if DEVELOPMENT || TEST
      data.debug_inputs = {};
      data.debug_render_size = {};
      data.debug_output_size = {};
      data.debug_used_cached_clip_depth = false;
#endif // DEVELOPMENT || TEST
      data.has_previous_source_desc = false;
      data.has_previous_depth_desc = false;
      data.has_previous_motion_vectors_desc = false;
      data.sr_clip_depth_desc = {};
      data.previous_source_desc = {};
      data.previous_depth_desc = {};
      data.previous_motion_vectors_desc = {};
      data.dlss_scene_warmup_remaining = device_data.sr_type == SR::Type::DLSS ? dlss_scene_warmup_resolve_count : 0u;
#else
      (void)device_data;
      (void)data;
#endif
   }

   void OnDestroyDevice(Data* data)
   {
#if ENABLE_SR
      NativeHooks::Reset();
#if DEVELOPMENT || TEST
      if (data != nullptr)
      {
         WriteUsage(*data, "FINAL_DEVICE_DESTRUCTION", true);
      }
#endif
#endif
      (void)data;
   }

   void OnPresent(DeviceData& device_data, Data& data)
   {
#if ENABLE_SR
      NativeHooks::SetActive(IsRequested(device_data));
      if (IsRequested(device_data))
      {
         // Menu presents can establish hooks before the first gameplay scene begins.
         NativeHooks::TryInstall();
      }
#if DEVELOPMENT || TEST
      WriteUsage(data, "PROGRESS");
#endif
      const bool had_clip_depth = data.sr_clip_depth.get() != nullptr;
      const bool had_motion_vectors = data.sr_motion_vectors.get() != nullptr;
      if (device_data.sr_type != SR::Type::None && !device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }
      if (device_data.sr_type == SR::Type::DLSS && !device_data.has_drawn_sr && !had_clip_depth && !had_motion_vectors)
      {
         // No-scene/UI frames restart the valid-resolve warmup, rather than consuming time.
         data.dlss_scene_warmup_remaining = (std::max)(data.dlss_scene_warmup_remaining, dlss_scene_warmup_resolve_count);
      }
#if DEVELOPMENT || TEST
      data.debug_prev_had_clip_depth = had_clip_depth;
      data.debug_prev_had_motion_vectors = had_motion_vectors;
      data.debug_prev_used_cached_clip_depth = data.debug_used_cached_clip_depth;
      data.debug_used_cached_clip_depth = false;
#endif // DEVELOPMENT || TEST
      data.sr_clip_depth = nullptr;
      data.sr_motion_vectors = nullptr;
      data.sr_clip_depth_desc = {};
#else
      (void)device_data;
      (void)data;
#endif
      if (cb_luma_global_settings.SRType != 0u)
      {
         cb_luma_global_settings.SRType = 0u;
         device_data.cb_luma_global_settings_dirty = true;
      }
   }

   void DrawDebug(const Data& data, bool saw_history_reprojection_pass, bool saw_temporal_resolve_pass,
      bool had_scene_temporal_resolve_last_frame, uint32_t ui_only_frame_hold_counter)
   {
#if ENABLE_SR && (DEVELOPMENT || TEST)
      auto begin_table = [](const char* id)
      {
         constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings;
         if (!ImGui::BeginTable(id, 2, flags))
         {
            return false;
         }
         const float field_column_width = (std::max)(420.f,
            ImGui::CalcTextSize("Had Scene Temporal Resolve Last Frame:").x + ImGui::GetStyle().CellPadding.x * 2.f + 48.f);
         ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, field_column_width);
         ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
         return true;
      };
      auto row_label = [](const char* label)
      {
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted(label);
         ImGui::TableSetColumnIndex(1);
      };
      auto row_bool = [&](const char* label, bool value)
      {
         row_label(label);
         ImGui::TextUnformatted(value ? "Yes" : "No");
      };
      auto row_uint = [&](const char* label, uint32_t value)
      {
         row_label(label);
         ImGui::Text("%u", value);
      };
      auto row_uint64 = [&](const char* label, uint64_t value)
      {
         row_label(label);
         ImGui::Text("%llu", static_cast<unsigned long long>(value));
      };
      auto row_float = [&](const char* label, float value)
      {
         row_label(label);
         ImGui::Text("%.6f", value);
      };
      ImGui::NewLine();
      if (ImGui::CollapsingHeader("Super Resolution Debug") && begin_table("QB_SR_Debug_Overview"))
      {
         row_bool("History Reprojection Pass Seen:", saw_history_reprojection_pass);
         row_bool("Temporal Resolve Pass Seen:", saw_temporal_resolve_pass);
         row_bool("Clip Depth Captured:", data.debug_prev_had_clip_depth);
         row_bool("Motion Vectors Captured:", data.debug_prev_had_motion_vectors);
         row_bool("Cached Clip Depth Used For SR:", data.debug_prev_used_cached_clip_depth);
         row_bool("Had Scene Temporal Resolve Last Frame:", had_scene_temporal_resolve_last_frame);
         row_uint("UI-Only Hold Frames:", ui_only_frame_hold_counter);
         ImGui::EndTable();
      }
      if (ImGui::CollapsingHeader("Active SR Inputs"))
      {
         if (begin_table("QB_SR_Debug_Active"))
         {
            row_uint("Last SR Render Width:", data.debug_render_size.width);
            row_uint("Last SR Render Height:", data.debug_render_size.height);
            row_uint("Last SR Output Width:", data.debug_output_size.width);
            row_uint("Last SR Output Height:", data.debug_output_size.height);
            row_label("Selected SR Input Source:");
            ImGui::TextUnformatted(data.debug_inputs.camera.valid ? "Native SSAA path 0 (live scope)" : "Unavailable; game TAA / guarded skip");
            row_uint64("Native Camera/Jitter Selections:", data.native_input_usage.selections[0].load(std::memory_order_relaxed));
            row_uint64("Native Camera/Jitter SR Draws:", data.native_input_usage.native_draws.load(std::memory_order_relaxed));
            row_float("Last Raw SSAA Jitter X:", data.debug_inputs.jitter_x);
            row_float("Last Raw SSAA Jitter Y:", data.debug_inputs.jitter_y);
            row_float("Last SR Pixel Jitter X:", data.debug_inputs.pixel_jitter_x);
            row_float("Last SR Pixel Jitter Y:", data.debug_inputs.pixel_jitter_y);
            row_bool("Using Native Camera And Jitter:", data.debug_inputs.camera.valid);
            row_label("Native SSAA Hook Status:");
            ImGui::TextUnformatted(NativeHooks::StatusText());
            row_float("Selected Vertical FOV:", data.debug_inputs.camera.vertical_fov);
            row_float("Selected Near Plane:", data.debug_inputs.camera.near_plane);
            row_label("Native Input Rejection Reason:");
            ImGui::TextUnformatted(NativeInputFallbackText(data.debug_inputs.reason));
            row_uint64("Game TAA Fallback Resolves:", data.native_input_usage.taa_resolves.load(std::memory_order_relaxed));
            ImGui::EndTable();
         }
         bool prefer_native = NativeHooks::IsPreferred();
         if (ImGui::Checkbox("Enable native SR inputs (off uses game TAA)", &prefer_native))
         {
            NativeHooks::SetPreferred(prefer_native);
         }
      }
#else
      (void)data;
      (void)saw_history_reprojection_pass;
      (void)saw_temporal_resolve_pass;
      (void)had_scene_temporal_resolve_last_frame;
      (void)ui_only_frame_hold_counter;
#endif
   }
} // namespace QuantumBreakUpscaling