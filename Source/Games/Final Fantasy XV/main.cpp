#define GAME_FINALFANTASYXV 1

#define ALLOW_SHADERS_DUMPING 1
#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define DEBUG_LOG 0

#include <cstddef>
#include <mutex>
#include "../../Core/core.hpp"
#include "includes/common.hpp"
#include "includes/dither_patch.hpp"
#include "includes/sr_helpers.hpp"

namespace
{
   ShaderHashesList shader_hashes_tonemap;
   ShaderHashesList shader_hashes_autoexposure;
   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_upscale;
   ShaderHashesList shader_hashes_dither_frame_cb13;
   std::mutex shader_hashes_dither_frame_cb13_mutex;
   const uint32_t CBTemporalAA_buffer_size = 256;
   const uint32_t CBView_buffer_size = 768;

} // namespace

class FinalFantasyXV final : public Game
{
   static GameDeviceDataFFXV& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataFFXV*>(device_data.game);
   }

   static GameDeviceDataFFXV& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataFFXV*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs CS"), ShaderDefinition{"Luma_FFXV_MotionVec_Decode", reshade::api::pipeline_subobject_type::compute_shader});
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // #w## Update this (find the right value) ###
   }
   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(FinalFantasyXV::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(FinalFantasyXV::OnUnmapBufferRegion);
         reshade::register_event<reshade::addon_event::update_buffer_region>(FinalFantasyXV::OnUpdateBufferRegion);
      }
   }

   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash = -1, const std::byte* shader_object = nullptr, size_t shader_object_size = 0) override
   {
      if (code == nullptr || size == 0)
      {
         return nullptr;
      }

      if (type != reshade::api::pipeline_subobject_type::pixel_shader)
      {
         return nullptr;
      }

      FFXVDitheringShaderInfo dither_info = {};
      if (!IsFFXVDitheringShader(reinterpret_cast<const uint8_t*>(code), size, dither_info, shader_hash))
      {
         return nullptr;
      }

      auto modified_code = ModifyFFXVDitheringShader(reinterpret_cast<const uint8_t*>(code), size, dither_info, shader_hash);
      if (!modified_code)
      {
         return nullptr;
      }

      {
         const std::lock_guard lock(shader_hashes_dither_frame_cb13_mutex);
         shader_hashes_dither_frame_cb13.pixel_shaders.emplace(static_cast<uint32_t>(shader_hash));
      }

      return modified_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      bool should_bind_dither_frame_counter = false;
      {
         const std::lock_guard lock(shader_hashes_dither_frame_cb13_mutex);
         should_bind_dither_frame_counter = original_shader_hashes.Contains(shader_hashes_dither_frame_cb13);
      }

      if (should_bind_dither_frame_counter && (stages & reshade::api::shader_stage::pixel) != 0)
      {
         ID3D11Buffer* const luma_settings_cb = device_data.luma_global_settings.get();
         native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &luma_settings_cb);
         updated_cbuffers = true;
      }

      // Extract exposure texture from autoexposure pass
      if (original_shader_hashes.Contains(shader_hashes_autoexposure))
      {
         Log_Debug(
            reshade::log::level::info,
            "FFXV Autoexposure pass detected - extracting exposure texture");
         game_device_data.has_drawn_autoexposure = true;
         game_device_data.use_exposure_texture = ExtractExposureTexture(native_device, native_device_context, game_device_data);
         game_device_data.use_exposure_texture = true;
         return DrawOrDispatchOverrideType::None;
      }

      // Mark baseline logging flag when sr_type is None and TAA shader is detected
      // (This runs separately from the sr_type != None TAA handler above)
#if DEVELOPMENT || TEST
      if (device_data.sr_type == SR::Type::None && original_shader_hashes.Contains(shader_hashes_TAA))
      {
         game_device_data.dbg_log_baseline_state = true;
      }

      // When sr_type is None, log baseline viewport/scissor state for passes between TAA and Upscale
      if (device_data.sr_type == SR::Type::None && game_device_data.dbg_log_baseline_state)
      {
         // Helper lambda to log viewport and scissor state
         auto log_state = [&](const char* label)
         {
            UINT num_viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            native_device_context->RSGetViewports(&num_viewports, viewports);

            UINT num_scissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            native_device_context->RSGetScissorRects(&num_scissors, scissors);

            char log_buf[1024];
            snprintf(log_buf, sizeof(log_buf),
               "[FFXV Baseline] %s sr_type=None  stages=%u  VPs=%u  scissors=%u",
               label, static_cast<unsigned>(stages), num_viewports, num_scissors);
            Log_Debug(reshade::log::level::info, log_buf);

            if (num_viewports > 0)
            {
               for (UINT i = 0; i < num_viewports; ++i)
               {
                  snprintf(log_buf, sizeof(log_buf),
                     "[FFXV Baseline]   VP[%u] = {%f, %f, %f, %f, %f, %f}",
                     i, viewports[i].TopLeftX, viewports[i].TopLeftY,
                     viewports[i].Width, viewports[i].Height,
                     viewports[i].MinDepth, viewports[i].MaxDepth);
                  Log_Debug(reshade::log::level::info, log_buf);
               }
            }

            if (num_scissors > 0)
            {
               for (UINT i = 0; i < num_scissors; ++i)
               {
                  snprintf(log_buf, sizeof(log_buf),
                     "[FFXV Baseline]   Scissor[%u] = [%d, %d, %d, %d] (%dx%d)",
                     i, scissors[i].left, scissors[i].top, scissors[i].right, scissors[i].bottom,
                     scissors[i].right - scissors[i].left, scissors[i].bottom - scissors[i].top);
                  Log_Debug(reshade::log::level::info, log_buf);
               }
            }
         };

         if (original_shader_hashes.Contains(shader_hashes_upscale))
         {
            // Upscale shader detected - this is the terminal pass, log and reset flag
            log_state("[terminal]");
            game_device_data.dbg_log_baseline_state = false;
         }
         else
         {
            // Intermediate pass between TAA and Upscale - log viewport/scissor state
            log_state("[intermediate]");
         }
      }
