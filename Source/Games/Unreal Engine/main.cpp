#define GAME_UNREAL_ENGINE 1

#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define ENABLE_POST_DRAW_CALLBACK 1

#include "..\..\Core\core.hpp"
#include "includes\shader_detect.hpp"

namespace
{
   ShaderHashesList shader_hashes_TAA;
   ShaderHashesList shader_hashes_TAA_Candidates;
   ShaderHashesList shader_hashes_SSAO;      // Added SSAO list
   ShaderHashesList shader_hashes_Dithering; // Dithering shader list
   ShaderHashesList shader_hashes_tonemap_candidates;
   ShaderHashesList shader_hashes_tonemap_lut_candidates;
   GlobalCBInfo global_cb_info;
   std::shared_mutex taa_mutex;
   std::shared_mutex ssao_mutex;      // Added mutex for SSAO info
   std::shared_mutex dithering_mutex; // Mutex for dithering info
   std::unordered_map<uint64_t, TAAShaderInfo> taa_shader_candidate_info;
   std::unordered_map<uint64_t, SSAOShaderInfo> ssao_shader_info_map;           // Added map for SSAO info
   std::unordered_map<uint64_t, DitheringShaderInfo> dithering_shader_info_map; // Map for dithering info

   // User settings
   namespace
   {
      bool enable_dithering_fix = false; // Master switch for dithering fix
      bool sr_auto_exposure = true;

      bool first_boot = true; // Automatic setting
      bool enable_hdr = false;
      bool next_enable_hdr = enable_hdr; // The value we serialize, that will be ignored until reboot

      CB::LumaGameSettings cb_default_game_settings;
   } // namespace

   constexpr UINT tonemap_lut_size = 32;
   constexpr UINT tonemap_lut_thread_size = 8; // Note: this might be 4 too in 3D textures, but 8 also works for our custom shaders

   static inline bool NearZero(float v, float eps)
   {
      return std::fabs(v) <= eps;
   }

   // Row-major projection shape check; falls back to transpose if needed
   static inline bool MatrixLikeProjection(const Math::Matrix44F& m, float eps = 1e-3f)
   {
      // Rows 0–1 off-diagonals ~ 0
      if (!NearZero(m.m01, eps) || !NearZero(m.m02, eps) || !NearZero(m.m03, eps))
         return false;
      if (!NearZero(m.m10, eps) || !NearZero(m.m12, eps) || !NearZero(m.m13, eps))
         return false;

      // Perspective term and last row
      if (std::fabs(m.m23) < 0.95f)
         return false; // m23 ≈ ±1
      if (!NearZero(m.m30, eps) || !NearZero(m.m31, eps) || !NearZero(m.m33, eps))
         return false;

      // Depth terms (normal/reversed/infinite)
      if (NearZero(m.m22, eps) && NearZero(m.m32, eps))
         return false;

      return true;
   }

   static inline bool ProjectionHasJitter(const Math::Matrix44F& m, float2 max_jitter, float eps = 1e-3f)
   {
      if ((m.m20 == 0.0f && m.m21 == 0.0f) || std::fabs(m.m20) > max_jitter.x || std::fabs(m.m21) > max_jitter.y)
      {
         return false;
      }
      return true;
   }

   static inline bool IsViewSizeInvSize(const float4& v, float aspect_ratio, float eps = 1e-3f)
   {
      if (v.x > 32.0f && v.y > 32.0f &&
          v.z > 0.f && v.w > 0.f)
      {
         const float inv_w = 1.0f / v.x;
         const float inv_h = 1.0f / v.y;
         if (std::fabs(v.z - inv_w) < FLT_EPSILON &&
             std::fabs(v.w - inv_h) < FLT_EPSILON)
         {
            if (std::fabs((v.x / v.y) - aspect_ratio) < eps)
            {
               return true;
            }
         }
      }
      return false;
   }

   static inline float ComputeNearPlane(const Math::Matrix44F& view_to_clip)
   {
      return (view_to_clip.m33 - view_to_clip.m32) / (view_to_clip.m22 - view_to_clip.m23);
   }

   static inline float ComputeFovY(const Math::Matrix44F& view_to_clip)
   {
      float eps = 1e-3f;
      if (!NearZero(view_to_clip.m11, eps))
      {
         return atan(1.f / view_to_clip.m11) * 2.0;
      }
      return 0.0f;
   }
} // namespace

struct GameDeviceDataUnrealEngine final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   com_ptr<ID3D11Texture2D> sr_motion_vectors;
   com_ptr<ID3D11Resource> sr_source_color;
   com_ptr<ID3D11Resource> depth_buffer;
   com_ptr<ID3D11RenderTargetView> sr_motion_vectors_rtv;
   com_ptr<ID3D11UnorderedAccessView> sr_motion_vectors_uav;
   std::unique_ptr<SR::SettingsData> sr_settings_data;
   std::unique_ptr<SR::SuperResolutionImpl::DrawData> sr_draw_data;
   std::atomic<bool> found_per_view_globals = false;
   std::atomic<bool> camera_cut = false;
#endif // ENABLE_SR
   float4 render_resolution = {0.0f, 0.0f, 0.0f, 0.0f};
   float4 viewport_rect = {0.0f, 0.0f, 0.0f, 0.0f};
   float2 jitter = {0.0f, 0.0f};
   float near_plane = 0.01f;
   float far_plane = FLT_MAX;
   float fov_y = 60.0f;
   Matrix44F view_to_clip_matrix;
   Matrix44F clip_to_prev_clip_matrix;

   // HDR
   com_ptr<ID3D11Resource> tonemap_lut_texture; // 2D or 3D
   com_ptr<ID3D11ShaderResourceView> tonemap_lut_texture_srv;
   com_ptr<ID3D11UnorderedAccessView> tonemap_lut_texture_uav;
   bool tonemap_lut_texture_is_3d = true;
};

class UnrealEngine final : public Game // ### Rename this to your game's name ###
{
   static GameDeviceDataUnrealEngine& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataUnrealEngine*>(device_data.game);
   }

   static void DecodeMotionVectorsPS(ID3D11DeviceContext* native_device_context, DeviceData& device_data, TAAShaderInfo& taa_shader_info)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      com_ptr<ID3D11ShaderResourceView> depth_texture_srv;
      com_ptr<ID3D11ShaderResourceView> mv_texture_srv;
      com_ptr<ID3D11Buffer> global_cbuffer;

      if (taa_shader_info.depth_texture_register != 0)
      {
         native_device_context->PSGetShaderResources(taa_shader_info.depth_texture_register, 1, &depth_texture_srv);
         ID3D11ShaderResourceView* const depth_srv = depth_texture_srv.get();
         native_device_context->PSSetShaderResources(0, 1, &depth_srv);
      }
      if (taa_shader_info.velocity_texture_register != 1)
      {
         native_device_context->PSGetShaderResources(taa_shader_info.velocity_texture_register, 1, &mv_texture_srv);
         ID3D11ShaderResourceView* const motion_vector_srv = mv_texture_srv.get();
         native_device_context->PSSetShaderResources(1, 1, &motion_vector_srv);
      }
      if (taa_shader_info.global_buffer_register_index != 1)
      {
         native_device_context->PSGetConstantBuffers(taa_shader_info.global_buffer_register_index, 1, &global_cbuffer);
         ID3D11Buffer* const cb1 = global_cbuffer.get();
         native_device_context->PSSetConstantBuffers(1, 1, &cb1);
      }
      ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.sr_motion_vectors_rtv.get();

      native_device_context->VSSetShader(device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), nullptr, 0);
      native_device_context->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs PS")].get(), nullptr, 0);
      native_device_context->CSSetShader(nullptr, nullptr, 0);
      native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
      ID3D11SamplerState* const sampler_state_point = device_data.sampler_state_point.get();
      native_device_context->PSSetSamplers(0, 1, &sampler_state_point);
      native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, nullptr);
      native_device_context->Draw(4, 0);
   }

   static void DecodeMotionVectorsCS(ID3D11DeviceContext* context, DeviceData& device_data, TAAShaderInfo& taa_shader_info)
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      com_ptr<ID3D11Buffer> global_cbuffer;

      if (taa_shader_info.depth_texture_register != 0)
      {
         ID3D11ShaderResourceView* depth_srv;
         context->CSGetShaderResources(taa_shader_info.depth_texture_register, 1, &depth_srv);
         context->CSSetShaderResources(0, 1, &depth_srv);
      }
      if (taa_shader_info.velocity_texture_register != 1)
      {
         ID3D11ShaderResourceView* motion_vector_srv;
         context->CSGetShaderResources(taa_shader_info.velocity_texture_register, 1, &motion_vector_srv);
         context->CSSetShaderResources(1, 1, &motion_vector_srv);
      }
      if (taa_shader_info.global_buffer_register_index != 1)
      {
         context->CSGetConstantBuffers(taa_shader_info.global_buffer_register_index, 1, &global_cbuffer);
         ID3D11Buffer* const cb1 = global_cbuffer.get();
         context->CSSetConstantBuffers(1, 1, &cb1);
      }
      ID3D11UnorderedAccessView* const dlss_motion_vectors_uav_const = game_device_data.sr_motion_vectors_uav.get();

      context->VSSetShader(nullptr, nullptr, 0);
      context->PSSetShader(nullptr, nullptr, 0); // TODO: delete these? not needed
      context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Decode MVs CS")].get(), nullptr, 0);
      context->CSSetUnorderedAccessViews(0, 1, &dlss_motion_vectors_uav_const, nullptr);
      UINT width = static_cast<UINT>(game_device_data.render_resolution.x);
      UINT height = static_cast<UINT>(game_device_data.render_resolution.y);
      UINT groupsX = (width + 8 - 1) / 8;
      UINT groupsY = (height + 8 - 1) / 8;
      context->Dispatch(
         groupsX,
         groupsY,
         1);
   }

   static void DecodeMotionVectors(bool is_compute_shader, ID3D11DeviceContext* context, DeviceData& device_data, TAAShaderInfo& taa_shader_info)
   {
      if (is_compute_shader)
         DecodeMotionVectorsCS(context, device_data, taa_shader_info);
      else
         DecodeMotionVectorsPS(context, device_data, taa_shader_info);
   }

