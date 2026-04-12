#include <cstddef>
#define GAME_FINALFANTASYXV 1

#define ALLOW_SHADERS_DUMPING 0
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define DEBUG_LOG 0

#include "..\..\Core\core.hpp"
#include "includes\cbuffers.h"

namespace
{
   float2 projection_jitters = {0, 0};
   // const uint32_t shader_hash_mvec_pixel = std::stoul("FFFFFFF3", nullptr, 16);
   ShaderHashesList shader_hashes_tonemap;
   ShaderHashesList shader_hashes_autoexposure;
   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_upscale;
   const uint32_t CBTemporalAA_buffer_size = 256;
   const uint32_t CBView_buffer_size = 768;

} // namespace

struct GameDeviceDataFFXV final : public GameDeviceData
{
#if ENABLE_SR
   // SR - Resources extracted from TAA pass (may be reused by game)
   com_ptr<ID3D11Resource> sr_motion_vectors;
   com_ptr<ID3D11Resource> sr_source_color;
   com_ptr<ID3D11Resource> depth_buffer;
   com_ptr<ID3D11RenderTargetView> sr_motion_vectors_rtv;
   com_ptr<ID3D11Texture2D> exposure_texture;
   com_ptr<ID3D11Texture2D> sr_depth_backup;

#endif // ENABLE_SR
   std::atomic<bool> has_drawn_upscaling = false;
   std::atomic<bool> has_drawn_autoexposure = false;
   std::atomic<bool> found_taa_cb = false;
   std::atomic<bool> found_per_view_globals = false;
   std::atomic<bool> use_exposure_texture = false;
   std::unique_ptr<cbTemporalAA> taa_cb_data;

   com_ptr<ID3D11Buffer> cb_taa_buffer;
   void* cb_taa_buffer_map_data = nullptr;

   // Cached view buffer (once found)
   ID3D11Buffer* cached_view_buffer = nullptr;

   // Extracted camera data
   float camera_fov = 60.0f * (3.14159265f / 180.0f); // Default to 60 degrees in radians
   float camera_near = 0.1f;
   float camera_far = 1000.0f;

   // Upscaling support - when game uses render scale < 100%
   std::atomic<bool> is_using_upscaling = false;       // True if render resolution < output resolution
   std::atomic<bool> has_cached_taa_resources = false; // True if we've cached MVs/depth from TAA pass
   bool has_processed_view_buffer = false;
};

class FinalFantasyXV final : public Game // ### Rename this to your game's name ###
{
   static GameDeviceDataFFXV& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataFFXV*>(device_data.game);
   }

#include "includes\dlss_helpers.hpp"

