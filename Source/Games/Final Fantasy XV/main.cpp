#define GAME_FINALFANTASYXV 1


#ifdef _DEBUG
#define ALLOW_SHADERS_DUMPING 1
#define ALLOW_SHADER_PATCHES_DUMPING 1
#endif

#define ENABLE_BLOOM 1
#define LUMA_PATCH_RECIPE_ASYNC 1

#define DEBUG_LOG 0

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

#if LUMA_HAS_RECIPE_PROVIDERS
static constexpr uint32_t FFXV_DEPTH_DITHERING_RECIPE_HASH = "FFXV Depth Dithering"_h;
#endif

#if ENABLE_FAST_NOISE_TEXTURES
static constexpr uint32_t kCoreFastNoiseTextureHash = "FAST Noise"_h;
#endif

namespace
{
   ShaderHashesList shader_hashes_tonemap;
   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_OutputScaled;
   ShaderHashesList shader_hashes_bloom_highpass;
   ShaderHashesList shader_hashes_bloom_skip;
   ShaderHashesList shader_hashes_bloom_glare_vignette;
   ShaderHashesList shader_hashes_attach_fast_noise;
   ShaderHashesList shader_hashes_directional_light;
   const uint32_t CBTemporalAA_buffer_size = 256;
   const uint32_t CBView_buffer_size = 768;

   bool enable_directional_shadows = false;

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
         Log_Debug(reshade::log::level::error, log_buf);
         ASSERT_ONCE(false);
      }

      if (bound_srv)
      {
         bound_srv->Release();
      }
   }
#endif

#if ENABLE_BLOOM
   bool g_luma_bloom_enable = true;
   int g_bloom_nmips = 6;
   std::vector<float> g_bloom_sigmas;
#endif

#if LUMA_HAS_RECIPE_PROVIDERS
   bool BindPatchedResources(ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, reshade::api::shader_stage stages, bool& updated_cbuffers)
   {
      if ((stages & reshade::api::shader_stage::pixel) == 0 || original_shader_hashes.pixel_shaders[0] == UINT64_MAX)
      {
         return false;
      }

      // Resolved per shader at patch-apply time; read every draw on the render
      // thread (finalized map only, updated on present — no lock needed).
      // Resources are bound only once the clone was published (ready), so they
      // never reach a shader that is still running its original bytecode.
      auto& game_device_data = *static_cast<GameDeviceDataFFXV*>(device_data.game);
      const auto binding_it = game_device_data.dxp_bindings.find(static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]));
      const bool patch_ready = binding_it != game_device_data.dxp_bindings.end() && binding_it->second.ready;
      const uint32_t fast_noise_bind_point = patch_ready ? binding_it->second.fast_noise_bind_point : UINT32_MAX;
      const uint32_t frame_constants_bind_point = patch_ready ? binding_it->second.frame_constants_bind_point : UINT32_MAX;

      bool any_bound = false;

      if (fast_noise_bind_point != UINT32_MAX)
      {
         ID3D11ShaderResourceView* fast_noise_srv = GetFastNoiseSrv(device_data);
         if (fast_noise_srv)
         {
            native_device_context->PSSetShaderResources(fast_noise_bind_point, 1, &fast_noise_srv);
#if DEVELOPMENT
            VerifyFastNoiseBinding(native_device_context, fast_noise_bind_point, fast_noise_srv, "DXP");
#endif
            any_bound = true;
         }
      }

      if (frame_constants_bind_point != UINT32_MAX)
      {
         // The recipe reads the frame index from Luma's own settings cbuffer
         // (cb13[2].y). Upload the current settings (FrameIndex) if dirty and
         // bind it at the recipe's fixed slot (register 13).
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
         ID3D11Buffer* luma_settings_cb = device_data.luma_global_settings.get();
         native_device_context->PSSetConstantBuffers(frame_constants_bind_point, 1, &luma_settings_cb);
         updated_cbuffers = true;
         any_bound = true;
      }

      return any_bound;
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