#endif

      if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && original_shader_hashes.Contains(shader_hashes_TAA))
      {

         device_data.taa_detected = true;
         if (!game_device_data.found_taa_cb || !game_device_data.has_processed_view_buffer)
         {
            std::string reason = std::format("{} {}", (!game_device_data.found_taa_cb ? "TAA constant buffer not found" : ""), (!game_device_data.has_processed_view_buffer ? "per-view global buffer not processed yet" : ""));
            Log_Debug(
               reshade::log::level::warning,
               ("TAA constant buffer not found or view buffer not processed yet - skipping TAA pass handling: " + reason).c_str());
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }

         // Check if the motion vector decode shader is available
         if (device_data.native_compute_shaders[CompileTimeStringHash("Decode MVs CS")].get() == nullptr)
         {
            Log_Debug(
               reshade::log::level::warning,
               "Motion vector decode compute shader not available - skipping TAA pass handling");
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }

         // Extract TAA shader resources (source color, depth, motion vectors)
         Log_Debug(
            reshade::log::level::info,
            "TAA pass detected - extracting shader resources");
         com_ptr<ID3D11ShaderResourceView> depth_srv;
         com_ptr<ID3D11ShaderResourceView> velocity_srv;
         if (!ExtractTAAShaderResources(native_device, native_device_context, game_device_data, &depth_srv, &velocity_srv))
         {
            Log_Debug(
               reshade::log::level::warning,
               "Failed to extract TAA shader resources (depth or velocity SRV missing) - skipping TAA pass handling");
            ASSERT_ONCE(false);
            return DrawOrDispatchOverrideType::None;
         }

         // Get render targets
         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);

         ID3D11RenderTargetView* output_rtv = render_target_views[0].get();
         if (!output_rtv)
         {
            Log_Debug(
               reshade::log::level::warning,
               "No render target view bound - skipping TAA pass handling");
            return DrawOrDispatchOverrideType::None;
         }

         // Setup output texture
         com_ptr<ID3D11Texture2D> output_color;
         D3D11_TEXTURE2D_DESC output_texture_desc;
         bool output_supports_uav = false;
         bool output_changed = false;
         uintptr_t taa_output_key = 0;
         bool taa_upscaled_mapping_ready = false;

         Log_Debug(
            reshade::log::level::info,
            "Setting up SR output texture for TAA pass");
         // Only treat as upscaling when render < output (DLSS/FSR).
         // Supersampling (render > output) and DLAA (render == output) both use the DLAA path.
         const bool is_upscaling = device_data.render_resolution.x < device_data.output_resolution.x &&
                                   device_data.render_resolution.y < device_data.output_resolution.y;
         const float2* sr_target_res = is_upscaling ? &device_data.output_resolution : nullptr;

         if (is_upscaling)
         {
            // Link TAA output to its upscaled replacement for this frame.
            // SR draw writes directly into the linked pooled texture.
            ComPtr<ID3D11Resource> output_resource;
            output_rtv->GetResource(output_resource.put());
            output_resource->QueryInterface(&output_color);

            UpscaledResource* taa_upscaled = LinkUpscaledResource(
               native_device,
               native_device_context,
               output_resource.get(),
               game_device_data.upscale_tracking,
               device_data.output_resolution,
               true);
            if (!taa_upscaled || !taa_upscaled->texture)
            {
               Log_Debug(
                  reshade::log::level::warning,
                  "Failed to prepare pooled upscaled output for TAA pass - skipping TAA pass handling");
               return DrawOrDispatchOverrideType::None;
            }

            device_data.sr_output_color = taa_upscaled->texture.get();
            taa_output_key = reinterpret_cast<uintptr_t>(output_resource.get());
            game_device_data.upscale_tracking.post_taa_upscale_active = true;
            taa_upscaled_mapping_ready = true;

#if DEVELOPMENT || TEST
            {
               D3D11_TEXTURE2D_DESC orig_desc{}, up_desc{};
               output_color->GetDesc(&orig_desc);
               taa_upscaled->texture->GetDesc(&up_desc);
               char taa_log[512];
               snprintf(taa_log, sizeof(taa_log),
                  "[FFXV UpscaleChain] TAA link established:"
                  "  orig=%p %ux%u fmt=%u"
                  "  ->  tex=%p srv=%p %ux%u fmt=%u",
                  static_cast<void*>(output_resource.get()),
                  orig_desc.Width, orig_desc.Height, static_cast<uint32_t>(orig_desc.Format),
                  static_cast<void*>(taa_upscaled->texture.get()),
                  static_cast<void*>(taa_upscaled->srv.get()),
                  up_desc.Width, up_desc.Height, static_cast<uint32_t>(up_desc.Format));
               Log_Debug(reshade::log::level::info, taa_log);
            }
#endif
         }
         else
         {
            if (!SetupSROutput(native_device, device_data, output_rtv, output_color, output_texture_desc, output_supports_uav, output_changed))
            {
               Log_Debug(
                  reshade::log::level::warning,
                  "Failed to set up SR output texture for TAA pass - skipping TAA pass handling");
               return DrawOrDispatchOverrideType::None;
            }
         }

         // auto clear_upscale_frame_tracking = [&]()
         // {
         //    if (!taa_upscaled_mapping_ready)
         //       return;

         //    UnlinkUpscaledResource(game_device_data.upscale_tracking, taa_output_key);
         //    game_device_data.upscale_tracking.post_taa_upscale_active = false;
         //    taa_upscaled_mapping_ready = false;
         // };

         // Setup motion vector decode target
         if (!SetupMotionVectorDecodeTarget(native_device, game_device_data, velocity_srv.get()))
         {
            Log_Debug(
               reshade::log::level::warning,
               "Failed to set up motion vector decode target - skipping TAA pass handling");
            // Roll back tracking activation: SR never ran so the pool texture has no valid data.
            if (taa_upscaled_mapping_ready)
            {
               UnlinkUpscaledResource(game_device_data.upscale_tracking, taa_output_key);
               game_device_data.upscale_tracking.post_taa_upscale_active = false;
            }
            return DrawOrDispatchOverrideType::None;
         }

         // Cache state before motion vector decode
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         Log_Debug(
            reshade::log::level::info,
            "Starting Decode Motion Vectors compute shader");
         // Decode motion vectors
         DecodeMotionVectors(
            native_device_context,
            cmd_list_data,
            device_data,
            depth_srv.get(),
            velocity_srv.get(),
            game_device_data.sr_motion_vectors_uav.get());

         Log_Debug(
            reshade::log::level::info,
            "Finished Decode Motion Vectors compute shader");
