#pragma once

#include "shared.h"

// Quantum Break's SR hook sits inside the temporal resolve path:
// history reprojection provides motion vectors, temporal resolve provides color/depth/cbuffer inputs,
// and a fullscreen pre-decode pass lets DLSS run on linear color while the game remains gamma-space.
namespace QuantumBreakUpscaling
{
   // Pass hashes used to identify the two parts of QB's temporal pipeline we need to observe/replace.
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
      HistoryReprojectionHashes().compute_shaders.emplace(std::stoul("E8337D48", nullptr, 16));
      TemporalResolveHashes().pixel_shaders.emplace(std::stoul("99274617", nullptr, 16));
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
   // Byte offsets into QB's cb_update_1 cbuffer for the SR inputs that are not available from textures.
   constexpr uint32_t cb_update_1_inv_near_offset = 47u * 16u;
   constexpr uint32_t cb_update_1_view_to_clip_offset = 10u * 16u;
   constexpr uint32_t cb_update_1_tess_view_to_clip_11_offset = 112u * 16u + 12u;
   constexpr uint32_t cb_update_1_jitter_offset = 121u * 16u;
   constexpr uint32_t cb_update_1_min_size = cb_update_1_jitter_offset + sizeof(float) * 2u;
   // The temporal resolve samples current color with g_vSSAAJitterOffset[0].
   constexpr uint32_t ssaa_jitter_offset = 12u * 16u;
   constexpr uint32_t ssaa_min_size = ssaa_jitter_offset + sizeof(float) * 2u;

   struct Data
   {
      bool debug_prev_had_motion_vectors = false;

#if ENABLE_SR
      // Resources captured or created around the temporal resolve pass.
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
      float sr_vertical_fov = vertical_fov_fallback;
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
      return (std::isfinite(fov) && fov > 0.f && fov < 3.13f) ? fov : 0.f;
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
      // The temporal resolve samples current color with g_vSSAAJitterOffset[0], so keep this as the jitter source.
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
         CaptureCBUpdate1Data(native_device, native_device_context, data);
         CaptureSSAAData(native_device, native_device_context, data);
      }

      if (result.requested && immediate_context && data.sr_motion_vectors.get() && has_main_temporal_resolve_bindings)
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
            const HRESULT motion_vectors_hr = data.sr_motion_vectors.get() ? data.sr_motion_vectors->QueryInterface(&motion_vectors_texture) : E_FAIL;

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

               if (SetupOutput(native_device, device_data, data, output_desc))
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
                     const float jitter_x = data.has_ssaa_data ? data.sr_jitter_x : data.sr_cb_jitter_x;
                     const float jitter_y = data.has_ssaa_data ? data.sr_jitter_y : data.sr_cb_jitter_y;

                     if (render_width == 0u || render_height == 0u || output_width == 0u || output_height == 0u)
                     {
                        device_data.force_reset_sr = true;
                        result.stop_processing = true;
                        return result;
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
                        draw_data.depth_buffer = depth_resource.get();
                        draw_data.pre_exposure = 1.f;
                        draw_data.jitter_x = jitter_x * Settings::jitter_scale;
                        draw_data.jitter_y = jitter_y * Settings::jitter_scale;
                        draw_data.vert_fov = (std::isfinite(data.sr_vertical_fov) && data.sr_vertical_fov > 0.f)
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

                        result.succeeded = pre_sr_encoded && sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

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
      device_data.force_reset_sr = true;
      device_data.has_drawn_sr = false;

      data.sr_motion_vectors = nullptr;
      data.sr_linear_input_color = nullptr;
      data.sr_linear_input_color_srv = nullptr;
      data.sr_linear_input_color_rtv = nullptr;
      data.sr_output_color_srv = nullptr;
      data.output_changed = false;

      data.has_previous_source_desc = false;
      data.has_previous_depth_desc = false;
      data.has_previous_motion_vectors_desc = false;
      data.previous_source_desc = {};
      data.previous_depth_desc = {};
      data.previous_motion_vectors_desc = {};
      data.previous_render_width = 0u;
      data.previous_render_height = 0u;
      data.previous_output_width = 0u;
      data.previous_output_height = 0u;
#else
      (void)device_data;
      (void)data;
#endif
   }

   inline void OnPresent(DeviceData& device_data, Data& data)
   {
#if ENABLE_SR
      // If SR was requested but no scene resolve produced SR output, force a history reset for the next scene frame.
      if (device_data.sr_type != SR::Type::None && !device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }

      data.debug_prev_had_motion_vectors = data.sr_motion_vectors.get() != nullptr;
      data.sr_motion_vectors = nullptr;
      data.output_changed = false;
#else
      data.debug_prev_had_motion_vectors = false;
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
            table_row_bool("Motion Vectors Captured:", data.debug_prev_had_motion_vectors);
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