public:
   std::atomic<bool> dithering_patch_enabled = true;

   void OnInit(bool async) override
   {
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
      native_shaders_definitions.emplace(CompileTimeStringHash("Output Scaled PS"), ShaderDefinition{"Luma_FFXV_Output_Scaled", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(0x2100CE9BU, ShaderDefinition{"Luma_Directional_Light", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(0xA315F1E7U, ShaderDefinition{"Luma_Directional_Light_CSM", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(0x4B8E0FF8U, ShaderDefinition{"Luma_Directional_Light_CSM_AO", reshade::api::pipeline_subobject_type::pixel_shader});


      default_luma_global_game_settings.BloomStrength = 1.f;
      default_luma_global_game_settings.Sharpness = 0.3f;
      default_luma_global_game_settings.UseSDROverHDR = 1;
      default_luma_global_game_settings.UseVanillaGamutRatio = 0;

      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12; // #w## Update this (find the right value) ###

#if ENABLE_BLOOM
      g_bloom_sigmas.resize(g_bloom_nmips);
      g_bloom_sigmas[0] = 1.5f;
      g_bloom_sigmas[1] = 2.0f;
      g_bloom_sigmas[2] = 2.0f;
      g_bloom_sigmas[3] = 2.0f;
      g_bloom_sigmas[4] = 1.0f;
      g_bloom_sigmas[5] = 1.0f;
#endif
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

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if LUMA_HAS_RECIPE_PROVIDERS
      reshade::log::message(reshade::log::level::info, std::format("Recipe directory: {}", device_data.recipes.GetRootPath().string()).c_str());
      bool loaded = device_data.recipes.LoadFromFile(FFXV_DEPTH_DITHERING_RECIPE_HASH, "FFXV_Dithering_Fixes");
      ASSERT_ONCE_MSG(loaded, "Failed to load FFXV depth dithering DXP recipe");
#endif
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "BloomStrenght", cb_luma_global_settings.GameSettings.BloomStrength);
      reshade::get_config_value(runtime, NAME, "Sharpness", cb_luma_global_settings.GameSettings.Sharpness);

      bool use_sdr_over_hdr = true;
      reshade::get_config_value(runtime, NAME, "UseSDROverHDR", use_sdr_over_hdr);
      cb_luma_global_settings.GameSettings.UseSDROverHDR = use_sdr_over_hdr ? 1 : 0;
      
      enable_directional_shadows = false;
      reshade::get_config_value(runtime, NAME, "DirectionalShadows", enable_directional_shadows);

      bool dithering_patch_enabled = true;
      reshade::get_config_value(runtime, NAME, "DitheringFix", dithering_patch_enabled);
      this->dithering_patch_enabled.store(dithering_patch_enabled, std::memory_order_relaxed);

      bool use_vanilla_gamut_ratio = false;
      reshade::get_config_value(runtime, NAME, "UseVanillaGamutRatio", use_vanilla_gamut_ratio);
      cb_luma_global_settings.GameSettings.UseVanillaGamutRatio = use_vanilla_gamut_ratio ? 1 : 0;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      if (ImGui::TreeNodeEx("Post Process", ImGuiTreeNodeFlags_DefaultOpen))
      {

         if (ImGui::Checkbox("Directional Shadows", &enable_directional_shadows))
         {
            reshade::set_config_value(runtime, NAME, "DirectionalShadows", enable_directional_shadows);
         }
         if (DrawResetButton(enable_directional_shadows, false, "DirectionalShadows", runtime))
         {
            enable_directional_shadows = false;
            reshade::set_config_value(runtime, NAME, "DirectionalShadows", enable_directional_shadows);
         }

#if LUMA_HAS_RECIPE_PROVIDERS
         bool dithering_patch_enabled = this->dithering_patch_enabled.load(std::memory_order_relaxed);
         if (ImGui::Checkbox("Dithering Fix", &dithering_patch_enabled))
         {
            this->dithering_patch_enabled.store(dithering_patch_enabled, std::memory_order_relaxed);
            reshade::set_config_value(runtime, NAME, "DitheringFix", dithering_patch_enabled);

         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Fixes issues with low quality dithering. (Noisy transparency, etc...)");
         if (DrawResetButton(dithering_patch_enabled, true, "DitheringFix", runtime))
         {
            this->dithering_patch_enabled.store(true, std::memory_order_relaxed);
            reshade::set_config_value(runtime, NAME, "DitheringFix", dithering_patch_enabled);
         }
#endif

#if ENABLE_BLOOM
         if (ImGui::Checkbox("Enable Luma Bloom", &g_luma_bloom_enable))
         {
            reshade::set_config_value(runtime, NAME, "UseLumaBloom", g_luma_bloom_enable);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Replaces game bloom, fixes blockyness issues.");

         if (DrawResetButton(g_luma_bloom_enable, true, "UseLumaBloom", runtime))
         {
            reshade::set_config_value(runtime, NAME, "UseLumaBloom", g_luma_bloom_enable);
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
#endif

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

#if DEVELOPMENT
#if ENABLE_BLOOM

      ImGui::NewLine();

      if (ImGui::SliderInt("Luma Bloom Mips", &g_bloom_nmips, 1, 10))
         g_bloom_sigmas.resize(g_bloom_nmips, 2.0f);
      for (int i = 0; i < g_bloom_nmips; ++i)
      {
         ImGui::PushID(i); // Unique ID per slider, otherwise ImGui flags the loop items as conflicting IDs
         const std::string name = "Luma Bloom Sigma " + std::to_string(i);
         ImGui::SliderFloat(name.c_str(), &g_bloom_sigmas[i], 0.0f, 15.0f, "%.3f");
         ImGui::PopID();
      }
#endif
#endif
   }

#if LUMA_PATCH_RECIPE_ASYNC
   // Recipe provider (DXP), asynchronous: runs on the background thread; the
   // patched container is applied via a pipeline clone (bind-time swap).
   std::optional<dxp::RecipeReport> PatchShaderRecipeAsync(DeviceData& device_data, const Patch::ShaderPatchRequest& request) override
   {
      std::shared_ptr<dxp::sm5::Recipe> recipe = device_data.recipes.GetRecipe(FFXV_DEPTH_DITHERING_RECIPE_HASH);
      if (recipe == nullptr || request.shader_container == nullptr || request.shader_container_size < sizeof(DXBCHeader))
      {
         return std::nullopt;
      }

      if (request.type != reshade::api::pipeline_subobject_type::pixel_shader)
      {
         return std::nullopt;
      }

      dxp::PatchOptions options;
      options.logger = [](dxp::LogLevel level, const std::string& message)
      {
         reshade::log::level reshade_level = reshade::log::level::info;
         switch (level)
         {
         case dxp::LogLevel::Error:
            reshade_level = reshade::log::level::error;
            break;
         case dxp::LogLevel::Warning:
            reshade_level = reshade::log::level::warning;
            break;
         case dxp::LogLevel::Debug:
            reshade_level = reshade::log::level::debug;
            break;
         case dxp::LogLevel::Info:
            break;
         }
         reshade::log::message(reshade_level, ("[Luma] [Patch] " + message).c_str());
      };
      options.log_level = dxp::LogLevel::Warning;

      std::span<const uint8_t> input_container;
      if (request.shader_container_owned != nullptr)
      {
         input_container = *request.shader_container_owned;
      }
      else
      {
         input_container = {reinterpret_cast<const uint8_t*>(request.shader_container), request.shader_container_size};
      }

      auto result = recipe->Execute(input_container, options);
      if (!result || result->output_bytes.empty())
      {
         reshade::log::message(reshade::log::level::warning,
            std::format("[Patch] recipe execution failed for shader {:08X}", request.shader_hash).c_str());
         return std::nullopt;
      }

      if (!result->modified)
      {
         return std::nullopt;
      }

      {
         auto& game_device_data = *static_cast<GameDeviceDataFFXV*>(device_data.game);
         GameDeviceDataFFXV::FFXVDxpBindingEntry entry{};
         for (const auto& binding_pair : result->new_bindings)
         {
            const auto& handle = binding_pair.first;
            const auto& binding = binding_pair.second;
            if (handle == "fast_noise" && binding.binding_class == dxp::BindingClass::Texture)
            {
               entry.fast_noise_bind_point = binding.register_index;
            }
            else if (handle == "frame_constants" && binding.binding_class == dxp::BindingClass::CBuffer)
            {
               entry.frame_constants_bind_point = binding.register_index;
            }
         }
         // Pending until the clone is published on present (ready).
         const std::lock_guard lock(game_device_data.pending_mutex);
         game_device_data.pending_dxp_bindings.insert_or_assign(request.shader_hash, std::move(entry));
      }

#if DEVELOPMENT || TEST
      reshade::log::message(reshade::log::level::debug,
         std::format("[Patch] patched shader {:08X} ({} bytes, {} new bindings)", request.shader_hash, result->output_bytes.size(), result->new_bindings.size()).c_str());
#endif
      return std::move(*result);
   }
#endif

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if LUMA_HAS_RECIPE_PROVIDERS
      // Bind recipe resources only when toggle is ON and this shader was patched.
      if ((stages & reshade::api::shader_stage::pixel) != 0
          && dithering_patch_enabled.load(std::memory_order_relaxed)
          && !original_shader_hashes.pixel_shaders.empty())
      {
         BindPatchedResources(native_device_context, cmd_list_data, device_data, original_shader_hashes, stages, updated_cbuffers);
      }
#endif


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
#if DEVELOPMENT
            VerifyFastNoiseBinding(native_device_context, 42, fast_noise_srv, "AttachFastNoise");
#endif
            }
         }

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

         if (original_shader_hashes.Contains(shader_hashes_OutputScaled))
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

         // Extract jitter from cached TAA cbuffer (guaranteed to have correct values at this point)
         if (game_device_data.taa_cb_data)
         {
            game_device_data.taa_jitters.x = game_device_data.taa_cb_data->g_uvJitterOffset.x * game_device_data.taa_cb_data->g_screenSize.x;
            game_device_data.taa_jitters.y = game_device_data.taa_cb_data->g_uvJitterOffset.y * game_device_data.taa_cb_data->g_screenSize.y;
            device_data.render_resolution = {game_device_data.taa_cb_data->g_screenSize.x, game_device_data.taa_cb_data->g_screenSize.y};
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
         D3D11_TEXTURE2D_DESC output_texture_desc = {};
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
            output_color->GetDesc(&output_texture_desc); // The upscale branch also fills the copyback size below

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
            game_device_data.sr_output_srv = taa_upscaled->srv.get();
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
            {
               game_device_data.sr_output_srv = nullptr;
               device_data.sr_output_color = nullptr;
            }
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

         float2 jitters = game_device_data.taa_jitters;

         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.source_color = game_device_data.sr_source_color.get();
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
               // need to copyback the upscaled result to the original TAA output. It is used as previous frame input for SSR.

               native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
               SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);

               DrawCustomPixelShader(
                  native_device_context,
                  device_data.default_depth_stencil_state.get(),
                  device_data.default_blend_state.get(),
                  device_data.sampler_state_linear.get(),
                  device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(),
                  device_data.native_pixel_shaders[CompileTimeStringHash("Output Scaled PS")].get(),
                  game_device_data.sr_output_srv.get(),
                  output_rtv,
                  output_texture_desc.Width,
                  output_texture_desc.Height);

               device_data.sr_output_color = nullptr; // SR output is the pooled texture linked to TAA output, so clear the main SR output reference to avoid confusion. The pooled texture is accessed via the upscale tracking system, not the main SR output slot.
               game_device_data.sr_output_srv = nullptr;

               draw_state_stack.Restore(native_device_context);
               compute_state_stack.Restore(native_device_context);
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
                  game_device_data.sr_output_srv = nullptr;
               }
               draw_state_stack.Restore(native_device_context);
               compute_state_stack.Restore(native_device_context);
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
            game_device_data.sr_output_srv = nullptr;
         }
         draw_state_stack.Restore(native_device_context);
         compute_state_stack.Restore(native_device_context);
      }

      // Luma bloom: at the highpass pass capture the pre-bloom scene SRV, skip all intermediate
      // passes, and at the final glare/vignette pass substitute the game's composite bloom with
      // Luma's separable Gaussian bloom so the original shader applies its glare/vignette on top.
#if ENABLE_BLOOM
      if (g_luma_bloom_enable)
      {
         if (original_shader_hashes.Contains(shader_hashes_bloom_highpass))
         {
            native_device_context->PSGetShaderResources(0, 1, game_device_data.bloom_scene_srv.put());
            native_device_context->PSGetConstantBuffers(0, 1, game_device_data.bloom_globals_cb.put());
            game_device_data.captured_bloom_scene = true;
            return DrawOrDispatchOverrideType::Skip;
         }

         if (original_shader_hashes.Contains(shader_hashes_bloom_skip))
            return DrawOrDispatchOverrideType::Skip;

         if (original_shader_hashes.Contains(shader_hashes_bloom_glare_vignette) && game_device_data.captured_bloom_scene)
         {
            // Rebind the highpass cbuffer to b0 so bloom_prefilter_ps can read
            // highpass_gamma and highpass_threshold directly from the game's data.
            ComPtr<ID3D11Buffer> prev_cb;
            native_device_context->PSGetConstantBuffers(0, 1, prev_cb.put());
            native_device_context->PSSetConstantBuffers(0, 1, &game_device_data.bloom_globals_cb);

            // If upscaling is active, prefer the upscaled version of the captured scene SRV
            // so bloom operates at output resolution rather than render resolution.
            ID3D11ShaderResourceView* bloom_input_srv = game_device_data.bloom_scene_srv.get();
            if (game_device_data.upscale_tracking.post_taa_upscale_active && bloom_input_srv)
            {
               ComPtr<ID3D11Resource> srv_resource;
               bloom_input_srv->GetResource(srv_resource.put());
               if (srv_resource)
               {
                  const uintptr_t key = reinterpret_cast<uintptr_t>(srv_resource.get());
                  UpscaledResource* upscaled = GetLinkedUpscaled(game_device_data.upscale_tracking, key);
                  if (upscaled && upscaled->srv)
                     bloom_input_srv = upscaled->srv.get();
               }
            }

            ComPtr<ID3D11ShaderResourceView> srv_bloom;
            DrawBloom(native_device, native_device_context, device_data,
               bloom_input_srv, g_bloom_nmips, g_bloom_sigmas.data(), srv_bloom.put());

            native_device_context->PSSetConstantBuffers(0, 1, &prev_cb);

            if (srv_bloom)
            {
               ID3D11ShaderResourceView* p = srv_bloom.get();
               native_device_context->PSSetShaderResources(0, 1, &p);
            }
         }
      }
#endif
      // Post-TAA upscale tracking: replace inputs/outputs/viewports for all passes
      // between TAA and the Upscale shader (1B6C8C68) when DLSS upscaling is active.
      // Chain propagation is restricted to render-resolution RTV replacements.
      if (game_device_data.upscale_tracking.post_taa_upscale_active)
      {
         const bool is_tonemap = original_shader_hashes.Contains(shader_hashes_tonemap);
         const bool is_upscale_pass = original_shader_hashes.Contains(shader_hashes_OutputScaled);
         const bool is_compute = (stages == reshade::api::shader_stage::compute);

         // Always check the terminal pass first, even for compute dispatches, so the tracking
         // window is closed regardless of shader stage. The upscale shader is a pixel shader
         // in practice, but ordering this first prevents a future regression.
         if (is_upscale_pass && game_device_data.has_drawn_tonemap)
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
#if DEVELOPMENT
            {
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::shared_lock lock_generic(s_mutex_generic);
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                  const std::shared_lock lock_device(device_data.mutex);
                  AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_vertex_shader.handle, shader_cache, last_draw_dispatch_data, device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views);
                  if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0)
                     AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_pixel_shader.handle, shader_cache, last_draw_dispatch_data, device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views);
               }
            }
#endif
            return DrawOrDispatchOverrideType::None;
         }

         if (is_tonemap)
         {
            game_device_data.has_drawn_tonemap = true;
         }

         if (is_compute)
            return DrawOrDispatchOverrideType::None; // It's just Motion Blur, we scale it in the pixel shader instead

         // All intermediate passes get RTV+viewport replacement.
         // (The Upscale shader returns early above so replace_outputs is always true here.)

         // Replace input SRVs that reference upscaled resources
         uint32_t srv_swapped = 0;
