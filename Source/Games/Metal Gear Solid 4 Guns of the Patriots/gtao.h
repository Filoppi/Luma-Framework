#pragma once

namespace MGS4GTAO
{
   constexpr UINT depth_mip_count = 5;
   // Matches Luma_MGS4_XeGTAO.hlsl
   constexpr UINT camera_constant_buffer_slot = 8;
   constexpr UINT constant_buffer_slot = 9;

   // Matches Luma_MGS4_XeGTAO.hlsl, b9.
   // The camera (projection) isn't in here, the shaders read it from the game's constant buffer (b8).
   struct Constants
   {
      uint32_t viewport_size[2] = {};
      float viewport_pixel_size[2] = {};
      float depth_scale = 1.f; // Game uses millimeters as unit but this is fine as 1.
      float effect_radius = 500.f; // In mm
      float final_value_power = 0.85f;
      uint32_t linear_depth = 0; // The hook supplies device depth; linearize in the prefilter pass.
      float radius_scaling_min_depth = 25000.f; // In mm
      float radius_scaling_max_depth = 100000.f;
      float radius_scaling_multiplier = 5.0f;
      float padding = 0.f;
   };
   static_assert(sizeof(Constants) == 48);

   struct Surface
   {
      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11UnorderedAccessView> uav; // Single mip surfaces only
   };

   // Resolution dependent
   struct Resources
   {
      Surface depth;
      std::array<ComPtr<ID3D11UnorderedAccessView>, depth_mip_count> depth_uavs;
      Surface ao;
      Surface denoised_ao;
      Surface final_ao;
      UINT width = 0;
      UINT height = 0;
   };

   struct Data
   {
      Constants constants;
      ComPtr<ID3D11Buffer> gtao_constant_buffer;
      // The game's opaque scene raster VS CB0, with the projection matrix in rows 4 to 7 (found in "OnDrawOrDispatch()").
      // It's searched again every frame, and the last one found is kept. The shaders read the camera from it directly.
      ComPtr<ID3D11Buffer> constant_buffer;
      bool found_constant_buffer = false;
      ComPtr<ID3D11BlendState> multiply_rgb_blend_state;
      Resources resources;
      uint32_t last_frame = UINT32_MAX;

      // The first translucency composition of a frame composes hair and decals onto the opaque scene (PS t0), into RTV 0.
      // The first draw that renders on the opaque scene is the opaque scene raster, which has the camera projection.
      // AO is written on the composed scene, right before the depth linearization draw that follows the composition.
      ComPtr<ID3D11Resource> scene_resource;
      ComPtr<ID3D11RenderTargetView> composed_scene_rtv;
      uint32_t scene_frame = UINT32_MAX;
   };

