#pragma once

#include "shared.h"

// Quantum Break's SR hook is assembled from several stable Remedy passes:
// - 0xA43343D6 "Depth Linearization" reads clip/device depth from PS SRV0 and writes linear depth to RTV0.
//   We cache the PS SRV0 input, not the linearized RTV output, so SR receives pre-linearized depth.
// - 0xE8337D48 "History Reprojection" reads the geometry velocity texture from CS SRV0.
// - 0x99274617 "Temporal Resolve" is the SR insertion point. It provides source color in PS SRV2,
//   final output in OM RTV0, temporal cbuffer data in PS CB0/CB1, and a same-depth fallback in PS SRV0.
// - Luma_QB_PreSRDecode converts QB's gamma-space temporal source color to linear before DLSS/FSR.
namespace QuantumBreakUpscaling
{
   constexpr uint32_t hash_depth_linearization = 0xA43343D6u;
   constexpr uint32_t hash_history_reprojection = 0xE8337D48u;
   constexpr uint32_t hash_temporal_resolve = 0x99274617u;
   constexpr uint32_t hash_msaa_gbuffer_reconstruction = 0x037FCE62u;
   constexpr uint32_t hash_msaa_gbuffer_depth_resolve = 0x84DD3C0Cu;

   // Pass hashes used to identify the parts of QB's depth/temporal pipeline we need to observe/replace.
   inline ShaderHashesList<>& DepthLinearizationHashes()
   {
      static ShaderHashesList<> hashes;
      return hashes;
   }

   inline ShaderHashesList<>& HistoryReprojectionHashes()
   {
      static ShaderHashesList<> hashes;
      return hashes;
   }

   inline ShaderHashesList<>& TemporalResolveHashes()
   {
      static ShaderHashesList<> hashes;
      return hashes;
   }

   inline void RegisterShaderHashes()
   {
      DepthLinearizationHashes().pixel_shaders.emplace(hash_depth_linearization);
      HistoryReprojectionHashes().compute_shaders.emplace(hash_history_reprojection);
      TemporalResolveHashes().pixel_shaders.emplace(hash_temporal_resolve);

#if DEVELOPMENT
      forced_shader_names.emplace(hash_depth_linearization, "QB Depth Linearization (Clip Depth -> Linear Depth)");
      forced_shader_names.emplace(hash_history_reprojection, "QB History Reprojection (Motion Vectors)");
      forced_shader_names.emplace(hash_temporal_resolve, "QB Temporal Resolve / TAA");
      forced_shader_names.emplace(hash_msaa_gbuffer_reconstruction, "QB MSAA GBuffer Reconstruction Mask");
      forced_shader_names.emplace(hash_msaa_gbuffer_depth_resolve, "QB MSAA GBuffer Depth Resolve");
#endif
   }

   inline bool IsDepthLinearizationPass(const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      return original_shader_hashes.Contains(DepthLinearizationHashes());
   }

   inline bool IsHistoryReprojectionPass(const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      return original_shader_hashes.Contains(HistoryReprojectionHashes());
   }

   inline bool IsTemporalResolvePass(const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      return original_shader_hashes.Contains(TemporalResolveHashes());
   }

   constexpr float vertical_fov_fallback = 0.775934f; // ~44.46 degrees
   // Counted in valid scene temporal-resolve draws, not seconds. DLSS is held off for this many
   // resolves after startup/loading/no-scene periods so QB's AA-off scene transition can stabilize.
   constexpr uint32_t dlss_scene_warmup_resolve_count = 120u;
   // Byte offsets into QB's cb_update_1 cbuffer for the SR inputs that are not available from textures.
   constexpr uint32_t cb_update_1_inv_near_offset = 47u * 16u;
   constexpr uint32_t cb_update_1_view_to_clip_offset = 10u * 16u;
   constexpr uint32_t cb_update_1_tess_view_to_clip_11_offset = 112u * 16u + 12u;
   constexpr uint32_t cb_update_1_jitter_offset = 121u * 16u;
   constexpr uint32_t cb_update_1_min_size = cb_update_1_jitter_offset + sizeof(float) * 2u;
   // The temporal resolve samples current color with g_vSSAAJitterOffset[0], directly added to UVs.
   // Logged scene frames repeat a 4-sample raw UV pattern:
   //   ( 0.00014648, -0.00008681), (-0.00014648,  0.00008681),
   //   ( 0.00004883,  0.00026042), (-0.00004883, -0.00026042).
   // At 2560x1440, observed when QB's own upscaling is enabled for 4K output,
   // this becomes roughly (0.375, 0.125), (-0.375, -0.125),
   // (0.125, -0.375), (-0.125, 0.375) render pixels after the SR Y flip.
   constexpr uint32_t ssaa_jitter_offset = 12u * 16u;
   constexpr uint32_t ssaa_min_size = ssaa_jitter_offset + sizeof(float) * 2u;