public:
   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(UnrealEngine::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(UnrealEngine::OnUnmapBufferRegion);
      }
   }
   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue(enable_hdr ? '0' : '1'); // Unreal Engine games almost always encode with sRGB, not gamma 2.2 // TODO: expose a user setting for this, or auto detect it from shader branches (though the selection happens at runtime)
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');

      if (enable_hdr)
      {
         std::vector<ShaderDefineData> game_shader_defines_data = {
            {"TONEMAP_TYPE", '2', true, false, "0 - Vanilla SDR\n1 - Pumbo AdvancedAutoHDR\n2 - Luma HDR (Vanilla+) (Suggested)\n3 - Raw/Untonemapped", 3},
         };
         shader_defines_data.append_range(game_shader_defines_data);
      }

      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs PS"), ShaderDefinition{"Luma_MotionVec_UE4_Decode", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode MVs CS"), ShaderDefinition{"Luma_MotionVec_UE4_Decode", reshade::api::pipeline_subobject_type::compute_shader});
      luma_settings_cbuffer_index = 9;
      luma_data_cbuffer_index = 8;

      native_shaders_definitions.emplace(CompileTimeStringHash("Upgrade Tonemap LUT 3D"), ShaderDefinition{"Luma_UpgradeTonemapLUT", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, {{"DIMENSIONS_3D", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("Upgrade Tonemap LUT 2D"), ShaderDefinition{"Luma_UpgradeTonemapLUT", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, {{"DIMENSIONS_2D", "1"}}});
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataUnrealEngine;
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.view_to_clip_matrix.SetIdentity();
      game_device_data.clip_to_prev_clip_matrix.SetIdentity();
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      // Start from here, we then update it later in case the game rendered with black bars due to forcing a different aspect ratio from the swapchain buffer
      game_device_data.render_resolution = {device_data.render_resolution.x, device_data.render_resolution.y, 1.0f / device_data.render_resolution.x, 1.0f / device_data.render_resolution.y};
      game_device_data.viewport_rect = {0.0f, 0.0f, device_data.render_resolution.x, device_data.render_resolution.y};
   }

   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash = -1, const std::byte* shader_object = nullptr, size_t shader_object_size = 0) override
   {
      // Only process pixel/compute shaders
      if (type != reshade::api::pipeline_subobject_type::pixel_shader && type != reshade::api::pipeline_subobject_type::compute_shader)
         return nullptr;

      // Tonemap and Tonemap LUT Detection
      // UE always (~) has this matrix in the tonemap LUT.
      // We also reliably find sRGB encoding and decoding coeffs, ACES matrices, BT.601 luminance coeffs etc.
      // TODO: not always there!?
      constexpr std::array<float, 9> aces_blue_correct_matrix = {
         0.9404372683f, -0.0183068787f, 0.0778696104f,
         0.0083786969f, 0.8286599939f, 0.1629613092f,
         0.0005471261f, -0.0008833746f, 1.0003362486f};
      // x /= 1.05
      const std::vector<uint8_t> pattern_lut_output_scaling_a = {0x3E, 0xCF, 0x73, 0x3F, 0x3E, 0xCF, 0x73, 0x3F, 0x3E, 0xCF, 0x73, 0x3F};
      const std::vector<uint8_t> pattern_lut_output_scaling_b = {0x3D, 0xCF, 0x73, 0x3F, 0x3D, 0xCF, 0x73, 0x3F, 0x3D, 0xCF, 0x73, 0x3F};
      // sRGB identifier (1.055 etc)
      const std::vector<uint8_t> pattern_srgb = {0x3D, 0x0A, 0x87, 0x3F, 0x3D, 0x0A, 0x87, 0x3F, 0x3D, 0x0A, 0x87, 0x3F};
      // "6.10352e-5" from "x = max(6.10352e-5, x)" in the sRGB encode/decode conversions
      const std::vector<uint8_t> pattern_srgb_conversion_max = {0x06, 0x00, 0x80, 0x38, 0x06, 0x00, 0x80, 0x38, 0x06, 0x00, 0x80, 0x38};
      // float3(0.299, 0.587, 0.114)
      const std::vector<std::byte> pattern_bt_601_luminance_vector_a = {
         std::byte{0x87}, std::byte{0x16}, std::byte{0x99}, std::byte{0x3E},
         std::byte{0xA2}, std::byte{0x45}, std::byte{0x16}, std::byte{0x3F},
         std::byte{0xD5}, std::byte{0x78}, std::byte{0xE9}, std::byte{0x3D}};
      // float3(0.3, 0.59, 0.11)
      const std::vector<uint8_t> pattern_bt_601_luminance_vector_b = {0x9A, 0x99, 0x99, 0x3E, 0x3D, 0x0A, 0x17, 0x3F, 0xAE, 0x47, 0xE1, 0x3D};
      const std::vector<std::byte> pattern_bt_709_luminance_vector = {
         std::byte{0xD0}, std::byte{0xB3}, std::byte{0x59}, std::byte{0x3E},
         std::byte{0x59}, std::byte{0x17}, std::byte{0x37}, std::byte{0x3F},
         std::byte{0x98}, std::byte{0xDD}, std::byte{0x93}, std::byte{0x3D}};
      const std::vector<uint8_t> pattern_zero = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

      constexpr auto pattern_aces_blue_correct_matrix = MakeFloatsPattern(aces_blue_correct_matrix);
      // if (!System::ScanMemoryForPattern(code, size, pattern_aces_blue_correct_matrix.data(), pattern_aces_blue_correct_matrix.size()).empty())
      if ((!System::ScanMemoryForPattern(code, size, pattern_lut_output_scaling_a).empty() || !System::ScanMemoryForPattern(code, size, pattern_lut_output_scaling_b).empty()) && !System::ScanMemoryForPattern(code, size, pattern_srgb).empty())
      {
         if (type == reshade::api::pipeline_subobject_type::pixel_shader)
         {
            shader_hashes_tonemap_lut_candidates.pixel_shaders.emplace(uint32_t(shader_hash));
            // Automatically upgrade these!!!
            auto_texture_format_upgrade_shader_hashes.try_emplace(
               uint32_t(shader_hash),
               std::vector<uint8_t>{0},
               std::vector<uint8_t>{}); // TODO: make these thread safe
         }
         else
         {
            shader_hashes_tonemap_lut_candidates.compute_shaders.emplace(uint32_t(shader_hash));
            auto_texture_format_upgrade_shader_hashes.try_emplace(
               uint32_t(shader_hash),
               std::vector<uint8_t>{},
               std::vector<uint8_t>{0});
         }

#if DEVELOPMENT
         forced_shader_names.emplace(uint32_t(shader_hash), "Tonemap LUT");
#endif

         std::unique_ptr<std::byte[]> new_code = nullptr;

         // TODO:
         // Avoid clipping 0.2667 nits when encoding the LUT input. Also increase "LogLinearRange" form 14 to 16 to roughly map up to ~10k nits.
         // When sampling tonemapper LUT mid points, correct the encoding in and out. Also, use tetrahedral interpolation
         // Tonemap UI background?
         // Add UI and Scene paper white (there's dither in between, and possibly other passes! Maybe we could just add a pass to scale the scene before UI draws)
         // Remove post tonemap LUT sampling dithering (it's off by default?)
         // Avoid filmic tonemapper clipping colors beyond BT.709 (max 0 around pow funcs)
         // Remove unnecessary 1.05 scale from LUTs
         // Force "FilmWhiteClip" to 0 to make sure ~inf maps to 1, and nothing goes beyond, it'd get clipped in SDR LUTs later anyway
         // Test for inverted colors LUTs
         // Detect LUTs that are fading to white or black and don't upgrade them?
         // Detect white clipping point and skip from there up?
         // Add support for the oldest UE4 versions
         // Fix up raised blacks from TM parameters and from color grading LUTs

         // Always correct the wrong luminance calculations and sRGB encode/decode to not clip near black detail
         std::vector<std::byte*> scan_matches = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_srgb_conversion_max);
         if (!scan_matches.empty())
         {
            if (!new_code)
            {
               new_code = std::make_unique<std::byte[]>(size);
               std::memcpy(new_code.get(), code, size);
            }

            for (std::byte* match : scan_matches)
            {
               // Calculate offset of each match relative to original code
               size_t offset = match - code;
               std::memcpy(new_code.get() + offset, pattern_zero.data(), pattern_srgb_conversion_max.size());
            }
         }
         scan_matches = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_bt_601_luminance_vector_a);
         scan_matches.append_range(System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_bt_601_luminance_vector_b));
         if (!scan_matches.empty())
         {
            if (!new_code)
            {
               new_code = std::make_unique<std::byte[]>(size);
               std::memcpy(new_code.get(), code, size);
            }
            for (std::byte* match : scan_matches)
            {
               size_t offset = match - code;
               std::memcpy(new_code.get() + offset, pattern_bt_709_luminance_vector.data(), pattern_bt_709_luminance_vector.size());
            }
         }

         return new_code;
      }

      // x *= 1.05
      constexpr std::array<float, 1> lut_input_scaling = {
         1.05f};
      constexpr std::array<float, 2> lut_input_mapping_scale = {
         0.96875f, 0.015625f};
      const std::vector<uint8_t> pattern_lut_input_scaling = {0x66, 0x66, 0x86, 0x3F, 0x66, 0x66, 0x86, 0x3F, 0x66, 0x66, 0x86, 0x3F};
      // mad (sat?) register_n.xyz * 0.96875f + 0.015625f (mapping for 32x LUT size)
      // Both patterns might happen depending on how the game compiled the shaders:
      // mad r0.xyz, r0.xyzx, l(0.968750, 0.968750, 0.968750, 0.000000), l(0.015625, 0.015625, 0.015625, 0.000000)
      // mad r0.yzw, r0.yyzw, l(0.000000, 0.968750, 0.968750, 0.968750), l(0.000000, 0.015625, 0.015625, 0.015625)
      // We could probably append further patterns to this, given they seem to always match, but it's not really needed for now.
      const std::vector<uint8_t> pattern_lut_input_mapping_scale_a = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x3F, 0x00, 0x00, 0x78, 0x3F, 0x00, 0x00, 0x78, 0x3F, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x80, 0x3C};
      const std::vector<uint8_t> pattern_lut_input_mapping_scale_b = {0x00, 0x00, 0x78, 0x3F, 0x00, 0x00, 0x78, 0x3F, 0x00, 0x00, 0x78, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x80, 0x3C, 0x00, 0x00, 0x00, 0x00};
      // constexpr auto pattern_lut_input_scaling = MakeFloatsPattern(lut_input_scaling);
      // constexpr auto pattern_lut_input_mapping_scale = MakeFloatsPattern(lut_input_mapping_scale); // TODO: turn into a mad etc, make all of these more safe!!!
      if (!System::ScanMemoryForPattern(code, size, pattern_bt_601_luminance_vector_a.data(), pattern_bt_601_luminance_vector_a.size()).empty() && !System::ScanMemoryForPattern(code, size, pattern_lut_input_scaling).empty() && (!System::ScanMemoryForPattern(code, size, pattern_lut_input_mapping_scale_a).empty() || !System::ScanMemoryForPattern(code, size, pattern_lut_input_mapping_scale_b).empty()))
      {
         if (type == reshade::api::pipeline_subobject_type::pixel_shader)
         {
            shader_hashes_tonemap_candidates.pixel_shaders.emplace(uint32_t(shader_hash));
            auto_texture_format_upgrade_shader_hashes.try_emplace(uint32_t(shader_hash), std::vector<uint8_t>{0}, std::vector<uint8_t>{});
         }
         else
         {
            shader_hashes_tonemap_candidates.compute_shaders.emplace(uint32_t(shader_hash));
            auto_texture_format_upgrade_shader_hashes.try_emplace(uint32_t(shader_hash), std::vector<uint8_t>{}, std::vector<uint8_t>{0});
         }

#if DEVELOPMENT
         forced_shader_names.emplace(uint32_t(shader_hash), "Tonemap");
#endif

         std::unique_ptr<std::byte[]> new_code = nullptr;

         std::vector<std::byte*> scan_matches = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_srgb_conversion_max);
         if (!scan_matches.empty())
         {
            if (!new_code)
            {
               new_code = std::make_unique<std::byte[]>(size);
               std::memcpy(new_code.get(), code, size);
            }

            for (std::byte* match : scan_matches)
            {
               size_t offset = match - code;
               std::memcpy(new_code.get() + offset, pattern_zero.data(), pattern_srgb_conversion_max.size());
            }
         }
         scan_matches = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_bt_601_luminance_vector_a);
         scan_matches.append_range(System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_bt_601_luminance_vector_b));
         if (!scan_matches.empty())
         {
            if (!new_code)
            {
               new_code = std::make_unique<std::byte[]>(size);
               std::memcpy(new_code.get(), code, size);
            }
            for (std::byte* match : scan_matches)
            {
               size_t offset = match - code;
               std::memcpy(new_code.get() + offset, pattern_bt_709_luminance_vector.data(), pattern_bt_709_luminance_vector.size());
            }
         }

         return new_code;
      }