   inline void RegisterShaders()
   {
      shader_defines_data.push_back({ "XE_GTAO_QUALITY", '2', true, false, "0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4 });

      native_shaders_definitions.emplace("MGS4 GTAO Prefilter CS"_h, ShaderDefinition{ "Luma_MGS4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs" });
      native_shaders_definitions.emplace("MGS4 GTAO Main CS"_h, ShaderDefinition{ "Luma_MGS4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" });
      native_shaders_definitions.emplace("MGS4 GTAO Denoise CS"_h, ShaderDefinition{ "Luma_MGS4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", { { "XE_GTAO_FINAL_APPLY", "0" } } });
      native_shaders_definitions.emplace("MGS4 GTAO Denoise Final CS"_h, ShaderDefinition{ "Luma_MGS4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", { { "XE_GTAO_FINAL_APPLY", "1" } } });
      native_shaders_definitions.emplace("MGS4 GTAO Apply PS"_h, ShaderDefinition{ "Luma_MGS4_XeGTAO", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "apply_ps", { { "MGS4_GTAO_APPLY", "1" } } });
   }

   inline bool CreateSurface(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, Surface& surface, UINT mip_count = 1)
   {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = mip_count;
      desc.ArraySize = 1;
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      if (FAILED(device->CreateTexture2D(&desc, nullptr, surface.texture.put()))
         || FAILED(device->CreateShaderResourceView(surface.texture.get(), nullptr, surface.srv.put())))
         return false;
      // Mipped surfaces need a UAV per mip, created by the caller
      return mip_count != 1 || SUCCEEDED(device->CreateUnorderedAccessView(surface.texture.get(), nullptr, surface.uav.put()));
   }

   inline bool PrepareResources(ID3D11Device* device, Data& data, UINT width, UINT height)
   {
      if (!data.gtao_constant_buffer)
      {
         D3D11_BUFFER_DESC buffer_desc = {};
         buffer_desc.ByteWidth = sizeof(Constants);
         buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
         buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, data.gtao_constant_buffer.put())))
            return false;
      }
      if (!data.multiply_rgb_blend_state)
      {
         // Multiplies the target RGB by the source RGB, leaving alpha untouched
         D3D11_BLEND_DESC blend_desc = {};
         blend_desc.RenderTarget[0].BlendEnable = TRUE;
         blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
         blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_SRC_COLOR;
         blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
         blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
         blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
         blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
         blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
         if (FAILED(device->CreateBlendState(&blend_desc, data.multiply_rgb_blend_state.put())))
            return false;
      }

      if (data.resources.width == width && data.resources.height == height)
         return true;

