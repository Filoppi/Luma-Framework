#define GAME_QUANTUM_BREAK 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "../../../Shaders/Quantum Break/Includes/GameCBuffers.hlsl"
#include "../../Core/core.hpp"

// Quantum Break's SR hook sits inside the temporal resolve path:
// history reprojection provides motion vectors, temporal resolve provides color/depth/cbuffer inputs,
// and two fullscreen conversion passes let DLSS run on linear color while the game remains gamma-space.
namespace
{
   // Pass hashes used to identify the two parts of QB's temporal pipeline we need to observe/replace.
   ShaderHashesList shader_hashes_history_reprojection;
   ShaderHashesList shader_hashes_temporal_resolve;

   constexpr float sr_vertical_fov_fallback = 0.775934f; // ~44.46 degrees
   // Byte offsets into QB's cb_update_1 cbuffer for the SR inputs that are not available from textures.
   constexpr uint32_t cb_update_1_inv_near_offset = 47u * 16u;
   constexpr uint32_t cb_update_1_view_to_clip_offset = 10u * 16u;
   constexpr uint32_t cb_update_1_tess_view_to_clip_11_offset = 112u * 16u + 12u;
   constexpr uint32_t cb_update_1_jitter_offset = 121u * 16u;
   constexpr uint32_t cb_update_1_min_size = cb_update_1_jitter_offset + sizeof(float) * 2u;
   // The temporal resolve samples current color with g_vSSAAJitterOffset[0].
   constexpr uint32_t ssaa_jitter_offset = 12u * 16u;
   constexpr uint32_t ssaa_min_size = ssaa_jitter_offset + sizeof(float) * 2u;

   float ComputeVerticalFovFromProjectionScale(float projection_scale)
   {
      // Projection matrix scale is 1 / tan(fov / 2). Invalid values fall back to the previous FOV.
      if (!std::isfinite(projection_scale))
      {
         return 0.f;
      }

      const float abs_projection_scale = std::fabs(projection_scale);
      if (abs_projection_scale <= 1e-6f)
      {
         return 0.f;
      }

      const float fov = 2.f * std::atan(1.f / abs_projection_scale);
      return (std::isfinite(fov) && fov > 0.f && fov < 3.13f) ? fov : 0.f;
   }

   bool HasTextureShapeChanged(const D3D11_TEXTURE2D_DESC& current_desc, const D3D11_TEXTURE2D_DESC& previous_desc)
   {
      return current_desc.Width != previous_desc.Width || current_desc.Height != previous_desc.Height || current_desc.Format != previous_desc.Format || current_desc.ArraySize != previous_desc.ArraySize || current_desc.MipLevels != previous_desc.MipLevels || current_desc.SampleDesc.Count != previous_desc.SampleDesc.Count || current_desc.SampleDesc.Quality != previous_desc.SampleDesc.Quality;
   }

   bool UpdatePreviousTextureDesc(const D3D11_TEXTURE2D_DESC& current_desc, D3D11_TEXTURE2D_DESC& previous_desc, bool& has_previous_desc)
   {
      // DLSS history must reset when any SR input/output texture shape changes.
      const bool changed = has_previous_desc && HasTextureShapeChanged(current_desc, previous_desc);
      previous_desc = current_desc;
      has_previous_desc = true;
      return changed;
   }

   template <typename T>
   T ReadCBufferValue(const uint8_t* base, uint32_t offset)
   {
      T value = {};
      std::memcpy(&value, base + offset, sizeof(T));
      return value;
   }

   void ReadCBufferFloat2(const uint8_t* base, uint32_t offset, float& x, float& y)
   {
      x = ReadCBufferValue<float>(base, offset);
      y = ReadCBufferValue<float>(base, offset + sizeof(float));
   }

   namespace Settings
   {
      // Slider descriptors keep defaults, config keys, UI labels, and cbuffer members in one table.
      enum class Kind : uint8_t
      {
         Float,
         Integer
      };

      struct Descriptor
      {
         Kind kind = Kind::Float;
         const char* label = "";
         float CB::LumaGameSettings::* member = nullptr;
         float default_value = 1.f;
         float min_value = 0.f;
         float max_value = 2.f;
         const char* format = "%.2f";
         const char* tooltip = nullptr;
         std::vector<std::string> labels = {"Off", "On"};
         bool (*is_enabled)(const CB::LumaGameSettings&) = nullptr;
      };

      const Descriptor k_descriptors[] = {
         {
            .kind = Kind::Float,
            .label = "Highlights",
            .member = &CB::LumaGameSettings::Highlights,
         },
         {
            .kind = Kind::Float,
            .label = "Shadows",
            .member = &CB::LumaGameSettings::Shadows,
         },
         {
            .kind = Kind::Float,
            .label = "Contrast",
            .member = &CB::LumaGameSettings::Contrast,
         },
         {
            .kind = Kind::Float,
            .label = "Saturation",
            .member = &CB::LumaGameSettings::Saturation,
         },
         {
            .kind = Kind::Float,
            .label = "Highlight Saturation",
            .member = &CB::LumaGameSettings::HighlightSaturation,
         },
         {
            .kind = Kind::Float,
            .label = "Dechroma",
            .member = &CB::LumaGameSettings::Dechroma,
            .default_value = 0.f,
            .max_value = 1.f,
            .tooltip = "Controls highlight desaturation due to overexposure.",
         },
         {
            .kind = Kind::Float,
            .label = "Flare",
            .member = &CB::LumaGameSettings::Flare,
            .default_value = 0.f,
            .max_value = 1.f,
            .tooltip = "Flare/Glare Compensation",
         },
         {
            .kind = Kind::Float,
            .label = "LUT Strength",
            .member = &CB::LumaGameSettings::LUTStrength,
            .max_value = 1.f,
         },
         {
            .kind = Kind::Float,
            .label = "LUT Scaling",
            .member = &CB::LumaGameSettings::LUTScaling,
            .max_value = 1.f,
            .tooltip = "Scales the color grade LUT to full range when size is clamped.",
         },
         {
            .kind = Kind::Integer,
            .label = "Grain Type",
            .member = &CB::LumaGameSettings::GrainType,
            .labels = {"Vanilla", "Perceptual"},
         },
         {
            .kind = Kind::Float,
            .label = "Grain Strength",
            .member = &CB::LumaGameSettings::GrainStrength,
            .max_value = 1.f,
         },
      };