#if DEVELOPMENT || TEST
         std::vector<UpscaleSwapDetail> swap_details;
         UpscaleRtvDecisionDebug rtv_debug;
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
         rtv_count = ReplaceUpscaledOutputs(native_device, native_device_context, game_device_data.upscale_tracking, device_data.render_resolution, device_data.output_resolution, &swap_details, &rtv_debug);
#else
         rtv_count = ReplaceUpscaledOutputs(native_device, native_device_context, game_device_data.upscale_tracking, device_data.render_resolution, device_data.output_resolution);
#endif

#if DEVELOPMENT || TEST
         {
            const uint32_t active_hash = static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]);
            if (rtv_count == 0)
            {
               char log_buf[512];
               snprintf(log_buf, sizeof(log_buf),
                  "[FFXV UpscaleChain] [shader=%08X] RTV not replaced: reason=%s src=%ux%u fmt=%u view_fmt=%u render=%ux%u output=%ux%u target=%ux%u ar=%.6f render_ar=%.6f res=%p",
                  active_hash,
                  UpscaleRtvDecisionToString(rtv_debug.decision),
                  rtv_debug.source_width, rtv_debug.source_height,
                  static_cast<uint32_t>(rtv_debug.source_format),
                  static_cast<uint32_t>(rtv_debug.view_format),
                  rtv_debug.render_width, rtv_debug.render_height,
                  rtv_debug.output_width, rtv_debug.output_height,
                  rtv_debug.target_width, rtv_debug.target_height,
                  rtv_debug.source_aspect_ratio, rtv_debug.render_aspect_ratio,
                  reinterpret_cast<void*>(rtv_debug.source_key));
               Log_Debug(reshade::log::level::info, log_buf);
            }
         }
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
#if DEVELOPMENT
            {
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::shared_lock lock_generic(s_mutex_generic);
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                  const std::shared_lock lock_device(device_data.mutex);
                  AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_vertex_shader.handle, shader_cache, last_draw_dispatch_data, device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views);
                  if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0)
                     AddTraceDrawCallData(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_pixel_shader.handle, shader_cache, last_draw_dispatch_data, device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views);
               }
            }
