static bool ExtractTAAShaderResources(
   ID3D11DeviceContext* native_device_context,
   GameDeviceDataGBFR& game_device_data)
{
   ComPtr<ID3D11ShaderResourceView> ps_shader_resources[24];
   native_device_context->PSGetShaderResources(0, 24, ps_shader_resources[0].put());

   if (!ps_shader_resources[3].get() || !ps_shader_resources[23].get())
   {
      return false;
   }
   game_device_data.sr_source_color_srv = ps_shader_resources[3];
   ps_shader_resources[3]->GetResource(game_device_data.sr_source_color.put());

   if (ps_shader_resources[5].get())
   {
      ps_shader_resources[5]->GetResource(game_device_data.depth_buffer.put());
   }

   game_device_data.sr_motion_vectors = nullptr;
   ps_shader_resources[23]->GetResource(game_device_data.sr_motion_vectors.put());

   return game_device_data.sr_source_color.get() != nullptr &&
          game_device_data.sr_motion_vectors.get() != nullptr;
}

static bool SetupSROutput(
   ID3D11Device* native_device,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11RenderTargetView* rtv)
{
   game_device_data.output_changed = false;
   game_device_data.output_supports_uav = false;

   HRESULT hr;
   game_device_data.taa_output_texture_rtv = rtv;
   ID3D11Resource* taa_resource;
   game_device_data.taa_output_texture_rtv->GetResource(&taa_resource);
   hr = taa_resource->QueryInterface(game_device_data.taa_output_texture.put());

   if (FAILED(hr))
      return false;

   D3D11_TEXTURE2D_DESC out_texture_desc;

   game_device_data.taa_output_texture->GetDesc(&out_texture_desc);

   out_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

   auto* sr_instance_data = device_data.GetSRInstanceData();
   if (sr_instance_data)
   {
      if (out_texture_desc.Width < sr_instance_data->min_resolution ||
          out_texture_desc.Height < sr_instance_data->min_resolution)
         return false;
   }

   game_device_data.output_changed =
      CreateOrRecreateTextureIfNeeded(game_device_data, native_device, out_texture_desc, game_device_data.sr_output_color, game_device_data.sr_output_color_srv);

   return true;
}

static bool DrawNativePreSREncodePass(
   ID3D11Device* native_device,
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Pre SR Encode"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second ||
       !game_device_data.sr_source_color.get() || !game_device_data.sr_source_color_srv.get())
   {
      return false;
   }

   ComPtr<ID3D11Texture2D> input_texture;
   if (FAILED(game_device_data.sr_source_color->QueryInterface(input_texture.put())) || !input_texture)
   {
      return false;
   }
   constexpr bool do_safety_checks = false;
   SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
   SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   D3D11_TEXTURE2D_DESC input_desc = {};
   input_texture->GetDesc(&input_desc);

   D3D11_SHADER_RESOURCE_VIEW_DESC input_srv_desc = {};
   game_device_data.sr_source_color_srv->GetDesc(&input_srv_desc);

   D3D11_TEXTURE2D_DESC output_desc = input_desc;
   output_desc.Format = input_srv_desc.Format != DXGI_FORMAT_UNKNOWN ? input_srv_desc.Format : input_desc.Format;
   output_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
   output_desc.CPUAccessFlags = 0;
   output_desc.Usage = D3D11_USAGE_DEFAULT;
   output_desc.MiscFlags = 0;
   output_desc.MipLevels = 1;
   output_desc.ArraySize = 1;

   CreateOrRecreateTextureIfNeeded(
      game_device_data,
      native_device,
      output_desc,
      game_device_data.pre_sr_encode_texture,
      game_device_data.pre_sr_encode_srv,
      game_device_data.pre_sr_encode_rtv);

   if (!game_device_data.pre_sr_encode_texture || !game_device_data.pre_sr_encode_rtv)
   {
      return false;
   }

   // H1: Reset render state that may be left over from the TUP/TAA deferred command list.
   // DrawNativePreSREncodePass runs on the immediate context after partial_command_list replay;
   // the predecessor draw (TUP or TAA) may have an active blend mode, depth-stencil test, scissor
   // rect, or zero write-mask that would corrupt pre_sr_encode_rtv output.
   ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
   ctx->OMSetDepthStencilState(nullptr, 0);
   ctx->RSSetState(nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.Width = static_cast<float>(output_desc.Width);
   viewport.Height = static_cast<float>(output_desc.Height);
   viewport.MinDepth = 0.0f;
   viewport.MaxDepth = 1.0f;
   ctx->RSSetViewports(1, &viewport);

   ID3D11ShaderResourceView* const input_srv = game_device_data.sr_source_color_srv.get();
   ctx->PSSetShaderResources(0, 1, &input_srv);

   ID3D11SamplerState* samplers[2] = {
      device_data.sampler_state_linear.get(),
      device_data.sampler_state_linear.get()};
   ctx->PSSetSamplers(0, 2, samplers);

   ID3D11RenderTargetView* const output_rtv = game_device_data.pre_sr_encode_rtv.get();
   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);

   ctx->IASetInputLayout(nullptr);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   ctx->VSSetShader(vs_it->second.get(), nullptr, 0);
   ctx->PSSetShader(ps_it->second.get(), nullptr, 0);
   ctx->Draw(4, 0);

   // H2: Unbind pre_sr_encode_rtv before FSR reads pre_sr_encode_texture as a CS input.
   // Leaving the RTV bound triggers a D3D11 resource hazard unbind that can interfere with
   // FSR's internal state management.
   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ComPtr<ID3D11Resource> output_resource;
   game_device_data.pre_sr_encode_rtv->GetResource(output_resource.put());
   game_device_data.sr_source_color = output_resource;
   game_device_data.sr_source_color_srv = game_device_data.pre_sr_encode_srv;
   return true;
}

