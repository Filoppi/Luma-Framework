#define GAME_FINALFANTASYXV 1
#define ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#ifdef _DEBUG
#define ALLOW_SHADERS_DUMPING 1
#define ALLOW_SHADER_PATCHES_DUMPING 1
#endif

#include <d3d11.h>
#include <memory>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#include "../../Core/core.hpp"
#include "includes/common.hpp"
#include "includes/sr_helpers.hpp"

#if ENABLE_FAST_NOISE_TEXTURES
static constexpr uint32_t kCoreFastNoiseTextureHash = "FAST Noise"_h;
#endif
#if LUMA_HAS_RECIPE_PROVIDERS
static constexpr const char* kDitheringFixRecipe = "PS/FFXV_Dithering_Fixes.recipe.yml";
#endif

namespace
{
   ShaderHashesList shader_hashes_tonemap;
   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_dof_coc;
   ShaderHashesList shader_hashes_dof_composite;
   ShaderHashesList shader_hashes_motion_blur;
   ShaderHashesList shader_hashes_lens_distortion;
   ShaderHashesList shader_hashes_scene_tonemap;
   ShaderHashesList shader_hashes_OutputScaled;
   ShaderHashesList shader_hashes_bloom_highpass;
   ShaderHashesList shader_hashes_bloom_glare_vignette;
   ShaderHashesList shader_hashes_rain;
   ShaderHashesList shader_hashes_directional_light;
   const uint32_t CBTemporalAA_buffer_size = 256;
   const uint32_t CBView_buffer_size = 768;

   bool enable_directional_shadows = false;

   int rain_composition = 0; // 0 = after AA, 1 = after game DoF

#if ENABLE_FAST_NOISE_TEXTURES
   ID3D11ShaderResourceView* GetFastNoiseSrv(DeviceData& device_data)
   {
      const std::shared_lock lock_device(device_data.mutex);
      const auto fast_noise_it = device_data.managed_resources.shader_resource_views.find(kCoreFastNoiseTextureHash);
      if (fast_noise_it == device_data.managed_resources.shader_resource_views.end() || !fast_noise_it->second)
      {
         return nullptr;
      }

      return fast_noise_it->second.get();
   }
#endif

#if DEVELOPMENT
   void VerifyFastNoiseBinding(ID3D11DeviceContext* native_device_context, uint32_t bind_point, ID3D11ShaderResourceView* expected_srv, const char* path_label)
   {
      if (!native_device_context || !expected_srv)
      {
         ASSERT_ONCE(false);
         return;
      }

      ID3D11ShaderResourceView* bound_srv = nullptr;
      native_device_context->PSGetShaderResources(bind_point, 1, &bound_srv);

      if (bound_srv != expected_srv)
      {
         char log_buf[256];
         snprintf(log_buf, sizeof(log_buf),
            "FFXV: FAST noise bind verify failed (%s) slot=%u expected=%p got=%p",
            path_label, bind_point, expected_srv, bound_srv);
         ASSERT_ONCE(false);
      }

      if (bound_srv)
      {
         bound_srv->Release();
      }
   }
#endif

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

   bool CompositeRain(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data,
      ID3D11RenderTargetView* target, bool resolved)
   {
      auto& rain = GetGameDeviceData(device_data).rain;
      if (!rain.pending || !target) return false;
      ComPtr<ID3D11Resource> resource;
      target->GetResource(resource.put());
      D3D11_RENDER_TARGET_VIEW_DESC target_desc;
      target->GetDesc(&target_desc);
      if (target_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) return false;
      D3D11_SHADER_RESOURCE_VIEW_DESC desc = {};
      desc.Format = target_desc.Format;
      desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      desc.Texture2D.MostDetailedMip = target_desc.Texture2D.MipSlice;
      desc.Texture2D.MipLevels = 1;
      ComPtr<ID3D11ShaderResourceView> scene;
      if (FAILED(device->CreateShaderResourceView(resource.get(), &desc, scene.put()))) return false;
      return CompositeRainInput(device, context, device_data, scene.get(), resolved);
   }

   bool CompositeRainInput(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data,
      ID3D11ShaderResourceView* scene, bool resolved)
   {
      DrawStateStack<DrawStateStackType::FullGraphics> graphics;
      DrawStateStack<DrawStateStackType::Compute> compute;
      graphics.Cache(context, device_data.uav_max_count);
      compute.Cache(context, device_data.uav_max_count);
      ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
      context->CSSetUnorderedAccessViews(0, device_data.uav_max_count, null_uavs, nullptr);
      auto& rain = GetGameDeviceData(device_data).rain;
      rain.resolved = resolved;
      const bool result = rain.Composite(device, context, scene);
      compute.Restore(context);
      graphics.Restore(context);
      return result;
   }

