#pragma once

// XeGTAO for Hitman Absolution: replaces the game's SSAO draw (PS 0xF6310D28).
// Matches Luma_HMA_XeGTAO.hlsl. Two resolutions: full res (from the depth captured by PS 0x7BB58B6E this frame) and the half res fallback that writes RT0.
namespace HMAXeGTAO
{
   // Fixed mip count of the prefilter (5 levels: 1 full res + 4 downsampled). Valid for any resolution >= 16.
   constexpr UINT depth_mip_count = 5;
   constexpr UINT min_dimension = 16;

   // Matches Luma_HMA_XeGTAO.hlsl "LumaGTAO" (b2)
   struct Constants
   {
      uint32_t viewport_size[2] = {};
      float viewport_pixel_size[2] = {};
      float depth_scale = 1.f; // Set to -1 if the linear view depth comes out negative in testing
      float effect_radius = 0.5f; // In meters (view depth units)
      float final_value_power = 2.2f; // "Intensity"
      uint32_t linear_depth = 0; // The SRV holds device depth, linearized in the prefilter pass.
      float thickness = 0.5f; // GT-VBAO only: constant depth surface thickness in meters (view depth units)
      uint32_t padding[3] = {}; // Constant buffers are 16 byte aligned
   };
   static_assert(sizeof(Constants) == 48);

   struct Surface
   {
      ComPtr<ID3D11Texture2D> texture;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11UnorderedAccessView> uav; // Single mip surfaces only

      void Reset()
      {
         srv.reset();
         uav.reset();
         texture.reset();
      }
   };

   // Resolution dependent
   struct Resources
   {
      Surface depth; // R32_FLOAT, depth_mip_count mips (SRV on all mips, one UAV per mip)
      std::vector<ComPtr<ID3D11UnorderedAccessView>> depth_uavs;
      Surface ao; // R8G8_UNORM
      Surface denoised_ao; // R8G8_UNORM
      Surface final_ao; // R16G16B16A16_FLOAT, copied into RT0 (half res path)
      UINT width = 0;
      UINT height = 0;

      void Reset()
      {
         depth.Reset();
         depth_uavs = {};
         ao.Reset();
         denoised_ao.Reset();
         final_ao.Reset();
         width = 0;
         height = 0;
      }
   };

   struct Data
   {
      Constants gtao_constants;
      ComPtr<ID3D11Buffer> gtao_constant_buffer;
      Resources resources;
      // Full res output (R16G16B16A16_FLOAT, rgb = visibility), read by the AO consumer through t5 when valid this frame
      Surface fullres_ao;
      UINT fullres_width = 0;
      UINT fullres_height = 0;
   };