#if DEVELOPMENT
         // Add trace info for motion vector decode pass
         {
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = "SR Decode Motion Vectors";
               // Get resource info for the motion vectors texture
               GetResourceInfo(game_device_data.sr_motion_vectors.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
         }
#endif
         // Get SR instance data
         auto* sr_instance_data = device_data.GetSRInstanceData();
         if (!sr_instance_data)
         {
            ASSERT_ONCE(false);
            if (output_supports_uav)
               device_data.sr_output_color = nullptr;
            // Roll back tracking activation: SR never ran so the pool texture has no valid data.
            if (taa_upscaled_mapping_ready)
            {
               UnlinkUpscaledResource(game_device_data.upscale_tracking, taa_output_key);
               game_device_data.upscale_tracking.post_taa_upscale_active = false;
            }
            return DrawOrDispatchOverrideType::None;
         }

         // When upscaling, SR output dimensions are output_resolution, not the RTV size
         SR::SettingsData settings_data;
         settings_data.output_width = is_upscaling ? static_cast<uint>(device_data.output_resolution.x) : output_texture_desc.Width;
         settings_data.output_height = is_upscaling ? static_cast<uint>(device_data.output_resolution.y) : output_texture_desc.Height;
         settings_data.render_width = static_cast<uint>(device_data.render_resolution.x);
         settings_data.render_height = static_cast<uint>(device_data.render_resolution.y);
         settings_data.dynamic_resolution = false;
         settings_data.hdr = true;
         settings_data.auto_exposure = true;
         settings_data.inverted_depth = false;
         settings_data.mvs_jittered = false;
         settings_data.render_preset = dlss_render_preset;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

         // Prepare SR draw data
         bool reset_sr = device_data.force_reset_sr || output_changed;
         device_data.force_reset_sr = false;

         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.source_color = game_device_data.sr_source_color.get();
         draw_data.output_color = device_data.sr_output_color.get();
         draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
         draw_data.depth_buffer = game_device_data.depth_buffer.get();
         if (game_device_data.use_exposure_texture)
            draw_data.exposure = game_device_data.exposure_texture.get();
         // draw_data.pre_exposure = 0.0f;
         draw_data.jitter_x = game_device_data.projection_jitters.x;
         draw_data.jitter_y = game_device_data.projection_jitters.y;
         draw_data.vert_fov = game_device_data.camera_fov;
         draw_data.far_plane = game_device_data.camera_far;
         draw_data.near_plane = game_device_data.camera_near;
         draw_data.reset = reset_sr;
         draw_data.render_width = static_cast<uint>(device_data.render_resolution.x);
         draw_data.render_height = static_cast<uint>(device_data.render_resolution.y);

         Log_Debug(
            reshade::log::level::info,
            "Prepared SR draw data; executing SR draw");

         std::string sr_data = std::format("SR draw data: source_color={:x}, output_color={:x}, motion_vectors={:x}, depth_buffer={:x}, jitter=({}, {}), fov={}, near_plane={}, far_plane={}, render_dims=({}x{}), reset={}",
            (size_t)draw_data.source_color, (size_t)draw_data.output_color, (size_t)draw_data.motion_vectors, (size_t)draw_data.depth_buffer,
            draw_data.jitter_x, draw_data.jitter_y, draw_data.vert_fov, draw_data.near_plane, draw_data.far_plane,
            draw_data.render_width, draw_data.render_height, draw_data.reset);
         Log_Debug(reshade::log::level::info, sr_data.c_str());
         // Execute SR
         device_data.has_drawn_sr = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

         draw_state_stack.Restore(native_device_context);
         compute_state_stack.Restore(native_device_context);

#if DEVELOPMENT
         // Add trace info for DLSS/FSR execution
         if (device_data.has_drawn_sr)
         {
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = device_data.sr_type == SR::Type::DLSS ? "DLSS" : "FSR";
               GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
         }
#endif

         // Clear temporary resources
         game_device_data.sr_source_color = nullptr;
         game_device_data.depth_buffer = nullptr;

         // Handle SR result
         if (device_data.has_drawn_sr)
         {
            if (is_upscaling)
            {
#if DEVELOPMENT || TEST
               Log_Debug(reshade::log::level::info, "[FFXV UpscaleChain] Pooled TAA mapping ready - tracking active until Upscale shader (1B6C8C68)");
#endif
               device_data.sr_output_color = nullptr; // SR output is the pooled texture linked to TAA output, so clear the main SR output reference to avoid confusion. The pooled texture is accessed via the upscale tracking system, not the main SR output slot.

               return DrawOrDispatchOverrideType::Replaced;
            }
            else
            {
               // DLAA (same resolution): copy result back to the original TAA output
               if (!output_supports_uav)
               {
                  native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get());
               }
               else
               {
                  device_data.sr_output_color = nullptr;
               }
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
         else
         {
            Log_Debug(
               reshade::log::level::warning,
               "Super Resolution draw failed");
            device_data.force_reset_sr = true;
            if (taa_upscaled_mapping_ready)
            {
               UnlinkUpscaledResource(game_device_data.upscale_tracking, taa_output_key);
               game_device_data.upscale_tracking.post_taa_upscale_active = false;
            }
         }

         if (output_supports_uav)
         {
            device_data.sr_output_color = nullptr;
         }
      }

      // Post-TAA upscale tracking: replace inputs/outputs/viewports for all passes
      // between TAA and the Upscale shader (1B6C8C68) when DLSS upscaling is active.
      // Chain propagation is restricted to render-resolution RTV replacements.
      if (game_device_data.upscale_tracking.post_taa_upscale_active)
      {
         const bool is_tonemap = original_shader_hashes.Contains(shader_hashes_tonemap);
         const bool is_upscale_pass = original_shader_hashes.Contains(shader_hashes_upscale);
         const bool is_compute = (stages == reshade::api::shader_stage::compute);

         // Always check the terminal pass first, even for compute dispatches, so the tracking
         // window is closed regardless of shader stage. The upscale shader is a pixel shader
         // in practice, but ordering this first prevents a future regression.
         if (is_upscale_pass)
         {
            game_device_data.upscale_tracking.post_taa_upscale_active = false;

            // Replace SRVs bound to the Upscale shader so it reads the upscaled pool texture.
            // This is unconditional (not dev-only) so the fix is live in all build configs.
            uint32_t upscale_srv_swapped = 0;
#if DEVELOPMENT || TEST
            {
               std::vector<UpscaleSwapDetail> upscale_details;
               const uint32_t upscale_hits = ReplaceUpscaledInputs(
                  native_device_context, game_device_data.upscale_tracking,
                  is_compute, &upscale_srv_swapped, &upscale_details);

               const uint32_t upscale_hash = static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]);
               char log_buf[384];
               snprintf(log_buf, sizeof(log_buf),
                  "[FFXV UpscaleChain] [Upscale shader=%08X] Chain terminated - SRV hits=%u swapped=%u",
                  upscale_hash, upscale_hits, upscale_srv_swapped);
               Log_Debug(reshade::log::level::info, log_buf);

               for (const auto& d : upscale_details)
               {
                  snprintf(log_buf, sizeof(log_buf),
                     "[FFXV UpscaleChain]   shader=%08X  SRV slot=%u  orig=%p  tex=%p  view=%p  %ux%u fmt=%u",
                     upscale_hash, d.slot, d.original, d.texture_ptr, d.replacement,
                     d.width, d.height, d.format);
                  Log_Debug(reshade::log::level::info, log_buf);
               }
            }
#else
            ReplaceUpscaledInputs(
               native_device_context, game_device_data.upscale_tracking,
               is_compute, &upscale_srv_swapped);
#endif
            return DrawOrDispatchOverrideType::None;
         }

         if (is_compute)
            return DrawOrDispatchOverrideType::None; // It's just Motion Blur, we scale it in the pixel shader instead

         // All intermediate passes get RTV+viewport replacement.
         // (The Upscale shader returns early above so replace_outputs is always true here.)

         // Replace input SRVs that reference upscaled resources
         uint32_t srv_swapped = 0;