#endif
#if DEVELOPMENT || TEST
            {
               const uint32_t active_hash = static_cast<uint32_t>(original_shader_hashes.pixel_shaders[0]);
               char log_buf[256];
               snprintf(log_buf, sizeof(log_buf),
                  "[FFXV UpscaleChain] [shader=%08X] pass applied: hits=%u srv_swapped=%u rtv_replaced=%u vp_replaced=%u",
                  active_hash, chain_hits, srv_swapped, rtv_count, vp_count);
               Log_Debug(reshade::log::level::info, log_buf);
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

#if LUMA_PATCH_PROVIDERS != 0
   void OnPatchedShadersPublished(DeviceData& device_data, const std::vector<uint32_t>& published_shader_hashes) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      // Only hashes whose clone actually went live this present become usable.
      const std::lock_guard lock(game_device_data.pending_mutex);
      for (uint32_t shader_hash : published_shader_hashes)
      {
         auto it = game_device_data.pending_dxp_bindings.find(shader_hash);
         if (it == game_device_data.pending_dxp_bindings.end())
         {
            continue;
         }
         it->second.ready = true;
         game_device_data.dxp_bindings.insert_or_assign(shader_hash, std::move(it->second));
         game_device_data.pending_dxp_bindings.erase(it);
      }
   }

   bool OnBindPatchedShader(DeviceData& device_data, uint32_t shader_hash, reshade::api::pipeline_subobject_type type) override
   {
      return dithering_patch_enabled.load(std::memory_order_relaxed)
         && type == reshade::api::pipeline_subobject_type::pixel_shader;
   }