      // Build all resources before replacing the previous set. Failures leave the game draw intact.
      Resources next;
      if (!CreateSurface(device, width, height, DXGI_FORMAT_R32_FLOAT, next.depth, depth_mip_count)
         || !CreateSurface(device, width, height, DXGI_FORMAT_R8G8_UNORM, next.ao)
         || !CreateSurface(device, width, height, DXGI_FORMAT_R8G8_UNORM, next.denoised_ao)
         || !CreateSurface(device, width, height, DXGI_FORMAT_R8_UNORM, next.final_ao))
         return false;

      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      for (UINT mip = 0; mip < depth_mip_count; ++mip)
      {
         uav_desc.Texture2D.MipSlice = mip;
         if (FAILED(device->CreateUnorderedAccessView(next.depth.texture.get(), &uav_desc, next.depth_uavs[mip].put())))
            return false;
      }
      next.width = width;
      next.height = height;
      data.resources = std::move(next);
      return true;
   }

   inline bool GetTextureDesc(ID3D11Resource* resource, D3D11_TEXTURE2D_DESC& desc)
   {
      ComPtr<ID3D11Texture2D> texture;
      if (!resource || FAILED(resource->QueryInterface(texture.put()))) return false;
      texture->GetDesc(&desc);
      // Can't run UAV on array or MS textures.
      return desc.ArraySize == 1 && desc.SampleDesc.Count == 1;
   }

   inline bool GetTextureDesc(ID3D11ShaderResourceView* srv, D3D11_TEXTURE2D_DESC& desc)
   {
      if (!srv) return false;
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc;
      srv->GetDesc(&view_desc);
      if (view_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || view_desc.Texture2D.MostDetailedMip != 0)
         return false;
      ComPtr<ID3D11Resource> resource;
      srv->GetResource(resource.put());
      return GetTextureDesc(resource.get(), desc);
   }

   // Call on the translucency composition draws. Only the first one of each frame is used.
   inline void OnComposeTranslucency(ID3D11DeviceContext* context, DeviceData& device_data, Data& data)
   {
      const uint32_t frame = cb_luma_global_settings.FrameIndex;
      if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || data.scene_frame == frame)
         return;

      ComPtr<ID3D11ShaderResourceView> scene_srv;
      context->PSGetShaderResources(0, 1, scene_srv.put());
      D3D11_TEXTURE2D_DESC scene_desc = {};
      if (!GetTextureDesc(scene_srv.get(), scene_desc)
         // Only draw on fullscreen buffers, avoiding accidental shadow maps or mirror views renders if possible
         || scene_desc.Width != UINT(device_data.render_resolution.x) || scene_desc.Height != UINT(device_data.render_resolution.y))
         return;

      // The opaque scene is what finds the camera constant buffer, independently of whether AO can be drawn on this composition's output below
      ComPtr<ID3D11Resource> scene_resource;
      scene_srv->GetResource(scene_resource.put());
      data.scene_resource = scene_resource;
      data.scene_frame = frame;
      data.found_constant_buffer = false; // Clear this flag so we search for it again in the next frame, or re-use the old alternatively
      data.composed_scene_rtv.reset();

      ComPtr<ID3D11RenderTargetView> rtv;
      context->OMGetRenderTargets(1, rtv.put(), nullptr);
      if (!rtv) return;
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      rtv->GetDesc(&rtv_desc);
      ComPtr<ID3D11Resource> rtv_resource;
      rtv->GetResource(rtv_resource.put());
      D3D11_TEXTURE2D_DESC rtv_texture_desc = {};
      if (rtv_desc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D || rtv_desc.Texture2D.MipSlice != 0
         || !GetTextureDesc(rtv_resource.get(), rtv_texture_desc)
         || rtv_texture_desc.Width != scene_desc.Width || rtv_texture_desc.Height != scene_desc.Height)
         return;

      data.composed_scene_rtv = rtv;
   }

   // Call on the depth linearization draws, before the game draws them.
   // The first one after the first translucency composition of a frame writes AO on the composed scene.
   inline bool Draw(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data, Data& data)
   {
      const uint32_t frame = cb_luma_global_settings.FrameIndex;
      if (context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || data.scene_frame != frame || data.last_frame == frame || !data.composed_scene_rtv)
         return false;
      // Only try on the first one, the following ones aren't directly after the composition
      data.last_frame = frame;

      // The camera isn't known until the game's constant buffer has been found once
      if (!data.constant_buffer)
         return false;

      // PS t0 is the device depth this draw linearizes
      ComPtr<ID3D11ShaderResourceView> depth_srv;
      context->PSGetShaderResources(0, 1, depth_srv.put());
      D3D11_TEXTURE2D_DESC depth_desc = {};
      if (!GetTextureDesc(depth_srv.get(), depth_desc) || depth_desc.Width != UINT(device_data.render_resolution.x) || depth_desc.Height != UINT(device_data.render_resolution.y))
         return false;

      constexpr uint32_t shader_names[] = { "MGS4 GTAO Prefilter CS"_h, "MGS4 GTAO Main CS"_h, "MGS4 GTAO Denoise CS"_h, "MGS4 GTAO Denoise Final CS"_h };
      ID3D11ComputeShader* shaders[4] = {};
      for (UINT i = 0; i < 4; ++i)
      {
         const auto shader = device_data.native_compute_shaders.find(shader_names[i]);
         if (shader == device_data.native_compute_shaders.end() || !shader->second) return false;
         shaders[i] = shader->second.get();
      }
      if (!device_data.sampler_state_point) return false;
      const auto copy_vs = device_data.native_vertex_shaders.find("Copy VS"_h);
      const auto apply_ps = device_data.native_pixel_shaders.find("MGS4 GTAO Apply PS"_h);
      if (copy_vs == device_data.native_vertex_shaders.end() || !copy_vs->second
         || apply_ps == device_data.native_pixel_shaders.end() || !apply_ps->second) return false;

      if (!PrepareResources(device, data, UINT(device_data.render_resolution.x), UINT(device_data.render_resolution.y))) return false;
      const Resources& resources = data.resources;

      // Luma: the Luma Settings CB already has these but whatever, it won't hurt
      data.constants.viewport_size[0] = resources.width;
      data.constants.viewport_size[1] = resources.height;
      data.constants.viewport_pixel_size[0] = 1.f / float(resources.width);
      data.constants.viewport_pixel_size[1] = 1.f / float(resources.height);
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(context->Map(data.gtao_constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
      std::memcpy(mapped.pData, &data.constants, sizeof(Constants));
      context->Unmap(data.gtao_constant_buffer.get(), 0);

      // Back up graphics too: reading depth in CS can conflict with the current DSV.
      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(context, device_data.uav_max_count);
      compute_state.Cache(context, device_data.uav_max_count);

      context->OMSetRenderTargets(0, nullptr, nullptr);
      ID3D11Buffer* const camera_cb = data.constant_buffer.get();
      context->CSSetConstantBuffers(camera_constant_buffer_slot, 1, &camera_cb);
      ID3D11Buffer* const cb = data.gtao_constant_buffer.get();
      context->CSSetConstantBuffers(constant_buffer_slot, 1, &cb);
      ID3D11SamplerState* sampler = device_data.sampler_state_point.get();
      context->CSSetSamplers(0, 1, &sampler);
      ID3D11UnorderedAccessView* depth_uavs[depth_mip_count];
      for (UINT mip = 0; mip < depth_mip_count; ++mip) depth_uavs[mip] = resources.depth_uavs[mip].get();
      ID3D11UnorderedAccessView* null_uavs[depth_mip_count] = {};
      ID3D11ShaderResourceView* null_srvs[3] = {};

      ID3D11ShaderResourceView* const device_depth_srv = depth_srv.get();
      context->CSSetShaderResources(0, 1, &device_depth_srv);
      context->CSSetUnorderedAccessViews(0, depth_mip_count, depth_uavs, nullptr);
      context->CSSetShader(shaders[0], nullptr, 0);
      context->Dispatch((resources.width + 15) / 16, (resources.height + 15) / 16, 1);
      context->CSSetUnorderedAccessViews(0, depth_mip_count, null_uavs, nullptr);

      // Output slots match Luma_MGS4_XeGTAO.hlsl
      constexpr UINT main_pass_uav_slot = 5;
      constexpr UINT denoise_uav_slot = 6;
      auto Dispatch = [&](ID3D11ComputeShader* shader, ID3D11ShaderResourceView* input, UINT output_slot, ID3D11UnorderedAccessView* output, UINT pixels_per_group_x)
      {
         context->CSSetShaderResources(0, 1, &input);
         context->CSSetUnorderedAccessViews(output_slot, 1, &output, nullptr);
         context->CSSetShader(shader, nullptr, 0);
         context->Dispatch((resources.width + pixels_per_group_x - 1) / pixels_per_group_x, (resources.height + 7) / 8, 1);
         context->CSSetUnorderedAccessViews(output_slot, 1, null_uavs, nullptr);
      };
      Dispatch(shaders[1], resources.depth.srv.get(), main_pass_uav_slot, resources.ao.uav.get(), 8);
      Dispatch(shaders[2], resources.ao.srv.get(), denoise_uav_slot, resources.denoised_ao.uav.get(), 16);
      Dispatch(shaders[3], resources.denoised_ao.srv.get(), denoise_uav_slot, resources.final_ao.uav.get(), 16);
      context->CSSetShaderResources(0, 3, null_srvs);

      // Multiply AO into the composed scene RGB. Its alpha is the scale the game divides RGB by, so it's left untouched.
      ID3D11ShaderResourceView* const linear_depth_srv = resources.depth.srv.get();
      context->PSSetShaderResources(1, 1, &linear_depth_srv);
      // Reuse the opaque VS fog parameters and the depth scale in the final AO application.
      context->PSSetConstantBuffers(camera_constant_buffer_slot, 1, &camera_cb);
      context->PSSetConstantBuffers(constant_buffer_slot, 1, &cb);
      DrawCustomPixelShader(context, device_data.default_depth_stencil_state.get(), data.multiply_rgb_blend_state.get(), nullptr,
         copy_vs->second.get(), apply_ps->second.get(), resources.final_ao.srv.get(), data.composed_scene_rtv.get(), resources.width, resources.height);

      compute_state.Restore(context);
      graphics_state.Restore(context);

      return true;
   }
}