#if DEVELOPMENT || TEST
         std::vector<UpscaleSwapDetail> swap_details;
         uint32_t chain_hits = ReplaceUpscaledInputs(native_device_context, game_device_data.upscale_tracking, is_compute, &srv_swapped, &swap_details);
#else
         uint32_t chain_hits = ReplaceUpscaledInputs(native_device_context, game_device_data.upscale_tracking, is_compute, &srv_swapped);
#endif

         uint32_t rtv_count = 0;
         uint32_t vp_count = 0;

         // Replace output RTVs at render_resolution and propagate the chain.
         // Called unconditionally: ReplaceUpscaledOutputs already checks frame_links
         // against every bound RTV, so it naturally catches blend-accumulate passes
         // (e.g. 0xBBC1036C) that write to a tracked target without reading any
         // tracked SRV input. rtv_count == 0 when nothing is tracked.
#if DEVELOPMENT || TEST
         rtv_count = ReplaceUpscaledOutputs(native_device, native_device_context, game_device_data.upscale_tracking, device_data.render_resolution, device_data.output_resolution, &swap_details);
#else
         rtv_count = ReplaceUpscaledOutputs(native_device, native_device_context, game_device_data.upscale_tracking, device_data.render_resolution, device_data.output_resolution);
#endif

         if (chain_hits > 0 || rtv_count > 0)
         {

            // DEBUG: Verify RTVs right after ReplaceUpscaledOutputs returns
#if DEVELOPMENT || TEST
            if (rtv_count > 0)
            {
               ID3D11RenderTargetView* post_rtv_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
               ComPtr<ID3D11DepthStencilView> post_rtv_dsv;
               native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, post_rtv_rtvs, post_rtv_dsv.put());
               char post_rtv_log[512];
               int po = snprintf(post_rtv_log, sizeof(post_rtv_log), "[RTV AFTER ReplaceOutputs] ");
               for (UINT j = 0; j < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++j)
               {
                  if (post_rtv_rtvs[j])
                  {
                     ComPtr<ID3D11Resource> pr;
                     post_rtv_rtvs[j]->GetResource(pr.put());
                     D3D11_TEXTURE2D_DESC pdtd = {};
                     ComPtr<ID3D11Texture2D> pdt;
                     D3D11_RESOURCE_DIMENSION prdim;
                     if (pr)
                     {
                        pr->GetType(&prdim);
                        if (prdim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                        {
                           pr->QueryInterface(pdt.put());
                           if (pdt)
                              pdt->GetDesc(&pdtd);
                        }
                     }
                     po += snprintf(post_rtv_log + po, sizeof(post_rtv_log) - po, "slot[%u]=%p %ux%u ", j, post_rtv_rtvs[j], pdtd.Width, pdtd.Height);
                  }
                  else
                  {
                     po += snprintf(post_rtv_log + po, sizeof(post_rtv_log) - po, "slot[%u]=NULL ", j);
                  }
                  if (post_rtv_rtvs[j])
                     post_rtv_rtvs[j]->Release();
               }
               Log_Debug(reshade::log::level::debug, post_rtv_log);
            }
#endif

            // // Disable scissor rects during upscaling passes
            // UINT saved_scissors_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            // D3D11_RECT saved_scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            // native_device_context->RSGetScissorRects(&saved_scissors_count, saved_scissors);

            // Replace viewports matching render_resolution (scissors are disabled below)
            uint32_t scissors_count = 0;
            if (rtv_count > 0)
            {
               // Disable all scissor rects during upscaling passes
               // native_device_context->RSSetScissorRects(0, nullptr);
               vp_count = ReplaceViewports(native_device_context, device_data.render_resolution, device_data.output_resolution, &scissors_count);
            }
            // DEBUG: Log RTVs right before draw call
#if DEVELOPMENT || TEST
            {
               ID3D11RenderTargetView* pre_draw_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
               ComPtr<ID3D11DepthStencilView> pre_draw_dsv;
               native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, pre_draw_rtvs, pre_draw_dsv.put());
               char pre_draw_log[512];
               int po = snprintf(pre_draw_log, sizeof(pre_draw_log), "[RTV BEFORE DRAW] ");
               for (UINT j = 0; j < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++j)
               {
                  if (pre_draw_rtvs[j])
                  {
                     ComPtr<ID3D11Resource> pr;
                     pre_draw_rtvs[j]->GetResource(pr.put());
                     D3D11_TEXTURE2D_DESC pdtd = {};
                     ComPtr<ID3D11Texture2D> pdt;
                     D3D11_RESOURCE_DIMENSION prdim;
                     if (pr)
                     {
                        pr->GetType(&prdim);
                        if (prdim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                        {
                           pr->QueryInterface(pdt.put());
                           if (pdt)
                              pdt->GetDesc(&pdtd);
                        }
                     }
                     po += snprintf(pre_draw_log + po, sizeof(pre_draw_log) - po, "slot[%u]=%p %ux%u ", j, pre_draw_rtvs[j], pdtd.Width, pdtd.Height);
                  }
                  else
                  {
                     po += snprintf(pre_draw_log + po, sizeof(pre_draw_log) - po, "slot[%u]=NULL ", j);
                  }
                  if (pre_draw_rtvs[j])
                     pre_draw_rtvs[j]->Release();
               }
               Log_Debug(reshade::log::level::debug, pre_draw_log);
            }
#endif
            return DrawOrDispatchOverrideType::None;
            // (*original_draw_dispatch_func)();

            // Restore original scissor rects
            // native_device_context->RSSetScissorRects(saved_scissors_count, saved_scissors);

#if DEVELOPMENT || TEST
            game_device_data.dbg_replaced_srvs += srv_swapped;
            game_device_data.dbg_replaced_rtvs += rtv_count;
            game_device_data.dbg_replaced_viewports += vp_count;
            game_device_data.dbg_replaced_scissors += 0; // Scissors disabled during upscaling

            {
               // Retrieve the active shader hash from the OneShaderPerPipeline container.
               // pixel_shaders[0] holds a uint64_t; cast to uint32_t to get the CRC32 hash.
               const uint32_t active_hash = static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]);
               const char* label = is_tonemap ? " [tonemap]" : " [intermediate]";
               char log_buf[512];
               snprintf(log_buf, sizeof(log_buf),
                  "[FFXV UpscaleChain]%s  shader=%08X  hits=%u swapped=%u RTVs=%u VPs=%u Scissors=%u  pool=%zu links=%zu",
                  label, active_hash, chain_hits, srv_swapped, rtv_count, vp_count, scissors_count,
                  game_device_data.upscale_tracking.pool.size(),
                  game_device_data.upscale_tracking.frame_links.size());
               Log_Debug(reshade::log::level::info, log_buf);

               // Log scissor rect state: whether enabled and first scissor rect details
               {
                  UINT num_scissors = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                  D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
                  native_device_context->RSGetScissorRects(&num_scissors, scissors);
                  if (num_scissors > 0)
                  {
                     const D3D11_RECT& first = scissors[0];
                     snprintf(log_buf, sizeof(log_buf),
                        "[FFXV UpscaleChain]  scissors_enabled=true  count=%u  first=[%d, %d, %d, %d] (%dx%d)",
                        num_scissors, first.left, first.top, first.right, first.bottom,
                        first.right - first.left, first.bottom - first.top);
                     Log_Debug(reshade::log::level::info, log_buf);
                  }
                  else
                  {
                     snprintf(log_buf, sizeof(log_buf),
                        "[FFXV UpscaleChain]  scissors_enabled=false  (no scissor rects set)");
                     Log_Debug(reshade::log::level::info, log_buf);
                  }
               }

               // Per-resource detail: one line per upgraded SRV or RTV
               for (const auto& d : swap_details)
               {
                  snprintf(log_buf, sizeof(log_buf),
                     "[FFXV UpscaleChain]   shader=%08X  %s slot=%u  orig=%p  tex=%p  view=%p  %ux%u fmt=%u",
                     active_hash,
                     d.is_rtv ? "RTV" : "SRV",
                     d.slot,
                     d.original,
                     d.texture_ptr,
                     d.replacement,
                     d.width, d.height, d.format);
                  Log_Debug(reshade::log::level::info, log_buf);
               }
            }