#if ENABLE_SR
      // SSAO Detection
      {
         SSAOShaderInfo ssao_info;
         if (IsUE4SSAOCandidate(code, size, ssao_info))
         {
            reshade::log::message(reshade::log::level::info, std::format("UE4: Detected UE4 SSAO shader. Hash: 0x{:08X}", shader_hash).c_str());
            if (type == reshade::api::pipeline_subobject_type::pixel_shader)
               shader_hashes_SSAO.pixel_shaders.emplace(static_cast<unsigned long>(shader_hash));
            else
               shader_hashes_SSAO.compute_shaders.emplace(static_cast<unsigned long>(shader_hash));

            const std::unique_lock ssao_lock(ssao_mutex);
            ssao_shader_info_map.emplace(shader_hash, ssao_info);
         }
      }

      // Dithering Detection and Modification
      if (enable_dithering_fix)
      {
         DitheringShaderInfo dither_info;
         if (IsUE4DitheringShader(code, size, shader_hash, dither_info))
         {
            const char* dither_type_str = "Unknown";
            switch (dither_info.type)
            {
            case DitheringType::Texture_ScreenSpace:
               dither_type_str = "Texture_ScreenSpace";
               break;
            default:
               break;
            }

            reshade::log::message(reshade::log::level::info,
               std::format("UE4: Detected dithering shader. Hash: 0x{:08X}, Type: {}, TexSize: {}x{}, TextureReg: t{}, HasDiscard: {}, Modifiable: {}",
                  shader_hash, dither_type_str, dither_info.noise_texture_size, dither_info.noise_texture_size,
                  dither_info.noise_texture_register, dither_info.has_discard, dither_info.modification_supported)
                  .c_str());

            if (type == reshade::api::pipeline_subobject_type::pixel_shader)
               shader_hashes_Dithering.pixel_shaders.emplace(static_cast<unsigned long>(shader_hash));
            else
               shader_hashes_Dithering.compute_shaders.emplace(static_cast<unsigned long>(shader_hash));

            {
               const std::unique_lock dither_lock(dithering_mutex);
               dithering_shader_info_map.emplace(shader_hash, dither_info);
            }

            // Attempt to modify the shader
            // For Texture_ScreenSpace:
            //   - No discard: Replace SAMPLE with MOV 0.5 (neutral noise)
            //   - Has discard: Inject cbuffer for temporal randomization (TODO)
            bool should_modify = dither_info.modification_supported;

            if (should_modify)
            {
               auto modified = ModifyDitheringShader(code, size, dither_info);
               if (modified)
               {
                  reshade::log::message(reshade::log::level::info,
                     std::format("UE4: Successfully modified dithering shader. Hash: 0x{:08X}", shader_hash).c_str());
                  return modified;
               }
            }
         }
      }

      // TAA Detection (only if we haven't found TAA yet)
      if (!shader_hashes_TAA.Empty())
         return nullptr;

      TAAShaderInfo taa_shader_info = {};
      bool is_taa_candidate = IsUE4TAACandidate(code, size, shader_hash, taa_shader_info) && FindShaderInfo(code, size, taa_shader_info);
      if (is_taa_candidate)
      {
         reshade::log::message(reshade::log::level::info, std::format("UE4: Detected UE4 TAA shader Candidate. Hash: 0x{:08X}", shader_hash).c_str());
         if (type == reshade::api::pipeline_subobject_type::pixel_shader)
            shader_hashes_TAA_Candidates.pixel_shaders.emplace(static_cast<unsigned long>(shader_hash));
         else if (type == reshade::api::pipeline_subobject_type::compute_shader)
            shader_hashes_TAA_Candidates.compute_shaders.emplace(static_cast<unsigned long>(shader_hash));
         if (global_cb_info.clip_to_prev_clip_start_index == -1)
            global_cb_info.clip_to_prev_clip_start_index = taa_shader_info.clip_to_prev_clip_start_index;
         ASSERT_ONCE(global_cb_info.clip_to_prev_clip_start_index == taa_shader_info.clip_to_prev_clip_start_index); // Check if there is any mismatch, if it happens we should probably keep highest index.
         global_cb_info.clip_to_prev_clip_start_index = taa_shader_info.clip_to_prev_clip_start_index;
         const std::unique_lock taa_lock(taa_mutex);
         taa_shader_candidate_info.emplace(shader_hash, taa_shader_info);
      }