   struct Data
   {
      bool debug_prev_had_motion_vectors = false;
      bool debug_prev_had_clip_depth = false;
      bool debug_prev_used_cached_clip_depth = false;

#if ENABLE_SR
      // Resources captured or created around the temporal resolve pass.
      // sr_clip_depth is captured from 0xA43343D6 PS SRV0, the pre-linearized clip/device depth.
      com_ptr<ID3D11Resource> sr_clip_depth;
      com_ptr<ID3D11Resource> sr_motion_vectors;
      com_ptr<ID3D11Buffer> cb_update_1_readback;
      com_ptr<ID3D11Buffer> ssaa_readback;
      // Conversion scratch texture: game gamma color -> DLSS linear input.
      com_ptr<ID3D11Texture2D> sr_linear_input_color;
      com_ptr<ID3D11ShaderResourceView> sr_linear_input_color_srv;
      com_ptr<ID3D11RenderTargetView> sr_linear_input_color_rtv;
      com_ptr<ID3D11ShaderResourceView> sr_output_color_srv;

      // Last captured frame constants that feed SR.
      float sr_jitter_x = 0.f;
      float sr_jitter_y = 0.f;
      float sr_cb_jitter_x = 0.f;
      float sr_cb_jitter_y = 0.f;
      float sr_render_pixel_jitter_x = 0.f;
      float sr_render_pixel_jitter_y = 0.f;
      float sr_vertical_fov = vertical_fov_fallback;
      float sr_near_plane = 0.1f;
      float sr_far_plane = 1000.f;

      // Per-resource history used to decide when DLSS history must reset.
      bool has_ssaa_data = false;
      bool output_changed = false;
      bool has_sr_clip_depth_desc = false;
      bool used_cached_clip_depth = false;
      bool has_previous_source_desc = false;
      bool has_previous_depth_desc = false;
      bool has_previous_motion_vectors_desc = false;
      D3D11_TEXTURE2D_DESC sr_clip_depth_desc = {};
      D3D11_TEXTURE2D_DESC previous_source_desc = {};
      D3D11_TEXTURE2D_DESC previous_depth_desc = {};
      D3D11_TEXTURE2D_DESC previous_motion_vectors_desc = {};
      uint32_t previous_render_width = 0u;
      uint32_t previous_render_height = 0u;
      uint32_t previous_output_width = 0u;
      uint32_t previous_output_height = 0u;
      // DLSS transition state. previous_sr_type detects switching into DLSS; dlss_scene_warmup_remaining
      // also gets refreshed by no-scene frames so DLSS does not start on the first unstable gameplay resolves.
      uint32_t dlss_scene_warmup_remaining = 0u;
      SR::Type previous_sr_type = SR::Type::None;
#endif
   };

   namespace Settings
   {
      constexpr float fsr_sharpness = 0.f;
      constexpr float mv_scale = 1.f;
      constexpr float jitter_scale = 1.f;

      inline void Initialize()
      {
      }

      inline void Load(reshade::api::effect_runtime* runtime)
      {
         (void)runtime;
      }

      inline void Draw(DeviceData& device_data, reshade::api::effect_runtime* runtime)
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

      inline void SetRenderData(uint32_t render_width, uint32_t render_height, uint32_t output_width, uint32_t output_height, float jitter_x, float jitter_y, DeviceData& device_data)
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

   struct TemporalResolveResult
   {
      bool requested = false;
      bool succeeded = false;
      bool stop_processing = false;
   };

   inline float ComputeVerticalFovFromProjectionScale(float projection_scale)
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
      // Loading/menu variants of QB's temporal resolve can expose non-scene constants that decode to a
      // technically positive but effectively zero FOV. Do not feed those into DLSS; keep the previous/fallback FOV.
      return (std::isfinite(fov) && fov >= 0.1f && fov < 3.13f) ? fov : 0.f;
   }

   inline bool HasTextureShapeChanged(const D3D11_TEXTURE2D_DESC& current_desc, const D3D11_TEXTURE2D_DESC& previous_desc)
   {
      return current_desc.Width != previous_desc.Width || current_desc.Height != previous_desc.Height || current_desc.Format != previous_desc.Format || current_desc.ArraySize != previous_desc.ArraySize || current_desc.MipLevels != previous_desc.MipLevels || current_desc.SampleDesc.Count != previous_desc.SampleDesc.Count || current_desc.SampleDesc.Quality != previous_desc.SampleDesc.Quality;
   }