   static bool InitializeUpscaledTarget(ID3D11Device* device, ID3D11DeviceContext* context,
      DeviceData& device_data, ID3D11Texture2D* source, DXGI_FORMAT format, ID3D11RenderTargetView* target)
   {
      ComPtr<ID3D11VertexShader> vs;
      ComPtr<ID3D11PixelShader> ps;
      {
         const std::shared_lock lock(s_mutex_shader_objects);
         auto vertex = device_data.native_vertex_shaders.find(CompileTimeStringHash("Scale VS"));
         auto pixel = device_data.native_pixel_shaders.find(CompileTimeStringHash("Scale PS"));
         if (vertex == device_data.native_vertex_shaders.end() || pixel == device_data.native_pixel_shaders.end())
            return false;
         vs = vertex->second.get();
         ps = pixel->second.get();
      }
      if (!source || !target || !vs || !ps || !device_data.sampler_state_linear ||
          !device_data.default_blend_state || !device_data.default_depth_stencil_state)
         return false;
      UINT format_support = 0;
      if (FAILED(device->CheckFormatSupport(format, &format_support)) ||
          (format_support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) == 0)
         return false; // Scale PS samples float colors; integer/load-only views need another path.
      D3D11_TEXTURE2D_DESC desc{};
      source->GetDesc(&desc);
      ComPtr<ID3D11Texture2D> readable = source;
      const bool needs_copy = (desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0;
      if (needs_copy)
      {
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.CPUAccessFlags = 0;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         desc.MiscFlags = 0;
         readable.reset();
         if (FAILED(device->CreateTexture2D(&desc, nullptr, readable.put())))
            return false;
      }
      D3D11_SHADER_RESOURCE_VIEW_DESC view{};
      view.Format = format;
      view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      view.Texture2D.MipLevels = 1;
      ComPtr<ID3D11ShaderResourceView> input;
      if (FAILED(device->CreateShaderResourceView(readable.get(), &view, input.put())))
         return false;
      ComPtr<ID3D11Resource> target_resource;
      ComPtr<ID3D11Texture2D> target_texture;
      target->GetResource(target_resource.put());
      if (FAILED(target_resource->QueryInterface(target_texture.put())))
         return false;
      D3D11_TEXTURE2D_DESC target_desc{};
      target_texture->GetDesc(&target_desc);

      DrawStateStack<DrawStateStackType::FullGraphics> graphics;
      DrawStateStack<DrawStateStackType::Compute> compute;
      graphics.Cache(context, device_data.uav_max_count);
      compute.Cache(context, device_data.uav_max_count);
      ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
      context->CSSetUnorderedAccessViews(0, device_data.uav_max_count, null_uavs, nullptr);
      context->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, device_data.uav_max_count, null_uavs, nullptr);
      // The fullscreen copy must not inherit geometry/tessellation or predication.
      ComPtr<ID3D11GeometryShader> gs;
      ComPtr<ID3D11HullShader> hs;
      ComPtr<ID3D11DomainShader> ds;
      ComPtr<ID3D11Predicate> predicate;
      BOOL predicate_value = FALSE;
      ID3D11ClassInstance* gs_instances[256] = {};
      ID3D11ClassInstance* hs_instances[256] = {};
      ID3D11ClassInstance* ds_instances[256] = {};
      UINT gs_count = ARRAYSIZE(gs_instances), hs_count = ARRAYSIZE(hs_instances), ds_count = ARRAYSIZE(ds_instances);
      context->GSGetShader(gs.put(), gs_instances, &gs_count);
      context->HSGetShader(hs.put(), hs_instances, &hs_count);
      context->DSGetShader(ds.put(), ds_instances, &ds_count);
      context->GetPredication(predicate.put(), &predicate_value);
      context->GSSetShader(nullptr, nullptr, 0);
      context->HSSetShader(nullptr, nullptr, 0);
      context->DSSetShader(nullptr, nullptr, 0);
      context->SetPredication(nullptr, FALSE);
      if (needs_copy)
         context->CopyResource(readable.get(), source);
      DrawCustomPixelShader(context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(),
         device_data.sampler_state_linear.get(), vs.get(), ps.get(), input.get(), target, target_desc.Width, target_desc.Height);
      context->OMSetRenderTargets(0, nullptr, nullptr);
      ID3D11ShaderResourceView* null_srv = nullptr;
      context->PSSetShaderResources(0, 1, &null_srv);
      compute.Restore(context);
      graphics.Restore(context);
      context->GSSetShader(gs.get(), gs_instances, gs_count);
      context->HSSetShader(hs.get(), hs_instances, hs_count);
      context->DSSetShader(ds.get(), ds_instances, ds_count);
      for (UINT i = 0; i < gs_count; ++i) gs_instances[i]->Release();
      for (UINT i = 0; i < hs_count; ++i) hs_instances[i]->Release();
      for (UINT i = 0; i < ds_count; ++i) ds_instances[i]->Release();
      context->SetPredication(predicate.get(), predicate_value);
      return true;
   }