bool SetupTempTAAOutput(ID3D11Device* native_device, GameDeviceDataGBFR& game_device_data, ID3D11RenderTargetView* rtv)
{
   if (!native_device || !rtv)
   {
      game_device_data.taa_output_texture = nullptr;
      game_device_data.taa_output_texture_rtv = nullptr;
      game_device_data.taa_temp_output_resource = nullptr;
      game_device_data.taa_temp_output_srv = nullptr;
      game_device_data.taa_temp_output_rtv = nullptr;
      return false;
   }

   game_device_data.taa_output_texture_rtv = rtv;
   ComPtr<ID3D11Resource> taa_resource;
   game_device_data.taa_output_texture_rtv->GetResource(taa_resource.put());
   if (!taa_resource)
   {
      game_device_data.taa_output_texture = nullptr;
      game_device_data.taa_temp_output_resource = nullptr;
      game_device_data.taa_temp_output_srv = nullptr;
      game_device_data.taa_temp_output_rtv = nullptr;
      return false;
   }

   const HRESULT hr = taa_resource->QueryInterface(game_device_data.taa_output_texture.put());
   if (FAILED(hr) || !game_device_data.taa_output_texture)
   {
      game_device_data.taa_output_texture = nullptr;
      game_device_data.taa_temp_output_resource = nullptr;
      game_device_data.taa_temp_output_srv = nullptr;
      game_device_data.taa_temp_output_rtv = nullptr;
      return false;
   }

   D3D11_TEXTURE2D_DESC taa_rt1_desc;
   game_device_data.taa_output_texture->GetDesc(&taa_rt1_desc);
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device, taa_rt1_desc, game_device_data.taa_temp_output_resource, game_device_data.taa_temp_output_srv, game_device_data.taa_temp_output_rtv);

   return game_device_data.taa_temp_output_resource != nullptr &&
          game_device_data.taa_temp_output_srv != nullptr &&
          game_device_data.taa_temp_output_rtv != nullptr;
}

static bool DrawNativePostSREncodePass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_srv,
   ID3D11ShaderResourceView* alpha_source_srv = nullptr)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Post SR Encode"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second ||
       !input_srv)
   {
      return false;
   }

   ComPtr<ID3D11Texture2D> input_texture;
   ComPtr<ID3D11Resource> input_resource;
   input_srv->GetResource(input_resource.put());
   if (FAILED(input_resource->QueryInterface(input_texture.put())) || !input_texture)
   {
      return false;
   }

   D3D11_TEXTURE2D_DESC input_desc = {};
   input_texture->GetDesc(&input_desc);

   D3D11_SHADER_RESOURCE_VIEW_DESC input_srv_desc = {};
   input_srv->GetDesc(&input_srv_desc);

   D3D11_TEXTURE2D_DESC output_desc = input_desc;
   output_desc.Format = input_srv_desc.Format != DXGI_FORMAT_UNKNOWN ? input_srv_desc.Format : input_desc.Format;
   output_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
   output_desc.CPUAccessFlags = 0;
   output_desc.Usage = D3D11_USAGE_DEFAULT;
   output_desc.MiscFlags = 0;
   output_desc.MipLevels = 1;
   output_desc.ArraySize = 1;

   ComPtr<ID3D11Device> native_device;
   ctx->GetDevice(native_device.put());
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device.get(), output_desc, game_device_data.post_sr_encode_texture, game_device_data.post_sr_encode_srv, game_device_data.post_sr_encode_rtv);

   if (!game_device_data.post_sr_encode_texture || !game_device_data.post_sr_encode_rtv)
   {
      return false;
   }

   constexpr bool do_safety_checks = false;
   SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
   SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ID3D11ShaderResourceView* srvs[] = {input_srv, alpha_source_srv ? alpha_source_srv : input_srv};
   ctx->PSSetShaderResources(0, 2, srvs);

   ID3D11SamplerState* samplers[2] = {
      device_data.sampler_state_linear.get(),
      device_data.sampler_state_linear.get()};
   ctx->PSSetSamplers(0, 2, samplers);

   D3D11_VIEWPORT viewport = {};
   viewport.Width = static_cast<float>(output_desc.Width);
   viewport.Height = static_cast<float>(output_desc.Height);
   viewport.MinDepth = 0.0f;
   viewport.MaxDepth = 1.0f;
   ctx->RSSetViewports(1, &viewport);

   ID3D11RenderTargetView* const output_rtv = game_device_data.post_sr_encode_rtv.get();
   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);

   ctx->IASetInputLayout(nullptr);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   ctx->VSSetShader(vs_it->second.get(), nullptr, 0);
   ctx->PSSetShader(ps_it->second.get(), nullptr, 0);
   ctx->Draw(4, 0);

   return true;
}

static bool DrawNativeUIEncodePass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_color_srv)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR UI Encode"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second ||
       !CanDrawNativeUIEncodePass(input_color_srv, game_device_data))
   {
      return false;
   }

   constexpr bool do_safety_checks = false;
   SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
   SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ctx->PSSetShaderResources(0, 1, &input_color_srv);

   ID3D11SamplerState* const sampler = device_data.sampler_state_linear.get();
   ctx->PSSetSamplers(0, 1, &sampler);

   D3D11_VIEWPORT viewport = {};
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   ctx->RSSetViewports(1, &viewport);

   ID3D11RenderTargetView* const output_rtv = game_device_data.taa_output_texture_rtv.get();
   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);

   ctx->IASetInputLayout(nullptr);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   ctx->VSSetShader(vs_it->second.get(), nullptr, 0);
   ctx->PSSetShader(ps_it->second.get(), nullptr, 0);
   ctx->Draw(4, 0);
   return true;
}