#endif

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

      D3D11_TEXTURE2D_DESC src_upscaled_desc;
      upscaled_source->texture->GetDesc(&src_upscaled_desc);
      const float2 src_target_res = {static_cast<float>(src_upscaled_desc.Width), static_cast<float>(src_upscaled_desc.Height)};
      UpscaledResource* upscaled_dest = LinkUpscaledResource(
         native_device, native_context.get(),
         dest_resource,
         tracking,
         src_target_res);
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
      if (!game_device_data.taa_cb_data)
         return;

      // Copy the motion matrix byte-for-byte - the game's cbuffer already has the matrix
      // in the correct format for the shader (we're just replicating what the game does)
      data.GameData.IsUpscaling = game_device_data.upscale_tracking.post_taa_upscale_active ? 1 : 0;
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

      shader_hashes_OutputScaled.pixel_shaders.emplace(std::stoul("1B6C8C68", nullptr, 16));
      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("0DF0A97D", nullptr, 16));

      // Bloom: highpass captures the pre-bloom scene, all intermediate passes are skipped,
      // and the glare/vignette pass is the final combine that receives the Luma bloom.
      shader_hashes_bloom_highpass.pixel_shaders.emplace(0xFF665135u);       // Highpass
      shader_hashes_bloom_skip.pixel_shaders.emplace(0xD1F0305Eu);           // Blur3Tap
      shader_hashes_bloom_skip.pixel_shaders.emplace(0xDB66D833u);           // Blur5Tap
      shader_hashes_bloom_skip.pixel_shaders.emplace(0x2558DE2Fu);           // Blur9Tap
      shader_hashes_bloom_skip.pixel_shaders.emplace(0x6150AEF2u);           // Blur17Tap
      shader_hashes_bloom_skip.pixel_shaders.emplace(0x1B5AC203u);           // Blur33Tap
      shader_hashes_bloom_skip.pixel_shaders.emplace(0x635313E0u);           // Blur65Tap
      shader_hashes_bloom_skip.pixel_shaders.emplace(0xA6FC2BA8u);           // Copy
      shader_hashes_bloom_skip.pixel_shaders.emplace(0x22F93AE1u);           // ConstantColor
      shader_hashes_bloom_skip.pixel_shaders.emplace(0xDDF4077Eu);           // Composite
      shader_hashes_bloom_glare_vignette.pixel_shaders.emplace(0xBBC1036Cu); // GlareVignette
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
      debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::Fullscreen;

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
#endif

      // texture_upgrade_formats = {
      //   reshade::api::format::r11g11b10_float
      // };
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectAndIndirectDependencies;

      // auto_texture_format_upgrade_shader_hashes[std::stoul("75DFE4B0", nullptr, 16)] = {{0}, {}}; // Main game tonemapping
      // auto_texture_format_upgrade_shader_hashes[std::stoul("18EF8C72", nullptr, 16)] = {{0}, {}}; // Title screen tonemapping
      // auto_texture_format_upgrade_shader_hashes[std::stoul("DD4C5B74", nullptr, 16)] = {{0}, {}}; // Post-processing / swapchain
      //  TAA seed: upgrade its output RTV 0 and scale it render_resolution -> output_resolution.
      {
         AutoTextureFormatUpgradeShaderHash taa_upgrade;
         taa_upgrade.rtv_slots = {0};
         taa_upgrade.scale = true;
         auto_texture_format_upgrade_shader_hashes[std::stoul("0DF0A97D", nullptr, 16)] = taa_upgrade; // TAA
      }

      enable_samplers_upgrade = true;

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