public:
   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs PS"), ShaderDefinition{"Luma_FFXV_MotionVec_Decode", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Fullscreen VS"), ShaderDefinition{"Luma_FFXV_Fullscreen_VS", reshade::api::pipeline_subobject_type::vertex_shader});
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

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Extract exposure texture from autoexposure pass
      if (original_shader_hashes.Contains(shader_hashes_autoexposure))
      {
#if DEVELOPMENT && DEBUG_LOG
         reshade::log::message(
            reshade::log::level::info,
            "FFXV Autoexposure pass detected - extracting exposure texture");
#endif // DEVELOPMENT
         game_device_data.has_drawn_autoexposure = true;
         //    game_device_data.use_exposure_texture = ExtractExposureTexture(native_device, native_device_context, game_device_data);
         game_device_data.use_exposure_texture = true;
         return DrawOrDispatchOverrideType::None;
      }

      // =========================================================================
      // TAA Pass Handling
      // =========================================================================
      // If using upscaling: only cache MVs/depth, skip drawing SR (SR will be drawn in upscale pass)
      // If not using upscaling: replace TAA with SR (DLAA/FSRAA mode)
      if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && original_shader_hashes.Contains(shader_hashes_TAA))
      {
#if DEVELOPMENT && DEBUG_LOG
         reshade::log::message(
            reshade::log::level::info,
            "FFXV TAA pass detected - extracting resources for SR");
#endif // DEVELOPMENT
         device_data.taa_detected = true;
         if (!game_device_data.found_taa_cb || !game_device_data.has_processed_view_buffer)
         {
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }
         // Check if the motion vector decode shader is available
         if (device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs PS")].get() == nullptr)
         {
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }

         // Extract TAA shader resources (source color, depth, motion vectors)
         com_ptr<ID3D11ShaderResourceView> depth_srv;
         com_ptr<ID3D11ShaderResourceView> velocity_srv;
         if (!ExtractTAAShaderResources(native_device, native_device_context, game_device_data, &depth_srv, &velocity_srv, game_device_data.is_using_upscaling))
         {
            ASSERT_ONCE(false);
            return DrawOrDispatchOverrideType::None;
         }

         // Setup motion vector decode target
         if (!SetupMotionVectorDecodeTarget(native_device, game_device_data, velocity_srv.get()))
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Update Luma constant buffers if not already done
         if (!updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

         // Cache state before motion vector decode
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Decode motion vectors
         DecodeMotionVectors(
            native_device_context,
            cmd_list_data,
            device_data,
            depth_srv.get(),
            velocity_srv.get(),
            game_device_data.sr_motion_vectors_rtv.get());

         // Restore state after motion vector decode
         {
            ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            native_device_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
            // Motion Vector decode uses VS/PS pipeline, but good to clear CS inputs too if we ran CS before
            native_device_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
            ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);
         }
         draw_state_stack.Restore(native_device_context);
         compute_state_stack.Restore(native_device_context);

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

         // Mark that we've cached the TAA resources
         game_device_data.has_cached_taa_resources = true;

         // If using upscaling, don't draw SR here - let the upscale pass handle it
         if (game_device_data.is_using_upscaling)
         {
#if DEVELOPMENT && DEBUG_LOG
            reshade::log::message(
               reshade::log::level::info,
               "Skipping SR draw in TAA pass (will be drawn in upscale pass)");
#endif // DEVELOPMENT
            return DrawOrDispatchOverrideType::Skip;
         }

         // =====================================================================
         // Not using upscaling - replace TAA with SR (DLAA/FSRAA mode)
         // =====================================================================
         game_device_data.has_drawn_upscaling = true;

         // Get render targets
         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);

         ID3D11RenderTargetView* output_rtv = render_target_views[0].get();
         if (!output_rtv)
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Setup output texture
         com_ptr<ID3D11Texture2D> output_color;
         D3D11_TEXTURE2D_DESC output_texture_desc;
         bool output_supports_uav = false;
         bool output_changed = false;

         if (!SetupSROutput(native_device, device_data, output_rtv, output_color, output_texture_desc, output_supports_uav, output_changed))
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get SR instance data
         auto* sr_instance_data = device_data.GetSRInstanceData();
         if (!sr_instance_data)
         {
            ASSERT_ONCE(false);
            if (output_supports_uav)
               device_data.sr_output_color = nullptr;
            return DrawOrDispatchOverrideType::None;
         }

         // Calculate render resolution
         //    std::array<uint32_t, 2> render_resolution = FindClosestIntegerResolutionForAspectRatio(
         //        (double)output_texture_desc.Width * (double)device_data.sr_render_resolution_scale,
         //        (double)output_texture_desc.Height * (double)device_data.sr_render_resolution_scale,
         //        (double)output_texture_desc.Width / (double)output_texture_desc.Height);

         // Update SR settings
         SR::SettingsData settings_data;
         settings_data.output_width = output_texture_desc.Width;
         settings_data.output_height = output_texture_desc.Height;
         settings_data.render_width = output_texture_desc.Width;
         settings_data.render_height = output_texture_desc.Height;
         settings_data.dynamic_resolution = false;
         settings_data.hdr = true;
         settings_data.auto_exposure = !game_device_data.use_exposure_texture;
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
         draw_data.pre_exposure = 0.0f;
         draw_data.jitter_x = projection_jitters.x;
         draw_data.jitter_y = projection_jitters.y;
         draw_data.vert_fov = game_device_data.camera_fov;
         draw_data.far_plane = game_device_data.camera_far;
         draw_data.near_plane = game_device_data.camera_near;
         draw_data.reset = reset_sr;
         draw_data.render_width = output_texture_desc.Width;
         draw_data.render_height = output_texture_desc.Height;

         // Cache state for SR
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Execute SR
         bool sr_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

         // Restore full graphics state
         {
            ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            native_device_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
            native_device_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
            ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
            native_device_context->CSSetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, null_uavs, nullptr);
            ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);
         }
         draw_state_stack.Restore(native_device_context);
         compute_state_stack.Restore(native_device_context);

         if (sr_succeeded)
         {
            device_data.has_drawn_sr = true;
         }

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
            // Copy result to output if we created a separate UAV-capable texture
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
         else
         {
            device_data.force_reset_sr = true;
         }

         if (output_supports_uav)
         {
            device_data.sr_output_color = nullptr;
         }
      }

      // =========================================================================
      // Upscale Pass Handling (when game uses render scale < 100%)
      // =========================================================================
      // Replace the game's upscale pass with SR using cached MVs/depth from TAA
      if (!game_device_data.has_drawn_upscaling && game_device_data.is_using_upscaling &&
          game_device_data.has_cached_taa_resources && device_data.sr_type != SR::Type::None &&
          !device_data.sr_suppressed && original_shader_hashes.Contains(shader_hashes_upscale))
      {
#if DEVELOPMENT && DEBUG_LOG
         reshade::log::message(
            reshade::log::level::info,
            "FFXV Upscale pass detected - executing SR");
#endif // DEVELOPMENT
         if (!game_device_data.found_taa_cb || !game_device_data.has_processed_view_buffer)
         {
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }
         game_device_data.has_drawn_upscaling = true;

         // Get render targets - these should be at output resolution
         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);

         ID3D11RenderTargetView* output_rtv = render_target_views[0].get();
         if (!output_rtv)
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get the input color texture from the upscale pass (slot 0)
         com_ptr<ID3D11ShaderResourceView> ps_shader_resources[4];
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

         if (!ps_shader_resources[0].get())
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Use the upscale pass input as the source color for SR
         com_ptr<ID3D11Resource> upscale_source_color;
         ps_shader_resources[0]->GetResource(&upscale_source_color);

         if (!upscale_source_color.get())
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Setup output texture
         com_ptr<ID3D11Texture2D> output_color;
         D3D11_TEXTURE2D_DESC output_texture_desc;
         bool output_supports_uav = false;
         bool output_changed = false;

         if (!SetupSROutput(native_device, device_data, output_rtv, output_color, output_texture_desc, output_supports_uav, output_changed))
         {
            return DrawOrDispatchOverrideType::None;
         }

         // Get SR instance data
         auto* sr_instance_data = device_data.GetSRInstanceData();
         if (!sr_instance_data)
         {
            ASSERT_ONCE(false);
            if (output_supports_uav)
               device_data.sr_output_color = nullptr;
            return DrawOrDispatchOverrideType::None;
         }

         // Get the input texture resolution (this is the actual render resolution)
         com_ptr<ID3D11Texture2D> source_color_texture;
         HRESULT hr = upscale_source_color->QueryInterface(&source_color_texture);
         if (FAILED(hr))
         {
            if (output_supports_uav)
               device_data.sr_output_color = nullptr;
            return DrawOrDispatchOverrideType::None;
         }

         D3D11_TEXTURE2D_DESC source_texture_desc;
         source_color_texture->GetDesc(&source_texture_desc);

         // Use the actual input resolution as render resolution for SR
         uint32_t render_width = source_texture_desc.Width;
         uint32_t render_height = source_texture_desc.Height;

         // Update SR settings
         SR::SettingsData settings_data;
         settings_data.output_width = output_texture_desc.Width;
         settings_data.output_height = output_texture_desc.Height;
         settings_data.render_width = render_width;
         settings_data.render_height = render_height;
         settings_data.dynamic_resolution = false;
         settings_data.hdr = true; // FFXV does SR before tonemapping
         settings_data.inverted_depth = false;
         settings_data.mvs_jittered = false;
         settings_data.auto_exposure = !game_device_data.use_exposure_texture;
         settings_data.render_preset = dlss_render_preset;
         sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

         // Prepare SR draw data
         bool reset_sr = device_data.force_reset_sr || output_changed;
         device_data.force_reset_sr = false;

         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.source_color = upscale_source_color.get(); // Use upscale pass input
         draw_data.output_color = device_data.sr_output_color.get();
         draw_data.motion_vectors = game_device_data.sr_motion_vectors.get(); // From TAA pass
         draw_data.depth_buffer = game_device_data.sr_depth_backup.get();     // From TAA pass
         if (game_device_data.use_exposure_texture)
            draw_data.exposure = game_device_data.exposure_texture.get();
         draw_data.pre_exposure = 0.0f;
         draw_data.jitter_x = projection_jitters.x;
         draw_data.jitter_y = projection_jitters.y;
         draw_data.vert_fov = game_device_data.camera_fov;
         draw_data.far_plane = game_device_data.camera_far;
         draw_data.near_plane = game_device_data.camera_near;
         draw_data.reset = reset_sr;
         draw_data.render_width = render_width;
         draw_data.render_height = render_height;

         // Cache state for SR
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Execute SR
         bool sr_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

         // Restore full graphics state
         {
            ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            native_device_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
            native_device_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
            ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
            native_device_context->CSSetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, null_uavs, nullptr);
            ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);
         }
         draw_state_stack.Restore(native_device_context);
         compute_state_stack.Restore(native_device_context);

         if (sr_succeeded)
         {
            device_data.has_drawn_sr = true;
         }