#endif // ENABLE_SR

      return nullptr; // Return nullptr to use the original shader
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      GameDeviceDataUnrealEngine& game_device_data = GetGameDeviceData(device_data);
      bool is_compute_shader = stages == reshade::api::shader_stage::all_compute;

      // TODO: filter then to a more optimized list after confirming them
      // Find the shader that reads the tonemap LUT to do the per tonemapping.
      // This usually happens after every other post process and TAA, just before UI, and directly writes on the swapchain.
      if (enable_hdr && original_shader_hashes.Contains(shader_hashes_tonemap_candidates) && test_index != 15 && custom_shaders_enabled)
      {
         com_ptr<ID3D11ShaderResourceView> shader_resources[8]; // Should always be enough
         if (is_compute_shader)                                 // Can be both compute or pixel shader
            native_device_context->CSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         else
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);

         int32_t lut_srv_index = -1;
         for (size_t i = 0; i < ARRAYSIZE(shader_resources); i++)
         {
            if (shader_resources[i] == nullptr)
               continue; // TODO: break instead? Could it be?
            com_ptr<ID3D11Resource> resource;
            shader_resources[i]->GetResource(&resource);
            if (resource == nullptr)
               continue;
            D3D11_RESOURCE_DIMENSION res_type;
            resource->GetType(&res_type);
            if (res_type == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
               com_ptr<ID3D11Texture2D> texture2d = (ID3D11Texture2D*)resource.get();
               D3D11_TEXTURE2D_DESC desc;
               texture2d->GetDesc(&desc);
               if (desc.Width == 32 * 32 && desc.Height == 32)
               {
                  if (!game_device_data.tonemap_lut_texture || game_device_data.tonemap_lut_texture_is_3d)
                  {
                     game_device_data.tonemap_lut_texture = (com_ptr<ID3D11Resource>&&)CloneTexture<ID3D11Texture2D>(native_device, texture2d.get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS, D3D11_BIND_RENDER_TARGET, false, false, native_device_context);
                     native_device->CreateShaderResourceView(game_device_data.tonemap_lut_texture.get(), nullptr, &game_device_data.tonemap_lut_texture_srv);
                     native_device->CreateUnorderedAccessView(game_device_data.tonemap_lut_texture.get(), nullptr, &game_device_data.tonemap_lut_texture_uav);
                     game_device_data.tonemap_lut_texture_is_3d = false;
                  }
                  lut_srv_index = i;
                  break;
               }
            }
            else if (res_type == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
            {
               com_ptr<ID3D11Texture3D> texture3d = (ID3D11Texture3D*)resource.get();
               D3D11_TEXTURE3D_DESC desc;
               texture3d->GetDesc(&desc);
               if (desc.Width == 32 && desc.Height == 32 && desc.Depth == 32)
               {
                  if (!game_device_data.tonemap_lut_texture || !game_device_data.tonemap_lut_texture_is_3d)
                  {
                     game_device_data.tonemap_lut_texture = (com_ptr<ID3D11Resource>&&)CloneTexture<ID3D11Texture3D>(native_device, texture3d.get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS, D3D11_BIND_RENDER_TARGET, false, false, native_device_context);
                     native_device->CreateShaderResourceView(game_device_data.tonemap_lut_texture.get(), nullptr, &game_device_data.tonemap_lut_texture_srv);
                     native_device->CreateUnorderedAccessView(game_device_data.tonemap_lut_texture.get(), nullptr, &game_device_data.tonemap_lut_texture_uav);
                     game_device_data.tonemap_lut_texture_is_3d = true;
                  }
                  lut_srv_index = i;
                  break;
               }
            }
         }

         if (lut_srv_index > 0 && game_device_data.tonemap_lut_texture.get())
         {
            // Upgrade the LUT through a compute shader
            const auto shader_key = game_device_data.tonemap_lut_texture_is_3d ? CompileTimeStringHash("Upgrade Tonemap LUT 3D") : CompileTimeStringHash("Upgrade Tonemap LUT 2D");
            constexpr bool update_lut = true; // Disable to freeze the LUT
            if (device_data.native_compute_shaders[shader_key].get() != nullptr && update_lut)
            {
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

               if (!updated_cbuffers)
               {
                  constexpr bool do_safety_checks = false; // No need to check as we cache the states and restore them.
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
               }

               // Always set both on slot 0 for simplicity
               native_device_context->CSSetShaderResources(0, 1, &(ID3D11ShaderResourceView* const&)(shader_resources[lut_srv_index].get()));                         // Input
               native_device_context->CSSetUnorderedAccessViews(0, 1, &(ID3D11UnorderedAccessView* const&)(game_device_data.tonemap_lut_texture_uav.get()), nullptr); // Output

               native_device_context->CSSetShader(device_data.native_compute_shaders[shader_key].get(), nullptr, 0);

               ID3D11SamplerState* const sampler_state_point = device_data.sampler_state_point.get();
               native_device_context->CSSetSamplers(0, 1, &sampler_state_point);

               UINT width = tonemap_lut_size * (game_device_data.tonemap_lut_texture_is_3d ? 1 : tonemap_lut_size);
               UINT height = tonemap_lut_size;
               UINT depth = game_device_data.tonemap_lut_texture_is_3d ? tonemap_lut_size : 1;
               UINT groupsX = (width + tonemap_lut_thread_size - 1) / tonemap_lut_thread_size;
               UINT groupsY = (height + tonemap_lut_thread_size - 1) / tonemap_lut_thread_size;
               UINT groupsZ = game_device_data.tonemap_lut_texture_is_3d ? ((depth + tonemap_lut_thread_size - 1) / tonemap_lut_thread_size) : 1;
               native_device_context->Dispatch(groupsX, groupsY, groupsZ);

#if DEVELOPMENT
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  TraceDrawCallData trace_draw_call_data;
                  trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                  trace_draw_call_data.command_list = native_device_context;
                  trace_draw_call_data.custom_name = "Upgrade Tonemap LUT";
                  // Re-use the RTV data for simplicity
                  GetResourceInfo(game_device_data.tonemap_lut_texture.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                  cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
               }
#endif

               // Restore everything for extra safety (likely not needed!)
               compute_state_stack.Restore(native_device_context);
            }

            // Restore SRVs and set upgraded LUT
            if (is_compute_shader)
            {
               native_device_context->CSSetShaderResources(0, 1, &(ID3D11ShaderResourceView* const&)(shader_resources[0].get()));
               native_device_context->CSSetShaderResources(lut_srv_index, 1, &(ID3D11ShaderResourceView* const&)(game_device_data.tonemap_lut_texture_srv.get()));
            }
            else
            {
               native_device_context->PSSetShaderResources(0, 1, &(ID3D11ShaderResourceView* const&)(shader_resources[0].get()));
               native_device_context->PSSetShaderResources(lut_srv_index, 1, &(ID3D11ShaderResourceView* const&)(game_device_data.tonemap_lut_texture_srv.get()));
            }

            // Only do indirect upgrades after tonemapping to avoid any risk for false positives that could lead to graphical glitches or crashes.
            // UE uses one command list in DX11 so it's not a problem.
            cmd_list_data.enable_chain_indirect_texture_format_upgrades = (uint)enable_chain_indirect_texture_format_upgrades;
            ASSERT_ONCE(cmd_list_data.is_primary);

            return DrawOrDispatchOverrideType::None;
         }
      }

      bool is_taa = original_shader_hashes.Contains(shader_hashes_TAA);
      bool is_taa_candidate = !is_taa && original_shader_hashes.Contains(shader_hashes_TAA_Candidates);
      if (!is_taa && !is_taa_candidate)
         return DrawOrDispatchOverrideType::None;

      uint64_t shader_hash = is_compute_shader ? original_shader_hashes.compute_shaders[0] : original_shader_hashes.pixel_shaders[0];
      TAAShaderInfo taa_shader_info;
      {
         std::shared_lock taa_lock(taa_mutex);
         taa_shader_info = taa_shader_candidate_info[shader_hash];
      }

