#pragma once

// XeGTAO for Brutal Legend: replaces the game's AO chain input. Shaders: Shaders/Brutal Legend/Luma_BL_XeGTAO.hlsl.
// Output: full res R8_UNORM visibility (1 = unoccluded), denoised, with the intensity exponent applied.
// The game's AO upsample (0x51BA9E06) reads it at t5 when LumaSettings.GameSettings.XeGTAOActive > 0.5.
// Needs main.cpp to include the Luma core headers first (uses ComPtr, DeviceData, DrawStateStack).
//
// Shader registration (native_shaders_definitions, file "Luma_BL_XeGTAO", compute_shader):
//   "BL XeGTAO Prefilter CS"     -> "prefilter_depths16x16_cs"
//   "BL XeGTAO Main CS"          -> "main_pass_cs"
//   "BL XeGTAO Denoise CS"       -> "denoise_pass_cs"
//   "BL XeGTAO Denoise Final CS" -> "denoise_final_cs"
// Defines: XE_GTAO_QUALITY 2 (the shader defaults to it).

namespace BLXeGTAO
{
   // Matches the "LumaGTAO" cbuffer (b2) of Luma_BL_XeGTAO.hlsl
   struct Constants
   {
      uint32_t viewport_size[2] = {};
      float viewport_pixel_size[2] = {};
      float tan_half_fov_x = 1.12004f;
      float tan_half_fov_y = 0.630023f;
      float radius = 0.5f; // view space units (meters)
      float intensity = 1.f; // final visibility exponent, 1 = neutral
      float darkness = 1.f; // linear occlusion strength, 1 = neutral
      float use_gpu_fov = 0.f; // 1 = TanHalfFovX/Y come from the GPU copy of the game's cb4 row 17 (see CaptureFov)
      float pad[2] = {}; // constant buffer size must be a multiple of 16 bytes
   };
   static_assert(sizeof(Constants) == 48);

   struct Inputs
   {
      ID3D11ShaderResourceView* depth_srv = nullptr; // game's raw non-linear depth (R32_FLOAT, single sample)
      ID3D11Buffer* game_cb = nullptr; // game's PS constant buffer in slot 4 (row 16 = depth linearization), bound to b3
      float tan_half_fov_x = 1.12004f; // CPU fallback, used when no GPU FOV copy exists yet
      float tan_half_fov_y = 0.630023f;
      float radius = 0.5f;
      float intensity = 1.f;
      float darkness = 1.f;
      uint32_t width = 0;
      uint32_t height = 0;
   };

   // Fixed mip count of the depth prefilter (1 full res + 4 downsampled). Valid for any size >= 16.
   constexpr UINT depth_mip_count = 5;
   constexpr UINT min_dimension = 16;

   // Resolution dependent resources, recreated when width/height change
   struct Resources
   {
      ComPtr<ID3D11Texture2D> depth_texture; // R32_FLOAT, depth_mip_count mips
      ComPtr<ID3D11ShaderResourceView> depth_srv; // all mips
      std::vector<ComPtr<ID3D11UnorderedAccessView>> depth_uavs; // one per mip

      ComPtr<ID3D11Texture2D> ao_texture; // R8G8_UNORM (visibility, edges)
      ComPtr<ID3D11ShaderResourceView> ao_srv;
      ComPtr<ID3D11UnorderedAccessView> ao_uav;

      ComPtr<ID3D11Texture2D> denoised_texture; // R8G8_UNORM
      ComPtr<ID3D11ShaderResourceView> denoised_srv;
      ComPtr<ID3D11UnorderedAccessView> denoised_uav;

      ComPtr<ID3D11Texture2D> final_texture; // R8_UNORM, visibility
      ComPtr<ID3D11ShaderResourceView> final_srv;
      ComPtr<ID3D11UnorderedAccessView> final_uav;

      ComPtr<ID3D11Buffer> constant_buffer; // dynamic, Constants
      ComPtr<ID3D11Buffer> fov_buffer; // default, 16 bytes: game cb4 row 17 copied on the GPU (CaptureFov)
      bool fov_valid = false;
      UINT width = 0;
      UINT height = 0;

      void Release()
      {
         fov_valid = false;
         fov_buffer.reset();
         depth_uavs.clear();
         depth_srv.reset();
         depth_texture.reset();
         ao_uav.reset();
         ao_srv.reset();
         ao_texture.reset();
         denoised_uav.reset();
         denoised_srv.reset();
         denoised_texture.reset();
         final_uav.reset();
         final_srv.reset();
         final_texture.reset();
         constant_buffer.reset();
         width = 0;
         height = 0;
      }
   };