      namespace SuperResolution
      {
         constexpr float fsr_sharpness = 0.f;
         constexpr float mv_scale = 1.f;
         constexpr float jitter_scale = 1.f;

         void Initialize()
         {
         }

         void Load(reshade::api::effect_runtime* runtime)
         {
            (void)runtime;
         }

         void Draw(DeviceData& device_data, reshade::api::effect_runtime* runtime)
         {
            (void)runtime;

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
      } // namespace SuperResolution

      int IntegerSliderMin(const Descriptor& setting)
      {
         return setting.labels.empty()
                   ? static_cast<int>(setting.min_value)
                   : 0;
      }

      int IntegerSliderMax(const Descriptor& setting)
      {
         return setting.labels.empty()
                   ? static_cast<int>(setting.max_value)
                   : static_cast<int>(setting.labels.size() - 1);
      }

      void SaveSettingValue(reshade::api::effect_runtime* runtime, const Descriptor& setting, float value)
      {
         reshade::set_config_value(runtime, NAME, setting.label, value);
      }

      void Initialize()
      {
         for (const Descriptor& setting : k_descriptors)
         {
            default_luma_global_game_settings.*(setting.member) = setting.default_value;
            cb_luma_global_settings.GameSettings.*(setting.member) = setting.default_value;
         }

         SuperResolution::Initialize();
      }

      void Load(reshade::api::effect_runtime* runtime)
      {
         for (const Descriptor& setting : k_descriptors)
         {
            float& value = cb_luma_global_settings.GameSettings.*(setting.member);
            reshade::get_config_value(runtime, NAME, setting.label, value);
         }

         SuperResolution::Load(runtime);
      }

      void DrawIntegerSetting(const Descriptor& setting, float& value, reshade::api::effect_runtime* runtime)
      {
         const int min_value_i = IntegerSliderMin(setting);
         const int max_value_i = IntegerSliderMax(setting);
         int slider_value = std::clamp(static_cast<int>(std::lround(value)), min_value_i, max_value_i);

         const char* slider_format = "%d";
         if (!setting.labels.empty())
         {
            slider_format = setting.labels[static_cast<size_t>(slider_value - min_value_i)].c_str();
         }

         if (ImGui::SliderInt(setting.label, &slider_value, min_value_i, max_value_i, slider_format))
         {
            value = static_cast<float>(slider_value);
            SaveSettingValue(runtime, setting, value);
         }
      }

      void DrawFloatSetting(const Descriptor& setting, float& value, reshade::api::effect_runtime* runtime)
      {
         if (ImGui::SliderFloat(setting.label, &value, setting.min_value, setting.max_value, setting.format))
         {
            SaveSettingValue(runtime, setting, value);
         }
      }

      void DrawOne(const Descriptor& setting, reshade::api::effect_runtime* runtime)
      {
         float& value = cb_luma_global_settings.GameSettings.*(setting.member);
         const float default_value = default_luma_global_game_settings.*(setting.member);
         const bool is_enabled = setting.is_enabled == nullptr || setting.is_enabled(cb_luma_global_settings.GameSettings);

         if (!is_enabled)
         {
            ImGui::BeginDisabled();
         }

         if (setting.kind == Kind::Integer)
         {
            DrawIntegerSetting(setting, value, runtime);
         }
         else
         {
            DrawFloatSetting(setting, value, runtime);
         }

         if (setting.tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("%s", setting.tooltip);
         }

         DrawResetButton(value, default_value, setting.label, runtime);

         if (!is_enabled)
         {
            ImGui::EndDisabled();
         }
      }

      void DrawAll(reshade::api::effect_runtime* runtime)
      {
         for (const Descriptor& setting : k_descriptors)
         {
            DrawOne(setting, runtime);
         }
      }

      void SetRenderData(uint32_t render_width, uint32_t render_height, uint32_t output_width, uint32_t output_height, float jitter_x, float jitter_y, DeviceData& device_data)
      {
         // Shaders need both SR input and final output sizes so the temporal resolve can sample the correct buffer.
         const float render_width_f = static_cast<float>(render_width);
         const float render_height_f = static_cast<float>(render_height);
         const float output_width_f = static_cast<float>(output_width);
         const float output_height_f = static_cast<float>(output_height);

         cb_luma_global_settings.GameSettings.RenderRes = float2{render_width_f, render_height_f};
         cb_luma_global_settings.GameSettings.InvRenderRes = float2{render_width_f > 0.f ? (1.f / render_width_f) : 0.f, render_height_f > 0.f ? (1.f / render_height_f) : 0.f};
         cb_luma_global_settings.GameSettings.OutputRes = float2{output_width_f, output_height_f};
         cb_luma_global_settings.GameSettings.InvOutputRes = float2{output_width_f > 0.f ? (1.f / output_width_f) : 0.f, output_height_f > 0.f ? (1.f / output_height_f) : 0.f};

         const float render_scale = output_height_f > 0.f ? (render_height_f / output_height_f) : 1.f;
         cb_luma_global_settings.GameSettings.RenderScale = render_scale;
         cb_luma_global_settings.GameSettings.InvRenderScale = render_scale != 0.f ? (1.f / render_scale) : 1.f;
         cb_luma_global_settings.GameSettings.JitterOffset = float2{jitter_x, jitter_y};

         device_data.cb_luma_global_settings_dirty = true;
      }
   } // namespace Settings

   namespace RuntimeConfig
   {
      void ConfigureSwapchainAndFormatUpgrades()
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
         texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;

         texture_upgrade_formats = {
            reshade::api::format::r11g11b10_float,
         };
         texture_format_upgrades_2d_size_filters =
            0 | static_cast<uint32_t>(TextureFormatUpgrades2DSizeFilters::SwapchainResolution) | static_cast<uint32_t>(TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio);
      }
   } // namespace RuntimeConfig
} // namespace