#if ENABLE_SR
      if (is_taa_candidate && !taa_shader_info.found_all)
      {
         // verify it's really TAA by checking the SRV signatures, there should be 2 color textures, a depth texture(R32G8X24 or other depth stencil formats) and a velocity texture(unorm RG)
         // we can also check the sampler states, there should be point and linear filtering samplers (we can do this later)
         com_ptr<ID3D11ShaderResourceView> shader_resources[16];
         if (is_compute_shader)
            native_device_context->CSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         else
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         size_t color_texture_count = 0;
         size_t depth_texture_count = 0;
         size_t velocity_texture_count = 0;
         for (size_t i = 0; i < ARRAYSIZE(shader_resources); i++)
         {
            if (shader_resources[i] == nullptr)
               continue;
            com_ptr<ID3D11Resource> resource;
            shader_resources[i]->GetResource(&resource);
            if (resource == nullptr)
               continue;
            D3D11_RESOURCE_DIMENSION res_type;
            resource->GetType(&res_type);
            if (res_type != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
               continue;
            com_ptr<ID3D11Texture2D> texture2d = (ID3D11Texture2D*)resource.get();
            D3D11_TEXTURE2D_DESC desc;
            texture2d->GetDesc(&desc);
            // check format
            float output_aspect_ratio = static_cast<float>(desc.Width) / static_cast<float>(desc.Height);
            float swapchain_aspect_ratio = device_data.render_resolution.x / device_data.render_resolution.y;

            if (std::fabs(output_aspect_ratio - swapchain_aspect_ratio) > FLT_EPSILON)
               continue;
            if (desc.Width < device_data.render_resolution.x || desc.Height < device_data.render_resolution.y)
               continue;

            switch (desc.Format)
            {
            case DXGI_FORMAT_R11G11B10_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
               color_texture_count++;
               // assume lowest index is the main color texture
               if (taa_shader_info.source_texture_register == -1)
                  taa_shader_info.source_texture_register = (uint32_t)i;
               break;
            case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            case DXGI_FORMAT_R24G8_TYPELESS:
            case DXGI_FORMAT_R32G8X24_TYPELESS:
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
            case DXGI_FORMAT_D16_UNORM:
               depth_texture_count++;
               if (taa_shader_info.depth_texture_register == -1)
                  taa_shader_info.depth_texture_register = (uint32_t)i;
               break;
            case DXGI_FORMAT_R16G16_UNORM:
            case DXGI_FORMAT_R16G16B16A16_UNORM:
               velocity_texture_count++;
               if (taa_shader_info.velocity_texture_register == -1)
                  taa_shader_info.velocity_texture_register = (uint32_t)i;
               break;
            default:
               break;
            }
         }
         // we should have at least 2 color textures, 1 depth texture and 1 velocity texture
         if (color_texture_count >= 2 && depth_texture_count >= 1 && velocity_texture_count >= 1)
         {
            is_taa = true;
            taa_shader_info.found_all = true;
            {
               const std::unique_lock taa_lock(taa_mutex);
               taa_shader_candidate_info[shader_hash] = taa_shader_info;
            }
            reshade::log::message(reshade::log::level::info, std::format("UE4: Detected UE4 TAA. Hash: 0x{:08X}", shader_hash).c_str());
            if (is_compute_shader)
               shader_hashes_TAA.compute_shaders.emplace(static_cast<unsigned long>(shader_hash));
            else
               shader_hashes_TAA.pixel_shaders.emplace(static_cast<unsigned long>(shader_hash));
         }
      }

      // if we already drew SR this frame, copy dlss output to shader output (some games run different quality settings in the same frame?)
      if (is_taa && device_data.has_drawn_sr)
      {
         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1 or 2
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         com_ptr<ID3D11UnorderedAccessView> unordered_access_views[D3D11_PS_CS_UAV_REGISTER_COUNT];
         if (is_compute_shader)
            native_device_context->CSGetUnorderedAccessViews(0, ARRAYSIZE(unordered_access_views), &unordered_access_views[0]);
         else
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);

         com_ptr<ID3D11Resource> output_color_resource;

         if (is_compute_shader)
            unordered_access_views[0]->GetResource(&output_color_resource);
         else
            render_target_views[0]->GetResource(&output_color_resource);

         com_ptr<ID3D11Texture2D> output_color;
         HRESULT hr = output_color_resource->QueryInterface(&output_color);
         ASSERT_ONCE(SUCCEEDED(hr));
         if (device_data.sr_output_color.get() && output_color.get())
         {
            native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get());
         }
         return DrawOrDispatchOverrideType::Skip;
      }

      if (is_taa && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
      {
         device_data.taa_detected = true;
         if ((is_compute_shader && device_data.native_compute_shaders[CompileTimeStringHash("Decode MVs CS")].get() == nullptr) ||
             (!is_compute_shader && device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs PS")].get() == nullptr))
         {
            device_data.force_reset_sr = true;
            return DrawOrDispatchOverrideType::None;
         }
         com_ptr<ID3D11ShaderResourceView> shader_resources[16];

         if (is_compute_shader)
            native_device_context->CSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);
         else
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(shader_resources), &shader_resources[0]);

         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1 or 2
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         com_ptr<ID3D11UnorderedAccessView> unordered_access_views[D3D11_PS_CS_UAV_REGISTER_COUNT];
         if (is_compute_shader)
            native_device_context->CSGetUnorderedAccessViews(0, ARRAYSIZE(unordered_access_views), &unordered_access_views[0]);
         else
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         if (global_cb_info.size == 0)
         {
            // The first time we run TAA, we can get the global cbuffer size now
            // we can then use this to detect the cbuffer in the CPU during OnMapBufferRegion and OnUnmapBufferRegion hooks
            // Buffers are seemengly pooled and cycled in UE so we can't just match them by pointers
            com_ptr<ID3D11Buffer> global_cbuffer;
            if (is_compute_shader)
               native_device_context->CSGetConstantBuffers(taa_shader_info.global_buffer_register_index, 1, &global_cbuffer);
            else
               native_device_context->PSGetConstantBuffers(taa_shader_info.global_buffer_register_index, 1, &global_cbuffer);
            ASSERT_ONCE(global_cbuffer != nullptr);
            D3D11_BUFFER_DESC global_cbuffer_desc;
            global_cbuffer->GetDesc(&global_cbuffer_desc);
            global_cb_info.size = global_cbuffer_desc.ByteWidth;

            // Skip this draw call, we will run DLSS next frame after we detected the global cbuffer on the CPU (if necessary, we could extract the data from the cbuffer already in DX11, though that's slow and for now we just wait 1 frame)
            return DrawOrDispatchOverrideType::None;
         }
         const bool dlss_inputs_valid = shader_resources[taa_shader_info.source_texture_register].get() != nullptr && shader_resources[taa_shader_info.depth_texture_register].get() != nullptr && shader_resources[taa_shader_info.velocity_texture_register].get() != nullptr && (render_target_views[0].get() != nullptr || unordered_access_views[0].get() != nullptr);
         ASSERT_ONCE(dlss_inputs_valid);
         if (dlss_inputs_valid)
         {
            if (game_device_data.found_per_view_globals.load() == false)
               return DrawOrDispatchOverrideType::None;
            auto* sr_instance_data = device_data.GetSRInstanceData();
            ASSERT_ONCE(sr_instance_data);

            com_ptr<ID3D11Resource> output_color_resource;
            if (is_compute_shader)
               unordered_access_views[0]->GetResource(&output_color_resource);
            else
               render_target_views[0]->GetResource(&output_color_resource);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT hr = output_color_resource->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));

            D3D11_TEXTURE2D_DESC taa_output_texture_desc;
            output_color->GetDesc(&taa_output_texture_desc);

            if (taa_output_texture_desc.Width != device_data.render_resolution.x || taa_output_texture_desc.Height != device_data.render_resolution.y)
            {
               float output_aspect_ratio = static_cast<float>(taa_output_texture_desc.Width) / static_cast<float>(taa_output_texture_desc.Height);
               float swapchain_aspect_ratio = device_data.render_resolution.x / device_data.render_resolution.y;

               if (std::fabs(output_aspect_ratio - swapchain_aspect_ratio) > FLT_EPSILON)
               {
                  device_data.force_reset_sr = true;
                  return DrawOrDispatchOverrideType::None;
               }
            }

            D3D11_VIEWPORT viewport;
            uint32_t num_viewports = 1;
            native_device_context->RSGetViewports(&num_viewports, &viewport);
            // game_device_data.viewport_rect         = {viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height};
            // game_device_data.render_resolution     = {(float)taa_output_texture_desc.Width, (float)taa_output_texture_desc.Height, 1.0f / (float)taa_output_texture_desc.Width, 1.0f / (float)taa_output_texture_desc.Height};
            device_data.sr_render_resolution_scale = 1.0f; // DLAA only

            SR::SettingsData settings_data;
            settings_data.output_width = taa_output_texture_desc.Width;
            settings_data.output_height = taa_output_texture_desc.Height;
            settings_data.render_width = game_device_data.render_resolution.x;
            settings_data.render_height = game_device_data.render_resolution.y;
            settings_data.dynamic_resolution = true;
            settings_data.hdr = true; // Unreal Engine does DLSS before tonemapping, in HDR linear space
            settings_data.inverted_depth = true;
            settings_data.mvs_jittered = false;
            settings_data.auto_exposure = sr_auto_exposure; // Unreal Engine does TAA before tonemapping
            settings_data.render_preset = dlss_render_preset;
            settings_data.mvs_x_scale = 1.0f;
            settings_data.mvs_y_scale = 1.0f;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

            constexpr bool dlss_use_native_uav = true;
            bool dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;

            bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
            bool dlss_output_changed = false;
            // Create a copy that supports Unordered Access if it wasn't already supported
            if (!dlss_output_supports_uav)
            {
               D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
               // dlss_output_texture_desc.Width = std::lrintf(game_device_data.render_resolution.x);
               // dlss_output_texture_desc.Height = std::lrintf(game_device_data.render_resolution.y);
               dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

               if (device_data.sr_output_color.get())
               {
                  D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                  device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                  dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
               }
               if (!device_data.sr_output_color.get() || dlss_output_changed)
               {
                  device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                  hr = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
               // Texture creation failed, we can't proceed with DLSS
               if (!device_data.sr_output_color.get())
               {
                  skip_dlss = true;
               }
            }
            else
            {
               ASSERT_ONCE(device_data.sr_output_color == nullptr);
               device_data.sr_output_color = output_color;
            }
            if (!skip_dlss)
            {
               game_device_data.sr_source_color = nullptr;
               shader_resources[taa_shader_info.source_texture_register]->GetResource(&game_device_data.sr_source_color);
               game_device_data.depth_buffer = nullptr;
               shader_resources[taa_shader_info.depth_texture_register]->GetResource(&game_device_data.depth_buffer);
               com_ptr<ID3D11Resource> object_velocity;
               shader_resources[taa_shader_info.velocity_texture_register]->GetResource(&object_velocity);

               {
                  if (!AreResourcesEqual(object_velocity.get(), game_device_data.sr_motion_vectors.get(), false /*check_format*/))
                  {
                     com_ptr<ID3D11Texture2D> object_velocity_texture;
                     hr = object_velocity->QueryInterface(&object_velocity_texture);
                     ASSERT_ONCE(SUCCEEDED(hr));
                     D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                     object_velocity_texture->GetDesc(&object_velocity_texture_desc);
                     ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);
#if 1 // Use the higher quality for MVs, the game's one were R16G16F. This has a ~1% cost on performance but helps with reducing shimmering on fine lines (stright lines looking segmented, like Bart's hair or Shark's teeth) when the camera is moving in a linear fashion.
                     object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
#else // Note: for FF7, 16bit might be enough, to be tried and compared, but the extra precision won't hurt
                     object_velocity_texture_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
#endif
                     if (is_compute_shader)
                     {
                        object_velocity_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
                        object_velocity_texture_desc.BindFlags &= ~D3D11_BIND_RENDER_TARGET;
                        game_device_data.sr_motion_vectors_uav = nullptr; // Make sure we discard the previous one
                        game_device_data.sr_motion_vectors = nullptr;     // Make sure we discard the previous one
                        hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &game_device_data.sr_motion_vectors);
                        ASSERT_ONCE(SUCCEEDED(hr));
                        if (SUCCEEDED(hr))
                        {
                           hr = native_device->CreateUnorderedAccessView(game_device_data.sr_motion_vectors.get(), nullptr, &game_device_data.sr_motion_vectors_uav);
                           ASSERT_ONCE(SUCCEEDED(hr));
                        }
                     }
                     else
                     {
                        game_device_data.sr_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                        game_device_data.sr_motion_vectors = nullptr;     // Make sure we discard the previous one
                        hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &game_device_data.sr_motion_vectors);
                        ASSERT_ONCE(SUCCEEDED(hr));
                        if (SUCCEEDED(hr))
                        {
                           hr = native_device->CreateRenderTargetView(game_device_data.sr_motion_vectors.get(), nullptr, &game_device_data.sr_motion_vectors_rtv);
                           ASSERT_ONCE(SUCCEEDED(hr));
                        }
                     }
                  }
               }

               if (taa_shader_info.has_multiple_render_targets)
               {
                  // Call original draw to populate all render targets then replace the first one later with DLSS output
                  if (original_draw_dispatch_func && *original_draw_dispatch_func)
                  {
                     (*original_draw_dispatch_func)();
                  }
               }

               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               // We don't actually replace the shaders with the classic luma shader swapping feature, so we need to set the CBs manually
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

               if (!updated_cbuffers)
               {
                  constexpr bool do_safety_checks = false; // No need to check as we cache the states and restore them.
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
               }

               DecodeMotionVectors(is_compute_shader, native_device_context, device_data, taa_shader_info);
               ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
               native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());