   bool DrawAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, ID3D11ShaderResourceView* scene_srv, ID3D11RenderTargetView* scene_rtv)
   {

      auto& game_device_data = GetGameDeviceData(device_data);
      if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed || device_data.has_drawn_sr || !scene_srv || !scene_rtv || !game_device_data.taa_state.IsValid())
         return false;

      ComPtr<ID3D11Resource> scene_resource;
      scene_srv->GetResource(scene_resource.put());
      ComPtr<ID3D11Resource> output_resource;
      scene_rtv->GetResource(output_resource.put());
      D3D11_SHADER_RESOURCE_VIEW_DESC scene_view_desc{};
      scene_srv->GetDesc(&scene_view_desc);

      ComPtr<ID3D11Texture2D> scene_texture;
      if (FAILED(scene_resource->QueryInterface(scene_texture.put())))
         return false;
      D3D11_TEXTURE2D_DESC output_desc{};
      scene_texture->GetDesc(&output_desc);
      const DXGI_FORMAT view_format = output_desc.Format != scene_view_desc.Format ? scene_view_desc.Format : DXGI_FORMAT_UNKNOWN;

      // Only treat as upscaling when render < output (DLSS/FSR).
      // Supersampling (render > output) and DLAA (render == output) both use the DLAA path.
      const bool is_upscaling = device_data.render_resolution.x < device_data.output_resolution.x && device_data.render_resolution.y < device_data.output_resolution.y;
      const float2 target_resolution = is_upscaling ? device_data.output_resolution : device_data.render_resolution;
      auto& tracking = game_device_data.upscale_tracking;
      tracking.InvalidatePoolIfScaleChanged(device_data.output_resolution, device_data.render_resolution);
      std::lock_guard lock(tracking.mutex);
      UpscaledResource* result = nullptr;
      ComPtr<ID3D11ShaderResourceView> result_srv;

      result = AcquireUpscaledFromPool(native_device, native_device_context, tracking, output_desc, target_resolution, true, view_format);
      if (result)
      {
         device_data.sr_output_color = result->texture.get();
         result_srv = result->srv.get();
      }

      DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
      DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
      draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
      compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
      native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
      auto* sr_instance_data = device_data.GetSRInstanceData();
      const auto implementation = sr_implementations.find(device_data.sr_type);
      if (result_srv && sr_instance_data && implementation != sr_implementations.end() && implementation->second)
      {
         // When upscaling, SR output dimensions are output_resolution, not the RTV size
         SR::SettingsData settings_data;
         settings_data.output_width = static_cast<uint>(target_resolution.x);
         settings_data.output_height = static_cast<uint>(target_resolution.y);
         settings_data.render_width = static_cast<uint>(device_data.render_resolution.x);
         settings_data.render_height = static_cast<uint>(device_data.render_resolution.y);
         settings_data.dynamic_resolution = false;
         settings_data.hdr = true;
         settings_data.auto_exposure = true;
         settings_data.inverted_depth = false;
         settings_data.mvs_jittered = false;
         settings_data.render_preset = dlss_render_preset;
         if (implementation->second->UpdateSettings(sr_instance_data, native_device_context, settings_data))
         {
            // Prepare SR draw data
            bool reset_sr = device_data.force_reset_sr;
            device_data.force_reset_sr = false;

            float2 jitters = game_device_data.taa_jitters;

            SR::SuperResolutionImpl::DrawData draw_data;
            draw_data.source_color = scene_resource.get();
            draw_data.output_color = device_data.sr_output_color.get();
            draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
            draw_data.depth_buffer = game_device_data.depth_buffer.get();
            draw_data.jitter_x = jitters.x;
            draw_data.jitter_y = jitters.y;
            draw_data.vert_fov = game_device_data.camera_fov;
            draw_data.far_plane = game_device_data.camera_far;
            draw_data.near_plane = game_device_data.camera_near;
            draw_data.reset = reset_sr;
            draw_data.render_width = static_cast<uint>(device_data.render_resolution.x);
            draw_data.render_height = static_cast<uint>(device_data.render_resolution.y);

            // Execute SR
            device_data.has_drawn_sr = implementation->second->Draw(sr_instance_data, native_device_context, draw_data);
         }
      }
      if (!device_data.has_drawn_sr)
      {
         if (result)
            result->in_use_this_frame = false;
         result = AcquireUpscaledFromPool(native_device, native_device_context, tracking, output_desc, device_data.render_resolution, false, view_format);
         if (result)
         {
            // Fall back to native TAA, retaining its integer history output.
            game_device_data.taa_state.Restore(native_device_context);
            ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
            for (UINT i = 0; i < ARRAYSIZE(rtvs); ++i)
               rtvs[i] = game_device_data.taa_state.state->render_target_views[i].get();
            rtvs[0] = result->rtv.get();
            native_device_context->OMSetRenderTargets(game_device_data.taa_state.state->valid_render_target_views_bound, rtvs, game_device_data.taa_state.state->depth_stencil_view.get());
            native_device_context->PSSetShaderResources(0, 1, &scene_srv);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            native_device_context->Draw(4, 0);
            result_srv = result->srv.get();
         }
         else
            result_srv.reset();
         device_data.force_reset_sr = true;
      }

      if (result_srv)
      {
         game_device_data.rain.resolved = true;
         // need to copyback the upscaled result to the original TAA output. It is used as previous frame input for SSR.
         ID3D11RenderTargetView* output_rtv = game_device_data.taa_state.state->render_target_views[0].get();
         native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
         if (device_data.has_drawn_sr && is_upscaling)
            DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), device_data.sampler_state_linear.get(), device_data.native_vertex_shaders[CompileTimeStringHash("Scale VS")].get(), device_data.native_pixel_shaders[CompileTimeStringHash("Scale PS")].get(), result_srv.get(), output_rtv, static_cast<UINT>(device_data.render_resolution.x), static_cast<UINT>(device_data.render_resolution.y));
         else
         {
            ComPtr<ID3D11Resource> taa_output;
            output_rtv->GetResource(taa_output.put());
            native_device_context->CopyResource(taa_output.get(), result->texture.get());
         }

         if (device_data.has_drawn_sr && is_upscaling)
         {
            // Publish the resolved AA scene for downstream post-processing.
            tracking.frame_links[reinterpret_cast<uintptr_t>(output_resource.get())] = FindPoolIndexByPtr(tracking, result);
            tracking.post_taa_upscale_active = true;
         }
      }
      device_data.sr_output_color = nullptr;
      compute_state_stack.Restore(native_device_context);
      draw_state_stack.Restore(native_device_context);
      if (result_srv && game_device_data.rain.WantsComposition(FFXVRainComposition::AfterAA))
      {
         ID3D11RenderTargetView* target = device_data.has_drawn_sr && is_upscaling ? result->rtv.get() : scene_rtv;
         CompositeRain(native_device, native_device_context, device_data, target, true);
      }
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
      // Consume the replay state after TAA fallback; has_drawn_sr only marks successful SR.
      if (result_srv && !device_data.has_drawn_sr)
         game_device_data.taa_state.state.reset();
      return result_srv != nullptr;
   }