struct GameDeviceDataQuantumBreak final : public GameDeviceData
{
#if ENABLE_SR
   // Resources captured or created around the temporal resolve pass.
   com_ptr<ID3D11Resource> sr_motion_vectors;
   com_ptr<ID3D11Buffer> cb_update_1_readback;
   com_ptr<ID3D11Buffer> ssaa_readback;
   // Conversion scratch textures: game gamma color -> DLSS linear input -> game gamma resolve input.
   com_ptr<ID3D11Texture2D> sr_linear_input_color;
   com_ptr<ID3D11ShaderResourceView> sr_linear_input_color_srv;
   com_ptr<ID3D11RenderTargetView> sr_linear_input_color_rtv;
   com_ptr<ID3D11Texture2D> sr_gamma_output_color;
   com_ptr<ID3D11ShaderResourceView> sr_gamma_output_color_srv;
   com_ptr<ID3D11RenderTargetView> sr_gamma_output_color_rtv;
   com_ptr<ID3D11ShaderResourceView> sr_output_color_srv;

   // Last captured frame constants that feed SR.
   float sr_jitter_x = 0.f;
   float sr_jitter_y = 0.f;
   float sr_cb_jitter_x = 0.f;
   float sr_cb_jitter_y = 0.f;
   float sr_vertical_fov = sr_vertical_fov_fallback;
   float sr_near_plane = 0.1f;
   float sr_far_plane = 1000.f;

   // Per-resource history used to decide when DLSS history must reset.
   bool has_ssaa_data = false;
   bool output_changed = false;
   bool has_previous_source_desc = false;
   bool has_previous_depth_desc = false;
   bool has_previous_motion_vectors_desc = false;
   D3D11_TEXTURE2D_DESC previous_source_desc = {};
   D3D11_TEXTURE2D_DESC previous_depth_desc = {};
   D3D11_TEXTURE2D_DESC previous_motion_vectors_desc = {};
   uint32_t previous_render_width = 0u;
   uint32_t previous_render_height = 0u;
   uint32_t previous_output_width = 0u;
   uint32_t previous_output_height = 0u;
#endif

   // Frame markers are latched on present so pause/menu frames can be distinguished from scene frames.
   bool had_scene_temporal_resolve_last_frame = false;
   uint32_t ui_only_frame_hold_counter = 0u;
   bool debug_prev_saw_history_reprojection_pass = false;
   bool debug_prev_saw_temporal_resolve_pass = false;
   bool debug_prev_had_motion_vectors = false;
   bool saw_history_reprojection_pass = false;
   bool saw_temporal_resolve_pass = false;
};

class QuantumBreakGame final : public Game
{
   static GameDeviceDataQuantumBreak& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataQuantumBreak*>(device_data.game);
   }

   static const GameDeviceDataQuantumBreak& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataQuantumBreak*>(device_data.game);
   }

#if ENABLE_SR && (DEVELOPMENT || TEST)
   static void DrawSuperResolutionDebug(DeviceData& device_data)
   {
      // Runtime SR status only; detailed cbuffer dumps are intentionally kept out of the user-facing UI.
      auto& game_device_data = GetGameDeviceData(device_data);

      auto begin_table = [](const char* id)
      {
         constexpr ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings;
         if (!ImGui::BeginTable(id, 2, flags))
         {
            return false;
         }

         const float field_column_width = (std::max)(
            420.f,
            ImGui::CalcTextSize("Had Scene Temporal Resolve Last Frame:").x + ImGui::GetStyle().CellPadding.x * 2.f + 48.f);
         ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, field_column_width);
         ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
         return true;
      };

      auto table_row_label = [](const char* label)
      {
         ImGui::TableNextRow();
         ImGui::TableSetColumnIndex(0);
         ImGui::TextUnformatted(label);
         ImGui::TableSetColumnIndex(1);
      };

      auto table_row_bool = [&](const char* label, bool value)
      {
         table_row_label(label);
         ImGui::TextUnformatted(value ? "Yes" : "No");
      };

      auto table_row_uint = [&](const char* label, uint32_t value)
      {
         table_row_label(label);
         ImGui::Text("%u", value);
      };

      auto table_row_float = [&](const char* label, float value)
      {
         table_row_label(label);
         ImGui::Text("%.6f", value);
      };

      ImGui::NewLine();
      if (ImGui::CollapsingHeader("Super Resolution Debug"))
      {
         if (begin_table("QB_SR_Debug_Overview"))
         {
            table_row_bool("History Reprojection Pass Seen:", game_device_data.debug_prev_saw_history_reprojection_pass);
            table_row_bool("Temporal Resolve Pass Seen:", game_device_data.debug_prev_saw_temporal_resolve_pass);
            table_row_bool("Motion Vectors Captured:", game_device_data.debug_prev_had_motion_vectors);
            table_row_bool("Had Scene Temporal Resolve Last Frame:", game_device_data.had_scene_temporal_resolve_last_frame);
            table_row_uint("UI-Only Hold Frames:", game_device_data.ui_only_frame_hold_counter);
            ImGui::EndTable();
         }
      }

      if (ImGui::CollapsingHeader("Active SR Inputs"))
      {
         if (begin_table("QB_SR_Debug_Active"))
         {
            table_row_uint("Last SR Render Width:", game_device_data.previous_render_width);
            table_row_uint("Last SR Render Height:", game_device_data.previous_render_height);
            table_row_uint("Last SR Output Width:", game_device_data.previous_output_width);
            table_row_uint("Last SR Output Height:", game_device_data.previous_output_height);
            table_row_float("Active MV Scale Multiplier:", Settings::SuperResolution::mv_scale);
            table_row_float("Active Jitter Scale Multiplier:", Settings::SuperResolution::jitter_scale);
            ImGui::EndTable();
         }
      }
   }
#endif

