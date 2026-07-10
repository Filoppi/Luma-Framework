#define GAME_GRANBLUE_FANTASY_RELINK 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define JITTER_PHASES 8
#define PATCH_JITTER_TABLE_INIT
#define PATCH_SCENE_BUFFER 0
#define ENABLE_UI_VIEWPORT_SCALING_HOOK 0
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1

#include <d3d11.h>
#include "../../Core/core.hpp"
#include "includes/cbuffers.h"
#include "includes/common.hpp"
#include "includes/hooks.hpp"
#include "includes/sigscan.hpp"
#include "includes/safetyhook.hpp"
#include "includes/common.cpp"
#include "includes/sigscan.cpp"
#include "includes/hooks.cpp"

namespace
{
#include "includes/upscale.cpp"
#include "includes/postprocess.cpp"
#include "includes/ui_scale.cpp"

} // namespace

class GranblueFantasyRelink final : public Game
{
   static GameDeviceDataGBFR& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataGBFR*>(device_data.game);
   }
   static const GameDeviceDataGBFR& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataGBFR*>(device_data.game);
   }

public:
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& /*cmd_list_data*/, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Propagate frame jitter to all custom-shader invocations (PostTAA and others).
      const float resX = device_data.render_resolution.x;
      const float resY = device_data.render_resolution.y;
      if (resX > 0.f && resY > 0.f)
      {
         data.GameData.JitterOffset.x = game_device_data.table_jitter.x * 2.0f / resX;
         data.GameData.JitterOffset.y = game_device_data.table_jitter.y * -2.0f / resY;
         data.GameData.PrevJitterOffset.x = game_device_data.prev_table_jitter.x * 2.0f / resX;
         data.GameData.PrevJitterOffset.y = game_device_data.prev_table_jitter.y * -2.0f / resY;
      }
      data.GameData.IsTAARunning = IsTAARunningThisFrame() ? 1 : 0;
   }
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 9;
      luma_data_cbuffer_index = 10;

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_AFTER_TAA", '1', true, false, "If set to 1, tonemapping will be applied after TAA", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('3');

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Post Tonemap"),
         ShaderDefinition{"Luma_GBFR_Tonemap", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Cutscene Gamma"),
         ShaderDefinition{"Luma_GBFR_CutsceneGamma", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Cutscene ColorGrade"),
         ShaderDefinition{"Luma_GBFR_CutsceneColorGrade", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Cutscene Overlay Blend PS"),
         ShaderDefinition{"Luma_GBFR_CutsceneOverlayBlend_PS", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Post SR Encode"),
         ShaderDefinition{"Luma_GBFR_PostSREncode", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR UI Encode"),
         ShaderDefinition{"Luma_GBFR_UIEncode", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Pre SR Encode"),
         ShaderDefinition{"Luma_GBFR_PreSREncode", reshade::api::pipeline_subobject_type::pixel_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR Fullscreen UV VS"),
         ShaderDefinition{"Luma_GBFR_FullscreenUV_VS", reshade::api::pipeline_subobject_type::vertex_shader});

      native_shaders_definitions.emplace(
         CompileTimeStringHash("GBFR UI Background Copy"),
         ShaderDefinition{"Luma_GBFR_UIBackgroundCopy", reshade::api::pipeline_subobject_type::pixel_shader});
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::execute_secondary_command_list>(GranblueFantasyRelink::OnExecuteSecondaryCommandList);
         LoadConfigs();
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(
      ID3D11Device* native_device,
      ID3D11DeviceContext* native_device_context,
      CommandListData& cmd_list_data,
      DeviceData& device_data,
      reshade::api::shader_stage stages,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes,
      bool is_custom_pass,
      bool& updated_cbuffers,
      std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      bool is_taa_running = IsTAARunningThisFrame();
      if (is_taa_running && cb_luma_global_settings.GameSettings.IsTAARunning == 0)
      {
         cb_luma_global_settings.GameSettings.IsTAARunning = 1;
         device_data.cb_luma_global_settings_dirty = true;
      }

      const bool tonemap_after_taa = IsTonemapAfterTAAEnabled(false);

      if (original_shader_hashes.Contains(shader_hashes_Bloom))
      {
         ComPtr<ID3D11ShaderResourceView> ps_depth_srv;
         native_device_context->PSGetShaderResources(25, 1, ps_depth_srv.put());
         if (ps_depth_srv)
         {
            ps_depth_srv->GetResource(game_device_data.depth_buffer.put());
         }
      }
      // Since MotionBlur runs before TAA the game runs a smaller AA to dejitter the motion blur input
      // when we reorder the post process effects this is no longer needed and we can use the antialiased output from TAA/SR.
      if (original_shader_hashes.Contains(shader_hashes_MotionBlurDenoise) && tonemap_after_taa)
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_MotionBlur) && tonemap_after_taa)
      {
         game_device_data.motion_blur_seen = true;
         // Game runs motion blur shader twice with different parameters.
         if (game_device_data.motion_blur_invocation_count < 2)
         {
            const size_t pass_index = game_device_data.motion_blur_invocation_count;
            game_device_data.motion_blur_invocation_count++;
            CaptureMotionBlurReplayState(native_device_context, game_device_data, pass_index);

            if (game_device_data.motion_blur_replay_states[pass_index].valid)
            {
               if (pass_index == 0)
               {
                  game_device_data.motion_blur_first_pass_seen = true;
               }
               else
               {
                  game_device_data.motion_blur_second_pass_seen = true;
                  game_device_data.motion_blur_pending.store(true, std::memory_order_release);
               }
               return DrawOrDispatchOverrideType::Skip;
            }
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_Tonemap) && tonemap_after_taa)
      {
         game_device_data.exposure_texture = nullptr;
         game_device_data.exposure_texture_srv = nullptr;
         game_device_data.bloom_texture_srv = nullptr;

         // Capture the game's AdaptLuminance SRV directly from t2
         {
            ComPtr<ID3D11ShaderResourceView> current_exposure_srv;
            native_device_context->PSGetShaderResources(2, 1, current_exposure_srv.put());
            if (current_exposure_srv)
            {
               game_device_data.exposure_texture_srv = current_exposure_srv;

               // Extract the underlying texture so it can be passed to DLSS as an exposure hint.
               ComPtr<ID3D11Resource> resource;
               game_device_data.exposure_texture_srv->GetResource(resource.put());
               if (resource)
               {
                  resource->QueryInterface(game_device_data.exposure_texture.put());
               }
            }
         }

         // get bloom SRV from t3
         {
            ComPtr<ID3D11ShaderResourceView> current_bloom_srv;
            native_device_context->PSGetShaderResources(3, 1, current_bloom_srv.put());
            if (current_bloom_srv)
            {
               game_device_data.bloom_texture_srv = current_bloom_srv;
            }
         }

         game_device_data.tonemap_draw_pending.store(true, std::memory_order_release);
         game_device_data.tonemap_detected_context.store(native_device_context, std::memory_order_release);

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneGamma) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutscenePostPassReplayState(native_device_context, game_device_data.cutscene_gamma_replay_state);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_gamma_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneColorGrade) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutscenePostPassReplayState(native_device_context, game_device_data.cutscene_color_grade_replay_state);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_color_grade_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneOverlayModulate) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutsceneOverlayModulateReplayState(native_device_context, game_device_data);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_overlay_modulate_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_CutsceneOverlayBlend) && tonemap_after_taa && game_device_data.tonemap_detected_context.load(std::memory_order_acquire) == native_device_context)
      {
         CaptureCutsceneOverlayBlendReplayState(native_device_context, game_device_data);
         PassThroughToRenderTarget(native_device_context);
         game_device_data.cutscene_overlay_blend_pending.store(true, std::memory_order_release);
         return DrawOrDispatchOverrideType::Skip;
      }

      if (original_shader_hashes.Contains(shader_hashes_OutlineCS))
      {
         if (tonemap_after_taa)
         {
            CaptureOutlineReplayState(native_device_context, game_device_data);
            if (game_device_data.outline_replay_state.cs_depth_srv)
            {
               game_device_data.outline_replay_state.cs_depth_srv->GetResource(game_device_data.depth_buffer.put());
            }
            if (game_device_data.outline_replay_state.valid)
            {
               PassThroughToComputeUAV(native_device_context);
               game_device_data.outline_pending.store(true, std::memory_order_release);
               return DrawOrDispatchOverrideType::Skip;
            }
         }
         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_TAA))
      {
         device_data.taa_detected = true;
#if TEST || DEVELOPMENT
         game_device_data.taa_detected_this_frame = true;
#endif
         DrawOrDispatchOverrideType override_type = DrawOrDispatchOverrideType::None;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            override_type = [](GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, CommandListData& cmd_list_data) -> DrawOrDispatchOverrideType
            {
               if (!ExtractTAAShaderResources(native_device_context, game_device_data))
               {
                  ASSERT_ONCE_MSG(false, "ExtractTAAShaderResources: t3 (source color) or t23 (motion vectors) SRV not bound");
                  device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
                  LogExpectedCustomDrawSkipped("SR (TAA path)", "force_reset_sr set: ExtractTAAShaderResources failed (t3 or t23 not bound)");
#endif
                  return DrawOrDispatchOverrideType::None;
               }

               if (render_scale == 1.f)
               {
                  // Get render targets (TAA writes to RT0 and RT1)
                  ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
                  ID3D11DepthStencilView* dsv;
                  native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
                  if (rtvs[1] == nullptr)
                  {
                     device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped("SR (TAA path)", "rtvs[1]=null: TAA RTV1 not bound");
#endif
                     return DrawOrDispatchOverrideType::None;
                  }

                  if (!SetupSROutput(native_device, device_data, game_device_data, rtvs[1]))
                  {
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped("SR (TAA path)", "SetupSROutput failed: TAA output texture QueryInterface or min-resolution check failed");
#endif
                     return DrawOrDispatchOverrideType::None;
                  }

                  if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
                  {
                     const bool pre_sr_ok = DrawNativePreSREncodePass(native_device, native_device_context, cmd_list_data, device_data, game_device_data);
#if TEST || DEVELOPMENT
                     if (!pre_sr_ok)
                     {
                        std::string reason = "prerequisite missing:";
                        const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
                        const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Pre SR Encode"));
                        if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second)
                           reason += " copy_vs=missing;";
                        if (ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
                           reason += " pre_sr_encode_ps=missing;";
                        if (!game_device_data.sr_source_color.get())
                           reason += " sr_source_color=null;";
                        if (!game_device_data.sr_source_color_srv.get())
                           reason += " sr_source_color_srv=null;";
                        LogExpectedCustomDrawSkipped("PreSREncode (TAA path)", reason);
                     }
#endif
                  }

                  // Split here: partial_command_list captures all draws up to (not including) TAA,
                  // including the hash-replaced tonemap passthrough.
                  if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
                  {
                     native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());

                     if (game_device_data.modifiable_index_vertex_buffer)
                     {
                        D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                        native_device_context->Map(game_device_data.modifiable_index_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                        native_device_context->Unmap(game_device_data.modifiable_index_vertex_buffer.get(), 0);
                     }
                     game_device_data.draw_device_context = native_device_context;
                  }
                  else
                  {
                     game_device_data.draw_device_context = nullptr;
                     game_device_data.partial_command_list.reset();
                     RefreshFrameJitterForPostProcess(game_device_data);
                     RunLatePostProcessPasses(native_device_context, cmd_list_data, device_data, game_device_data);
                  }
               }
               return DrawOrDispatchOverrideType::Replaced;
            }(game_device_data, native_device, native_device_context, device_data, cmd_list_data);
         }
         else if (render_scale == 1.f)
         {
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            ID3D11DepthStencilView* dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            // Some non-SR paths can bind only RT0. Avoid null dereference in SetupTempTAAOutput.
            if (rtvs[1] == nullptr)
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!SetupTempTAAOutput(native_device, game_device_data, rtvs[1]))
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!game_device_data.taa_temp_output_rtv.get() || !game_device_data.taa_output_texture_rtv.get())
            {
               return DrawOrDispatchOverrideType::None;
            }

            rtvs[1] = game_device_data.taa_temp_output_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            (*original_draw_dispatch_func)();
            rtvs[1] = game_device_data.taa_output_texture_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            {
               game_device_data.draw_device_context = native_device_context;
               native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
            }
            else
            {
               game_device_data.draw_device_context = nullptr;
               game_device_data.partial_command_list.reset();
               RefreshFrameJitterForPostProcess(game_device_data);
               RunLatePostProcessPasses(native_device_context, cmd_list_data, device_data, game_device_data);
            }
            override_type = DrawOrDispatchOverrideType::Replaced;
         }
         else
         {
            // Run TAA normally and handle DLSS in TUP
            return DrawOrDispatchOverrideType::None;
         }

         return override_type;
      }

      if (original_shader_hashes.Contains(shader_hashes_Temporal_Upscale))
      {
         // Without an active SR implementation, leave the game's temporal upscale draw and
         // command-list scheduling untouched. Redirecting the native TUP through our temporary
         // post-AA target adds no SR value and can serialize the deferred path at sub-native scales.
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed)
         {
            return DrawOrDispatchOverrideType::None;
         }

         DrawOrDispatchOverrideType override_type = DrawOrDispatchOverrideType::Replaced;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            override_type = [](GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, CommandListData& cmd_list_data) -> DrawOrDispatchOverrideType
            {
               ID3D11RenderTargetView* rt;
               native_device_context->OMGetRenderTargets(1, &rt, nullptr);
#if TEST || DEVELOPMENT
               if (rt == nullptr)
               {
                  LogExpectedCustomDrawSkipped("SR (TUP path)", "rt=null: TUP RTV0 not bound; SR inputs will not be prepared");
               }
#endif
               if (rt != nullptr)
               {
                  if (!SetupSROutput(native_device, device_data, game_device_data, rt))
                  {
#if TEST || DEVELOPMENT
                     LogExpectedCustomDrawSkipped("SR (TUP path)", "SetupSROutput failed: TUP output texture QueryInterface or min-resolution check failed");
#endif
                     return DrawOrDispatchOverrideType::None;
                  }
                  // DrawNativePreSREncodePass reads sr_source_color which is written by TAA.
                  // TAA and TUP record on parallel deferred contexts so sr_source_color is not
                  // guaranteed valid here. Deferred to OnExecuteSecondaryCommandList where
                  // the TAA command list has already executed on the immediate context.
               }
               if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
               {
                  game_device_data.draw_device_context = native_device_context;
                  native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
                  if (game_device_data.modifiable_index_vertex_buffer)
                  {
                     D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                     native_device_context->Map(game_device_data.modifiable_index_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                     native_device_context->Unmap(game_device_data.modifiable_index_vertex_buffer.get(), 0);
                  }
               }
               else
               {
                  game_device_data.draw_device_context = nullptr;
                  game_device_data.partial_command_list.reset();
                  RefreshFrameJitterForPostProcess(game_device_data);
                  RunLatePostProcessPasses(native_device_context, cmd_list_data, device_data, game_device_data);
               }
               return DrawOrDispatchOverrideType::Replaced;
            }(game_device_data, native_device, native_device_context, device_data, cmd_list_data);
         }
         else
         {
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            ID3D11DepthStencilView* dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            if (rtvs[0] == nullptr)
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!SetupTempTAAOutput(native_device, game_device_data, rtvs[0]))
            {
               return DrawOrDispatchOverrideType::None;
            }

            if (!game_device_data.taa_temp_output_rtv.get() || !game_device_data.taa_output_texture_rtv.get())
            {
               return DrawOrDispatchOverrideType::None;
            }

            rtvs[0] = game_device_data.taa_temp_output_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            (*original_draw_dispatch_func)();
            rtvs[0] = game_device_data.taa_output_texture_rtv.get();
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], dsv);

            if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            {
               game_device_data.draw_device_context = native_device_context;
               native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
            }
            else
            {
               game_device_data.draw_device_context = nullptr;
               game_device_data.partial_command_list.reset();
               RefreshFrameJitterForPostProcess(game_device_data);
               RunLatePostProcessPasses(native_device_context, cmd_list_data, device_data, game_device_data);
            }
         }
         return override_type;
      }

      // UI phase detection and redirect to output-resolution texture.
      // Skipped at render_scale == 1 — no upscaling means no compositing needed.
      if (render_scale != 1.f && !IsTAARunningThisFrame() && DetectUIPhase(device_data, native_device_context, original_shader_hashes))
      {
         RedirectUIDrawToScaledTarget(native_device_context, device_data, game_device_data);
      }

      // Capture Output for deferred-context replay only.
      // Immediate-context output draws run natively, with an optional source SRV override
      // when UI was redirected to the scaled composition target.
      if (original_shader_hashes.Contains(shader_hashes_Output) && render_scale != 1.f && !IsTAARunningThisFrame())
      {
         if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         {
            CaptureOutputReplayState(native_device_context, game_device_data.ui_scale.output_replay_state);

            // Unbind scaled_texture_rtv before sealing the command list.  If it remains bound as
            // RTV inside the partial command list, D3D11 silently nulls the SRV binding when we
            // try to read scaled_texture in ReplayOutputDraw (same-resource RTV/SRV hazard).
            if (game_device_data.ui_scale.ui_scaled_needed)
            {
               ID3D11RenderTargetView* null_rtv = nullptr;
               native_device_context->OMSetRenderTargets(1, &null_rtv, nullptr);
            }

            game_device_data.ui_scale.output_device_context.store(native_device_context, std::memory_order_release);
            game_device_data.ui_scale.output_pending.store(true, std::memory_order_release);
            native_device_context->FinishCommandList(TRUE, game_device_data.ui_scale.output_partial_command_list.put());
            return DrawOrDispatchOverrideType::Skip;
         }

         // Immediate-context output: keep the game's native draw and only override source SRV
         // when the UI was redirected to the scaled composition target.
         if (game_device_data.ui_scale.ui_scaled_needed.load(std::memory_order_acquire) &&
             game_device_data.ui_scale.scaled_texture_srv)
         {
            ID3D11ShaderResourceView* scaled_srv = game_device_data.ui_scale.scaled_texture_srv.get();
            native_device_context->PSSetShaderResources(0, 1, &scaled_srv);
         }
         return DrawOrDispatchOverrideType::None;
      }
      return DrawOrDispatchOverrideType::None;
   }

   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(cmd_list->get_device()->get_api());

      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());

      ComPtr<ID3D11CommandList> native_cmd_list;
      device_child->QueryInterface(native_cmd_list.put());

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      ComPtr<ID3D11DeviceContext> source_deferred_ctx;
      ComPtr<ID3D11CommandList> secondary_native_cmd_list;
      {
         ID3D11DeviceChild* secondary_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         secondary_child->QueryInterface(source_deferred_ctx.put());
         secondary_child->QueryInterface(secondary_native_cmd_list.put());
      }

      const bool is_finish_command_list = source_deferred_ctx != nullptr;
      if (is_finish_command_list)
      {
         const ID3D11DeviceContext* ui_ctx = game_device_data.ui_scale.ui_detected_context.load(std::memory_order_acquire);
         if (native_cmd_list && ui_ctx != nullptr && source_deferred_ctx.get() == ui_ctx)
         {
            game_device_data.ui_scale.ui_finish_command_list.store(native_cmd_list.get(), std::memory_order_release);
         }
      }

      if (native_device_context)
      {
         // This is an ExecuteCommandList callback — a command list is about to be replayed
         // on the immediate context. Patch the ring buffer before the first one runs
         // (Map/Unmap already happened on the immediate context at start of frame).
         ComPtr<ID3D11CommandList> native_command_list;
         native_command_list = secondary_native_cmd_list;

         if (native_command_list &&
             native_command_list.get() == game_device_data.ui_scale.ui_finish_command_list.load(std::memory_order_acquire))
         {
            device_data.has_drawn_main_post_processing = true;
         }

         if (
            native_command_list.get() == game_device_data.ui_scale.output_remainder_command_list.load(std::memory_order_acquire) &&
            game_device_data.ui_scale.output_pending.load(std::memory_order_acquire))
         {
            // Unlikely to be needed this is the last shader to run
            // DrawStateStack<DrawStateStackType::FullGraphics> ui_output_state_stack;
            // ui_output_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);
            if (game_device_data.ui_scale.output_partial_command_list)
            {
               native_device_context->ExecuteCommandList(game_device_data.ui_scale.output_partial_command_list.get(), FALSE);
               game_device_data.ui_scale.output_partial_command_list.reset();
            }
            ReplayOutputDraw(native_device_context.get(), game_device_data);
            game_device_data.ui_scale.output_pending.store(false, std::memory_order_release);
            // ui_output_state_stack.Restore(native_device_context.get());
         }

         if (native_command_list.get() == game_device_data.remainder_command_list.load(std::memory_order_acquire) && game_device_data.partial_command_list.get() != nullptr)
         {
            {
               native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
               game_device_data.partial_command_list.reset();

               // Read jitter here, mid-frame, after geometry/camera setup but before DLSS and
               // all custom passes. By the time the partial command list (containing TAA) has
               // been replayed the game must have written the current-frame projection jitter.
               RefreshFrameJitterForPostProcess(game_device_data);
               CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
               RunLatePostProcessPasses(native_device_context.get(), cmd_list_data, device_data, game_device_data);
            }
         }
      }

      ComPtr<ID3D11CommandList> finish_command_list;
      hr = device_child->QueryInterface(finish_command_list.put());
      if (finish_command_list)
      {
         ComPtr<ID3D11DeviceContext> deferred_ctx;
         ID3D11DeviceChild* secondary_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = secondary_child->QueryInterface(deferred_ctx.put());
         if (deferred_ctx)
         {
            if (deferred_ctx.get() == game_device_data.draw_device_context)
            {
               game_device_data.remainder_command_list.store(finish_command_list.get(), std::memory_order_release);
            }
            if (deferred_ctx.get() == game_device_data.ui_scale.output_device_context.load(std::memory_order_acquire))
            {
               game_device_data.ui_scale.output_remainder_command_list.store(finish_command_list.get(), std::memory_order_release);
            }
         }
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
#if ENABLE_SR
      game_device_data.sr_source_color = nullptr;
      game_device_data.sr_source_color_srv = nullptr;
      game_device_data.pre_sr_encode_texture = nullptr;
      game_device_data.pre_sr_encode_srv = nullptr;
      game_device_data.pre_sr_encode_rtv = nullptr;
      game_device_data.post_sr_encode_texture = nullptr;
      game_device_data.post_sr_encode_srv = nullptr;
      game_device_data.post_sr_encode_rtv = nullptr;
      game_device_data.depth_buffer = nullptr;
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.sr_output_color = nullptr;
      game_device_data.sr_output_color_srv = nullptr;
      game_device_data.draw_device_context = nullptr;
      game_device_data.remainder_command_list = nullptr;
      game_device_data.partial_command_list = nullptr;
      game_device_data.output_changed = false;
#endif
      game_device_data.taa_temp_output_resource = nullptr;
      game_device_data.taa_temp_output_srv = nullptr;
      game_device_data.taa_temp_output_rtv = nullptr;
      game_device_data.taa_output_texture = nullptr;
      game_device_data.taa_output_texture_rtv = nullptr;
      game_device_data.sr_settings_valid = false;
      game_device_data.last_sr_settings_data = {};
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
#if ENABLE_SR
      CleanExtraSRResources(device_data);
      if (auto* sr_instance_data = device_data.GetSRInstanceData())
      {
         sr_instance_data->settings_data = SR::SettingsData{};
      }
      device_data.force_reset_sr = true;
      device_data.has_drawn_sr = false;
#endif
      EnsureScaledTexture(device_data, game_device_data);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataGBFR;

      {
         auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data.game);
         game_device_data.jitter = float2{0.0f, 0.0f};
         game_device_data.prev_jitter = game_device_data.jitter;
      }

      g_device_data_ptr.store(&device_data, std::memory_order_release);
      g_native_device_ptr.store(native_device, std::memory_order_release);

      if (!ResolveGBFRAddresses())
      {
         reshade::log::message(
            reshade::log::level::warning,
            "GBFR sigscan: incomplete; native GBFR hooks disabled for safety");
         return;
      }

      if (!g_rt_creation_hook)
      {
         void* rt_creation_fn = g_resolved_addresses.initialize_dx11_rendering_pipeline;
         if (rt_creation_fn)
         {
            g_rt_creation_hook = safetyhook::create_inline(
               rt_creation_fn,
               reinterpret_cast<void*>(&Hooked_InitializeDX11RenderingPipeline));
         }
      }