   inline bool UpdatePreviousTextureDesc(const D3D11_TEXTURE2D_DESC& current_desc, D3D11_TEXTURE2D_DESC& previous_desc, bool& has_previous_desc)
   {
      // DLSS history must reset when any SR input/output texture shape changes.
      const bool changed = has_previous_desc && HasTextureShapeChanged(current_desc, previous_desc);
      previous_desc = current_desc;
      has_previous_desc = true;
      return changed;
   }

   template <typename T>
   inline T ReadCBufferValue(const uint8_t* base, uint32_t offset)
   {
      T value = {};
      std::memcpy(&value, base + offset, sizeof(T));
      return value;
   }

   inline void ReadCBufferFloat2(const uint8_t* base, uint32_t offset, float& x, float& y)
   {
      x = ReadCBufferValue<float>(base, offset);
      y = ReadCBufferValue<float>(base, offset + sizeof(float));
   }

   inline void OnInit()
   {
#if ENABLE_SR
      sr_game_tooltip = "Super Resolution engages during the temporal resolve pass.\n";
      // Native fullscreen pass bridges QB's gamma-space post stack with DLSS' preferred linear input.
      native_shaders_definitions.emplace(CompileTimeStringHash("QB Pre SR Decode"), ShaderDefinition{"Luma_QB_PreSRDecode", reshade::api::pipeline_subobject_type::pixel_shader});
#endif
   }

   inline bool IsRequested(const DeviceData& device_data)
   {
#if ENABLE_SR
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#else
      (void)device_data;
      return false;
#endif
   }

   inline void CaptureClipDepthFromLinearizationPass(ID3D11DeviceContext* native_device_context, Data& data)
   {
#if ENABLE_SR
      if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
      {
         return;
      }

      // 0xA43343D6 converts clip/device depth to linear depth:
      //   PS SRV0 = pre-linearized clip depth, viewed as r32_float on an r32_typeless resource.
      //   RTV0    = linearized r16_float depth output.
      // SR wants the PS SRV0 input, not the RTV0 output. The pass can run at full/half/quarter res,
      // so keep the largest depth seen this frame to avoid replacing the main depth with downscaled copies.
      com_ptr<ID3D11ShaderResourceView> clip_depth_srv;
      native_device_context->PSGetShaderResources(0, 1, &clip_depth_srv);
      if (!clip_depth_srv.get())
      {
         return;
      }

      com_ptr<ID3D11Resource> clip_depth_resource;
      clip_depth_srv->GetResource(&clip_depth_resource);
      if (!clip_depth_resource.get())
      {
         return;
      }

      com_ptr<ID3D11Texture2D> clip_depth_texture;
      if (FAILED(clip_depth_resource->QueryInterface(&clip_depth_texture)) || !clip_depth_texture.get())
      {
         return;
      }

      D3D11_TEXTURE2D_DESC clip_depth_desc = {};
      clip_depth_texture->GetDesc(&clip_depth_desc);
      if (clip_depth_desc.Width == 0u || clip_depth_desc.Height == 0u)
      {
         return;
      }

      const uint64_t current_area = static_cast<uint64_t>(clip_depth_desc.Width) * static_cast<uint64_t>(clip_depth_desc.Height);
      const uint64_t previous_area = data.has_sr_clip_depth_desc
                                        ? (static_cast<uint64_t>(data.sr_clip_depth_desc.Width) * static_cast<uint64_t>(data.sr_clip_depth_desc.Height))
                                        : 0ull;
      if (!data.sr_clip_depth.get() || current_area > previous_area)
      {
         data.sr_clip_depth = clip_depth_resource;
         data.sr_clip_depth_desc = clip_depth_desc;
         data.has_sr_clip_depth_desc = true;
      }
#else
      (void)native_device_context;
      (void)data;
#endif
   }