#if DEVELOPMENT
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  TraceDrawCallData trace_draw_call_data;
                  trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                  trace_draw_call_data.command_list = native_device_context;
                  trace_draw_call_data.custom_name = "SR Decode Motion Vectors";
                  // Re-use the RTV data for simplicity
                  GetResourceInfo(game_device_data.sr_motion_vectors.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                  cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
               }
#endif
               // UE4 seems to skip taa on the first frame after a camera cut.
               bool reset_sr = device_data.force_reset_sr || dlss_output_changed || game_device_data.camera_cut;
               device_data.force_reset_sr = false;

               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = game_device_data.sr_source_color.get();
               draw_data.output_color = device_data.sr_output_color.get();
               draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth_buffer.get();
               draw_data.pre_exposure = 0.0f; // automatic exposure
               draw_data.jitter_x = game_device_data.jitter.x * game_device_data.render_resolution.x * 0.5f;
               draw_data.jitter_y = game_device_data.jitter.y * game_device_data.render_resolution.y * -0.5f;
               draw_data.reset = reset_sr;
               draw_data.render_width = game_device_data.render_resolution.x;
               draw_data.render_height = game_device_data.render_resolution.y;
               draw_data.near_plane = game_device_data.near_plane / 100.0f;
               draw_data.far_plane = FLT_MAX; // TODO: made up values
               draw_data.vert_fov = game_device_data.fov_y;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
               draw_state_stack.Restore(native_device_context);
               compute_state_stack.Restore(native_device_context);
               if (dlss_succeeded)
               {
                  device_data.has_drawn_sr = true;
               }
               game_device_data.camera_cut = false;
               game_device_data.sr_source_color = nullptr;
               game_device_data.depth_buffer = nullptr;

               // Restore the previous state

               if (device_data.has_drawn_sr)
               {
#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = "DLSS";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  if (!dlss_output_supports_uav)
                  {
                     native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                  }
                  else
                  {
                     device_data.sr_output_color = nullptr;
                  }

                  return DrawOrDispatchOverrideType::Replaced;
               }
               else
               {
                  // ASSERT_ONCE(false);
                  // cb_luma_global_settings.SRType = 0;
                  // device_data.cb_luma_global_settings_dirty = true;
                  // device_data.sr_suppressed = true;
                  device_data.force_reset_sr = true;
               }
            }
            if (dlss_output_supports_uav)
            {
               device_data.sr_output_color = nullptr;
            }
         }
      }