#if ENABLE_UI_VIEWPORT_SCALING_HOOK
      if (!g_dispatch_viewport_hook)
      {
         void* dispatch_fn = g_resolved_addresses.dispatch_render_pass_viewport;
         if (dispatch_fn)
         {
            g_dispatch_viewport_hook = safetyhook::create_inline(
               dispatch_fn,
               reinterpret_cast<void*>(&Hooked_DispatchRenderPassViewport));
         }
      }

      if (!g_ui_orchestrator_hook)
      {
         void* ui_orchestrator_fn = g_resolved_addresses.ui_render_orchestrator;
         if (ui_orchestrator_fn)
         {
            g_ui_orchestrator_hook = safetyhook::create_mid(
               ui_orchestrator_fn,
               &OnUIRenderOrchestratorEntry);
         }
      }
#endif

      PatchJitterPhases();

#ifdef PATCH_JITTER_TABLE_INIT
      if (!g_taa_init_hook)
      {
         void* taa_init_fn = g_resolved_addresses.temporal_aa_component_init;
         if (taa_init_fn)
         {
            g_taa_init_hook = safetyhook::create_inline(
               taa_init_fn,
               reinterpret_cast<void*>(&Hooked_TemporalAntiAliasingComponentInit));
         }
      }
#endif

      if (!g_jitter_write_hook)
      {
         void* jitter_write_site = g_resolved_addresses.jitter_write_site;
         if (jitter_write_site)
         {
            g_jitter_write_hook = safetyhook::create_mid(
               jitter_write_site,
               &OnJitterWrite);
         }
      }
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if DEVELOPMENT
      {
         const bool was_down = game_device_data.pause_trace_key_down;
         game_device_data.pause_trace_key_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
         if (game_device_data.pause_trace_key_down && !was_down)
         {
            game_device_data.pause_trace_delay_countdown = game_device_data.pause_trace_delay_frames;
         }
         if (game_device_data.pause_trace_delay_countdown >= 0)
         {
            if (game_device_data.pause_trace_delay_countdown == 0)
            {
               trace_scheduled = true;
               game_device_data.pause_trace_delay_countdown = -1;

               const uintptr_t settings_ptr_addr = ResolveGBFRDataOrFallback(
                  g_resolved_addresses.taa_settings_global,
                  kTAASettingsGlobal_RVA);
               const uintptr_t settings_obj = (settings_ptr_addr != 0)
                                                 ? *reinterpret_cast<const uintptr_t*>(settings_ptr_addr)
                                                 : 0;

               auto& snap = game_device_data.pause_snapshot;
               snap.valid = true;
               snap.render_resolution = device_data.render_resolution;
               snap.output_resolution = device_data.output_resolution;
               snap.render_scale_pct = render_scale * 100.0f;
               snap.jitter = game_device_data.jitter;
               snap.prev_jitter = game_device_data.prev_jitter;
               snap.prev_table_jitter = game_device_data.prev_table_jitter;
               snap.table_jitter = game_device_data.table_jitter;
               snap.taa_enabled = IsTAARunningThisFrame();
               snap.settings_obj_valid = (settings_obj != 0);
               snap.upscaling_disabled = (settings_obj != 0) && ((*reinterpret_cast<const uint8_t*>(settings_obj + 63) & 1) != 0);
               snap.drs_active = (settings_obj != 0) && ((*reinterpret_cast<const uint8_t*>(settings_obj + 101) & 1) != 0);
               snap.taa_output_ready = game_device_data.taa_output_texture.get() != nullptr;
            }
            else
            {
               --game_device_data.pause_trace_delay_countdown;
            }
         }
      }