   inline void CaptureMotionVectors(ID3D11DeviceContext* native_device_context, Data& data)
   {
#if ENABLE_SR
      // The history reprojection pass has the motion-vector resource bound as CS SRV 0.
      com_ptr<ID3D11ShaderResourceView> motion_vectors_srv;
      native_device_context->CSGetShaderResources(0, 1, &motion_vectors_srv);
      if (motion_vectors_srv.get())
      {
         data.sr_motion_vectors = nullptr;
         motion_vectors_srv->GetResource(&data.sr_motion_vectors);
      }
#else
      (void)native_device_context;
      (void)data;
#endif
   }

#if ENABLE_SR
   inline bool MapPixelShaderConstantBufferForReadback(
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

   inline bool CaptureCBUpdate1Data(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Data& data)
   {
      // cb_update_1 supplies fallback jitter, projection scale, and near plane for SR.
      data.sr_cb_jitter_x = 0.f;
      data.sr_cb_jitter_y = 0.f;
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (!MapPixelShaderConstantBufferForReadback(native_device, native_device_context, 0, cb_update_1_min_size, data.cb_update_1_readback, mapped))
      {
         return false;
      }

      const auto* base = static_cast<const uint8_t*>(mapped.pData);
      ReadCBufferFloat2(base, cb_update_1_jitter_offset, data.sr_cb_jitter_x, data.sr_cb_jitter_y);
      const float inv_near = ReadCBufferValue<float>(base, cb_update_1_inv_near_offset);
      const float projection_scale_x = ReadCBufferValue<float>(base, cb_update_1_view_to_clip_offset);
      const float projection_scale_y = ReadCBufferValue<float>(base, cb_update_1_view_to_clip_offset + sizeof(float) * 5u);
      const float tess_view_to_clip_11 = ReadCBufferValue<float>(base, cb_update_1_tess_view_to_clip_11_offset);
      if (std::isfinite(inv_near) && inv_near > 0.f)
      {
         data.sr_near_plane = 1.f / inv_near;
      }

      data.sr_cb_jitter_x = std::isfinite(data.sr_cb_jitter_x) ? data.sr_cb_jitter_x : 0.f;
      data.sr_cb_jitter_y = std::isfinite(data.sr_cb_jitter_y) ? data.sr_cb_jitter_y : 0.f;

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
         data.sr_vertical_fov = vertical_fov;
      }

      native_device_context->Unmap(data.cb_update_1_readback.get(), 0);
      return true;
   }

   inline bool CaptureSSAAData(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, Data& data)
   {
      // Prefer the temporal resolve's SSAA jitter over cb_update_1. It is the exact UV offset
      // used for the current color/depth sample, and the captured values identify QB's 4-sample pattern.
      data.has_ssaa_data = false;
      data.sr_jitter_x = 0.f;
      data.sr_jitter_y = 0.f;

      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (!MapPixelShaderConstantBufferForReadback(native_device, native_device_context, 1, ssaa_min_size, data.ssaa_readback, mapped))
      {
         return false;
      }

      const auto* base = static_cast<const uint8_t*>(mapped.pData);
      ReadCBufferFloat2(base, ssaa_jitter_offset, data.sr_jitter_x, data.sr_jitter_y);
      data.sr_jitter_x = std::isfinite(data.sr_jitter_x) ? data.sr_jitter_x : 0.f;
      data.sr_jitter_y = std::isfinite(data.sr_jitter_y) ? data.sr_jitter_y : 0.f;

      data.has_ssaa_data = true;

      native_device_context->Unmap(data.ssaa_readback.get(), 0);
      return true;
   }

   inline bool SetupOutput(ID3D11Device* native_device, DeviceData& device_data, Data& data, const D3D11_TEXTURE2D_DESC& output_desc)
   {
      // DLSS writes linear color here; temporal_resolve encodes to gamma when SRType > 0.
      data.output_changed = false;
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
         data.output_changed = prev_desc.Width != sr_output_desc.Width || prev_desc.Height != sr_output_desc.Height || prev_desc.Format != sr_output_desc.Format;
      }

      if (!device_data.sr_output_color.get() || data.output_changed)
      {
         device_data.sr_output_color = nullptr;
         HRESULT hr = native_device->CreateTexture2D(&sr_output_desc, nullptr, &device_data.sr_output_color);
         if (FAILED(hr) || !device_data.sr_output_color.get())
         {
            return false;
         }

         recreated_output_texture = true;
      }

      if (!data.sr_output_color_srv.get() || data.output_changed || recreated_output_texture)
      {
         data.sr_output_color_srv = nullptr;
         HRESULT hr = native_device->CreateShaderResourceView(device_data.sr_output_color.get(), nullptr, &data.sr_output_color_srv);
         if (FAILED(hr) || !data.sr_output_color_srv.get())
         {
            return false;
         }
      }