#endif
            return DrawOrDispatchOverrideType::Replaced;
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   static void UpdateLODBias(reshade::api::device* device)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);

         const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            // Reset to default (our mip offset is additive, so this is neutral)
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
         const auto new_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;

         bool texture_mip_lod_bias_offset_changed = prev_texture_mip_lod_bias_offset != new_texture_mip_lod_bias_offset;
         // Re-create all samplers immediately here instead of doing it at the end of the frame.
         // This allows us to avoid possible (but very unlikely) hitches that could happen if we re-created a new sampler for a new resolution later on when samplers descriptors are set.
         // It also allows us to use the right samplers for this frame's resolution.
         if (texture_mip_lod_bias_offset_changed)
         {
            ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
            for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
            {
               if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset))
                  continue; // Skip "resolutions" that already got their custom samplers created
               ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
               shared_lock_samplers.unlock(); // This is fine!
               {
                  D3D11_SAMPLER_DESC native_desc;
                  native_sampler->GetDesc(&native_desc);
                  com_ptr<ID3D11SamplerState> custom_sampler = CreateCustomSampler(device_data, native_device, native_desc);
                  const std::unique_lock unique_lock_samplers(s_mutex_samplers);
                  samplers_handle.second[new_texture_mip_lod_bias_offset] = custom_sampler;
               }
               shared_lock_samplers.lock();
            }
         }
      }
   }

   static bool ExecuteUpscaledCopy(const char* hook_name, ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource)
   {
      if (!native_device || !device_data.game)
         return false;

      auto& game_device_data = GetGameDeviceData(device_data);
      auto& tracking = game_device_data.upscale_tracking;

      if (!tracking.post_taa_upscale_active)
         return false;

#if DEVELOPMENT || TEST
      {
         size_t pool_size = 0;
         size_t link_count = 0;
         {
            std::lock_guard lock(tracking.mutex);
            pool_size = tracking.pool.size();
            link_count = tracking.frame_links.size();
         }
         char buf[384];
         snprintf(buf, sizeof(buf),
            "[FFXV UpscaleChain] %s src=%p dst=%p active=%d pool=%zu links=%zu",
            hook_name,
            (void*)src_resource, (void*)dst_resource,
            tracking.post_taa_upscale_active ? 1 : 0,
            pool_size, link_count);
         Log_Debug(reshade::log::level::info, buf);
      }
#endif

      ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(src_resource);
      ID3D11Resource* dest_resource = reinterpret_cast<ID3D11Resource*>(dst_resource);

      if (!source_resource || !dest_resource)
         return false;

      const uintptr_t source_key = reinterpret_cast<uintptr_t>(source_resource);
      UpscaledResource* upscaled_source = GetLinkedUpscaled(tracking, source_key);
      if (!upscaled_source || !upscaled_source->texture)
      {
#if DEVELOPMENT || TEST
         char buf[384];
         snprintf(buf, sizeof(buf),
            "[FFXV UpscaleChain] %s src={:#x} NOT_FOUND in frame_links - skipping",
            hook_name,
            source_key);
         Log_Debug(reshade::log::level::info, buf);
#endif
         return false;
      }

      const uintptr_t dest_key = reinterpret_cast<uintptr_t>(dest_resource);
      bool dest_already_linked = false;
      {
         std::lock_guard lock(tracking.mutex);
         dest_already_linked = tracking.frame_links.find(dest_key) != tracking.frame_links.end();
      }

      ComPtr<ID3D11DeviceContext> native_context;
      native_device->GetImmediateContext(native_context.put());
      if (!native_context)
         return false;

      UpscaledResource* upscaled_dest = LinkUpscaledResource(
         native_device, native_context.get(),
         dest_resource,
         tracking,
         device_data.output_resolution);
      if (!upscaled_dest || !upscaled_dest->texture)
      {
#if DEVELOPMENT || TEST
         char buf[384];
         snprintf(buf, sizeof(buf),
            "[FFXV UpscaleChain] %s dest={:#x} LinkUpscaledResource FAILED",
            hook_name,
            dest_key);
         Log_Debug(reshade::log::level::warning, buf);
#endif
         return false;
      }

#if DEVELOPMENT || TEST
      {
         D3D11_TEXTURE2D_DESC src_desc, dst_desc;
         upscaled_source->texture->GetDesc(&src_desc);
         upscaled_dest->texture->GetDesc(&dst_desc);
         char buf[384];
         snprintf(buf, sizeof(buf),
            "[FFXV UpscaleChain] %s copying upscaled src=%p(%ux%u fmt=%u) -> dst=%p(%ux%u fmt=%u) dest_was_linked=%d",
            hook_name,
            upscaled_source->texture.get(), src_desc.Width, src_desc.Height, (uint32_t)src_desc.Format,
            upscaled_dest->texture.get(), dst_desc.Width, dst_desc.Height, (uint32_t)dst_desc.Format,
            dest_already_linked ? 1 : 0);
         Log_Debug(reshade::log::level::info, buf);
      }
#endif

      native_context->CopyResource(upscaled_dest->texture.get(), upscaled_source->texture.get());

      return true;
   }

   bool OverrideCopyResource(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource) override
   {
      if (!native_device)
         return false;

      if (!device_data.game)
         return false;

      return ExecuteUpscaledCopy("OverrideCopyResource", native_device, device_data, dst_resource, src_resource);
   }

   bool OverrideCopyTextureRegion(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint32_t dst_subresource, const D3D11_BOX* dst_box, uint64_t& src_resource, uint32_t src_subresource, const D3D11_BOX* src_box) override
   {
      if (!native_device)
         return false;

      if (!device_data.game)
         return false;

      auto& game_device_data = GetGameDeviceData(device_data);
      auto& tracking = game_device_data.upscale_tracking;
      if (!tracking.post_taa_upscale_active)
         return false;

#if DEVELOPMENT || TEST
      {
         char buf[384];
         snprintf(buf, sizeof(buf),
            "[FFXV UpscaleChain] OverrideCopyTextureRegion src=%p dst=%p src_sub=%u dst_sub=%u src_box=%p dst_box=%p",
            (void*)src_resource, (void*)dst_resource,
            src_subresource, dst_subresource,
            (const void*)src_box, (const void*)dst_box);
         Log_Debug(reshade::log::level::info, buf);
      }
#endif

      // Only intercept full-texture copies (subresource 0, no source/dest box) that
      // act like a CopyResource. Partial copies are left to the original call.
      if (src_subresource != 0 || dst_subresource != 0 || src_box != nullptr || dst_box != nullptr)
      {
#if DEVELOPMENT || TEST
         Log_Debug(reshade::log::level::info, "[FFXV UpscaleChain] OverrideCopyTextureRegion skipped: partial/boxed copy");
#endif
         return false;
      }

      ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(src_resource);
      ID3D11Resource* dest_resource = reinterpret_cast<ID3D11Resource*>(dst_resource);
      if (!source_resource || !dest_resource)
         return false;

      // Keep CopyTextureRegion interception conservative: only reuse already linked resources.
      // Avoid creating new pooled links on this path, which can happen on load-time copies.
      const uintptr_t source_key = reinterpret_cast<uintptr_t>(source_resource);
      const uintptr_t dest_key = reinterpret_cast<uintptr_t>(dest_resource);
      UpscaledResource* upscaled_source = GetLinkedUpscaled(tracking, source_key);
      if (!upscaled_source || !upscaled_source->texture)
      {
#if DEVELOPMENT || TEST
         Log_Debug(reshade::log::level::info, "[FFXV UpscaleChain] OverrideCopyTextureRegion skipped: source link missing");
#endif
         return false;
      }

      ComPtr<ID3D11DeviceContext> native_context;
      native_device->GetImmediateContext(native_context.put());
      if (!native_context)
         return false;

      UpscaledResource* upscaled_dest = GetLinkedUpscaled(tracking, dest_key);
      if (!upscaled_dest || !upscaled_dest->texture)
      {
         // For chain continuity, create a destination link only for safe full-resource Texture2D copies.
         ComPtr<ID3D11Texture2D> src_tex;
         ComPtr<ID3D11Texture2D> dst_tex;
         if (FAILED(source_resource->QueryInterface(src_tex.put())) || FAILED(dest_resource->QueryInterface(dst_tex.put())) || !src_tex || !dst_tex)
         {
#if DEVELOPMENT || TEST
            Log_Debug(reshade::log::level::info, "[FFXV UpscaleChain] OverrideCopyTextureRegion skipped: non-Texture2D copy target");
#endif
            return false;
         }

         D3D11_TEXTURE2D_DESC src_orig_desc{}, dst_orig_desc{};
         src_tex->GetDesc(&src_orig_desc);
         dst_tex->GetDesc(&dst_orig_desc);

         const bool safe_full_copy_match =
            src_orig_desc.Width == dst_orig_desc.Width &&
            src_orig_desc.Height == dst_orig_desc.Height &&
            src_orig_desc.MipLevels == dst_orig_desc.MipLevels &&
            src_orig_desc.ArraySize == dst_orig_desc.ArraySize &&
            src_orig_desc.SampleDesc.Count == dst_orig_desc.SampleDesc.Count &&
            src_orig_desc.SampleDesc.Quality == dst_orig_desc.SampleDesc.Quality &&
            AreFormatsCopyCompatible(src_orig_desc.Format, dst_orig_desc.Format);

         if (!safe_full_copy_match)
         {
#if DEVELOPMENT || TEST
            Log_Debug(reshade::log::level::info, "[FFXV UpscaleChain] OverrideCopyTextureRegion skipped: source/dest descriptors not copy-compatible");
#endif
            return false;
         }

         upscaled_dest = LinkUpscaledResource(
            native_device, native_context.get(),
            dest_resource,
            tracking,
            device_data.output_resolution);
         if (!upscaled_dest || !upscaled_dest->texture)
         {
#if DEVELOPMENT || TEST
            Log_Debug(reshade::log::level::warning, "[FFXV UpscaleChain] OverrideCopyTextureRegion failed: could not create destination upscaled link");
#endif
            return false;
         }

#if DEVELOPMENT || TEST
         Log_Debug(reshade::log::level::info, "[FFXV UpscaleChain] OverrideCopyTextureRegion created destination link for chain continuity");
#endif
      }

#if DEVELOPMENT || TEST
      {
         D3D11_TEXTURE2D_DESC src_desc{}, dst_desc{};
         upscaled_source->texture->GetDesc(&src_desc);
         upscaled_dest->texture->GetDesc(&dst_desc);
         char buf[384];
         snprintf(buf, sizeof(buf),
            "[FFXV UpscaleChain] OverrideCopyTextureRegion copying linked upscaled src=%p(%ux%u fmt=%u) -> dst=%p(%ux%u fmt=%u)",
            upscaled_source->texture.get(), src_desc.Width, src_desc.Height, (uint32_t)src_desc.Format,
            upscaled_dest->texture.get(), dst_desc.Width, dst_desc.Height, (uint32_t)dst_desc.Format);
         Log_Debug(reshade::log::level::info, buf);
      }
#endif

      native_context->CopyResource(upscaled_dest->texture.get(), upscaled_source->texture.get());
      return true;
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (game_device_data.found_taa_cb || !game_device_data.has_drawn_autoexposure)
         return;

      // No need to convert to native DX11 flags
      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);
         if (buffer_desc.ByteWidth == CBTemporalAA_buffer_size)
         {
            Log_Debug(reshade::log::level::info, "Found TAA CBTemporalAA candidate");
            game_device_data.cb_taa_buffer = buffer;
            game_device_data.cb_taa_buffer_map_data = *data;
         }
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!game_device_data.found_taa_cb)
      {
         if (!game_device_data.found_taa_cb && game_device_data.cb_taa_buffer == buffer && game_device_data.cb_taa_buffer_map_data != nullptr)
         {
            CheckAndExtractTAABuffer(device, resource);
            UpdateLODBias(device);
         }
      }
      if (game_device_data.cb_taa_buffer_map_data != nullptr)
         game_device_data.cb_taa_buffer_map_data = nullptr;
      if (game_device_data.cb_taa_buffer.get() != nullptr)
         game_device_data.cb_taa_buffer = nullptr;
   }

   static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      uint64_t buffer_size = size;

      if (game_device_data.has_processed_view_buffer || buffer == nullptr || !device_data.taa_detected)
      {
         return false;
      }

      D3D11_BUFFER_DESC buffer_desc;
      buffer->GetDesc(&buffer_desc);
      buffer_size = buffer_desc.ByteWidth;

      if (buffer_size != CBView_buffer_size)
      {
         return false;
      }

      // if (game_device_data.found_per_view_globals && buffer == game_device_data.cached_view_buffer)
      // {
      //    ExtractCameraData(game_device_data, data);
      //    game_device_data.has_processed_view_buffer = true;
      // }

      if (!game_device_data.found_per_view_globals)
      {
         CheckAndExtractPerViewGlobalsBuffer(device, resource, data);
      }

      return false;
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!game_device_data.taa_cb_data)
         return;

      // Copy the motion matrix byte-for-byte - the game's cbuffer already has the matrix
      // in the correct format for the shader (we're just replicating what the game does)
      data.GameData.isUpscaling = game_device_data.upscale_tracking.post_taa_upscale_active ? 1 : 0;
   }
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataFFXV;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.upscale_tracking.InvalidatePoolIfOutputResolutionChanged(device_data.output_resolution);

      // Clear per-frame source links while keeping persistent pooled allocations.
      game_device_data.upscale_tracking.ResetFrame();