#if ENABLE_SR
   static bool MapPixelShaderConstantBufferForReadback(
      ID3D11Device* native_device,
      ID3D11DeviceContext* native_device_context,
      UINT slot,
      uint32_t min_size,
      com_ptr<ID3D11Buffer>& readback_buffer,
      D3D11_MAPPED_SUBRESOURCE& mapped)
   {
      // Constant buffers are GPU-only, so copy them into a staging buffer before CPU-side parsing.
      com_ptr<ID3D11Buffer> constant_buffer;
      native_device_context->PSGetConstantBuffers(slot, 1, &constant_buffer);
      if (!constant_buffer.get())
      {
         return false;
      }

      D3D11_BUFFER_DESC source_desc = {};
      constant_buffer->GetDesc(&source_desc);
      if (source_desc.ByteWidth < min_size)
      {
         return false;
      }

      bool needs_recreate = !readback_buffer.get();
      if (!needs_recreate)
      {
         D3D11_BUFFER_DESC readback_desc = {};
         readback_buffer->GetDesc(&readback_desc);
         needs_recreate = readback_desc.ByteWidth != source_desc.ByteWidth;
      }

      if (needs_recreate)
      {
         D3D11_BUFFER_DESC readback_desc = source_desc;
         readback_desc.BindFlags = 0;
         readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         readback_desc.Usage = D3D11_USAGE_STAGING;
         readback_desc.MiscFlags = 0;
         readback_desc.StructureByteStride = 0;

         readback_buffer = nullptr;
         HRESULT hr = native_device->CreateBuffer(&readback_desc, nullptr, &readback_buffer);
         if (FAILED(hr) || !readback_buffer.get())
         {
            return false;
         }
      }

      native_device_context->CopyResource(readback_buffer.get(), constant_buffer.get());

      HRESULT hr = native_device_context->Map(readback_buffer.get(), 0, D3D11_MAP_READ, 0, &mapped);
      return SUCCEEDED(hr) && mapped.pData != nullptr;
   }

   static bool CaptureCBUpdate1Data(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, GameDeviceDataQuantumBreak& game_device_data)
   {
      // cb_update_1 supplies fallback jitter, projection scale, and near plane for SR.
      game_device_data.sr_cb_jitter_x = 0.f;
      game_device_data.sr_cb_jitter_y = 0.f;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (!MapPixelShaderConstantBufferForReadback(native_device, native_device_context, 0, cb_update_1_min_size, game_device_data.cb_update_1_readback, mapped))
      {
         return false;
      }

      const auto* base = static_cast<const uint8_t*>(mapped.pData);
      ReadCBufferFloat2(base, cb_update_1_jitter_offset, game_device_data.sr_cb_jitter_x, game_device_data.sr_cb_jitter_y);
      const float inv_near = ReadCBufferValue<float>(base, cb_update_1_inv_near_offset);
      const float projection_scale_x = ReadCBufferValue<float>(base, cb_update_1_view_to_clip_offset);
      const float projection_scale_y = ReadCBufferValue<float>(base, cb_update_1_view_to_clip_offset + sizeof(float) * 5u);
      const float tess_view_to_clip_11 = ReadCBufferValue<float>(base, cb_update_1_tess_view_to_clip_11_offset);
      if (std::isfinite(inv_near) && inv_near > 0.f)
      {
         game_device_data.sr_near_plane = 1.f / inv_near;
      }

      game_device_data.sr_cb_jitter_x = std::isfinite(game_device_data.sr_cb_jitter_x) ? game_device_data.sr_cb_jitter_x : 0.f;
      game_device_data.sr_cb_jitter_y = std::isfinite(game_device_data.sr_cb_jitter_y) ? game_device_data.sr_cb_jitter_y : 0.f;

      float vertical_fov = ComputeVerticalFovFromProjectionScale(projection_scale_y);
      if (vertical_fov <= 0.f)
      {
         vertical_fov = ComputeVerticalFovFromProjectionScale(projection_scale_x);
      }
      if (vertical_fov <= 0.f)
      {
         vertical_fov = ComputeVerticalFovFromProjectionScale(tess_view_to_clip_11);
      }
      if (vertical_fov > 0.f)
      {
         game_device_data.sr_vertical_fov = vertical_fov;
      }

      native_device_context->Unmap(game_device_data.cb_update_1_readback.get(), 0);
      return true;
   }

   static bool CaptureSSAAData(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, GameDeviceDataQuantumBreak& game_device_data)
   {
      // The temporal resolve samples current color with g_vSSAAJitterOffset[0], so keep this as the jitter source.
      game_device_data.has_ssaa_data = false;
      game_device_data.sr_jitter_x = 0.f;
      game_device_data.sr_jitter_y = 0.f;

      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (!MapPixelShaderConstantBufferForReadback(native_device, native_device_context, 1, ssaa_min_size, game_device_data.ssaa_readback, mapped))
      {
         return false;
      }

      const auto* base = static_cast<const uint8_t*>(mapped.pData);
      ReadCBufferFloat2(base, ssaa_jitter_offset, game_device_data.sr_jitter_x, game_device_data.sr_jitter_y);
      game_device_data.sr_jitter_x = std::isfinite(game_device_data.sr_jitter_x) ? game_device_data.sr_jitter_x : 0.f;
      game_device_data.sr_jitter_y = std::isfinite(game_device_data.sr_jitter_y) ? game_device_data.sr_jitter_y : 0.f;

      game_device_data.has_ssaa_data = true;

      native_device_context->Unmap(game_device_data.ssaa_readback.get(), 0);
      return true;
   }

   static bool SetupSROutput(ID3D11Device* native_device, DeviceData& device_data, GameDeviceDataQuantumBreak& game_device_data, const D3D11_TEXTURE2D_DESC& output_desc)
   {
      // DLSS writes linear color here; a later fullscreen pass encodes it back to QB's gamma-space resolve input.
      game_device_data.output_changed = false;
      bool recreated_output_texture = false;

      auto* sr_instance_data = device_data.GetSRInstanceData();
      if (!sr_instance_data)
      {
         return false;
      }
      if (output_desc.Width < sr_instance_data->min_resolution || output_desc.Height < sr_instance_data->min_resolution)
      {
         return false;
      }

      D3D11_TEXTURE2D_DESC sr_output_desc = output_desc;
      sr_output_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

      if (device_data.sr_output_color.get())
      {
         D3D11_TEXTURE2D_DESC prev_desc = {};
         device_data.sr_output_color->GetDesc(&prev_desc);
         game_device_data.output_changed = prev_desc.Width != sr_output_desc.Width || prev_desc.Height != sr_output_desc.Height || prev_desc.Format != sr_output_desc.Format;
      }

      if (!device_data.sr_output_color.get() || game_device_data.output_changed)
      {
         device_data.sr_output_color = nullptr;
         HRESULT hr = native_device->CreateTexture2D(&sr_output_desc, nullptr, &device_data.sr_output_color);
         if (FAILED(hr) || !device_data.sr_output_color.get())
         {
            return false;
         }

         recreated_output_texture = true;
      }

      if (!game_device_data.sr_output_color_srv.get() || game_device_data.output_changed || recreated_output_texture)
      {
         game_device_data.sr_output_color_srv = nullptr;
         HRESULT hr = native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &game_device_data.sr_output_color_srv);
         if (FAILED(hr) || !game_device_data.sr_output_color_srv.get())
         {
            return false;
         }
      }

      return true;
   }

   static DXGI_FORMAT ResolveSRColorViewFormat(DXGI_FORMAT format)
   {
      // Conversion passes need RTV/SRV-compatible typed color formats, not typeless or sRGB view formats.
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

   static bool SetupSRConversionTexture(ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, com_ptr<ID3D11Texture2D>& texture, com_ptr<ID3D11ShaderResourceView>& srv, com_ptr<ID3D11RenderTargetView>& rtv)
   {
      // Scratch textures are simple single-mip render targets used only by the pre/post SR conversion passes.
      desc.Format = ResolveSRColorViewFormat(desc.Format);
      if (desc.Width == 0u || desc.Height == 0u || desc.Format == DXGI_FORMAT_UNKNOWN)
      {
         return false;
      }

      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      desc.CPUAccessFlags = 0;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.MiscFlags = 0;
      desc.MipLevels = 1u;
      desc.ArraySize = 1u;
      desc.SampleDesc.Count = 1u;
      desc.SampleDesc.Quality = 0u;

      bool recreate_texture = !texture.get();
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

         HRESULT hr = native_device->CreateTexture2D(&desc, nullptr, &texture);
         if (FAILED(hr) || !texture.get())
         {
            return false;
         }
      }

      if (!srv.get())
      {
         HRESULT hr = native_device->CreateShaderResourceView(texture.get(), nullptr, &srv);
         if (FAILED(hr) || !srv.get())
         {
            return false;
         }
      }

      if (!rtv.get())
      {
         HRESULT hr = native_device->CreateRenderTargetView(texture.get(), nullptr, &rtv);
         if (FAILED(hr) || !rtv.get())
         {
            return false;
         }
      }

      return true;
   }

   static bool DrawSRConversionPass(ID3D11DeviceContext* native_device_context, DeviceData& device_data, uint32_t pixel_shader_hash, ID3D11ShaderResourceView* source_srv, ID3D11RenderTargetView* target_rtv, uint32_t width, uint32_t height)
   {
      // Shared fullscreen draw for gamma->linear and linear->gamma SR color conversion.
      const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
      const auto ps_it = device_data.native_pixel_shaders.find(pixel_shader_hash);
      if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second.get() ||
          ps_it == device_data.native_pixel_shaders.end() || !ps_it->second.get() ||
          !source_srv || !target_rtv || width == 0u || height == 0u)
      {
         return false;
      }

      native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
      DrawCustomPixelShader(
         native_device_context,
         device_data.default_depth_stencil_state.get(),
         device_data.default_blend_state.get(),
         nullptr,
         vs_it->second.get(),
         ps_it->second.get(),
         source_srv,
         target_rtv,
         width,
         height,
         false);

      ID3D11ShaderResourceView* null_srv = nullptr;
      native_device_context->PSSetShaderResources(0, 1, &null_srv);
      ID3D11RenderTargetView* null_rtv = nullptr;
      native_device_context->OMSetRenderTargets(1, &null_rtv, nullptr);
      return true;
   }