   inline void Clear(Data& data)
   {
      data.gtao_constant_buffer.reset();
      data.resources.Reset();
      data.fullres_ao.Reset();
      data.fullres_width = 0;
      data.fullres_height = 0;
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

      if (data.resources.width == width && data.resources.height == height)
         return true;

      // Build all resources before replacing the previous set. Failures leave the game draw intact.
      Resources next;
      if (!CreateSurface(device, width, height, DXGI_FORMAT_R32_FLOAT, next.depth, depth_mip_count)
         || !CreateSurface(device, width, height, DXGI_FORMAT_R8G8_UNORM, next.ao)
         || !CreateSurface(device, width, height, DXGI_FORMAT_R8G8_UNORM, next.denoised_ao)
         || !CreateSurface(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, next.final_ao))
         return false;

      next.depth_uavs.resize(depth_mip_count);
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

   // Full res output surface, recreated on size change.
   inline bool PrepareFullResOutput(ID3D11Device* device, Data& data, UINT width, UINT height)
   {
      if (data.fullres_ao.texture && data.fullres_width == width && data.fullres_height == height)
         return true;
      Surface next;
      if (!CreateSurface(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, next))
         return false;
      data.fullres_ao.Reset();
      data.fullres_ao = std::move(next);
      data.fullres_width = width;
      data.fullres_height = height;
      return true;
   }

   // The depth SRV must be a plain 2D R32_FLOAT texture (not an array, not MSAA) of at least min_dimension. Returns its size.
   inline bool GetDepthSize(ID3D11ShaderResourceView* depth_srv, UINT& width, UINT& height)
   {
      if (!depth_srv)
         return false;
      ComPtr<ID3D11Resource> depth_resource;
      depth_srv->GetResource(depth_resource.put());
      ComPtr<ID3D11Texture2D> depth_tex;
      if (!depth_resource || FAILED(depth_resource->QueryInterface(depth_tex.put())))
         return false;
      D3D11_TEXTURE2D_DESC depth_desc = {};
      depth_tex->GetDesc(&depth_desc);
      D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv_desc = {};
      depth_srv->GetDesc(&depth_srv_desc);
      if (depth_srv_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || depth_srv_desc.Format != DXGI_FORMAT_R32_FLOAT
         || depth_desc.ArraySize != 1 || depth_desc.SampleDesc.Count != 1)
         return false;
      width = depth_desc.Width;
      height = depth_desc.Height;
      return width >= min_dimension && height >= min_dimension;
   }

   // Runs the prefilter, main, denoise and final denoise passes on the device depth "depth_srv" (width x height) and writes the visibility into "output".
   // "camera_cb" is the game's per-view constant buffer (b1). If "copy_to_rt0" is set, the output is copied there while RT0 is still unbound.
   // Returns false on any failure before touching RT0 or the output.
   inline bool Run(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data, Data& data,
      ID3D11ShaderResourceView* depth_srv, ID3D11Buffer* camera_cb, UINT width, UINT height,
      float effect_radius, float final_value_power, bool vbao, float thickness, Surface& output, ID3D11Resource* copy_to_rt0)
   {
      auto find_cs = [&](uint32_t name_hash) -> ID3D11ComputeShader*
      {
         const auto shader = device_data.native_compute_shaders.find(name_hash);
         return (shader == device_data.native_compute_shaders.end() || !shader->second) ? nullptr : shader->second.get();
      };
      ID3D11ComputeShader* prefilter_cs = find_cs(CompileTimeStringHash("HMA XeGTAO Prefilter CS"));
      // GT-VBAO replaces the main pass only, the prefilter and the denoise passes are shared
      ID3D11ComputeShader* main_cs = find_cs(vbao ? CompileTimeStringHash("HMA XeGTAO Main VBAO CS") : CompileTimeStringHash("HMA XeGTAO Main CS"));
      ID3D11ComputeShader* denoise_cs = find_cs(CompileTimeStringHash("HMA XeGTAO Denoise CS"));
      ID3D11ComputeShader* denoise_final_cs = find_cs(CompileTimeStringHash("HMA XeGTAO Denoise Final CS"));
      if (!prefilter_cs || !main_cs || !denoise_cs || !denoise_final_cs)
         return false;
      if (!device_data.sampler_state_point)
         return false;

      if (!PrepareResources(device, data, width, height))
         return false;
      // "output" may be data.resources.final_ao, which PrepareResources just (re)created
      if (!output.uav)
         return false;
      const Resources& resources = data.resources;

      data.gtao_constants.viewport_size[0] = width;
      data.gtao_constants.viewport_size[1] = height;
      data.gtao_constants.viewport_pixel_size[0] = 1.f / float(width);
      data.gtao_constants.viewport_pixel_size[1] = 1.f / float(height);
      data.gtao_constants.effect_radius = effect_radius;
      data.gtao_constants.final_value_power = final_value_power;
      data.gtao_constants.thickness = thickness;
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(context->Map(data.gtao_constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
         return false;
      std::memcpy(mapped.pData, &data.gtao_constants, sizeof(Constants));
      context->Unmap(data.gtao_constant_buffer.get(), 0);

      // Back up graphics too: reading depth in CS can conflict with the current render targets.
      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(context, device_data.uav_max_count);
      compute_state.Cache(context, device_data.uav_max_count);

      ID3D11Buffer* const gtao_cb = data.gtao_constant_buffer.get();
      ID3D11Buffer* const view_cb = camera_cb;
      ID3D11SamplerState* sampler = device_data.sampler_state_point.get();
      ID3D11ShaderResourceView* null_srv = nullptr;
      ID3D11UnorderedAccessView* null_uav = nullptr;

      context->OMSetRenderTargets(0, nullptr, nullptr);
      context->CSSetConstantBuffers(1, 1, &view_cb);
      context->CSSetConstantBuffers(2, 1, &gtao_cb);
      context->CSSetSamplers(0, 1, &sampler);

      // 1. Prefilter: device depth (t0) to the 5 depth mips (u0..u4)
      ID3D11ShaderResourceView* const device_depth_srv = depth_srv;
      context->CSSetShaderResources(0, 1, &device_depth_srv);
      ID3D11UnorderedAccessView* depth_uavs[depth_mip_count];
      for (UINT mip = 0; mip < depth_mip_count; ++mip)
         depth_uavs[mip] = resources.depth_uavs[mip].get();
      context->CSSetUnorderedAccessViews(0, depth_mip_count, depth_uavs, nullptr);
      context->CSSetShader(prefilter_cs, nullptr, 0);
      context->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
      ID3D11UnorderedAccessView* null_uavs[depth_mip_count] = {};
      context->CSSetUnorderedAccessViews(0, depth_mip_count, null_uavs, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // 2. Main pass: depth mips (t0) to AO + edges (u5)
      ID3D11ShaderResourceView* const depth_mips_srv = resources.depth.srv.get();
      context->CSSetShaderResources(0, 1, &depth_mips_srv);
      ID3D11UnorderedAccessView* const ao_uav = resources.ao.uav.get();
      context->CSSetUnorderedAccessViews(5, 1, &ao_uav, nullptr);
      context->CSSetShader(main_cs, nullptr, 0);
      context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
      context->CSSetUnorderedAccessViews(5, 1, &null_uav, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // 3. Denoise: AO (t0) to denoised AO (u6), 2 pixels per thread
      ID3D11ShaderResourceView* const ao_srv = resources.ao.srv.get();
      context->CSSetShaderResources(0, 1, &ao_srv);
      ID3D11UnorderedAccessView* const denoised_uav = resources.denoised_ao.uav.get();
      context->CSSetUnorderedAccessViews(6, 1, &denoised_uav, nullptr);
      context->CSSetShader(denoise_cs, nullptr, 0);
      context->Dispatch((width + 15) / 16, (height + 7) / 8, 1);
      context->CSSetUnorderedAccessViews(6, 1, &null_uav, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // 4. Final denoise: denoised AO (t0) to the output R16G16B16A16_FLOAT target (u7)
      ID3D11ShaderResourceView* const denoised_srv = resources.denoised_ao.srv.get();
      context->CSSetShaderResources(0, 1, &denoised_srv);
      ID3D11UnorderedAccessView* const final_uav = output.uav.get();
      context->CSSetUnorderedAccessViews(7, 1, &final_uav, nullptr);
      context->CSSetShader(denoise_final_cs, nullptr, 0);
      context->Dispatch((width + 15) / 16, (height + 7) / 8, 1);
      context->CSSetUnorderedAccessViews(7, 1, &null_uav, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // Half res path only: the result replaces RT0 of the cancelled SSAO draw (rgb = visibility). Copied while RT0 is unbound.
      if (copy_to_rt0)
         context->CopyResource(copy_to_rt0, output.texture.get());

      compute_state.Restore(context);
      graphics_state.Restore(context);

      return true;
   }

   // Full res AO from the full res device depth captured this frame. Writes data.fullres_ao. RT0 is not touched.
   inline bool DrawFullRes(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data, Data& data,
      ID3D11ShaderResourceView* depth_srv, ID3D11Buffer* camera_cb, float effect_radius, float final_value_power, bool vbao, float thickness)
   {
      UINT width = 0;
      UINT height = 0;
      if (!camera_cb || !GetDepthSize(depth_srv, width, height))
         return false;
      if (!PrepareFullResOutput(device, data, width, height))
         return false;
      return Run(device, context, device_data, data, depth_srv, camera_cb, width, height, effect_radius, final_value_power, vbao, thickness, data.fullres_ao, nullptr);
   }

   // Half res AO: runs on the game's SSAO depth input (t0 of the SSAO draw) and copies the result into RT0.
   // "rt0_resource"/"rt0_desc" are the render target that the draw writes. Returns false before touching RT0 on any failure, so the game draw can run.
   inline bool Draw(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data, Data& data,
      ID3D11ShaderResourceView* depth_srv, ID3D11Buffer* camera_cb, ID3D11Resource* rt0_resource, const D3D11_TEXTURE2D_DESC& rt0_desc,
      float effect_radius, float final_value_power, bool vbao, float thickness)
   {
      UINT width = 0;
      UINT height = 0;
      if (!camera_cb || !rt0_resource || !GetDepthSize(depth_srv, width, height))
         return false;
      // RT0 must be a single mip R16G16B16A16_FLOAT target of the same size, as the final copy needs identical formats and subresources.
      if (rt0_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT || rt0_desc.SampleDesc.Count != 1 || rt0_desc.ArraySize != 1 || rt0_desc.MipLevels != 1
         || rt0_desc.Width != width || rt0_desc.Height != height)
         return false;

      return Run(device, context, device_data, data, depth_srv, camera_cb, width, height, effect_radius, final_value_power, vbao, thickness, data.resources.final_ao, rt0_resource);
   }
}