#if DEVELOPMENT || TEST
      game_device_data.dbg_replaced_srvs = 0;
      game_device_data.dbg_replaced_rtvs = 0;
      game_device_data.dbg_replaced_viewports = 0;
      game_device_data.dbg_replaced_scissors = 0;
#endif

      game_device_data.has_drawn_upscaling = false;
      game_device_data.has_drawn_autoexposure = false;
      game_device_data.found_taa_cb = false;
      game_device_data.has_processed_view_buffer = false;
      game_device_data.found_per_view_globals = false;
   }

#if DEVELOPMENT || TEST
   // You can print game specific information here (e.g. the weapon FOV, once you got access to the projection matrix)
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // build table with information captured from the game
      if (ImGui::BeginTable("FFXV Info Table", 2, ImGuiTableFlags_Borders))
      {
         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Render Resolution");
         ImGui::TableNextColumn();
         ImGui::Text("%d x %d", static_cast<uint>(device_data.render_resolution.x), static_cast<uint>(device_data.render_resolution.y));

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Output Resolution");
         ImGui::TableNextColumn();
         ImGui::Text("%d x %d", static_cast<uint>(device_data.output_resolution.x), static_cast<uint>(device_data.output_resolution.y));

         float fov_deg = game_device_data.camera_fov * (180.0f / 3.14159265f);
         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Camera FOV");
         ImGui::TableNextColumn();
         ImGui::Text("%.2f / %.2f", game_device_data.camera_fov, fov_deg);

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Camera Near Plane");
         ImGui::TableNextColumn();
         ImGui::Text("%.2f", game_device_data.camera_near);

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Camera Far Plane");
         ImGui::TableNextColumn();
         ImGui::Text("%.2f", game_device_data.camera_far);

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("TAA Jitter X");
         ImGui::TableNextColumn();
         ImGui::Text("%.4f", game_device_data.taa_jitters.x);

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("TAA Jitter Y");
         ImGui::TableNextColumn();
         ImGui::Text("%.4f", game_device_data.taa_jitters.y);

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Projection Jitter X");
         ImGui::TableNextColumn();
         ImGui::Text("%.4f", game_device_data.projection_jitters.x);

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Projection Jitter Y");
         ImGui::TableNextColumn();
         ImGui::Text("%.4f", game_device_data.projection_jitters.y);

         // Add more rows as needed for other information

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Upscale Pool Size");
         ImGui::TableNextColumn();
         ImGui::Text("%zu", game_device_data.upscale_tracking.pool.size());

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Upscale Links (frame)");
         ImGui::TableNextColumn();
         ImGui::Text("%zu", game_device_data.upscale_tracking.frame_links.size());

         ImGui::TableNextRow();
         ImGui::TableNextColumn();
         ImGui::Text("Replaced SRVs / RTVs / VPs / Scissors");
         ImGui::TableNextColumn();
         ImGui::Text("%u / %u / %u / %u", game_device_data.dbg_replaced_srvs, game_device_data.dbg_replaced_rtvs, game_device_data.dbg_replaced_viewports, game_device_data.dbg_replaced_scissors);

         ImGui::EndTable();
      }
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::Text("FFXV Luma mod - about and credits section", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Final Fantasy XV Luma Edition");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("75DFE4B0", nullptr, 16)); // Main game tonemapping
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("18EF8C72", nullptr, 16)); // Title screen tonemapping
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("DD4C5B74", nullptr, 16)); // Post-processing / swapchain
      shader_hashes_autoexposure.compute_shaders.emplace(std::stoul("42D0E27F", nullptr, 16));
      shader_hashes_autoexposure.compute_shaders.emplace(std::stoul("0E10B96C", nullptr, 16));
      shader_hashes_upscale.pixel_shaders.emplace(std::stoul("1B6C8C68", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("0DF0A97D", nullptr, 16));

#if DEVELOPMENT
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled; // We don't need swapchain upgrade for this game
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;                      // 1 = scrgb
#endif
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)\n2 - Raw HDR (Untonemapped)\nThe HDR tonemapper works for SDR too\nThis games uses a filmic tonemapper, which slightly crushes blacks", 2},
      };

      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