#endif

public:
   void OnInit(bool async) override
   {
      (void)async;

      // QB custom shaders reserve these slots for Luma settings/data during the temporal resolve replacement.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

#if ENABLE_SR
      sr_game_tooltip = "Super Resolution engages during the temporal resolve pass.\n";
      // Native fullscreen passes bridge QB's gamma-space post stack with DLSS' preferred linear input.
      native_shaders_definitions.emplace(CompileTimeStringHash("QB Pre SR Decode"), ShaderDefinition{"Luma_QB_PreSRDecode", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("QB Post SR Encode"), ShaderDefinition{"Luma_QB_PostSREncode", reshade::api::pipeline_subobject_type::pixel_shader});
#endif

      Settings::Initialize();
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      Settings::Load(runtime);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      (void)native_device;
      device_data.game = new GameDeviceDataQuantumBreak;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (original_shader_hashes.Contains(shader_hashes_history_reprojection))
      {
         // The history reprojection pass has the motion-vector resource bound as CS SRV 0.
         game_device_data.saw_history_reprojection_pass = true;
         device_data.taa_detected = true;

#if ENABLE_SR
         com_ptr<ID3D11ShaderResourceView> motion_vectors_srv;
         native_device_context->CSGetShaderResources(0, 1, &motion_vectors_srv);
         if (motion_vectors_srv.get())
         {
            game_device_data.sr_motion_vectors = nullptr;
            motion_vectors_srv->GetResource(&game_device_data.sr_motion_vectors);
         }
#endif

         return DrawOrDispatchOverrideType::None;
      }

      if (!original_shader_hashes.Contains(shader_hashes_temporal_resolve))
      {
         return DrawOrDispatchOverrideType::None;
      }

      // Temporal resolve is the SR insertion point: it sees color, depth, post-stack constants, and final output.
      game_device_data.saw_temporal_resolve_pass = true;
      device_data.has_drawn_main_post_processing = true;

#if ENABLE_SR
      const bool sr_requested = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#else
      const bool sr_requested = false;
#endif
      bool sr_succeeded = false;

#if ENABLE_SR
      com_ptr<ID3D11ShaderResourceView> ps_shader_resources[3];
      com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
      com_ptr<ID3D11DepthStencilView> depth_stencil_view;
      const bool immediate_context = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
      const bool has_main_temporal_resolve_bindings = [&]()
      {
         if (!immediate_context)
         {
            return false;
         }

         // UI/menu-only resolves can hit the same shader without the scene color/depth/RTV bindings SR needs.
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         return ps_shader_resources[0].get() && ps_shader_resources[2].get() && render_target_views[0].get();
      }();

      if (has_main_temporal_resolve_bindings)
      {
         CaptureCBUpdate1Data(native_device, native_device_context, game_device_data);
         CaptureSSAAData(native_device, native_device_context, game_device_data);
      }

      if (sr_requested && immediate_context && game_device_data.sr_motion_vectors.get() && has_main_temporal_resolve_bindings)
      {
         if (ps_shader_resources[0].get() && ps_shader_resources[2].get() && render_target_views[0].get())
         {
            com_ptr<ID3D11Resource> source_color_resource;
            ps_shader_resources[2]->GetResource(&source_color_resource);

            com_ptr<ID3D11Resource> depth_resource;
            ps_shader_resources[0]->GetResource(&depth_resource);

            // The temporal resolve RTV is the final SR output target size.
            com_ptr<ID3D11Resource> output_resource;
            render_target_views[0]->GetResource(&output_resource);

            com_ptr<ID3D11Texture2D> source_color_texture;
            com_ptr<ID3D11Texture2D> output_texture;
            com_ptr<ID3D11Texture2D> depth_texture;
            com_ptr<ID3D11Texture2D> motion_vectors_texture;

            const HRESULT source_hr = source_color_resource.get() ? source_color_resource->QueryInterface(&source_color_texture) : E_FAIL;
            const HRESULT output_hr = output_resource.get() ? output_resource->QueryInterface(&output_texture) : E_FAIL;
            const HRESULT depth_hr = depth_resource.get() ? depth_resource->QueryInterface(&depth_texture) : E_FAIL;
            const HRESULT motion_vectors_hr = game_device_data.sr_motion_vectors.get() ? game_device_data.sr_motion_vectors->QueryInterface(&motion_vectors_texture) : E_FAIL;

            if (SUCCEEDED(source_hr) && SUCCEEDED(output_hr) && SUCCEEDED(depth_hr) && SUCCEEDED(motion_vectors_hr) && source_color_texture.get() && output_texture.get() && depth_texture.get() && motion_vectors_texture.get())
            {
               // Descs drive DLSS settings, conversion texture allocation, and history reset decisions.
               D3D11_TEXTURE2D_DESC source_desc = {};
               D3D11_TEXTURE2D_DESC depth_desc = {};
               D3D11_TEXTURE2D_DESC motion_vectors_desc = {};
               D3D11_TEXTURE2D_DESC output_desc = {};
               // Source/depth/MV descs bound the DLSS input resolution; output desc drives the upscaled target.
               source_color_texture->GetDesc(&source_desc);
               depth_texture->GetDesc(&depth_desc);
               motion_vectors_texture->GetDesc(&motion_vectors_desc);
               output_texture->GetDesc(&output_desc);

               if (SetupSROutput(native_device, device_data, game_device_data, output_desc))
               {
                  auto* sr_instance_data = device_data.GetSRInstanceData();
                  if (sr_instance_data)
                  {
                     // Use the smallest SR input texture so color, depth, and motion vectors cover the full render area.
                     const uint32_t max_input_width = (std::min)(source_desc.Width, (std::min)(depth_desc.Width, motion_vectors_desc.Width));
                     const uint32_t max_input_height = (std::min)(source_desc.Height, (std::min)(depth_desc.Height, motion_vectors_desc.Height));
                     const uint32_t render_width = max_input_width;
                     const uint32_t render_height = max_input_height;

                     const uint32_t output_width = output_desc.Width;
                     const uint32_t output_height = output_desc.Height;
                     const float jitter_x = game_device_data.has_ssaa_data ? game_device_data.sr_jitter_x : game_device_data.sr_cb_jitter_x;
                     const float jitter_y = game_device_data.has_ssaa_data ? game_device_data.sr_jitter_y : game_device_data.sr_cb_jitter_y;

                     if (render_width == 0u || render_height == 0u || output_width == 0u || output_height == 0u)
                     {
                        device_data.force_reset_sr = true;
                        return DrawOrDispatchOverrideType::None;
                     }

                     Settings::SetRenderData(render_width, render_height, output_width, output_height, jitter_x, jitter_y, device_data);

                     SR::SettingsData settings_data = {};
                     settings_data.output_width = output_width;
                     settings_data.output_height = output_height;
                     settings_data.render_width = render_width;
                     settings_data.render_height = render_height;
                     settings_data.dynamic_resolution = false;
                     // The pre-SR decode pass makes the color input linear; exposure is already baked by QB.
                     settings_data.hdr = true;
                     settings_data.auto_exposure = false;
                     settings_data.inverted_depth = false;
                     settings_data.mvs_jittered = false;
                     settings_data.mvs_x_scale = static_cast<float>(render_width) * Settings::SuperResolution::mv_scale;
                     settings_data.mvs_y_scale = static_cast<float>(render_height) * Settings::SuperResolution::mv_scale;
                     settings_data.render_preset = dlss_render_preset;

                     D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc = {};
                     ps_shader_resources[2]->GetDesc(&source_srv_desc);

                     D3D11_TEXTURE2D_DESC sr_linear_input_desc = source_desc;
                     sr_linear_input_desc.Format = source_srv_desc.Format != DXGI_FORMAT_UNKNOWN ? source_srv_desc.Format : source_desc.Format;

                     D3D11_TEXTURE2D_DESC sr_gamma_output_desc = output_desc;
                     const bool conversion_resources_ready =
                        SetupSRConversionTexture(native_device, sr_linear_input_desc, game_device_data.sr_linear_input_color, game_device_data.sr_linear_input_color_srv, game_device_data.sr_linear_input_color_rtv) &&
                        SetupSRConversionTexture(native_device, sr_gamma_output_desc, game_device_data.sr_gamma_output_color, game_device_data.sr_gamma_output_color_srv, game_device_data.sr_gamma_output_color_rtv);

                     const bool settings_updated = conversion_resources_ready && sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
                     if (settings_updated)
                     {
                        // Reset DLSS history when any physical resource or logical render size changes.
                        const bool source_changed = UpdatePreviousTextureDesc(source_desc, game_device_data.previous_source_desc, game_device_data.has_previous_source_desc);
                        const bool depth_changed = UpdatePreviousTextureDesc(depth_desc, game_device_data.previous_depth_desc, game_device_data.has_previous_depth_desc);
                        const bool motion_vectors_changed = UpdatePreviousTextureDesc(motion_vectors_desc, game_device_data.previous_motion_vectors_desc, game_device_data.has_previous_motion_vectors_desc);
                        const bool render_size_changed = game_device_data.previous_render_width != 0u && game_device_data.previous_render_height != 0u && (game_device_data.previous_render_width != render_width || game_device_data.previous_render_height != render_height);
                        game_device_data.previous_render_width = render_width;
                        game_device_data.previous_render_height = render_height;
                        game_device_data.previous_output_width = output_width;
                        game_device_data.previous_output_height = output_height;

                        const bool reset_sr = device_data.force_reset_sr || game_device_data.output_changed || source_changed || depth_changed || motion_vectors_changed || render_size_changed;
                        device_data.force_reset_sr = false;

                        SR::SuperResolutionImpl::DrawData draw_data = {};
                        draw_data.source_color = game_device_data.sr_linear_input_color.get();
                        draw_data.output_color = device_data.sr_output_color.get();
                        draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
                        draw_data.depth_buffer = depth_resource.get();
                        draw_data.pre_exposure = 1.f;
                        draw_data.jitter_x = jitter_x * Settings::SuperResolution::jitter_scale;
                        draw_data.jitter_y = jitter_y * Settings::SuperResolution::jitter_scale;
                        draw_data.vert_fov = (std::isfinite(game_device_data.sr_vertical_fov) && game_device_data.sr_vertical_fov > 0.f)
                                                ? game_device_data.sr_vertical_fov
                                                : sr_vertical_fov_fallback;
                        draw_data.near_plane = game_device_data.sr_near_plane;
                        draw_data.far_plane = game_device_data.sr_far_plane;
                        draw_data.reset = reset_sr;
                        draw_data.render_width = render_width;
                        draw_data.render_height = render_height;
                        draw_data.user_sharpness = device_data.sr_type == SR::Type::FSR ? Settings::SuperResolution::fsr_sharpness : -1.f;

                        DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
                        DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
                        draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
                        compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

                        // Feed DLSS linear color, then encode its linear result back to QB's gamma-space resolve input.
                        const bool pre_sr_encoded = DrawSRConversionPass(
                           native_device_context,
                           device_data,
                           CompileTimeStringHash("QB Pre SR Decode"),
                           ps_shader_resources[2].get(),
                           game_device_data.sr_linear_input_color_rtv.get(),
                           source_desc.Width,
                           source_desc.Height);

                        sr_succeeded = pre_sr_encoded && sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                        {
                           ID3D11ShaderResourceView* null_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
                           native_device_context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
                           native_device_context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, null_srvs);
                           ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
                           native_device_context->CSSetUnorderedAccessViews(0, D3D11_1_UAV_SLOT_COUNT, null_uavs, nullptr);
                           ID3D11RenderTargetView* null_rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
                           native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, null_rtvs, nullptr);
                        }

                        if (sr_succeeded)
                        {
                           sr_succeeded = DrawSRConversionPass(
                              native_device_context,
                              device_data,
                              CompileTimeStringHash("QB Post SR Encode"),
                              game_device_data.sr_output_color_srv.get(),
                              game_device_data.sr_gamma_output_color_rtv.get(),
                              output_width,
                              output_height);
                        }

                        draw_state_stack.Restore(native_device_context);
                        compute_state_stack.Restore(native_device_context);

                        if (sr_succeeded)
                        {
                           device_data.has_drawn_sr = true;
                        }
                        else
                        {
                           device_data.force_reset_sr = true;
                        }
                     }
                  }
               }
            }
         }
      }