#endif

      device_data.has_drawn_sr = false;
      game_device_data.tonemap_detected_context.store(nullptr, std::memory_order_relaxed);
      game_device_data.ui_scale.ui_detected_context.store(nullptr, std::memory_order_relaxed);
      game_device_data.ui_scale.ui_finish_command_list.store(nullptr, std::memory_order_relaxed);
      game_device_data.ui_scale.ResetPerFrame();
      device_data.ui_initial_original_rtv = nullptr;
#if TEST || DEVELOPMENT
      game_device_data.taa_detected_this_frame = false;
#endif
      device_data.has_drawn_main_post_processing = false;
      game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
      game_device_data.draw_device_context = nullptr;
      game_device_data.sr_source_color = nullptr;
      game_device_data.sr_source_color_srv = nullptr;
      game_device_data.pre_sr_encode_texture = nullptr;
      game_device_data.pre_sr_encode_srv = nullptr;
      game_device_data.pre_sr_encode_rtv = nullptr;
      game_device_data.outline_pending.store(false, std::memory_order_relaxed);
      game_device_data.outline_replay_state.Reset();
      game_device_data.depth_buffer = nullptr;
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.bloom_texture_srv = nullptr;
      game_device_data.exposure_texture = nullptr;
      game_device_data.exposure_texture_srv = nullptr;
      game_device_data.taa_output_texture = nullptr;
      game_device_data.taa_output_texture_rtv = nullptr;
      game_device_data.motion_blur_replay_states[0].Reset();
      game_device_data.motion_blur_replay_states[1].Reset();
      game_device_data.motion_blur_pending.store(false, std::memory_order_relaxed);
      game_device_data.motion_blur_seen = false;
      game_device_data.motion_blur_first_pass_seen = false;
      game_device_data.motion_blur_second_pass_seen = false;
      game_device_data.motion_blur_output_ready = false;
      game_device_data.motion_blur_invocation_count = 0;
      game_device_data.cutscene_gamma_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_color_grade_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_gamma_resource = nullptr;
      game_device_data.cutscene_gamma_srv = nullptr;
      game_device_data.cutscene_gamma_rtv = nullptr;
      game_device_data.cutscene_color_grade_resource = nullptr;
      game_device_data.cutscene_color_grade_srv = nullptr;
      game_device_data.cutscene_color_grade_rtv = nullptr;
      game_device_data.cutscene_overlay_blend_replay_state.Reset();
      game_device_data.cutscene_overlay_modulate_replay_state.Reset();
      game_device_data.cutscene_gamma_replay_state.Reset();
      game_device_data.cutscene_color_grade_replay_state.Reset();
      game_device_data.tonemap_draw_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_overlay_blend_pending.store(false, std::memory_order_relaxed);
      game_device_data.cutscene_overlay_modulate_pending.store(false, std::memory_order_relaxed);
      // cutscene_intermediate_* is a persistent resource; not reset per-frame.
      game_device_data.scene_buffer_patched_this_frame = false;
      game_device_data.scene_buffer_collect_guard.store(false, std::memory_order_relaxed);
      game_device_data.scene_buffer_info_collected.store(false, std::memory_order_relaxed);
      game_device_data.pending_scene_buffer = nullptr;
      game_device_data.pending_first_constant = 0;
      game_device_data.pending_num_constants = 0;
      {
         std::lock_guard<std::mutex> lock(game_device_data.scene_buffer_bindings_mutex);
         game_device_data.scene_buffer_offsets_this_frame.clear();
      }

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }

      if (render_scale_changed)
      {
         device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
         reshade::log::message(reshade::log::level::warning, "[GBFR][TEST] force_reset_sr set: render_scale_changed");
#endif
         render_scale_changed = false;
      }
      device_data.cb_luma_global_settings_dirty = true;
      int32_t sr_type = static_cast<int32_t>(device_data.sr_type);
      cb_luma_global_settings.SRType = static_cast<uint32_t>(sr_type + 1);
      cb_luma_global_settings.GameSettings.IsTAARunning = 0;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      float loaded_render_scale = render_scale;
      if (reshade::get_config_value(runtime, NAME, "RenderScale", loaded_render_scale))
      {
         loaded_render_scale = std::clamp(loaded_render_scale, 0.5f, 1.0f);
         if (std::abs(loaded_render_scale - render_scale) > 0.0001f)
         {
            render_scale = loaded_render_scale;
            render_scale_changed = true;
         }
      }
      // Load cbuffer values directly from config
      reshade::get_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
      reshade::get_config_value(runtime, NAME, "HighlightContrast", cb_luma_global_settings.GameSettings.HighlightContrast);
      reshade::get_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
      reshade::get_config_value(runtime, NAME, "ContrastShadows", cb_luma_global_settings.GameSettings.ShadowContrast);
      reshade::get_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
      reshade::get_config_value(runtime, NAME, "Gamma", cb_luma_global_settings.GameSettings.Gamma);
      reshade::get_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
      reshade::get_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
      // reshade::get_config_value(runtime, NAME, "BloomType", cb_luma_global_settings.GameSettings.BloomType);
      reshade::get_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      reshade::api::effect_runtime* runtime = nullptr;

      // Render scale slider
      {
         int scale = static_cast<int>(render_scale * 100.0f);
         if (ImGui::SliderInt("Render Scale (%)", &scale, 50, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))
         {
            scale = std::clamp(((scale + 2) / 5) * 5, 50, 100);
            const float new_render_scale = scale / 100.0f;
            if (std::abs(new_render_scale - render_scale) > 0.0001f)
            {
               render_scale = new_render_scale;
               render_scale_changed = true;
               reshade::set_config_value(runtime, NAME, "RenderScale", render_scale);
            }
         }
      }

      if (ImGui::TreeNodeEx("Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
      {

         float contrast = cb_luma_global_settings.GameSettings.Contrast * 50.0f;
         float highlights = cb_luma_global_settings.GameSettings.Highlights * 50.0f;
         float highlight_contrast = cb_luma_global_settings.GameSettings.HighlightContrast * 50.0f;
         float shadows = cb_luma_global_settings.GameSettings.Shadows * 50.0f;
         float shadow_contrast = cb_luma_global_settings.GameSettings.ShadowContrast * 50.0f;
         float flare = cb_luma_global_settings.GameSettings.Flare * 100.0f;
         float saturation = cb_luma_global_settings.GameSettings.Saturation * 50.0f;
         float dechroma = cb_luma_global_settings.GameSettings.Dechroma * 100.0f;
         float highlight_saturation = cb_luma_global_settings.GameSettings.HighlightSaturation * 50.0f;
         int bloom_type = cb_luma_global_settings.GameSettings.BloomType;
         float blooom_strength = cb_luma_global_settings.GameSettings.BloomStrength * 50.0f;

         if (ImGui::SliderFloat("Exposure", &cb_luma_global_settings.GameSettings.Exposure, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.Exposure, 1.f, "Exposure", runtime))
         {
            cb_luma_global_settings.GameSettings.Exposure = 1.f;
            reshade::set_config_value(runtime, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
         }

         if (ImGui::SliderFloat("Gamma", &cb_luma_global_settings.GameSettings.Gamma, 0.75f, 1.25f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "Gamma", cb_luma_global_settings.GameSettings.Gamma);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.Gamma, 1.f, "Gamma", runtime))
         {
            cb_luma_global_settings.GameSettings.Gamma = 1.f;
            reshade::set_config_value(runtime, NAME, "Gamma", cb_luma_global_settings.GameSettings.Gamma);
         }

         if (ImGui::SliderFloat("Highlights", &highlights, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Highlights = highlights * 0.02f;
            reshade::set_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
         }
         if (DrawResetButton(highlights, 50.f, "Highlights", runtime))
         {
            highlights = 50.f;
            cb_luma_global_settings.GameSettings.Highlights = highlights * 0.02f;
            reshade::set_config_value(runtime, NAME, "Highlights", cb_luma_global_settings.GameSettings.Highlights);
         }

         if (ImGui::SliderFloat("Highlight Contrast", &highlight_contrast, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.HighlightContrast = highlight_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightContrast", cb_luma_global_settings.GameSettings.HighlightContrast);
         }
         if (DrawResetButton(highlight_contrast, 50.f, "HighlightContrast", runtime))
         {
            highlight_contrast = 50.f;
            cb_luma_global_settings.GameSettings.HighlightContrast = highlight_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightContrast", cb_luma_global_settings.GameSettings.HighlightContrast);
         }

         if (ImGui::SliderFloat("Shadows", &shadows, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Shadows = shadows * 0.02f;
            reshade::set_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
         }
         if (DrawResetButton(shadows, 50.f, "Shadows", runtime))
         {
            shadows = 50.f;
            cb_luma_global_settings.GameSettings.Shadows = shadows * 0.02f;
            reshade::set_config_value(runtime, NAME, "Shadows", cb_luma_global_settings.GameSettings.Shadows);
         }

         if (ImGui::SliderFloat("Shadow Contrast", &shadow_contrast, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.ShadowContrast = shadow_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "ShadowContrast", cb_luma_global_settings.GameSettings.ShadowContrast);
         }
         if (DrawResetButton(shadow_contrast, 50.f, "ShadowContrast", runtime))
         {
            shadow_contrast = 50.f;
            cb_luma_global_settings.GameSettings.ShadowContrast = shadow_contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "ShadowContrast", cb_luma_global_settings.GameSettings.ShadowContrast);
         }

         if (ImGui::SliderFloat("Contrast", &contrast, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Contrast = contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
         }
         if (DrawResetButton(contrast, 50.f, "Contrast", runtime))
         {
            contrast = 50.f;
            cb_luma_global_settings.GameSettings.Contrast = contrast * 0.02f;
            reshade::set_config_value(runtime, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
         }

         if (ImGui::SliderFloat("Saturation", &saturation, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Saturation = saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
         }
         if (DrawResetButton(saturation, 50.f, "Saturation", runtime))
         {
            saturation = 50.f;
            cb_luma_global_settings.GameSettings.Saturation = saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
         }

         if (ImGui::SliderFloat("Highlight Saturation", &highlight_saturation, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.HighlightSaturation = highlight_saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
         }
         if (DrawResetButton(highlight_saturation, 50.f, "HighlightSaturation", runtime))
         {
            highlight_saturation = 50.f;
            cb_luma_global_settings.GameSettings.HighlightSaturation = highlight_saturation * 0.02f;
            reshade::set_config_value(runtime, NAME, "HighlightSaturation", cb_luma_global_settings.GameSettings.HighlightSaturation);
         }

         if (ImGui::SliderFloat("Dechroma", &dechroma, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Dechroma = dechroma * 0.01f;
            reshade::set_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
         }
         if (DrawResetButton(dechroma, 0.f, "Dechroma", runtime))
         {
            dechroma = 0.f;
            cb_luma_global_settings.GameSettings.Dechroma = dechroma * 0.01f;
            reshade::set_config_value(runtime, NAME, "Dechroma", cb_luma_global_settings.GameSettings.Dechroma);
         }

         if (ImGui::SliderFloat("Flare", &flare, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.Flare = flare * 0.01f;
            reshade::set_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
         }
         if (DrawResetButton(flare, 0.f, "Flare", runtime))
         {
            flare = 0.f;
            cb_luma_global_settings.GameSettings.Flare = flare * 0.01f;
            reshade::set_config_value(runtime, NAME, "Flare", cb_luma_global_settings.GameSettings.Flare);
         }

         // // Bloom settings
         // const char* bloom_type_names[] = {"Vanilla", "HDR"};
         // if (ImGui::SliderInt("Bloom Type", &bloom_type, 0, 1, bloom_type_names[bloom_type], ImGuiSliderFlags_AlwaysClamp))
         // {
         //    cb_luma_global_settings.GameSettings.BloomType = bloom_type;
         //    reshade::set_config_value(runtime, NAME, "BloomType", cb_luma_global_settings.GameSettings.BloomType);
         // }
         // if (DrawResetButton(bloom_type, 1, "BloomType", runtime))
         // {
         //    bloom_type = 1;
         //    cb_luma_global_settings.GameSettings.BloomType = bloom_type;
         //    reshade::set_config_value(runtime, NAME, "BloomType", cb_luma_global_settings.GameSettings.BloomType);
         // }

         if (ImGui::SliderFloat("Bloom Strength", &blooom_strength, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp))
         {
            cb_luma_global_settings.GameSettings.BloomStrength = blooom_strength * 0.02f;
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         }
         if (DrawResetButton(blooom_strength, 100.f, "BloomStrength", runtime))
         {
            blooom_strength = 50.f;
            cb_luma_global_settings.GameSettings.BloomStrength = blooom_strength * 0.02f;
            reshade::set_config_value(runtime, NAME, "BloomStrength", cb_luma_global_settings.GameSettings.BloomStrength);
         }

         ImGui::TreePop();
      }
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::SliderInt("Pause Trace Delay (frames)", &game_device_data.pause_trace_delay_frames, 0, 10);
      ImGui::Checkbox("Use Table Jitter for DLSS", &game_device_data.use_table_jitter_for_dlss);
   }
#endif // DEVELOPMENT

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Read TAA settings object for per-bit queries beyond the TAA-enabled flag
      const uintptr_t settings_ptr_addr = ResolveGBFRDataOrFallback(
         g_resolved_addresses.taa_settings_global,
         kTAASettingsGlobal_RVA);
      const uintptr_t settings_obj = (settings_ptr_addr != 0)
                                        ? *reinterpret_cast<const uintptr_t*>(settings_ptr_addr)
                                        : 0;

      ImGui::NewLine();
      if (ImGui::BeginTable("gbfr_info", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
      {
         ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableHeadersRow();

         // Resolution
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Render Resolution");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%dx%d", (int)device_data.render_resolution.x, (int)device_data.render_resolution.y);

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Output Resolution");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%dx%d", (int)device_data.output_resolution.x, (int)device_data.output_resolution.y);

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Render Scale");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%.0f%%", render_scale * 100.0f);

         // Camera jitter
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Jitter (NDC)");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%.6f, %.6f", game_device_data.jitter.x, game_device_data.jitter.y);

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Prev Jitter (NDC)");
         ImGui::TableSetColumnIndex(1);
         ImGui::Text("%.6f, %.6f", game_device_data.prev_jitter.x, game_device_data.prev_jitter.y);

         // Jitter phase and direct table read
         {
            const uintptr_t phase_counter_addr = ResolveGBFRDataOrFallback(
               g_resolved_addresses.jitter_phase_counter,
               kJitterPhaseCounter_RVA);
            const uint8_t phase = (phase_counter_addr != 0)
                                     ? (*reinterpret_cast<const uint8_t*>(phase_counter_addr) & static_cast<uint8_t>(JITTER_PHASES - 1))
                                     : 0u;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter Phase");
            ImGui::TableSetColumnIndex(1);
            if (phase_counter_addr != 0)
               ImGui::Text("%u / %u", static_cast<unsigned>(phase), static_cast<unsigned>(JITTER_PHASES));
            else
               ImGui::TextUnformatted("N/A");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", game_device_data.table_jitter.x, game_device_data.table_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Prev Jitter Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", game_device_data.prev_table_jitter.x, game_device_data.prev_table_jitter.y);

#if DEVELOPMENT
            {
               {
                  ASSERT_ONCE_MSG(
                     fabsf(game_device_data.table_jitter.x - game_device_data.jitter.x) < 1e-4f &&
                        fabsf(game_device_data.table_jitter.y - game_device_data.jitter.y) < 1e-4f,
                     "Jitter table value does not match camera projection jitter");
               }
            }
#endif
         }

         // TAA state (read from engine globals at runtime)
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("TAA Enabled");
         ImGui::TableSetColumnIndex(1);
         ImGui::TextUnformatted(IsTAARunningThisFrame() ? "Yes" : "No");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("Upscaling (TUP)");
         ImGui::TableSetColumnIndex(1);
         if (settings_obj != 0)
            ImGui::TextUnformatted((*reinterpret_cast<const uint8_t*>(settings_obj + 63) & 1) ? "Disabled" : "Enabled");
         else
            ImGui::TextUnformatted("N/A");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("DRS Active");
         ImGui::TableSetColumnIndex(1);
         if (settings_obj != 0)
            ImGui::TextUnformatted((*reinterpret_cast<const uint8_t*>(settings_obj + 101) & 1) ? "Yes" : "No");
         else
            ImGui::TextUnformatted("N/A");

         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted("TAA Output Texture");
         ImGui::TableSetColumnIndex(1);
         ImGui::TextUnformatted(game_device_data.taa_output_texture.get() ? "Ready" : "Null");

         ImGui::EndTable();
      }

      if (ImGui::BeginTable("gbfr_address_info", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
      {
         ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
         ImGui::TableHeadersRow();

         const auto draw_data_addr_row = [](const char* label, uintptr_t resolved_abs, uintptr_t fallback_rva)
         {
            const uintptr_t active_addr = ResolveGBFRDataOrFallback(resolved_abs, fallback_rva);
            const bool from_signature = resolved_abs != 0;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (active_addr != 0)
               ImGui::Text("0x%llX", static_cast<unsigned long long>(active_addr));
            else
               ImGui::TextUnformatted("N/A");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(from_signature ? "Signature" : "Unavailable");
         };

         const auto draw_code_addr_row = [](const char* label, void* resolved_abs, uintptr_t fallback_rva)
         {
            const uintptr_t active_addr = reinterpret_cast<uintptr_t>(ResolveGBFRCodeOrFallback(resolved_abs, fallback_rva));
            const bool from_signature = resolved_abs != nullptr;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            if (active_addr != 0)
               ImGui::Text("0x%llX", static_cast<unsigned long long>(active_addr));
            else
               ImGui::TextUnformatted("N/A");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(from_signature ? "Signature" : "Unavailable");
         };

         draw_code_addr_row("InitializeDX11RenderingPipeline", g_resolved_addresses.initialize_dx11_rendering_pipeline, kInitializeDX11RenderingPipeline_RVA);
         draw_code_addr_row("DispatchRenderPassViewport", g_resolved_addresses.dispatch_render_pass_viewport, kDispatchRenderPassViewport_RVA);
         draw_code_addr_row("UIRenderOrchestrator", g_resolved_addresses.ui_render_orchestrator, kUIRenderOrchestrator_RVA);
         draw_code_addr_row("Jitter Write Site", g_resolved_addresses.jitter_write_site, kJitterWrite_RVA);
#ifdef PATCH_JITTER_TABLE_INIT
         draw_code_addr_row("TemporalAAComponentInit", g_resolved_addresses.temporal_aa_component_init, kTemporalAntiAliasingComponent_Init_RVA);
#endif

         draw_data_addr_row("g_outputWidth", g_resolved_addresses.output_width, kOutputWidth_RVA);
         draw_data_addr_row("g_outputHeight", g_resolved_addresses.output_height, kOutputHeight_RVA);
         draw_data_addr_row("g_renderWidth", g_resolved_addresses.render_width, kRenderWidth_RVA);
         draw_data_addr_row("g_renderHeight", g_resolved_addresses.render_height, kRenderHeight_RVA);
         draw_data_addr_row("g_camera", g_resolved_addresses.camera_global, kCameraGlobal_RVA);
         draw_data_addr_row("g_cameraTable", g_resolved_addresses.camera_table, kCameraTable_RVA);
         draw_data_addr_row("g_cameraIndex", g_resolved_addresses.camera_index, kCameraIndex_RVA);
         draw_data_addr_row("g_taa_settings_obj", g_resolved_addresses.taa_settings_global, kTAASettingsGlobal_RVA);
         draw_data_addr_row("g_frame_counter", g_resolved_addresses.jitter_phase_counter, kJitterPhaseCounter_RVA);
         draw_data_addr_row("JitterPhaseMask CL imm", g_resolved_addresses.jitter_phase_mask_cl_imm, kJitterPhaseMask_CL_RVA);
         draw_data_addr_row("JitterPhaseMask EAX imm", g_resolved_addresses.jitter_phase_mask_eax_imm, kJitterPhaseMask_EAX_RVA);

         ImGui::EndTable();
      }
#if DEVELOPMENT
      if (game_device_data.pause_snapshot.valid)
      {
         const auto& snap = game_device_data.pause_snapshot;
         ImGui::NewLine();
         ImGui::TextUnformatted("Snapshot at last ESC press:");
         if (ImGui::BeginTable("gbfr_pause_snapshot", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
         {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Render Resolution");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%dx%d", (int)snap.render_resolution.x, (int)snap.render_resolution.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Output Resolution");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%dx%d", (int)snap.output_resolution.x, (int)snap.output_resolution.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Render Scale");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.0f%%", snap.render_scale_pct);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.jitter.x, snap.jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Jitter from Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.table_jitter.x, snap.table_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Prev Jitter (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.prev_jitter.x, snap.prev_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Prev Jitter from Table (NDC)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.6f, %.6f", snap.prev_table_jitter.x, snap.prev_table_jitter.y);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("TAA Enabled");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(snap.taa_enabled ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Upscaling (TUP)");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(!snap.settings_obj_valid ? "N/A" : (snap.upscaling_disabled ? "Disabled" : "Enabled"));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("DRS Active");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(!snap.settings_obj_valid ? "N/A" : (snap.drs_active ? "Yes" : "No"));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("TAA Output Texture");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(snap.taa_output_ready ? "Ready" : "Null");

            ImGui::EndTable();
         }
      }
#endif
   }
#endif // DEVELOPMENT || TEST

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Granblue Fantasy Relink\" is developed by Izueh and is open source and free.\nIf you enjoy it, consider donating");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_izueh = std::string("Buy Izueh a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_izueh.c_str()))
      {
         system("start https://ko-fi.com/izueh");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nIzueh"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nSafetyHook"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Granblue Fantasy Relink");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      // Outline prefilter and CS hashes (depth source for NewAA mode)
      shader_hashes_OutlinePrefilter.pixel_shaders.emplace(std::stoul("897DB2C0", nullptr, 16));
      shader_hashes_OutlineCS.compute_shaders.emplace(std::stoul("DA85F5BB", nullptr, 16));

      // PostAA temporal upsampling pass hash
      shader_hashes_Temporal_Upscale.pixel_shaders.emplace(std::stoul("6EEF1071", nullptr, 16));

      // TAA / NewAA pixel shader hashes
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("478E345C", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("E49E117A", nullptr, 16)); // RenoDX compatibility
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("14393629", nullptr, 16)); // TAA (engine-native at <100% scale)
      shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("60F0256B", nullptr, 16));
      shader_hashes_MotionBlur.pixel_shaders.emplace(std::stoul("45841F6D", nullptr, 16));
      shader_hashes_MotionBlurDenoise.pixel_shaders.emplace(std::stoul("199A3FBC", nullptr, 16));
      shader_hashes_CutsceneGamma.pixel_shaders.emplace(std::stoul("1085E11F", nullptr, 16));
      shader_hashes_CutsceneColorGrade.pixel_shaders.emplace(std::stoul("50BE35B0", nullptr, 16));
      shader_hashes_CutsceneOverlayBlend.pixel_shaders.emplace(std::stoul("4517077B", nullptr, 16));
      shader_hashes_CutsceneOverlayBlend.vertex_shaders.emplace(std::stoul("DAFDA220", nullptr, 16));
      shader_hashes_CutsceneOverlayModulate.pixel_shaders.emplace(std::stoul("B9AFD904", nullptr, 16));
      shader_hashes_CutsceneOverlayModulate.vertex_shaders.emplace(std::stoul("4741FB87", nullptr, 16));
      shader_hashes_Output.pixel_shaders.emplace(std::stoul("F55707D4", nullptr, 16));
      shader_hashes_Bloom.pixel_shaders.emplace(std::stoul("1C5F92B9", nullptr, 16));
      shader_hashes_UIBackgroundDownscale.pixel_shaders.emplace(std::stoul("C4013554", nullptr, 16));

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         reshade::api::format::r8g8b8a8_typeless,
         reshade::api::format::r11g11b10_float,
         reshade::api::format::r10g10b10a2_unorm};

      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("897DB2C0", nullptr, 16), "Outline Prefilter");
      forced_shader_names.emplace(std::stoul("DA85F5BB", nullptr, 16), "OutlineCS (depth)");
      forced_shader_names.emplace(std::stoul("6EEF1071", nullptr, 16), "Temporal Upscale");
      forced_shader_names.emplace(std::stoul("14393629", nullptr, 16), "TAA (for <100% scale)");
      forced_shader_names.emplace(std::stoul("478E345C", nullptr, 16), "TAA");
      forced_shader_names.emplace(std::stoul("E49E117A", nullptr, 16), "TAA RenoDX");
      forced_shader_names.emplace(std::stoul("45841F6D", nullptr, 16), "Motion Blur");
      forced_shader_names.emplace(std::stoul("0BB781D8", nullptr, 16), "DoF CoC Prefilter");
      forced_shader_names.emplace(std::stoul("AA6346F2", nullptr, 16), "DoF Bokeh Gather");
      forced_shader_names.emplace(std::stoul("560B601E", nullptr, 16), "DoF Temporal Resolve");
      forced_shader_names.emplace(std::stoul("F041F90A", nullptr, 16), "DoF Final Composite");
      forced_shader_names.emplace(std::stoul("1085E11F", nullptr, 16), "Cutscene Gamma");
      forced_shader_names.emplace(std::stoul("50BE35B0", nullptr, 16), "Cutscene Color Grade");
      forced_shader_names.emplace(std::stoul("B9AFD904", nullptr, 16), "Cutscene Overlay Modulate");
      forced_shader_names.emplace(std::stoul("4517077B", nullptr, 16), "Cutscene Overlay Blend");
      forced_shader_names.emplace(std::stoul("F55707D4", nullptr, 16), "Output");
      forced_shader_names.emplace(std::stoul("1C5F92B9", nullptr, 16), "Bloom");
      forced_shader_names.emplace(std::stoul("C4013554", nullptr, 16), "UI Background Downscale");
#endif
      // enable_samplers_upgrade = true;
      // ui_separation.enable = true;
      // ui_separation.scale_ui = true;
      // ui_separation.format = DXGI_FORMAT_R16G16B16A16_UNORM;
      // ui_separation.is_ui_phase = GBFRUIPhase;

      // Set default buffer values
      // cb_luma_global_settings.GameSettings.TonemapAfterSR = true;
      cb_luma_global_settings.GameSettings.Exposure = 1.f;
      cb_luma_global_settings.GameSettings.Highlights = 1.f;
      cb_luma_global_settings.GameSettings.HighlightContrast = 1.f;
      cb_luma_global_settings.GameSettings.Shadows = 1.f;
      cb_luma_global_settings.GameSettings.ShadowContrast = 1.f;
      cb_luma_global_settings.GameSettings.Contrast = 1.f;
      cb_luma_global_settings.GameSettings.Flare = 0.f;
      cb_luma_global_settings.GameSettings.Gamma = 1.f;
      cb_luma_global_settings.GameSettings.Saturation = 1.f;
      cb_luma_global_settings.GameSettings.Dechroma = 0.f;
      cb_luma_global_settings.GameSettings.HighlightSaturation = 1.f;
      cb_luma_global_settings.GameSettings.HueEmulation = 0.f;
      cb_luma_global_settings.GameSettings.PurityEmulation = 0.f;
      cb_luma_global_settings.GameSettings.BloomType = 0; // Default to HDR bloom
      cb_luma_global_settings.GameSettings.BloomStrength = 1.f;

      game = new GranblueFantasyRelink();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      g_rt_creation_hook.reset();
      g_update_screen_resolution_hook.reset();
      g_dispatch_viewport_hook.reset();
      g_ui_orchestrator_hook.reset();
      g_VSSetConstantBuffers1_hook_immediate.reset();
      g_VSSetConstantBuffers1_hook_deferred.reset();
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(GranblueFantasyRelink::OnExecuteSecondaryCommandList);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