#if DEVELOPMENT
      // These make things messy in this game, given it renders at lower resolutions and then upscales and adds black bars beyond 16:9
      debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Fullscreen;

      forced_shader_names.emplace(std::stoul("0DF0A97D", nullptr, 16), "TAA");
      forced_shader_names.emplace(std::stoul("75DFE4B0", nullptr, 16), "Tonemap");
      forced_shader_names.emplace(std::stoul("18EF8C72", nullptr, 16), "Tonemap_TitleScreen");
      forced_shader_names.emplace(std::stoul("1040DAB1", nullptr, 16), "MotionVectorDecode");
      forced_shader_names.emplace(std::stoul("42D0E27F", nullptr, 16), "AutoExposure");
      forced_shader_names.emplace(std::stoul("0E10B96C", nullptr, 16), "AutoExposure (Nights)");
      forced_shader_names.emplace(std::stoul("1B6C8C68", nullptr, 16), "Upscale");
      forced_shader_names.emplace(std::stoul("850830F0", nullptr, 16), "Dirt");
#endif

      texture_upgrade_formats = {
         reshade::api::format::r11g11b10_float};
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      auto_texture_format_upgrade_shader_hashes[std::stoul("75DFE4B0", nullptr, 16)] = {{0}, {}}; // Main game tonemapping
      auto_texture_format_upgrade_shader_hashes[std::stoul("18EF8C72", nullptr, 16)] = {{0}, {}}; // Title screen tonemapping
      auto_texture_format_upgrade_shader_hashes[std::stoul("DD4C5B74", nullptr, 16)] = {{0}, {}}; // Post-processing / swapchain
      game = new FinalFantasyXV();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(FinalFantasyXV::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(FinalFantasyXV::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(FinalFantasyXV::OnUpdateBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