#endif

#if ENABLE_SR
      const uint32_t sr_type_for_pass = sr_succeeded ? (static_cast<uint32_t>(device_data.sr_type) + 1u) : 0u;
#else
      const uint32_t sr_type_for_pass = 0u;
#endif
      if (cb_luma_global_settings.SRType != sr_type_for_pass)
      {
         cb_luma_global_settings.SRType = sr_type_for_pass;
         device_data.cb_luma_global_settings_dirty = true;
      }

#if ENABLE_SR
      if (!sr_succeeded && sr_requested)
      {
         device_data.force_reset_sr = true;
      }
#endif

      if (original_draw_dispatch_func && *original_draw_dispatch_func)
      {
         // Post-draw callback path lets us bind the SR result and then execute QB's original resolve draw once.
         com_ptr<ID3D11ShaderResourceView> original_ps_srv_2;
         native_device_context->PSGetShaderResources(2, 1, &original_ps_srv_2);

         com_ptr<ID3D11Buffer> original_luma_settings_cb;
         com_ptr<ID3D11Buffer> original_luma_data_cb;
         if (is_custom_pass)
         {
            native_device_context->PSGetConstantBuffers(luma_settings_cbuffer_index, 1, &original_luma_settings_cb);
            native_device_context->PSGetConstantBuffers(luma_data_cbuffer_index, 1, &original_luma_data_cb);

            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
            updated_cbuffers = true;
         }

#if ENABLE_SR
         if (sr_succeeded)
         {
            ID3D11ShaderResourceView* sr_output_srv = game_device_data.sr_gamma_output_color_srv.get();
            native_device_context->PSSetShaderResources(2, 1, &sr_output_srv);
         }
#endif

         (*original_draw_dispatch_func)();

         ID3D11ShaderResourceView* original_ps_srv_2_ptr = original_ps_srv_2.get();
         native_device_context->PSSetShaderResources(2, 1, &original_ps_srv_2_ptr);

         if (is_custom_pass)
         {
            ID3D11Buffer* original_luma_settings_cb_ptr = original_luma_settings_cb.get();
            ID3D11Buffer* original_luma_data_cb_ptr = original_luma_data_cb.get();
            native_device_context->PSSetConstantBuffers(luma_settings_cbuffer_index, 1, &original_luma_settings_cb_ptr);
            native_device_context->PSSetConstantBuffers(luma_data_cbuffer_index, 1, &original_luma_data_cb_ptr);
         }

         return DrawOrDispatchOverrideType::Replaced;
      }

      if (is_custom_pass)
      {
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
         updated_cbuffers = true;
      }