      return true;
   }

   inline DXGI_FORMAT ResolveColorViewFormat(DXGI_FORMAT format)
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

   inline bool SetupConversionTexture(ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, com_ptr<ID3D11Texture2D>& texture, com_ptr<ID3D11ShaderResourceView>& srv, com_ptr<ID3D11RenderTargetView>& rtv)
   {
      // Scratch textures are simple single-mip render targets used by SR conversion passes.
      desc.Format = ResolveColorViewFormat(desc.Format);
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

   inline bool DrawConversionPass(ID3D11DeviceContext* native_device_context, DeviceData& device_data, uint32_t pixel_shader_hash, ID3D11ShaderResourceView* source_srv, ID3D11RenderTargetView* target_rtv, uint32_t width, uint32_t height)
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
#endif // ENABLE_SR

   inline TemporalResolveResult RunTemporalResolve(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, Data& data)
   {
      TemporalResolveResult result = {};
      result.requested = IsRequested(device_data);

#if ENABLE_SR
      data.used_cached_clip_depth = false;
      // Switching into DLSS is equivalent to a fresh scene start for DLSS history purposes.
      if (data.previous_sr_type != device_data.sr_type)
      {
         data.previous_sr_type = device_data.sr_type;
         data.dlss_scene_warmup_remaining = device_data.sr_type == SR::Type::DLSS ? dlss_scene_warmup_resolve_count : 0u;
      }

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

      bool captured_cb_update_1 = false;
      bool captured_ssaa = false;
      if (has_main_temporal_resolve_bindings)
      {
         // These constant buffers distinguish the real scene temporal resolve from transition variants
         // that use the same shader/resources but do not carry valid scene camera/jitter state.
         captured_cb_update_1 = CaptureCBUpdate1Data(native_device, native_device_context, data);
         captured_ssaa = CaptureSSAAData(native_device, native_device_context, data);
      }

      if (result.requested && has_main_temporal_resolve_bindings && !captured_cb_update_1 && !captured_ssaa)
      {
         // Loading-to-gameplay transition resolves can bind scene-looking resources without the scene
         // constant buffers. Do not run SR, and do not replay the draw through the post-SR resolve path.
         device_data.force_reset_sr = true;
         if (cb_luma_global_settings.SRType != 0u)
         {
            cb_luma_global_settings.SRType = 0u;
            device_data.cb_luma_global_settings_dirty = true;
         }
         result.stop_processing = true;
         return result;
      }

      if (result.requested && device_data.sr_type == SR::Type::DLSS && has_main_temporal_resolve_bindings && captured_cb_update_1 && captured_ssaa && data.dlss_scene_warmup_remaining > 0u)
      {
         // This is not a wall-clock delay: it consumes one count per valid scene temporal resolve.
         // During the warmup, QB's own temporal resolve runs normally and DLSS history is kept reset.
         --data.dlss_scene_warmup_remaining;
         device_data.force_reset_sr = true;
         result.requested = false;
         return result;
      }

      if (result.requested && immediate_context && data.sr_motion_vectors.get() && has_main_temporal_resolve_bindings)
      {
         if (ps_shader_resources[0].get() && ps_shader_resources[2].get() && render_target_views[0].get())
         {
            com_ptr<ID3D11Resource> source_color_resource;
            ps_shader_resources[2]->GetResource(&source_color_resource);

            // Fallback depth from temporal resolve PS SRV0. Shader 0x99274617 names this g_tClipDepth.
            // Prefer the cached 0xA43343D6 PS SRV0 resource below when it matches the main render size,
            // because that cache point explicitly proves the resource is pre-linearized depth.
            com_ptr<ID3D11Resource> temporal_resolve_depth_resource;
            ps_shader_resources[0]->GetResource(&temporal_resolve_depth_resource);

            // The temporal resolve RTV is the final SR output target size.
            com_ptr<ID3D11Resource> output_resource;
            render_target_views[0]->GetResource(&output_resource);

            com_ptr<ID3D11Texture2D> source_color_texture;
            com_ptr<ID3D11Texture2D> output_texture;
            com_ptr<ID3D11Texture2D> temporal_resolve_depth_texture;
            com_ptr<ID3D11Texture2D> motion_vectors_texture;

            const HRESULT source_hr = source_color_resource.get() ? source_color_resource->QueryInterface(&source_color_texture) : E_FAIL;
            const HRESULT output_hr = output_resource.get() ? output_resource->QueryInterface(&output_texture) : E_FAIL;
            const HRESULT temporal_depth_hr = temporal_resolve_depth_resource.get() ? temporal_resolve_depth_resource->QueryInterface(&temporal_resolve_depth_texture) : E_FAIL;
            const HRESULT motion_vectors_hr = data.sr_motion_vectors.get() ? data.sr_motion_vectors->QueryInterface(&motion_vectors_texture) : E_FAIL;

            if (SUCCEEDED(source_hr) && SUCCEEDED(output_hr) && SUCCEEDED(temporal_depth_hr) && SUCCEEDED(motion_vectors_hr) && source_color_texture.get() && output_texture.get() && temporal_resolve_depth_texture.get() && motion_vectors_texture.get())
            {
               // Descs drive DLSS settings, conversion texture allocation, and history reset decisions.
               D3D11_TEXTURE2D_DESC source_desc = {};
               D3D11_TEXTURE2D_DESC temporal_resolve_depth_desc = {};
               D3D11_TEXTURE2D_DESC depth_desc = {};
               D3D11_TEXTURE2D_DESC motion_vectors_desc = {};
               D3D11_TEXTURE2D_DESC output_desc = {};
               // Source/depth/MV descs bound the DLSS input resolution; output desc drives the upscaled target.
               source_color_texture->GetDesc(&source_desc);
               temporal_resolve_depth_texture->GetDesc(&temporal_resolve_depth_desc);
               motion_vectors_texture->GetDesc(&motion_vectors_desc);
               output_texture->GetDesc(&output_desc);

               ID3D11Resource* sr_depth_resource = temporal_resolve_depth_resource.get();
               depth_desc = temporal_resolve_depth_desc;

               // The depth-linearization pass can also produce half/quarter-res linear-depth copies.
               // Only use the cached clip-depth input if it matches the main color and MV size for this SR draw;
               // otherwise keep the temporal resolve PS SRV0 fallback, which is also named g_tClipDepth.
               if (data.sr_clip_depth.get() && data.has_sr_clip_depth_desc &&
                   data.sr_clip_depth_desc.Width == source_desc.Width &&
                   data.sr_clip_depth_desc.Height == source_desc.Height &&
                   data.sr_clip_depth_desc.Width == motion_vectors_desc.Width &&
                   data.sr_clip_depth_desc.Height == motion_vectors_desc.Height)
               {
                  sr_depth_resource = data.sr_clip_depth.get();
                  depth_desc = data.sr_clip_depth_desc;
                  data.used_cached_clip_depth = true;
               }

               const bool output_ready = SetupOutput(native_device, device_data, data, output_desc);
               if (output_ready)
               {
                  auto* sr_instance_data = device_data.GetSRInstanceData();
                  if (sr_instance_data)
                  {
                     // Use the smallest SR input texture so color, depth, and motion vectors all cover the full render area.
                     const uint32_t input_width = (std::min)(source_desc.Width, (std::min)(depth_desc.Width, motion_vectors_desc.Width));
                     const uint32_t input_height = (std::min)(source_desc.Height, (std::min)(depth_desc.Height, motion_vectors_desc.Height));
                     const uint32_t render_width = input_width;
                     const uint32_t render_height = input_height;

                     const uint32_t output_width = output_desc.Width;
                     const uint32_t output_height = output_desc.Height;
                     const float jitter_x = data.has_ssaa_data ? data.sr_jitter_x : data.sr_cb_jitter_x;
                     const float jitter_y = data.has_ssaa_data ? data.sr_jitter_y : data.sr_cb_jitter_y;

                     if (render_width == 0u || render_height == 0u || output_width == 0u || output_height == 0u)
                     {
                        device_data.force_reset_sr = true;
                        result.stop_processing = true;
                        return result;
                     }

                     // g_vSSAAJitterOffset[0] is a normalized UV offset; shader 0x99274617 adds it directly to sampling UVs.
                     // Convert UV -> input pixels for DLSS/FSR and flip Y to match SR convention. Do not apply
                     // projection/NDC-style *0.5f scaling here; that would make the logged pattern half-strength.
                     if (data.has_ssaa_data)
                     {
                        data.sr_render_pixel_jitter_x = jitter_x * static_cast<float>(render_width);
                        data.sr_render_pixel_jitter_y = -jitter_y * static_cast<float>(render_height);
                     }
                     else
                     {
                        // Fallback g_vJitterOffset is not the temporal resolve sample offset. Captured scene frames had it
                        // at zero, and other QB shaders add it to SV_Position, so treat it as already pixel-space if used.
                        data.sr_render_pixel_jitter_x = jitter_x;
                        data.sr_render_pixel_jitter_y = -jitter_y;
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
                     settings_data.mvs_x_scale = static_cast<float>(render_width) * Settings::mv_scale;
                     settings_data.mvs_y_scale = static_cast<float>(render_height) * Settings::mv_scale;
                     settings_data.render_preset = dlss_render_preset;

                     D3D11_SHADER_RESOURCE_VIEW_DESC source_srv_desc = {};
                     ps_shader_resources[2]->GetDesc(&source_srv_desc);

                     D3D11_TEXTURE2D_DESC sr_linear_input_desc = source_desc;
                     sr_linear_input_desc.Format = source_srv_desc.Format != DXGI_FORMAT_UNKNOWN ? source_srv_desc.Format : source_desc.Format;

                     const bool conversion_resources_ready =
                        SetupConversionTexture(native_device, sr_linear_input_desc, data.sr_linear_input_color, data.sr_linear_input_color_srv, data.sr_linear_input_color_rtv);

                     const bool settings_updated = conversion_resources_ready && sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
                     if (settings_updated)
                     {
                        // Reset DLSS history when any physical resource or logical render size changes.
                        const bool source_changed = UpdatePreviousTextureDesc(source_desc, data.previous_source_desc, data.has_previous_source_desc);
                        const bool depth_changed = UpdatePreviousTextureDesc(depth_desc, data.previous_depth_desc, data.has_previous_depth_desc);
                        const bool motion_vectors_changed = UpdatePreviousTextureDesc(motion_vectors_desc, data.previous_motion_vectors_desc, data.has_previous_motion_vectors_desc);
                        const bool render_size_changed = data.previous_render_width != 0u && data.previous_render_height != 0u && (data.previous_render_width != render_width || data.previous_render_height != render_height);
                        data.previous_render_width = render_width;
                        data.previous_render_height = render_height;
                        data.previous_output_width = output_width;
                        data.previous_output_height = output_height;

                        const bool reset_sr = device_data.force_reset_sr || data.output_changed || source_changed || depth_changed || motion_vectors_changed || render_size_changed;
                        device_data.force_reset_sr = false;

                        SR::SuperResolutionImpl::DrawData draw_data = {};
                        draw_data.source_color = data.sr_linear_input_color.get();
                        draw_data.output_color = device_data.sr_output_color.get();
                        draw_data.motion_vectors = data.sr_motion_vectors.get();
                        draw_data.depth_buffer = sr_depth_resource;
                        draw_data.pre_exposure = 1.f;
                        draw_data.jitter_x = data.sr_render_pixel_jitter_x * Settings::jitter_scale;
                        draw_data.jitter_y = data.sr_render_pixel_jitter_y * Settings::jitter_scale;
                        draw_data.vert_fov = (std::isfinite(data.sr_vertical_fov) && data.sr_vertical_fov >= 0.1f)
                                                ? data.sr_vertical_fov
                                                : vertical_fov_fallback;
                        draw_data.near_plane = data.sr_near_plane;
                        draw_data.far_plane = data.sr_far_plane;
                        draw_data.reset = reset_sr;
                        draw_data.render_width = render_width;
                        draw_data.render_height = render_height;
                        draw_data.user_sharpness = device_data.sr_type == SR::Type::FSR ? Settings::fsr_sharpness : -1.f;

                        DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
                        DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
                        draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
                        compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

                        // Feed DLSS linear color; temporal_resolve encodes SR output back to gamma-space.
                        const bool pre_sr_encoded = DrawConversionPass(
                           native_device_context,
                           device_data,
                           CompileTimeStringHash("QB Pre SR Decode"),
                           ps_shader_resources[2].get(),
                           data.sr_linear_input_color_rtv.get(),
                           source_desc.Width,
                           source_desc.Height);

                        bool sr_drawn = false;
                        if (pre_sr_encoded)
                        {
                           sr_drawn = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
                        }
                        result.succeeded = pre_sr_encoded && sr_drawn;

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

                        if (result.succeeded)
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
#else
      (void)native_device;
      (void)native_device_context;
      (void)data;
#endif

      return result;
   }

   inline void SetSRTypeForTemporalResolve(DeviceData& device_data, bool sr_succeeded)
   {
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
   }

   inline void ForceResetIfRequestedAndFailed(DeviceData& device_data, const TemporalResolveResult& result)
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

   inline void BindOutputToTemporalResolve(ID3D11DeviceContext* native_device_context, Data& data, bool sr_succeeded)
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

   inline void CleanResources(DeviceData& device_data, Data& data)
   {
#if ENABLE_SR
      // Drop all transient SR resources so the next valid scene frame rebuilds them and resets DLSS history.
      // If DLSS is currently selected, the rebuild is also treated as a fresh scene warmup.
      device_data.force_reset_sr = true;
      device_data.has_drawn_sr = false;

      data.sr_clip_depth = nullptr;
      data.sr_motion_vectors = nullptr;
      data.sr_linear_input_color = nullptr;
      data.sr_linear_input_color_srv = nullptr;
      data.sr_linear_input_color_rtv = nullptr;
      data.sr_output_color_srv = nullptr;
      data.output_changed = false;
      data.has_sr_clip_depth_desc = false;
      data.used_cached_clip_depth = false;
      data.sr_render_pixel_jitter_x = 0.f;
      data.sr_render_pixel_jitter_y = 0.f;

      data.has_previous_source_desc = false;
      data.has_previous_depth_desc = false;
      data.has_previous_motion_vectors_desc = false;
      data.sr_clip_depth_desc = {};
      data.previous_source_desc = {};
      data.previous_depth_desc = {};
      data.previous_motion_vectors_desc = {};
      data.previous_render_width = 0u;
      data.previous_render_height = 0u;
      data.previous_output_width = 0u;
      data.previous_output_height = 0u;
      data.dlss_scene_warmup_remaining = device_data.sr_type == SR::Type::DLSS ? dlss_scene_warmup_resolve_count : 0u;
#else
      (void)device_data;
      (void)data;
#endif
   }

   inline void OnPresent(DeviceData& device_data, Data& data)
   {
#if ENABLE_SR
      const bool had_clip_depth = data.sr_clip_depth.get() != nullptr;
      const bool had_motion_vectors = data.sr_motion_vectors.get() != nullptr;

      // If SR was requested but no scene resolve produced SR output, force a history reset for the next scene frame.
      if (device_data.sr_type != SR::Type::None && !device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }

      if (device_data.sr_type == SR::Type::DLSS && !device_data.has_drawn_sr && !had_clip_depth && !had_motion_vectors)
      {
         // Title/loading/UI-only frames do not provide a real scene depth/MV pair. Refresh the DLSS warmup
         // so DLSS waits for stable gameplay resolves once scene rendering resumes.
         data.dlss_scene_warmup_remaining = (std::max)(data.dlss_scene_warmup_remaining, dlss_scene_warmup_resolve_count);
      }

      data.debug_prev_had_clip_depth = data.sr_clip_depth.get() != nullptr;
      data.debug_prev_had_motion_vectors = data.sr_motion_vectors.get() != nullptr;
      data.debug_prev_used_cached_clip_depth = data.used_cached_clip_depth;
      data.sr_clip_depth = nullptr;
      data.sr_motion_vectors = nullptr;
      data.has_sr_clip_depth_desc = false;
      data.sr_clip_depth_desc = {};
      data.used_cached_clip_depth = false;
      data.output_changed = false;
#else
      data.debug_prev_had_clip_depth = false;
      data.debug_prev_had_motion_vectors = false;
      data.debug_prev_used_cached_clip_depth = false;
      (void)device_data;
#endif

      if (cb_luma_global_settings.SRType != 0u)
      {
         cb_luma_global_settings.SRType = 0u;
         device_data.cb_luma_global_settings_dirty = true;
      }
   }

   inline void DrawDebug(const Data& data, bool saw_history_reprojection_pass, bool saw_temporal_resolve_pass, bool had_scene_temporal_resolve_last_frame, uint32_t ui_only_frame_hold_counter)
   {
#if ENABLE_SR && (DEVELOPMENT || TEST)
      // Runtime SR status only; detailed cbuffer dumps are intentionally kept out of the user-facing UI.
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
            table_row_bool("History Reprojection Pass Seen:", saw_history_reprojection_pass);
            table_row_bool("Temporal Resolve Pass Seen:", saw_temporal_resolve_pass);
            table_row_bool("Clip Depth Captured:", data.debug_prev_had_clip_depth);
            table_row_bool("Motion Vectors Captured:", data.debug_prev_had_motion_vectors);
            table_row_bool("Cached Clip Depth Used For SR:", data.debug_prev_used_cached_clip_depth);
            table_row_bool("Had Scene Temporal Resolve Last Frame:", had_scene_temporal_resolve_last_frame);
            table_row_uint("UI-Only Hold Frames:", ui_only_frame_hold_counter);
            ImGui::EndTable();
         }
      }

      if (ImGui::CollapsingHeader("Active SR Inputs"))
      {
         if (begin_table("QB_SR_Debug_Active"))
         {
            table_row_uint("Last SR Render Width:", data.previous_render_width);
            table_row_uint("Last SR Render Height:", data.previous_render_height);
            table_row_uint("Last SR Output Width:", data.previous_output_width);
            table_row_uint("Last SR Output Height:", data.previous_output_height);
            table_row_bool("Using SSAA Jitter Source:", data.has_ssaa_data);
            table_row_float("Last Raw SSAA Jitter X:", data.sr_jitter_x);
            table_row_float("Last Raw SSAA Jitter Y:", data.sr_jitter_y);
            table_row_float("Last Raw CB Jitter X:", data.sr_cb_jitter_x);
            table_row_float("Last Raw CB Jitter Y:", data.sr_cb_jitter_y);
            table_row_float("Last SR Pixel Jitter X:", data.sr_render_pixel_jitter_x);
            table_row_float("Last SR Pixel Jitter Y:", data.sr_render_pixel_jitter_y);
            table_row_float("Active MV Scale Multiplier:", Settings::mv_scale);
            table_row_float("Active Jitter Scale Multiplier:", Settings::jitter_scale);
            ImGui::EndTable();
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