public:
   void OnInit(bool async) override
   {
#if LUMA_HAS_RECIPE_PROVIDERS
      Patch::RegisterRecipe({
         .identity = kDitheringFixRecipe,
         .display_name = "Dithering Fix",
         .description = "Fixes issues with low quality dithering. (Noisy transparency, etc...)",
         .default_enabled = true,
         .visibility = Patch::RecipeVisibility::Always,
      });
#endif
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

      // Quality level of the per-pixel extended tonemap pivot computation (see "Includes/Tonemap.hlsli")
      std::vector<ShaderDefineData> game_shader_defines_data = {
         { "FFXV_TONEMAP_PRECISION", '0', true, false, "Precision of the HDR tone curve extension pivot computation (cost is only paid in HDR)\n0 - Simple (fixed pivot, cheapest)\n1 - High (curve inflection)\n2 - Very High (max curvature, most accurate)", 2 },
      };
      shader_defines_data.append_range(game_shader_defines_data);

      use_os_reference_white_level = false;

      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs CS"), ShaderDefinition{"Luma_FFXV_MotionVec_Decode", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Rain Composite PS"), ShaderDefinition{"Luma_RainComposite", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(0x2100CE9BU, ShaderDefinition{"Luma_Directional_Light", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(0xA315F1E7U, ShaderDefinition{"Luma_Directional_Light_CSM", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(0x4B8E0FF8U, ShaderDefinition{"Luma_Directional_Light_CSM_AO", reshade::api::pipeline_subobject_type::pixel_shader});

      default_luma_global_game_settings.BloomStrength = 1.f;
      default_luma_global_game_settings.Sharpness = 0.3f;
      default_luma_global_game_settings.UseSDROverHDR = 1;
      default_luma_global_game_settings.UseVanillaGamutRatio = 0;
      default_luma_global_game_settings.VignetteStrength = 1.f;
      default_luma_global_game_settings.EnableDoF = 1;

      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // #w## Update this (find the right value) ###
   }

#if LUMA_HAS_RECIPE_PROVIDERS
   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      constexpr size_t frame_index_offset = offsetof(CB::LumaGlobalSettings, FrameIndex);
      static_assert(frame_index_offset % sizeof(float4) == sizeof(uint32_t)); // Recipe selects the y component.
      dxp::PatchOptions options;
      options.SetEnv("frame_index_offset", uint32_t(frame_index_offset / sizeof(float4)));
      Patch::SetOptions(device_data, CompileTimeStringHash(kDitheringFixRecipe), std::move(options));
   }
#endif

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(FinalFantasyXV::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(FinalFantasyXV::OnUnmapBufferRegion);
         reshade::register_event<reshade::addon_event::update_buffer_region>(FinalFantasyXV::OnUpdateBufferRegion);
         reshade::register_event<reshade::addon_event::clear_render_target_view>(FinalFantasyXV::OnClearRenderTargetView);
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "BloomStrenght", cb_luma_global_settings.GameSettings.BloomStrength);
      reshade::get_config_value(runtime, NAME, "Sharpness", cb_luma_global_settings.GameSettings.Sharpness);
      reshade::get_config_value(runtime, NAME, "VignetteStrength", cb_luma_global_settings.GameSettings.VignetteStrength);
      cb_luma_global_settings.GameSettings.VignetteStrength = std::clamp(cb_luma_global_settings.GameSettings.VignetteStrength, 0.f, 2.f);

      bool enable_dof = true;
      reshade::get_config_value(runtime, NAME, "EnableDoF", enable_dof);
      cb_luma_global_settings.GameSettings.EnableDoF = enable_dof ? 1 : 0;

      bool use_sdr_over_hdr = true;
      reshade::get_config_value(runtime, NAME, "UseSDROverHDR", use_sdr_over_hdr);
      cb_luma_global_settings.GameSettings.UseSDROverHDR = use_sdr_over_hdr ? 1 : 0;
      
      enable_directional_shadows = false;
      reshade::get_config_value(runtime, NAME, "DirectionalShadows", enable_directional_shadows);

      bool use_vanilla_gamut_ratio = false;
      reshade::get_config_value(runtime, NAME, "UseVanillaGamutRatio", use_vanilla_gamut_ratio);
      cb_luma_global_settings.GameSettings.UseVanillaGamutRatio = use_vanilla_gamut_ratio ? 1 : 0;
      reshade::get_config_value(runtime, NAME, "RainComposition", rain_composition);
      rain_composition = std::clamp(rain_composition, 0, 1);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      if (ImGui::TreeNodeEx("Post Process", ImGuiTreeNodeFlags_DefaultOpen))
      {
         bool enable_dof = cb_luma_global_settings.GameSettings.EnableDoF != 0;
         if (ImGui::Checkbox("Enable DoF", &enable_dof))
         {
            cb_luma_global_settings.GameSettings.EnableDoF = enable_dof ? 1 : 0;
            device_data.force_reset_sr = true;
            reshade::set_config_value(runtime, NAME, "EnableDoF", enable_dof);
         }
         if (DrawResetButton(enable_dof, true, "EnableDoF", runtime))
         {
            cb_luma_global_settings.GameSettings.EnableDoF = 1;
            device_data.force_reset_sr = true;
            reshade::set_config_value(runtime, NAME, "EnableDoF", enable_dof);
         }
         const char* rain_timings[] = {"After TAA / DLSS", "After DoF"};
         if (ImGui::Combo("Rain Composition", &rain_composition, rain_timings, IM_ARRAYSIZE(rain_timings)))
            reshade::set_config_value(runtime, NAME, "RainComposition", rain_composition);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("After TAA / DLSS: rain receives game DoF and motion blur.\nAfter DoF: rain bypasses DoF and still receives motion blur when enabled.\nRequires active SR.");
         if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
         {
            bool use_sdr_over_hdr = cb_luma_global_settings.GameSettings.UseSDROverHDR == 1;
            if (ImGui::Checkbox("Use SDR Tonemap Curve", &use_sdr_over_hdr))
            {
               cb_luma_global_settings.GameSettings.UseSDROverHDR = use_sdr_over_hdr ? 1 : 0;
               reshade::set_config_value(runtime, NAME, "UseSDROverHDR", use_sdr_over_hdr);
            }
            if (DrawResetButton(use_sdr_over_hdr, true, "UseSDROverHDR", runtime))
            {
               cb_luma_global_settings.GameSettings.UseSDROverHDR = 1;
               reshade::set_config_value(runtime, NAME, "UseSDROverHDR", use_sdr_over_hdr);
            }

            bool use_vanilla_gamut_ratio = cb_luma_global_settings.GameSettings.UseVanillaGamutRatio == 1;
            if (ImGui::Checkbox("Use Vanilla Gamut Ratio", &use_vanilla_gamut_ratio))
            {
               cb_luma_global_settings.GameSettings.UseVanillaGamutRatio = use_vanilla_gamut_ratio ? 1 : 0;
               reshade::set_config_value(runtime, NAME, "UseVanillaGamutRatio", use_vanilla_gamut_ratio);
            }
            if (DrawResetButton(use_vanilla_gamut_ratio, false, "UseVanillaGamutRatio", runtime))
            {
               cb_luma_global_settings.GameSettings.UseVanillaGamutRatio = 0;
               reshade::set_config_value(runtime, NAME, "UseVanillaGamutRatio", use_vanilla_gamut_ratio);
            }
         }
         if (ImGui::Checkbox("Directional Shadows", &enable_directional_shadows))
         {
            reshade::set_config_value(runtime, NAME, "DirectionalShadows", enable_directional_shadows);
         }
         if (DrawResetButton(enable_directional_shadows, false, "DirectionalShadows", runtime))
         {
            enable_directional_shadows = false;
            reshade::set_config_value(runtime, NAME, "DirectionalShadows", enable_directional_shadows);
         }

         if (ImGui::SliderFloat("Bloom Strength", &cb_luma_global_settings.GameSettings.BloomStrength, 0.f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "BloomStrenght", cb_luma_global_settings.GameSettings.BloomStrength);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.BloomStrength, 1.f, "BloomStrenght", runtime))
         {
            cb_luma_global_settings.GameSettings.BloomStrength = 1.f;
            reshade::set_config_value(runtime, NAME, "BloomStrenght", cb_luma_global_settings.GameSettings.BloomStrength);
         }

         if (ImGui::SliderFloat("Vignette Strength", &cb_luma_global_settings.GameSettings.VignetteStrength, 0.f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "VignetteStrength", cb_luma_global_settings.GameSettings.VignetteStrength);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.VignetteStrength, 1.f, "VignetteStrength", runtime))
         {
            cb_luma_global_settings.GameSettings.VignetteStrength = 1.f;
         }

         if (ImGui::SliderFloat("Sharpness", &cb_luma_global_settings.GameSettings.Sharpness, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
         {
            reshade::set_config_value(runtime, NAME, "Sharpness", cb_luma_global_settings.GameSettings.Sharpness);
         }
         if (DrawResetButton(cb_luma_global_settings.GameSettings.Sharpness, 0.3f, "Sharpness", runtime))
         {
            cb_luma_global_settings.GameSettings.Sharpness = 0.3f;
            reshade::set_config_value(runtime, NAME, "Sharpness", cb_luma_global_settings.GameSettings.Sharpness);
         }

         ImGui::TreePop();
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (original_shader_hashes.Contains(shader_hashes_rain) &&
          device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && !device_data.has_drawn_sr &&
          original_draw_dispatch_func)
      {
         const auto vs = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
         const auto ps = device_data.native_pixel_shaders.find(CompileTimeStringHash("Rain Composite PS"));
         if (game_device_data.rain.Capture(native_device, native_device_context,
             vs != device_data.native_vertex_shaders.end() ? vs->second.get() : nullptr,
             ps != device_data.native_pixel_shaders.end() ? ps->second.get() : nullptr, [&]()
             {
                if (is_custom_pass)
                {
                   SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
                   SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
                }
                (*original_draw_dispatch_func)();
             }, static_cast<FFXVRainComposition>(rain_composition)))
            return DrawOrDispatchOverrideType::Replaced;
         // Preparation failed before any redirect: let the original draw run.
         return DrawOrDispatchOverrideType::None;
      }
      if (original_shader_hashes.Contains(shader_hashes_directional_light) && enable_directional_shadows)
      {

         auto shader = device_data.native_pixel_shaders.find(original_shader_hashes.pixel_shaders[0])->second.get();

         if (shader != nullptr)
         {
            native_device_context->PSSetShader(shader, nullptr, 0);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);

            ID3D11ShaderResourceView* fast_noise_srv = GetFastNoiseSrv(device_data);
            if (fast_noise_srv)
            {
               native_device_context->PSSetShaderResources(42, 1, &fast_noise_srv);
            }
         }

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_TAA))
      {
         device_data.taa_detected = true;
         auto native_taa_fallback = [&]()
         {
            if (!game_device_data.rain.pending || !original_draw_dispatch_func)
               return DrawOrDispatchOverrideType::None;
            ComPtr<ID3D11RenderTargetView> target;
            native_device_context->OMGetRenderTargets(1, target.put(), nullptr);
            if (is_custom_pass)
            {
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
            }
            (*original_draw_dispatch_func)();
            game_device_data.rain.resolved = true;
            if (game_device_data.rain.WantsComposition(FFXVRainComposition::AfterAA))
               CompositeRain(native_device, native_device_context, device_data, target.get(), true);
            return DrawOrDispatchOverrideType::Replaced;
         };
         game_device_data.rain.capture_closed = true;
         if (game_device_data.found_taa_cb && game_device_data.taa_cb_data)
         {
            game_device_data.rain.jitter_x = game_device_data.taa_cb_data->g_uvJitterOffset.x;
            game_device_data.rain.jitter_y = game_device_data.taa_cb_data->g_uvJitterOffset.y;
         }
         if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed || !game_device_data.taa_cb_data || !game_device_data.found_taa_cb || !game_device_data.has_processed_view_buffer)
         {
            return native_taa_fallback();
         }

         // Extract jitter from cached TAA cbuffer (guaranteed to have correct values at this point)
         game_device_data.taa_jitters.x = game_device_data.taa_cb_data->g_uvJitterOffset.x * game_device_data.taa_cb_data->g_screenSize.x;
         game_device_data.taa_jitters.y = game_device_data.taa_cb_data->g_uvJitterOffset.y * game_device_data.taa_cb_data->g_screenSize.y;
         device_data.render_resolution = {game_device_data.taa_cb_data->g_screenSize.x, game_device_data.taa_cb_data->g_screenSize.y};
         game_device_data.taa_jitter_uv = {game_device_data.taa_cb_data->g_uvJitterOffset.x, game_device_data.taa_cb_data->g_uvJitterOffset.y};
         game_device_data.taa_state.Cache(native_device_context, device_data.uav_max_count);
         auto& taa_state = *game_device_data.taa_state.state;
         if (!taa_state.shader_resource_views[0] || !taa_state.shader_resource_views[3] || !taa_state.shader_resource_views[6] || !taa_state.constant_buffers[0] || !taa_state.render_target_views[0])
         {
            game_device_data.rain.resolved = true;
            game_device_data.taa_state.state.reset();
            return native_taa_fallback();
         }
         ComPtr<ID3D11Resource> taa_output;
         taa_state.render_target_views[0]->GetResource(taa_output.put());
         ID3D11ShaderResourceView* depth_srv = taa_state.shader_resource_views[3].get();
         ID3D11ShaderResourceView* velocity_srv = taa_state.shader_resource_views[6].get();
         if (!device_data.native_compute_shaders[CompileTimeStringHash("Decode MVs CS")] || !SetupMotionVectorDecodeTarget(native_device, game_device_data, velocity_srv))
         {
            game_device_data.rain.resolved = true;
            game_device_data.taa_state.state.reset();
            return native_taa_fallback();
         }
         // Cache state before motion vector decode
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
         DecodeMotionVectors(native_device_context, cmd_list_data, device_data, depth_srv, velocity_srv, game_device_data.sr_motion_vectors_uav.get());
         compute_state_stack.Restore(native_device_context);
         game_device_data.taa_state.Restore(native_device_context);
         game_device_data.depth_buffer = nullptr;
         taa_state.shader_resource_views[3]->GetResource(game_device_data.depth_buffer.put());
         if (DrawAA(native_device, native_device_context, cmd_list_data, device_data, taa_state.shader_resource_views[0].get(), taa_state.render_target_views[0].get()))
            return DrawOrDispatchOverrideType::Replaced;
         return native_taa_fallback();
      }

      // CoC is generated from render-resolution jittered depth; retain its native grid.
      if (device_data.has_drawn_sr && original_shader_hashes.Contains(shader_hashes_dof_coc))
      {
         ForgetNativeUpscaleOutputs(native_device_context, game_device_data.upscale_tracking);
         return DrawOrDispatchOverrideType::None;
      }

      // Track post-TAA processing through render-sized Output Scaled copies.
      // The chain ends when Output Scaled writes to a native output-sized target.
      auto& tracking = game_device_data.upscale_tracking;
      if (tracking.post_taa_upscale_active)
      {
         const bool is_tonemap = original_shader_hashes.Contains(shader_hashes_tonemap);
         const bool is_upscale_pass = original_shader_hashes.Contains(shader_hashes_OutputScaled);
         const bool is_compute = stages == reshade::api::shader_stage::compute;
         if (is_upscale_pass && game_device_data.has_drawn_tonemap &&
             OutputScaledWritesOutputResolution(native_device_context, tracking, device_data.output_resolution))
         {
            tracking.post_taa_upscale_active = false;
            ReplaceUpscaledInputs(native_device_context, tracking, is_compute, 1);
            Trace::UpdateDraw(cmd_list_data, device_data, native_device_context);
            return DrawOrDispatchOverrideType::None;
         }
         if (is_tonemap)
            game_device_data.has_drawn_tonemap = true;
         if (is_compute)
            return DrawOrDispatchOverrideType::None; // Motion blur is scaled in its pixel shader.

         // Ignore slots these shaders do not read when following scene inputs.
         const UINT active_srv_slots = is_upscale_pass ? 1u
            : original_shader_hashes.Contains(shader_hashes_bloom_glare_vignette)
               ? (original_shader_hashes.pixel_shaders[0] == 0xB6934A17u ? 2u : 1u)
               : 16u;
         const uint32_t chain_hits = ReplaceUpscaledInputs(native_device_context, tracking, false, active_srv_slots, false);
         float2 source_resolution = {};
         float2 target_resolution = {};
         bool preparation_failed = false;
         const uint32_t replaced_outputs = ReplaceUpscaledOutputs(native_device, native_device_context, tracking,
             device_data.render_resolution, device_data.output_resolution, chain_hits > 0,
             source_resolution, target_resolution,
             [&](ID3D11Texture2D* source, DXGI_FORMAT format, ID3D11RenderTargetView* target)
             {
                return InitializeUpscaledTarget(native_device, native_device_context, device_data, source, format, target);
             }, preparation_failed);
         if (!preparation_failed)
            ReplaceUpscaledInputs(native_device_context, tracking, false, active_srv_slots);
         else
            ForgetNativeUpscaleOutputs(native_device_context, tracking);
         if (replaced_outputs > 0)
            ReplaceViewports(native_device_context, cmd_list_data, source_resolution, target_resolution);
         Trace::UpdateDraw(cmd_list_data, device_data, native_device_context);
      }

      auto& rain = game_device_data.rain;
      if (rain.WantsComposition(FFXVRainComposition::AfterDoF))
      {
         if (original_shader_hashes.Contains(shader_hashes_dof_composite) && original_draw_dispatch_func)
         {
            // Use RTV0 after upscale-chain replacement, and finish the game draw
            // before blending rain. The callback is consumed exactly once here.
            ComPtr<ID3D11RenderTargetView> target;
            native_device_context->OMGetRenderTargets(1, target.put(), nullptr);
            if (is_custom_pass)
            {
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
            }
            (*original_draw_dispatch_func)();
            CompositeRain(native_device, native_device_context, device_data, target.get(), true);
            return DrawOrDispatchOverrideType::Replaced;
         }
         if (original_shader_hashes.Contains(shader_hashes_motion_blur) ||
             original_shader_hashes.Contains(shader_hashes_lens_distortion) ||
             original_shader_hashes.Contains(shader_hashes_bloom_highpass) ||
             original_shader_hashes.Contains(shader_hashes_scene_tonemap))
         {
            // DoF may be omitted. These captured scene consumers all read their
            // full scene at t0; never blend into their output or a bloom mip.
            ComPtr<ID3D11ShaderResourceView> scene;
            native_device_context->PSGetShaderResources(0, 1, scene.put());
            ComPtr<ID3D11Resource> resource;
            ComPtr<ID3D11Texture2D> texture;
            if (scene)
            {
               scene->GetResource(resource.put());
               if (SUCCEEDED(resource->QueryInterface(texture.put())))
               {
                  D3D11_TEXTURE2D_DESC desc;
                  texture->GetDesc(&desc);
                  const bool upscaled = game_device_data.upscale_tracking.post_taa_upscale_active;
                  const UINT width = upscaled ? static_cast<UINT>(device_data.output_resolution.x) : rain.width;
                  const UINT height = upscaled ? static_cast<UINT>(device_data.output_resolution.y) : rain.height;
                  if (desc.Width == width && desc.Height == height)
                     CompositeRainInput(native_device, native_device_context, device_data, scene.get(), true);
               }
            }
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
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.sr_type, device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res (0 with XeSS)
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

   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv,
      const float color[4], uint32_t rect_count, const reshade::api::rect*)
   {
      auto* device = cmd_list->get_device();
      if (device->get_api() != reshade::api::device_api::d3d11 || rtv.handle == 0)
         return false;
      auto* device_data = device->get_private_data<DeviceData>();
      if (!device_data || !device_data->game)
         return false;

      ComPtr<ID3D11RenderTargetView> effective_rtv;
      {
         const std::shared_lock lock(device_data->mutex);
         auto upgraded_view = device_data->resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views.find(rtv.handle);
         const auto effective_handle = !ignore_indirect_upgraded_textures &&
            upgraded_view != device_data->resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views.end()
            ? upgraded_view->second : rtv.handle;
         effective_rtv = reinterpret_cast<ID3D11RenderTargetView*>(effective_handle);
      }
      // Resolve upgrades ourselves: the core event performs its native clear
      // separately, and its redirected clear does not emit another event.
      TrackAndClearUpscaleTarget(reinterpret_cast<ID3D11Device*>(device->get_native()),
         reinterpret_cast<ID3D11DeviceContext*>(cmd_list->get_native()), GetGameDeviceData(*device_data).upscale_tracking,
         effective_rtv.get(), color, rect_count, device_data->render_resolution, device_data->output_resolution);
      return false;
   }

   static bool ExecuteUpscaledCopy(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource)
   {
      if (!native_device || !device_data.game)
         return false;

      auto& game_device_data = GetGameDeviceData(device_data);
      auto& tracking = game_device_data.upscale_tracking;

      if (!tracking.post_taa_upscale_active)
         return false;

      ID3D11Resource* source_resource = reinterpret_cast<ID3D11Resource*>(src_resource);
      ID3D11Resource* dest_resource = reinterpret_cast<ID3D11Resource*>(dst_resource);

      if (!source_resource || !dest_resource)
         return false;

      if (game_device_data.taa_state.IsValid())
      {
         ComPtr<ID3D11Resource> taa_output;
         game_device_data.taa_state.state->render_target_views[0]->GetResource(taa_output.put());
         if (dest_resource == taa_output.get())
            return false;
      }

      const uintptr_t source_key = reinterpret_cast<uintptr_t>(source_resource);
      UpscaledResource* upscaled_source = GetLinkedUpscaled(tracking, source_key);
      if (!upscaled_source || !upscaled_source->texture)
         return false;

      ComPtr<ID3D11DeviceContext> native_context;
      native_device->GetImmediateContext(native_context.put());
      if (!native_context)
         return false;

      D3D11_TEXTURE2D_DESC src_upscaled_desc;
      upscaled_source->texture->GetDesc(&src_upscaled_desc);
      const float2 src_target_res = {static_cast<float>(src_upscaled_desc.Width), static_cast<float>(src_upscaled_desc.Height)};
      UpscaledResource* upscaled_dest = LinkUpscaledResource(
         native_device, native_context.get(),
         dest_resource,
         tracking,
         src_target_res);
      if (!upscaled_dest || !upscaled_dest->texture)
         return false;

      native_context->CopyResource(upscaled_dest->texture.get(), upscaled_source->texture.get());

      return true;
   }

   static void ForgetNativeCopyDestination(DeviceData& device_data, uint64_t dst_resource)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      auto& tracking = game_device_data.upscale_tracking;
      if (!tracking.post_taa_upscale_active || dst_resource == 0)
         return;
      auto* destination = reinterpret_cast<ID3D11Resource*>(dst_resource);
      if (game_device_data.taa_state.IsValid())
      {
         ComPtr<ID3D11Resource> taa_output;
         game_device_data.taa_state.state->render_target_views[0]->GetResource(taa_output.put());
         if (destination == taa_output.get())
            return; // Preserve the DLSS/TAA history target's established link semantics.
      }
      UnlinkUpscaledResource(tracking, reinterpret_cast<uintptr_t>(destination));
   }

   bool OverrideCopyResource(ID3D11Device* native_device, DeviceData& device_data, uint64_t& dst_resource, uint64_t& src_resource) override
   {
      if (!native_device)
         return false;

      if (!device_data.game)
         return false;

      const bool redirected = ExecuteUpscaledCopy(native_device, device_data, dst_resource, src_resource);
      if (!redirected)
         ForgetNativeCopyDestination(device_data, dst_resource);
      return redirected;
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

      // Only intercept full-texture copies (subresource 0, no source/dest box) that
      // act like a CopyResource. Partial copies are left to the original call.
      if (src_subresource != 0 || dst_subresource != 0 || src_box != nullptr || dst_box != nullptr)
      {
         ForgetNativeCopyDestination(device_data, dst_resource);
         return false;
      }

      const bool redirected = ExecuteUpscaledCopy(native_device, device_data, dst_resource, src_resource);
      if (!redirected)
         ForgetNativeCopyDestination(device_data, dst_resource);
      return redirected;
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      // Cache every 256-byte buffer on Map (data will be available on Unmap)
      if ((access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write))
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);
         if (buffer_desc.ByteWidth == CBTemporalAA_buffer_size)
         {
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

      // Update cached TAA cbuffer data on Unmap (data is now available)
      if (game_device_data.cb_taa_buffer == buffer && game_device_data.cb_taa_buffer_map_data != nullptr)
      {
         if (!game_device_data.taa_cb_data)
         {
            game_device_data.taa_cb_data = std::make_unique<cbTemporalAA>();
         }
         std::memcpy(game_device_data.taa_cb_data.get(), game_device_data.cb_taa_buffer_map_data, sizeof(cbTemporalAA));
         game_device_data.found_taa_cb = true;
      }

      // Clear the cached pointer
      game_device_data.cb_taa_buffer_map_data = nullptr;
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
         // Update LOD bias now that we know the render resolution from view cbuffer
         if (game_device_data.has_processed_view_buffer)
         {
            UpdateLODBias(device);
         }
      }

      return false;
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      data.GameData.IsUpscaling = game_device_data.upscale_tracking.post_taa_upscale_active ? 1 : 0;
      // Capture jitter at TAA; later native draws can reuse the tracked buffer.
      data.GameData.TemporalJitterUV = device_data.has_drawn_sr ? game_device_data.taa_jitter_uv : float2{};
   }
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataFFXV;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.upscale_tracking.InvalidatePoolIfScaleChanged(device_data.output_resolution, device_data.render_resolution);

      game_device_data.ResetPerFrameData();
      if (device_data.sr_type != SR::Type::None && (device_data.sr_suppressed || !device_data.has_drawn_sr))
      {
         device_data.force_reset_sr = true;
      }
      device_data.has_drawn_sr = false;
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

      shader_hashes_scene_tonemap.pixel_shaders.emplace(std::stoul("75DFE4B0", nullptr, 16)); // Main game tonemapping
      shader_hashes_scene_tonemap.pixel_shaders.emplace(std::stoul("18EF8C72", nullptr, 16)); // Title screen tonemapping
      shader_hashes_tonemap.pixel_shaders = shader_hashes_scene_tonemap.pixel_shaders;
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("DD4C5B74", nullptr, 16)); // Post-processing / swapchain

      shader_hashes_OutputScaled.pixel_shaders.emplace(std::stoul("1B6C8C68", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("0DF0A97D", nullptr, 16));
      shader_hashes_dof_coc.pixel_shaders.emplace(std::stoul("F6FD9C57", nullptr, 16));
      shader_hashes_dof_composite.pixel_shaders.emplace(std::stoul("982A3A44", nullptr, 16));
      shader_hashes_motion_blur.pixel_shaders.emplace(std::stoul("8D509D86", nullptr, 16));
      shader_hashes_motion_blur.pixel_shaders.emplace(std::stoul("37FAF8C5", nullptr, 16));
      shader_hashes_lens_distortion.pixel_shaders.emplace(std::stoul("91A3307D", nullptr, 16));

      shader_hashes_bloom_highpass.pixel_shaders.emplace(0xFF665135u);       // Highpass
      shader_hashes_bloom_glare_vignette.pixel_shaders.emplace(0xBBC1036Cu); // Glare/vignette
      shader_hashes_bloom_glare_vignette.pixel_shaders.emplace(0xB6934A17u); // Glare/vignette with sparks
      shader_hashes_rain.pixel_shaders.emplace(0x047D130Bu); // VFXRain_Material, both captured rain draws
      shader_hashes_directional_light.pixel_shaders.emplace(std::stoul("2100CE9B", nullptr, 16)); // Directional Light
      shader_hashes_directional_light.pixel_shaders.emplace(std::stoul("A315F1E7", nullptr, 16)); // CSM
      shader_hashes_directional_light.pixel_shaders.emplace(std::stoul("4B8E0FF8", nullptr, 16)); // CSM_AO
#if 1
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled; // We don't need swapchain upgrade for this game
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;                      // 1 = scrgb
#endif
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

#if DEVELOPMENT
      // These make things messy in this game, given it renders at lower resolutions and then upscales and adds black bars beyond 16:9
      Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Fullscreen;

      forced_shader_names.emplace(std::stoul("0DF0A97D", nullptr, 16), "TAA");
      forced_shader_names.emplace(std::stoul("75DFE4B0", nullptr, 16), "Tonemap");
      forced_shader_names.emplace(std::stoul("18EF8C72", nullptr, 16), "Tonemap_TitleScreen");
      forced_shader_names.emplace(std::stoul("1040DAB1", nullptr, 16), "MotionVectorDecode");

      forced_shader_names.emplace(std::stoul("1B6C8C68", nullptr, 16), "Output Scaled");
      forced_shader_names.emplace(std::stoul("850830F0", nullptr, 16), "Dirt");
      forced_shader_names.emplace(0xFF665135u, "Bloom Highpass");
      forced_shader_names.emplace(0xD1F0305Eu, "Bloom Blur3Tap");
      forced_shader_names.emplace(0xDB66D833u, "Bloom Blur5Tap");
      forced_shader_names.emplace(0x2558DE2Fu, "Bloom Blur9Tap");
      forced_shader_names.emplace(0x6150AEF2u, "Bloom Blur17Tap");
      forced_shader_names.emplace(0x1B5AC203u, "Bloom Blur33Tap");
      forced_shader_names.emplace(0x635313E0u, "Bloom Blur65Tap");
      forced_shader_names.emplace(0xA6FC2BA8u, "Bloom Copy");
      forced_shader_names.emplace(0x22F93AE1u, "Bloom ConstantColor");
      forced_shader_names.emplace(0xDDF4077Eu, "Bloom Composite");
      forced_shader_names.emplace(0xBBC1036Cu, "Bloom GlareVignette");
      forced_shader_names.emplace(0xB6934A17u, "Bloom GlareVignette Spark");
#endif

      // texture_upgrade_formats = {
      //   reshade::api::format::r11g11b10_float
      // };
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectAndIndirectDependencies;

      // auto_texture_format_upgrade_shader_hashes[std::stoul("75DFE4B0", nullptr, 16)] = {{0}, {}}; // Main game tonemapping
      // auto_texture_format_upgrade_shader_hashes[std::stoul("18EF8C72", nullptr, 16)] = {{0}, {}}; // Title screen tonemapping
      // auto_texture_format_upgrade_shader_hashes[std::stoul("DD4C5B74", nullptr, 16)] = {{0}, {}}; // Post-processing / swapchain
      //  TAA seed: upgrade its output RTV 0 (RGBA16F, required by the copy conversion paths) and scale it
      //  render_resolution -> output_resolution.
      //  {
      //    AutoTextureFormatUpgradeShaderHash taa_upgrade;
      //    taa_upgrade.rtv_slots = {0};
      //    taa_upgrade.scale = false;
      //    auto_texture_format_upgrade_shader_hashes[std::stoul("0DF0A97D", nullptr, 16)] = taa_upgrade; // TAA
      //  }

      enable_samplers_upgrade = true;

      game = new FinalFantasyXV();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(FinalFantasyXV::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(FinalFantasyXV::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(FinalFantasyXV::OnUpdateBufferRegion);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(FinalFantasyXV::OnClearRenderTargetView);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