#if ENABLE_SR
      if (sr_succeeded)
      {
         ID3D11ShaderResourceView* sr_output_srv = game_device_data.sr_gamma_output_color_srv.get();
         native_device_context->PSSetShaderResources(2, 1, &sr_output_srv);
      }
#endif

      return DrawOrDispatchOverrideType::None;
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
#if ENABLE_SR
      // Drop all transient SR resources so the next valid scene frame rebuilds them and resets DLSS history.
      auto& game_device_data = GetGameDeviceData(device_data);

      device_data.force_reset_sr = true;
      device_data.has_drawn_sr = false;

      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.sr_linear_input_color = nullptr;
      game_device_data.sr_linear_input_color_srv = nullptr;
      game_device_data.sr_linear_input_color_rtv = nullptr;
      game_device_data.sr_gamma_output_color = nullptr;
      game_device_data.sr_gamma_output_color_srv = nullptr;
      game_device_data.sr_gamma_output_color_rtv = nullptr;
      game_device_data.sr_output_color_srv = nullptr;
      game_device_data.output_changed = false;

      game_device_data.has_previous_source_desc = false;
      game_device_data.has_previous_depth_desc = false;
      game_device_data.has_previous_motion_vectors_desc = false;
      game_device_data.previous_source_desc = {};
      game_device_data.previous_depth_desc = {};
      game_device_data.previous_motion_vectors_desc = {};
      game_device_data.previous_render_width = 0u;
      game_device_data.previous_render_height = 0u;
      game_device_data.previous_output_width = 0u;
      game_device_data.previous_output_height = 0u;