#endif // ENABLE_SR
      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.found_per_view_globals = false;
      game_device_data.camera_cut = !device_data.taa_detected && !device_data.has_drawn_sr && !device_data.force_reset_sr;
      device_data.has_drawn_sr = false;
      game_device_data.jitter = {0.0f, 0.0f};

      if (device_data.primary_command_list_data)
      {
         device_data.primary_command_list_data->enable_chain_indirect_texture_format_upgrades = (uint)ChainTextureFormatUpgradesType::DirectDependencies;
      }
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.sr_motion_vectors_rtv = nullptr;
      game_device_data.sr_motion_vectors_uav = nullptr;
      game_device_data.sr_source_color = nullptr;
      game_device_data.depth_buffer = nullptr;
      game_device_data.sr_settings_data = nullptr;
      game_device_data.sr_draw_data = nullptr;
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      data.GameData.ViewportRect = game_device_data.viewport_rect;
      data.GameData.RenderResolution = game_device_data.render_resolution;
      data.GameData.ClipToPrevClip = game_device_data.clip_to_prev_clip_matrix;
      data.GameData.JitterOffset.x = game_device_data.jitter.x;
      data.GameData.JitterOffset.y = game_device_data.jitter.y;
      data.GameData.ClipToPrevClipIndex = global_cb_info.clip_to_prev_clip_start_index;
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::Text("Luma for \"Unreal Engine\" is developed by Izueh and Pumbo and is open source and free.\n"
                  "It replaces Unreal Engine default TAA implementation with DLAA\n"
                  "If you enjoy it, consider donating to any of the contributors.",
         "");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);

      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 136, 124, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(17, 149, 134, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(57, 133, 111, 255));
      static const std::string donation_link_izueh = std::string("Buy Izueh a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_izueh.c_str()))
      {
         system("start https://ko-fi.com/izueh");
      }
      ImGui::PopStyleColor(3);
      ImGui::NewLine();

      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));

      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
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
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "");
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::Checkbox("Enable Luma HDR", &next_enable_hdr))
      {
         reshade::set_config_value(runtime, NAME, "EnableHDR", next_enable_hdr);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Enables Luma's Unreal Engine HDR remastering. It works in the majority of games out of the box.\nIf the game already already supported HDR, make sure to turn it off in the settings.\n\nRequires rester to apply.");
      }
      // Print a check/warning if it's active or not active
      if (enable_hdr || next_enable_hdr)
      {
         ImGui::SameLine();
         ImGui::PushID("HDR Active");
         ImGui::BeginDisabled();
         ImGui::SmallButton(game_device_data.tonemap_lut_texture.get() ? ICON_FK_OK : ICON_FK_WARNING);
         ImGui::EndDisabled();
         ImGui::PopID();
      }

      if (enable_hdr && cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (GetShaderDefineCompiledNumericalValue(char_ptr_crc32("TONEMAP_TYPE")) == 2)
         {
            if (ImGui::SliderFloat("Highlights Hue Preservation", &cb_luma_global_settings.GameSettings.HDRHighlightsHuePreservation, 0.f, 1.f))
            {
               reshade::set_config_value(runtime, NAME, "HDRHighlightsHuePreservation", cb_luma_global_settings.GameSettings.HDRHighlightsHuePreservation);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("The higher the value, the more we preserve the original (SDR) highlights hue (ELI5: color), which depending on the game, might often be distorted for HDR.");
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.HDRHighlightsHuePreservation, cb_default_game_settings.HDRHighlightsHuePreservation, "HDRHighlightsHuePreservation", runtime);

            if (ImGui::SliderFloat("Highlights Chrominance Preservation", &cb_luma_global_settings.GameSettings.HDRHighlightsChrominancePreservation, 0.f, 1.f))
            {
               reshade::set_config_value(runtime, NAME, "HDRHighlightsChrominancePreservation", cb_luma_global_settings.GameSettings.HDRHighlightsChrominancePreservation);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("The higher the value, the more we preserve the original (SDR) highlights chrominance (ELI5: saturation), which depending on the game, might often be overly desaturated for HDR.");
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.HDRHighlightsChrominancePreservation, cb_default_game_settings.HDRHighlightsChrominancePreservation, "HDRHighlightsChrominancePreservation", runtime);
         }

         if (ImGui::SliderFloat("Saturation", &cb_luma_global_settings.GameSettings.HDRChrominance, 0.f, 2.f))
         {
            reshade::set_config_value(runtime, NAME, "HDRChrominance", cb_luma_global_settings.GameSettings.HDRChrominance);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Controls the global saturation/chrominance.");
         }
         DrawResetButton(cb_luma_global_settings.GameSettings.HDRChrominance, cb_default_game_settings.HDRChrominance, "HDRChrominance", runtime);
      }

#if ENABLE_SR
      ImGui::NewLine();

      // TODO: render this below the SR selection
      // Disable the checkbox if SR is not enabled
      bool sr_enabled = device_data.sr_type != SR::Type::None;
      bool sr_forces_auto_exposure = device_data.sr_type == SR::Type::DLSS && (dlss_render_preset == 0 /*NVSDK_NGX_DLSS_Hint_Render_Preset_Default*/ || dlss_render_preset >= 12 /*NVSDK_NGX_DLSS_Hint_Render_Preset_L*/); // L and M are now default and ignore the fixed exposure

      if (!sr_enabled || sr_forces_auto_exposure)
         ImGui::BeginDisabled();
      ImGui::Checkbox("Super Resolution Auto Exposure", sr_forces_auto_exposure ? &sr_forces_auto_exposure : &sr_auto_exposure); // Force show it as enabled if it's always on
      if (!sr_enabled || sr_forces_auto_exposure)
         ImGui::EndDisabled();

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Enable Super Resolution (DLSS/FSR) to change this setting.");
      }

      if (ImGui::TreeNode("Experimental Features"))
      {
         ImGui::Checkbox("Dithering Fix", &enable_dithering_fix); // TODO: this isn't serialized? Nor auto exposure is?

         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Fixes dithering issues that may appear when using DLSS/FSR with TAA. Very experimental. Requires restart.");
         }
         ImGui::TreePop();
      }