#if DEVELOPMENT
         // Add trace info for DLSS/FSR execution (upscale path)
         if (device_data.has_drawn_sr)
         {
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = device_data.sr_type == SR::Type::DLSS ? "DLSS (Upscale)" : "FSR (Upscale)";
               GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
         }
#endif

         // Clear cached resources
         game_device_data.depth_buffer = nullptr;

         // Handle SR result
         if (device_data.has_drawn_sr)
         {
            // Copy result to output if we created a separate UAV-capable texture
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
         else
         {
            device_data.force_reset_sr = true;
         }

         if (output_supports_uav)
         {
            device_data.sr_output_color = nullptr;
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

#if DEVELOPMENT && DEBUG_LOG
            reshade::log::message(
               reshade::log::level::info,
               "Mapping candidate TAA constant buffer at size 256 bytes");
#endif // DEVELOPMENT

            game_device_data.cb_taa_buffer = buffer;
            ASSERT_ONCE(!game_device_data.cb_taa_buffer_map_data);
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

      if (game_device_data.has_processed_view_buffer && game_device_data.found_taa_cb)
      {
         if (game_device_data.cb_taa_buffer_map_data != nullptr)
            game_device_data.cb_taa_buffer_map_data = nullptr;
         if (game_device_data.cb_taa_buffer.get() != nullptr)
            game_device_data.cb_taa_buffer = nullptr;
         return;
      }
      // Handle View Buffer Update (if already found)

      if (!game_device_data.found_taa_cb && game_device_data.cb_taa_buffer == buffer && game_device_data.cb_taa_buffer_map_data != nullptr)
      {
         CheckAndExtractTAABuffer(device, resource);
         UpdateLODBias(device);
      }
   }

   static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      uint64_t buffer_size = size;

      if ( game_device_data.has_processed_view_buffer || buffer == nullptr || !device_data.taa_detected)
      {
         return false;
      }

      if (size == UINT64_MAX)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);
         buffer_size = buffer_desc.ByteWidth;
      }

      if (size != CBView_buffer_size )
      {
         return false;
      }
      
      if (game_device_data.found_per_view_globals && buffer == game_device_data.cached_view_buffer)
      {
#if DEVELOPMENT && DEBUG_LOG
         reshade::log::message(
            reshade::log::level::info,
            "Updating cached View constant buffer at size 768 bytes");
#endif // DEVELOPMENT
         ExtractCameraData(game_device_data, data);
         game_device_data.has_processed_view_buffer = true;
      }

      if (!game_device_data.found_per_view_globals)
      {
#if DEVELOPMENT && DEBUG_LOG
         reshade::log::message(
            reshade::log::level::info,
            "Updating candidate Per-View Globals constant buffer at size 768 bytes");
#endif // DEVELOPMENT
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
      data.GameData.motMat.g_motionMatrix = game_device_data.taa_cb_data->g_motionMatrix;

      // Build g_jitterOfs from TAA cbuffer data:
      // The motion vector decode shader expects:
      //   g_jitterOfs.xy = jitter offset (UV space) - applied as: (jitter * 0.5) + motion
      //   g_jitterOfs.zw = scale to convert from UV to pixel space (screen dimensions)
      // DLSS expects pixel-space motion vectors, so we multiply UV-space motion by screen size.
      data.GameData.motMat.g_jitterOfs.x = game_device_data.taa_cb_data->g_uvJitterOffset.x;
      data.GameData.motMat.g_jitterOfs.y = game_device_data.taa_cb_data->g_uvJitterOffset.y;
      data.GameData.motMat.g_jitterOfs.z = game_device_data.taa_cb_data->g_screenSize.x;
      data.GameData.motMat.g_jitterOfs.w = game_device_data.taa_cb_data->g_screenSize.y;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataFFXV;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!game_device_data.has_drawn_upscaling)
      {
         device_data.force_reset_sr = true; // If the frame didn't draw the scene, DLSS needs to reset to prevent the old history from blending with the new scene
      }
      game_device_data.has_drawn_upscaling = false;
      game_device_data.has_drawn_autoexposure = false;
      game_device_data.found_taa_cb = false;
      game_device_data.has_cached_taa_resources = false;
      game_device_data.is_using_upscaling = false;
      game_device_data.has_processed_view_buffer = false;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("FFXV Luma mod - about and credits section", ""); // ### Rename this ###
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
      shader_hashes_upscale.pixel_shaders.emplace(std::stoul("1B6C8C68", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("0DF0A97D", nullptr, 16));

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled; // We don't need swapchain upgrade for this game
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;                      // 1 = scrgb

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
      forced_shader_names.emplace(std::stoul("1B6C8C68", nullptr, 16), "Upscale");

#endif

      // ### Check which of these are needed and remove the rest ###
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         // reshade::api::format::r8g8b8a8_unorm_srgb,
         // reshade::api::format::r8g8b8a8_typeless,
         // reshade::api::format::r8g8b8x8_unorm,
         // reshade::api::format::r8g8b8x8_unorm_srgb,
         reshade::api::format::b8g8r8a8_unorm,
         // reshade::api::format::b8g8r8a8_unorm_srgb,
         // reshade::api::format::b8g8r8a8_typeless,
         // reshade::api::format::b8g8r8x8_unorm,
         // reshade::api::format::b8g8r8x8_unorm_srgb,
         // reshade::api::format::b8g8r8x8_typeless,

         reshade::api::format::r11g11b10_float,
         reshade::api::format::r10g10b10a2_typeless,
         reshade::api::format::r10g10b10a2_unorm,
         // reshade::api::format::r16g16_float,
         // reshade::api::format::r16g16_unorm,
         // reshade::api::format::r32_g8_typeless
      };
      // ### Check these if textures are not upgraded ###
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

      game = new FinalFantasyXV();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(FinalFantasyXV::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(FinalFantasyXV::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(FinalFantasyXV::OnUpdateBufferRegion);
      //   reshade::unregister_addon<FinalFantasyXV>();
      //   delete game;
      //   game = nullptr;
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}