#else
      (void)device_data;
#endif
   }

   bool IsGamePaused(const DeviceData& device_data) const override
   {
      // If scene temporal resolve stops briefly after being active, treat it as pause/menu rather than gameplay.
      const auto& game_device_data = GetGameDeviceData(device_data);
      return !game_device_data.saw_temporal_resolve_pass && (game_device_data.had_scene_temporal_resolve_last_frame || game_device_data.ui_only_frame_hold_counter > 0u);
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      (void)native_device;
      auto& game_device_data = GetGameDeviceData(device_data);

#if ENABLE_SR
      // If SR was requested but no scene resolve produced SR output, force a history reset for the next scene frame.
      if (device_data.sr_type != SR::Type::None && !device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }
#endif

      game_device_data.debug_prev_saw_history_reprojection_pass = game_device_data.saw_history_reprojection_pass;
      game_device_data.debug_prev_saw_temporal_resolve_pass = game_device_data.saw_temporal_resolve_pass;
#if ENABLE_SR
      game_device_data.debug_prev_had_motion_vectors = game_device_data.sr_motion_vectors.get() != nullptr;
#else
      game_device_data.debug_prev_had_motion_vectors = false;
#endif

      device_data.taa_detected = game_device_data.saw_history_reprojection_pass;
      device_data.has_drawn_sr = false;
      device_data.has_drawn_main_post_processing = false;

      const uint32_t back_buffer_count = (std::max)(2u, static_cast<uint32_t>(device_data.back_buffers.size()));
      // Hold the paused state across the swapchain queue so UI-only frames do not look like gameplay frames.
      if (game_device_data.saw_temporal_resolve_pass)
      {
         game_device_data.had_scene_temporal_resolve_last_frame = true;
         game_device_data.ui_only_frame_hold_counter = 0u;
      }
      else
      {
         if (game_device_data.had_scene_temporal_resolve_last_frame)
         {
            game_device_data.ui_only_frame_hold_counter = back_buffer_count > 0u ? (back_buffer_count - 1u) : 0u;
         }
         else if (game_device_data.ui_only_frame_hold_counter > 0u)
         {
            --game_device_data.ui_only_frame_hold_counter;
         }

         game_device_data.had_scene_temporal_resolve_last_frame = false;
      }

      if (cb_luma_global_settings.SRType != 0u)
      {
         cb_luma_global_settings.SRType = 0u;
         device_data.cb_luma_global_settings_dirty = true;
      }

      game_device_data.saw_history_reprojection_pass = false;
      game_device_data.saw_temporal_resolve_pass = false;
#if ENABLE_SR
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.output_changed = false;
#endif
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      // Keep user-facing grading/effects controls above the SR tuning/debug block.
      Settings::DrawAll(runtime);
      Settings::SuperResolution::Draw(device_data, runtime);
#if ENABLE_SR && (DEVELOPMENT || TEST)
      DrawSuperResolutionDebug(device_data);
#endif
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Quantum Break\" is developed by Musa and is open source and free.\nIf you enjoy it, consider donating.\n");

      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_musa = std::string("Buy Musa a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_musa.c_str()))
      {
         system("start https://ko-fi.com/musaqh");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);
      ImGui::NewLine();

      ImGui::Text("Credits:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nNeutwo, LUT Scaling, and Film Grain (from RenoDX) - Copyright (c) 2026 Carlos Lopez Jr. Licensed under MIT."
                  "");
      static const std::string neutwo_license_link = std::string("RenoDX MIT License ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(neutwo_license_link.c_str()))
      {
         system("start https://github.com/clshortfuse/renodx/blob/main/LICENSE");
      }
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      // Register QB before CoreMain initializes hooks, shader hashes, and runtime-wide format upgrades.
      Globals::SetGlobals(PROJECT_NAME, "Quantum Break Luma mod", "https://ko-fi.com/musaqh");
      Globals::VERSION = 1;

      shader_hashes_history_reprojection.compute_shaders.emplace(std::stoul("E8337D48", nullptr, 16));
      shader_hashes_temporal_resolve.pixel_shaders.emplace(std::stoul("99274617", nullptr, 16));

      RuntimeConfig::ConfigureSwapchainAndFormatUpgrades();

      game = new QuantumBreakGame();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