#endif
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "HDRHighlightsHuePreservation", cb_luma_global_settings.GameSettings.HDRHighlightsHuePreservation);
      reshade::get_config_value(runtime, NAME, "HDRHighlightsChrominancePreservation", cb_luma_global_settings.GameSettings.HDRHighlightsChrominancePreservation);
      reshade::get_config_value(runtime, NAME, "HDRChrominance", cb_luma_global_settings.GameSettings.HDRChrominance);
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (game_device_data.found_per_view_globals)
      {
         return;
      }

      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         if (buffer_desc.ByteWidth == global_cb_info.size)
         {
            device_data.cb_per_view_global_buffer = buffer;
            ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
            device_data.cb_per_view_global_buffer_map_data = *data;
         }
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      // Already decided this frame
      if (game_device_data.found_per_view_globals)
      {
         device_data.cb_per_view_global_buffer_map_data = nullptr;
         device_data.cb_per_view_global_buffer = nullptr;
         return;
      }

      const bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr &&
                                     device_data.cb_per_view_global_buffer == buffer;
      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);
      if (!is_global_cbuffer || device_data.cb_per_view_global_buffer_map_data == nullptr)
         return;

      float4* float_data = reinterpret_cast<float4*>(device_data.cb_per_view_global_buffer_map_data);
      const size_t size_float = static_cast<size_t>(global_cb_info.size) / sizeof(float4);

      const bool have_offsets =
         (global_cb_info.view_to_clip_start_index >= 0 && global_cb_info.view_size_and_inv_size_index >= 0);

      // If offsets are known, never rescan; just validate and return.
      if (have_offsets)
      {
         float4 vsize_and_inv_size = float_data[global_cb_info.view_size_and_inv_size_index];
         Matrix44F matrix_a;
         std::memcpy(&matrix_a, &float_data[global_cb_info.view_to_clip_start_index], sizeof(Matrix44F));
         bool is_global_cb = IsViewSizeInvSize(vsize_and_inv_size, device_data.render_resolution.x / device_data.render_resolution.y) && MatrixLikeProjection(matrix_a) && ProjectionHasJitter(matrix_a, {vsize_and_inv_size.z, vsize_and_inv_size.w});
         if (is_global_cb)
         {
            game_device_data.jitter.x = matrix_a.m20;
            game_device_data.jitter.y = matrix_a.m21;
            game_device_data.near_plane = ComputeNearPlane(matrix_a);
            game_device_data.far_plane = FLT_MAX;
            game_device_data.fov_y = ComputeFovY(matrix_a);
            device_data.render_resolution.x = vsize_and_inv_size.x;
            device_data.render_resolution.y = vsize_and_inv_size.y;
            game_device_data.found_per_view_globals = true;
         }
      }
      else
      {
         global_cb_info.view_size_and_inv_size_index = -1;
         global_cb_info.view_to_clip_start_index = -1;

         for (size_t i = 0; i + 1 < size_float; ++i)
         {
            const float4 vsize_and_inv_size = float_data[i];
            bool is_vsize_inv_size = IsViewSizeInvSize(vsize_and_inv_size, device_data.render_resolution.x / device_data.render_resolution.y);
            if (is_vsize_inv_size)
            {
               global_cb_info.view_size_and_inv_size_index = static_cast<int>(i);
               break;
            }
         }

         if (global_cb_info.view_size_and_inv_size_index < 0)
         {
            device_data.cb_per_view_global_buffer_map_data = nullptr;
            device_data.cb_per_view_global_buffer = nullptr;
            return;
         }

         // Now scan for adjacent matrix pairs that look like ViewToClip / ClipToView
         // Should be before the view size index so we can stop then, specially because previous matrices are sometimes towards the end.
         Matrix44F matrix_a;
         size_t stopping_index = static_cast<size_t>(global_cb_info.view_size_and_inv_size_index);
         float4 vsize_and_inv_size = float_data[global_cb_info.view_size_and_inv_size_index];
         for (size_t i = 0; i + 4 <= stopping_index; ++i)
         {
            std::memcpy(&matrix_a, &float_data[i], sizeof(Matrix44F));
            bool is_projection = MatrixLikeProjection(matrix_a) && ProjectionHasJitter(matrix_a, {vsize_and_inv_size.z, vsize_and_inv_size.w});
            if (is_projection)
            {
               game_device_data.jitter.x = matrix_a.m20;
               game_device_data.jitter.y = matrix_a.m21;
               game_device_data.near_plane = ComputeNearPlane(matrix_a);
               game_device_data.far_plane = FLT_MAX;
               game_device_data.fov_y = ComputeFovY(matrix_a);

               global_cb_info.view_to_clip_start_index = static_cast<int>(i);
               device_data.render_resolution.x = vsize_and_inv_size.x;
               device_data.render_resolution.y = vsize_and_inv_size.y;
               game_device_data.found_per_view_globals = true;
               break;
            }
         }
      }
      UpdateLODBias(device);
      device_data.cb_per_view_global_buffer_map_data = nullptr;
      device_data.cb_per_view_global_buffer = nullptr;
   }

   static void UpdateLODBias(reshade::api::device* device)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
#if DEVELOPMENT
      if (!custom_texture_mip_lod_bias_offset)
#endif
      {
         DeviceData& device_data = *device->get_private_data<DeviceData>();
         std::shared_lock shared_lock_samplers(s_mutex_samplers);

         const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed /*&& device_data.taa_detected*/)
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
               D3D11_SAMPLER_DESC native_desc;
               native_sampler->GetDesc(&native_desc);
               shared_lock_samplers.unlock(); // This is fine!
               {
                  std::unique_lock unique_lock_samplers(s_mutex_samplers);
                  samplers_handle.second[new_texture_mip_lod_bias_offset] = CreateCustomSampler(device_data, native_device, native_desc);
               }
               shared_lock_samplers.lock();
            }
         }
      }
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Unreal Engine Generic Luma mod");
      Globals::VERSION = 1;

      cb_default_game_settings.HDRHighlightsHuePreservation = 0.667f;
      cb_default_game_settings.HDRHighlightsChrominancePreservation = 0.333f;
      cb_default_game_settings.HDRChrominance = 1.0f;
      cb_luma_global_settings.GameSettings = cb_default_game_settings;

      // Needed for Super Resolution mips bias
      enable_samplers_upgrade = true;

      // UE games scale the resolution by DPI, which is generally a bad thing for games
      force_ignore_dpi = true;

      game = new UnrealEngine();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(UnrealEngine::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(UnrealEngine::OnUnmapBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   // Needs to be done after core init (because it calls some ReShade funcs)
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
#if !DEVELOPMENT // Force HDR in dev mode, so we can easily debug textures
      reshade::get_config_value(nullptr, NAME, "FirstBoot", first_boot);
      if (first_boot)
      {
         reshade::set_config_value(nullptr, NAME, "FirstBoot", false);

         // Automatically enable HDR in the mod if it's supported on the primary display on first boot
         bool hdr_supported_display;
         bool hdr_enabled_display;
         Display::IsHDRSupportedAndEnabled(0, hdr_supported_display, hdr_enabled_display);
         enable_hdr = hdr_supported_display;

         reshade::set_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      else
      {
         reshade::get_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
#endif
      next_enable_hdr = enable_hdr;
      // Meant for games that already have good HDR, or to simply keep the original SDR output
      if (!enable_hdr)
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
         swapchain_upgrade_type = SwapchainUpgradeType::None;
         texture_format_upgrades_type = TextureFormatUpgradesType::None;
         texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::None;
         force_disable_display_composition = true; // Avoid any changes to gamma, and hide HDR related settings from UI // TODO: there shouldn't be any anyway! Why is this needed? Couldn't we simply check "swapchain_format_upgrade_type", at least in some cases?
      }
      else
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
         texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
         enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectAndIndirectDependencies;

#if 0 // Not needed as it's done automatically now
      // TODO: automatically upgrade all textures that sample the tonemap LUT, and all textures in between tonemapping and the swapchain final write
         texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_typeless,
            
            // sRGB formats are usually not used by UE post processing
            //reshade::api::format::r8g8b8a8_unorm_srgb,
            //reshade::api::format::r8g8b8x8_unorm_srgb,
            //reshade::api::format::b8g8r8a8_unorm_srgb,
            //reshade::api::format::b8g8r8x8_unorm_srgb,
            
            reshade::api::format::r10g10b10a2_typeless,
            reshade::api::format::r10g10b10a2_unorm,
         };
         texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution;
#else
         texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::None;
#endif

         constexpr bool extra_upgraded_formats = true;
         if (extra_upgraded_formats)
         {
            texture_upgrade_formats.emplace(reshade::api::format::r11g11b10_float); // Some games use this format for the whole post processing
            texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;
         }

#if 0 // Not needed as we do it through "auto_texture_format_upgrade_shader_hashes"
      // LUT is usually 3D 32x (occasionally 2D, especially in older games)
         texture_format_upgrades_lut_size = 32; // TODO: upgrade both 2D and 3D (maybe manually by detecting the shaders?)
         texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;
#endif

         // Add per game shader hashes that require texture format upgrades, after tonemapping:
         auto_texture_format_upgrade_shader_hashes.try_emplace(0x9A6F4220, std::vector<uint8_t>{}, std::vector<uint8_t>{0}); // Little Nightmares Enhanced Edition - FSR Sharpening/Upscaling - it changes the aspect ratio and resolution at the same time and thus fails aspect ratio checks
      }
   }

   return TRUE;
}