   inline bool CreateTexture(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format, UINT mip_count, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv, bool with_uav, ComPtr<ID3D11UnorderedAccessView>& uav)
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
      if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.put()))
         || FAILED(device->CreateShaderResourceView(texture.get(), nullptr, srv.put())))
         return false;
      return !with_uav || mip_count != 1 || SUCCEEDED(device->CreateUnorderedAccessView(texture.get(), nullptr, uav.put()));
   }

   // Builds a complete set before replacing the previous one. Returns false on any failure.
   inline bool PrepareResources(ID3D11Device* device, Resources& resources, UINT width, UINT height)
   {
      if (!resources.constant_buffer)
      {
         D3D11_BUFFER_DESC buffer_desc = {};
         buffer_desc.ByteWidth = sizeof(Constants);
         buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
         buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, resources.constant_buffer.put())))
            return false;
      }

      if (resources.width == width && resources.height == height && resources.depth_texture)
         return true;

      Resources next;
      next.constant_buffer = resources.constant_buffer;
      next.fov_buffer = resources.fov_buffer;
      next.fov_valid = resources.fov_valid;
      // The depth mips get one UAV each (below), the single mip targets get one UAV here
      ComPtr<ID3D11UnorderedAccessView> unused_uav;
      if (!CreateTexture(device, width, height, DXGI_FORMAT_R32_FLOAT, depth_mip_count, next.depth_texture, next.depth_srv, false, unused_uav)
         || !CreateTexture(device, width, height, DXGI_FORMAT_R8G8_UNORM, 1, next.ao_texture, next.ao_srv, true, next.ao_uav)
         || !CreateTexture(device, width, height, DXGI_FORMAT_R8G8_UNORM, 1, next.denoised_texture, next.denoised_srv, true, next.denoised_uav)
         || !CreateTexture(device, width, height, DXGI_FORMAT_R8_UNORM, 1, next.final_texture, next.final_srv, true, next.final_uav))
         return false;

      next.depth_uavs.resize(depth_mip_count);
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      for (UINT mip = 0; mip < depth_mip_count; ++mip)
      {
         uav_desc.Texture2D.MipSlice = mip;
         if (FAILED(device->CreateUnorderedAccessView(next.depth_texture.get(), &uav_desc, next.depth_uavs[mip].put())))
            return false;
      }

      next.width = width;
      next.height = height;
      resources = std::move(next);
      return true;
   }

   // Copies game cb4 row 17 (TanHalfFovX/Y in .xy) to a GPU buffer, so the compute shaders read it without a CPU readback.
   // Call from the shadow resolve draw, where that cbuffer is bound in slot 4.
   inline void CaptureFov(ID3D11Device* device, ID3D11DeviceContext* context, Resources& resources, ID3D11Buffer* game_cb)
   {
      if (!device || !context || !game_cb)
         return;
      D3D11_BUFFER_DESC desc = {};
      game_cb->GetDesc(&desc);
      if (desc.ByteWidth < 18 * 16)
         return;
      if (!resources.fov_buffer)
      {
         D3D11_BUFFER_DESC buffer_desc = {};
         buffer_desc.ByteWidth = 16;
         buffer_desc.Usage = D3D11_USAGE_DEFAULT;
         buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         if (FAILED(device->CreateBuffer(&buffer_desc, nullptr, resources.fov_buffer.put())))
            return;
      }
      D3D11_BOX box = {};
      box.left = 17 * 16;
      box.right = 18 * 16;
      box.top = 0;
      box.bottom = 1;
      box.front = 0;
      box.back = 1;
      context->CopySubresourceRegion(resources.fov_buffer.get(), 0, 0, 0, 0, game_cb, 0, &box);
      resources.fov_valid = true;
   }

   // Runs prefilter, main pass and the two denoise passes. On success "out_ao_srv" (if set) receives the R8_UNORM visibility,
   // valid until the next Draw or Release. Returns false before any game state is changed on failure.
   // Binds and restores the compute and graphics state it changes (the render targets are unbound while it runs).
   inline bool Draw(ID3D11Device* device, ID3D11DeviceContext* context, DeviceData& device_data, Resources& resources, const Inputs& inputs, ID3D11ShaderResourceView** out_ao_srv)
   {
      if (out_ao_srv) *out_ao_srv = nullptr;
      if (!device || !context || !inputs.depth_srv || inputs.width < min_dimension || inputs.height < min_dimension)
         return false;
      if (!inputs.game_cb)
         return false;
      D3D11_BUFFER_DESC game_cb_desc = {};
      inputs.game_cb->GetDesc(&game_cb_desc);
      if (game_cb_desc.ByteWidth < 17 * 16)
         return false;

      auto find_cs = [&](uint32_t name_hash) -> ID3D11ComputeShader*
      {
         const auto shader = device_data.native_compute_shaders.find(name_hash);
         return (shader == device_data.native_compute_shaders.end() || !shader->second) ? nullptr : shader->second.get();
      };
      ID3D11ComputeShader* prefilter_cs = find_cs(CompileTimeStringHash("BL XeGTAO Prefilter CS"));
      ID3D11ComputeShader* main_cs = find_cs(CompileTimeStringHash("BL XeGTAO Main CS"));
      ID3D11ComputeShader* denoise_cs = find_cs(CompileTimeStringHash("BL XeGTAO Denoise CS"));
      ID3D11ComputeShader* denoise_final_cs = find_cs(CompileTimeStringHash("BL XeGTAO Denoise Final CS"));
      if (!prefilter_cs || !main_cs || !denoise_cs || !denoise_final_cs)
         return false;
      if (!device_data.sampler_state_point)
         return false;

      const UINT width = inputs.width;
      const UINT height = inputs.height;
      if (!PrepareResources(device, resources, width, height))
         return false;

      Constants constants;
      constants.viewport_size[0] = width;
      constants.viewport_size[1] = height;
      constants.viewport_pixel_size[0] = 1.f / float(width);
      constants.viewport_pixel_size[1] = 1.f / float(height);
      constants.tan_half_fov_x = inputs.tan_half_fov_x;
      constants.tan_half_fov_y = inputs.tan_half_fov_y;
      constants.radius = inputs.radius;
      constants.intensity = inputs.intensity;
      constants.darkness = inputs.darkness;
      constants.use_gpu_fov = resources.fov_valid ? 1.f : 0.f;
      D3D11_MAPPED_SUBRESOURCE mapped;
      if (FAILED(context->Map(resources.constant_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
         return false;
      std::memcpy(mapped.pData, &constants, sizeof(Constants));
      context->Unmap(resources.constant_buffer.get(), 0);

      DrawStateStack<DrawStateStackType::FullGraphics> graphics_state;
      DrawStateStack<DrawStateStackType::Compute> compute_state;
      graphics_state.Cache(context, device_data.uav_max_count);
      compute_state.Cache(context, device_data.uav_max_count);

      ID3D11Buffer* const cb = resources.constant_buffer.get();
      ID3D11Buffer* const game_cb = inputs.game_cb;
      ID3D11Buffer* const fov_cb = resources.fov_buffer.get();
      ID3D11SamplerState* sampler = device_data.sampler_state_point.get();
      ID3D11ShaderResourceView* null_srv = nullptr;
      ID3D11UnorderedAccessView* null_uav = nullptr;
      ID3D11UnorderedAccessView* null_uavs[depth_mip_count] = {};

      context->OMSetRenderTargets(0, nullptr, nullptr);
      context->CSSetConstantBuffers(2, 1, &cb);
      context->CSSetConstantBuffers(3, 1, &game_cb);
      if (resources.fov_valid)
         context->CSSetConstantBuffers(4, 1, &fov_cb);
      context->CSSetSamplers(0, 1, &sampler);

      // 1. Prefilter: raw depth (t0) to the 5 linear depth mips (u0..u4)
      ID3D11ShaderResourceView* const raw_depth_srv = inputs.depth_srv;
      context->CSSetShaderResources(0, 1, &raw_depth_srv);
      ID3D11UnorderedAccessView* depth_uavs[depth_mip_count];
      for (UINT mip = 0; mip < depth_mip_count; ++mip)
         depth_uavs[mip] = resources.depth_uavs[mip].get();
      context->CSSetUnorderedAccessViews(0, depth_mip_count, depth_uavs, nullptr);
      context->CSSetShader(prefilter_cs, nullptr, 0);
      context->Dispatch((width + 15) / 16, (height + 15) / 16, 1);
      context->CSSetUnorderedAccessViews(0, depth_mip_count, null_uavs, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // 2. Main pass: depth mips (t0) to visibility + edges (u5)
      ID3D11ShaderResourceView* const depth_mips_srv = resources.depth_srv.get();
      context->CSSetShaderResources(0, 1, &depth_mips_srv);
      ID3D11UnorderedAccessView* const ao_uav = resources.ao_uav.get();
      context->CSSetUnorderedAccessViews(5, 1, &ao_uav, nullptr);
      context->CSSetShader(main_cs, nullptr, 0);
      context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
      context->CSSetUnorderedAccessViews(5, 1, &null_uav, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // 3. Denoise: visibility (t0) to denoised (u6), 2 pixels per thread
      ID3D11ShaderResourceView* const ao_srv = resources.ao_srv.get();
      context->CSSetShaderResources(0, 1, &ao_srv);
      ID3D11UnorderedAccessView* const denoised_uav = resources.denoised_uav.get();
      context->CSSetUnorderedAccessViews(6, 1, &denoised_uav, nullptr);
      context->CSSetShader(denoise_cs, nullptr, 0);
      context->Dispatch((width + 15) / 16, (height + 7) / 8, 1);
      context->CSSetUnorderedAccessViews(6, 1, &null_uav, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      // 4. Final denoise: denoised (t0) to the R8_UNORM visibility (u7), intensity applied
      ID3D11ShaderResourceView* const denoised_srv = resources.denoised_srv.get();
      context->CSSetShaderResources(0, 1, &denoised_srv);
      ID3D11UnorderedAccessView* const final_uav = resources.final_uav.get();
      context->CSSetUnorderedAccessViews(7, 1, &final_uav, nullptr);
      context->CSSetShader(denoise_final_cs, nullptr, 0);
      context->Dispatch((width + 15) / 16, (height + 7) / 8, 1);
      context->CSSetUnorderedAccessViews(7, 1, &null_uav, nullptr);
      context->CSSetShaderResources(0, 1, &null_srv);

      compute_state.Restore(context);
      graphics_state.Restore(context);

      if (out_ao_srv) *out_ao_srv = resources.final_srv.get();
      return true;
   }
}
