static bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture)
{
   bool needs_creation = false;

   if (texture.get() == nullptr)
   {
      needs_creation = true;
   }
   else
   {
      D3D11_TEXTURE2D_DESC existing_desc;
      texture->GetDesc(&existing_desc);
      if (existing_desc.Width != desc.Width ||
          existing_desc.Height != desc.Height)
      {
         needs_creation = true;
      }
   }

   if (needs_creation)
   {
      texture = nullptr;
      HRESULT hr = native_device->CreateTexture2D(&desc, nullptr, texture.put());
      if (FAILED(hr) || texture.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create texture");
   }
   return needs_creation;
}

static bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv)
{
   if (CreateOrRecreateTextureIfNeeded(game_device_data, native_device, desc, texture))
   {

      srv = nullptr;
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = desc.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;
      HRESULT hr = native_device->CreateShaderResourceView(texture.get(), &srv_desc, srv.put());
      if (FAILED(hr) || srv.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create shader resource view");

      return true;
   }

   return false;
}

static bool CreateOrRecreateTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11ShaderResourceView>& srv, ComPtr<ID3D11RenderTargetView>& rtv)
{
   if (CreateOrRecreateTextureIfNeeded(game_device_data, native_device, desc, texture))
   {

      srv = nullptr;
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = desc.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;
      HRESULT hr = native_device->CreateShaderResourceView(texture.get(), &srv_desc, srv.put());
      if (FAILED(hr) || srv.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create shader resource view");

      rtv = nullptr;
      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = desc.Format;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateRenderTargetView(texture.get(), &rtv_desc, rtv.put());
      if (FAILED(hr) || rtv.get() == nullptr)
         ASSERT_ONCE_MSG(false, "Failed to create render target view");

      return true;
   }

   return false;
}

static bool ExtractTAAShaderResources(
   ID3D11DeviceContext* native_device_context,
   GameDeviceDataGBFR& game_device_data)
{
   ComPtr<ID3D11ShaderResourceView> ps_shader_resources[24];
   native_device_context->PSGetShaderResources(0, 24, ps_shader_resources[0].put());

   // t3 = current color, t23 = motion vectors (required); t5 = depth (optional — absent for
   // NewAA at <100% scale; depth_buffer is pre-populated from the outline CS intercept).
   if (!ps_shader_resources[3].get() || !ps_shader_resources[23].get())
   {
      return false;
   }
   game_device_data.sr_source_color_srv = ps_shader_resources[3];
   ps_shader_resources[3]->GetResource(game_device_data.sr_source_color.put());

   // t5 = depth buffer (optional — NewAA at <100% scale does not bind t5;
   // depth was cached from the outline CS dispatch (0xDA85F5BB) earlier this frame).
   if (ps_shader_resources[5].get())
   {
      ps_shader_resources[5]->GetResource(game_device_data.depth_buffer.put());
   }

   // t23 = motion vectors (already decoded, not jittered)
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

   CreateOrRecreateTextureIfNeeded(game_device_data, native_device, out_texture_desc, game_device_data.sr_output_color, game_device_data.sr_output_color_srv);

   return true;
}

static bool DrawNativePreSREncodePass(
   ID3D11Device* native_device,
   ID3D11DeviceContext* ctx,
   const DeviceData& device_data,
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

   ComPtr<ID3D11Resource> output_resource;
   game_device_data.pre_sr_encode_rtv->GetResource(output_resource.put());
   game_device_data.sr_source_color = output_resource;
   game_device_data.sr_source_color_srv = game_device_data.pre_sr_encode_srv;
   return true;
}

void SetupTempTAAOutput(ID3D11Device* native_device, GameDeviceDataGBFR& game_device_data, ID3D11RenderTargetView* rtv)
{
   game_device_data.taa_output_texture_rtv = rtv;
   ID3D11Resource* taa_resource;
   game_device_data.taa_output_texture_rtv->GetResource(&taa_resource);
   HRESULT hr = taa_resource->QueryInterface(game_device_data.taa_output_texture.put());

   D3D11_TEXTURE2D_DESC taa_rt1_desc;
   game_device_data.taa_output_texture->GetDesc(&taa_rt1_desc);
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device, taa_rt1_desc, game_device_data.taa_temp_output_resource, game_device_data.taa_temp_output_srv, game_device_data.taa_temp_output_rtv);
}

static ID3D11ShaderResourceView* GetPostAAColorInputSRV(const DeviceData& device_data, const GameDeviceDataGBFR& game_device_data)
{
   const bool use_sr_input = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   return use_sr_input ? game_device_data.sr_output_color_srv.get()
                       : game_device_data.taa_temp_output_srv.get();
}

static bool CanDrawNativeUIEncodePass(ID3D11ShaderResourceView* input_color_srv, const GameDeviceDataGBFR& game_device_data)
{
   return input_color_srv != nullptr && game_device_data.taa_output_texture_rtv.get() != nullptr;
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

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   // t0: Color input (TAA/DLSS output), t1: Alpha source (pre-DLSS for proper alpha restoration, fallback to input_srv if not provided)
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

   // Unbind RTV before binding the input SRV to avoid the same resource being bound as both
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

static bool CanDrawNativeMotionBlurPass(ID3D11ShaderResourceView* input_color_srv, const GameDeviceDataGBFR& game_device_data)
{
   if (!game_device_data.motion_blur_pending.load(std::memory_order_acquire) ||
       !game_device_data.motion_blur_first_pass_seen ||
       !game_device_data.motion_blur_second_pass_seen ||
       !game_device_data.motion_blur_replay_states[0].valid ||
       !game_device_data.motion_blur_replay_states[1].valid ||
       !game_device_data.taa_output_texture.get())
   {
      return false;
   }

   return input_color_srv != nullptr;
}

static bool DrawNativeMotionBlurPassInstance(
   ID3D11DeviceContext* ctx,
   const DeviceData& device_data,
   const GameDeviceDataGBFR& game_device_data,
   size_t pass_index,
   ID3D11ShaderResourceView* color_texture_srv,
   ID3D11ShaderResourceView* denoised_texture_srv,
   ID3D11RenderTargetView* output_rtv)
{
   if (!ApplyMotionBlurReplayState(ctx, device_data, game_device_data, pass_index))
   {
      return false;
   }

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ctx->PSSetShaderResources(0, 1, &color_texture_srv);
   ID3D11ShaderResourceView* const denoised_srv = denoised_texture_srv ? denoised_texture_srv : game_device_data.motion_blur_replay_states[pass_index].ps_srv_t1.get();
   ctx->PSSetShaderResources(1, 1, &denoised_srv);

   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);
   ctx->Draw(3, 0);
   return true;
}

static void DrawNativeMotionBlurPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_color_srv)
{
   if (!CanDrawNativeMotionBlurPass(input_color_srv, game_device_data))
   {
      game_device_data.motion_blur_output_ready = false;
      return;
   }

   ComPtr<ID3D11Device> native_device;
   ctx->GetDevice(native_device.put());

   D3D11_TEXTURE2D_DESC output_desc;
   game_device_data.taa_output_texture->GetDesc(&output_desc);
   CreateOrRecreateTextureIfNeeded(
      game_device_data,
      native_device.get(),
      output_desc,
      game_device_data.motion_blur_intermediate_resource,
      game_device_data.motion_blur_intermediate_srv,
      game_device_data.motion_blur_intermediate_rtv);
   CreateOrRecreateTextureIfNeeded(
      game_device_data,
      native_device.get(),
      output_desc,
      game_device_data.motion_blur_output_resource,
      game_device_data.motion_blur_output_srv,
      game_device_data.motion_blur_output_rtv);

   // Pass 0: both t0 and t1 use the pipeline color input — the denoise pre-pass is skipped when
   // TonemapAfterAA is active, so the already-temporally-stable DLSS output fills both roles.
   const bool drew_first_pass = DrawNativeMotionBlurPassInstance(
      ctx,
      device_data,
      game_device_data,
      0,
      input_color_srv,
      input_color_srv,
      game_device_data.motion_blur_intermediate_rtv.get());

   const bool drew_second_pass = drew_first_pass && DrawNativeMotionBlurPassInstance(
                                                       ctx,
                                                       device_data,
                                                       game_device_data,
                                                       1,
                                                       input_color_srv,
                                                       game_device_data.motion_blur_intermediate_srv.get(),
                                                       game_device_data.motion_blur_output_rtv.get());

   if (!drew_second_pass)
   {
      ASSERT_ONCE_MSG(false, "DrawNativeMotionBlurPass: failed to replay both passes");
      game_device_data.motion_blur_output_ready = false;
      return;
   }

   game_device_data.motion_blur_output_ready = true;
}

static bool CanDrawNativeTonemapPass(ID3D11ShaderResourceView* input_color_srv, const GameDeviceDataGBFR& game_device_data)
{
   if (!game_device_data.exposure_texture_srv.get() ||
       !game_device_data.bloom_texture_srv.get() ||
       !game_device_data.taa_output_texture.get())
   {
      return false;
   }

   return input_color_srv != nullptr;
}

static void DrawNativeTonemapPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_color_srv)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Post Tonemap"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
   {
      ASSERT_ONCE_MSG(false, "DrawNativeTonemapPass: required shaders not found");
      return;
   }

   if (!CanDrawNativeTonemapPass(input_color_srv, game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeTonemapPass: missing inputs or output RTV");
      return;
   }

   // Bind Luma constant buffers (required after FinishCommandList in SR path,
   // which resets all deferred-context state to default).
   {
      constexpr bool do_safety_checks = false;
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
   }
   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   // Bind tonemap inputs at their original slots (t2=exposure, t3=bloom, t4=scene color).
   {
      ID3D11ShaderResourceView* srvs[3];
      srvs[0] = game_device_data.exposure_texture_srv.get();
      srvs[1] = game_device_data.bloom_texture_srv.get();
      srvs[2] = input_color_srv;
      ctx->PSSetShaderResources(2, 3, srvs);
   }

   {
      ID3D11SamplerState* samplers[3] = {
         device_data.sampler_state_linear.get(),
         device_data.sampler_state_linear.get(),
         device_data.sampler_state_linear.get()};
      ctx->PSSetSamplers(2, 3, samplers);
   }

   // Always write to cutscene_intermediate_rtv — UIEncode will always read from it and
   // write the final result to taa_output_texture_rtv, avoiding read-write hazards.
   {
      D3D11_TEXTURE2D_DESC taa_desc;
      game_device_data.taa_output_texture->GetDesc(&taa_desc);
      ComPtr<ID3D11Device> native_device;
      ctx->GetDevice(native_device.put());
      CreateOrRecreateTextureIfNeeded(game_device_data, native_device.get(), taa_desc, game_device_data.cutscene_intermediate_resource, game_device_data.cutscene_intermediate_srv, game_device_data.cutscene_intermediate_rtv);
   }

   ID3D11RenderTargetView* const tonemap_rtv = game_device_data.cutscene_intermediate_rtv.get();
   ctx->OMSetRenderTargets(1, &tonemap_rtv, nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   ctx->RSSetViewports(1, &viewport);

   ctx->VSSetShader(vs_it->second.get(), nullptr, 0);
   ctx->PSSetShader(ps_it->second.get(), nullptr, 0);
   ctx->IASetInputLayout(nullptr);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   ctx->Draw(4, 0);
}

static bool CanDrawNativeCutsceneGammaPass(const GameDeviceDataGBFR& game_device_data)
{
   return game_device_data.cutscene_intermediate_srv.get() != nullptr &&
          game_device_data.taa_output_texture.get() != nullptr &&
          game_device_data.cutscene_gamma_replay_state.valid;
}

static void DrawNativeCutsceneGammaPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Cutscene Gamma"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneGammaPass: required shaders not found");
      return;
   }

   if (!CanDrawNativeCutsceneGammaPass(game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneGammaPass: missing input/output resources");
      return;
   }

   if (!ApplyCutscenePostPassReplayState(ctx, game_device_data.cutscene_gamma_replay_state, device_data.sampler_state_linear.get()))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneGammaPass: replay state not cached");
      return;
   }

   D3D11_TEXTURE2D_DESC taa_desc;
   game_device_data.taa_output_texture->GetDesc(&taa_desc);
   ComPtr<ID3D11Device> native_device;
   ctx->GetDevice(native_device.put());
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device.get(), taa_desc, game_device_data.cutscene_gamma_resource, game_device_data.cutscene_gamma_srv, game_device_data.cutscene_gamma_rtv);

   {
      constexpr bool do_safety_checks = false;
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
   }

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ID3D11ShaderResourceView* const input_srv = game_device_data.cutscene_intermediate_srv.get();
   ctx->PSSetShaderResources(0, 1, &input_srv);

   ID3D11SamplerState* const linear_sampler = device_data.sampler_state_linear.get();
   ctx->PSSetSamplers(0, 1, &linear_sampler);

   ID3D11RenderTargetView* const output_rtv = game_device_data.cutscene_gamma_rtv.get();
   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   ctx->RSSetViewports(1, &viewport);

   ctx->IASetInputLayout(nullptr);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   ctx->VSSetShader(vs_it->second.get(), nullptr, 0);
   ctx->PSSetShader(ps_it->second.get(), nullptr, 0);
   ctx->Draw(4, 0);
}

static bool CanDrawNativeCutsceneColorGradePass(const GameDeviceDataGBFR& game_device_data)
{
   return game_device_data.cutscene_gamma_srv.get() != nullptr &&
          game_device_data.taa_output_texture.get() != nullptr &&
          game_device_data.cutscene_color_grade_replay_state.valid;
}

static void DrawNativeCutsceneColorGradePass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Cutscene ColorGrade"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneColorGradePass: required shaders not found");
      return;
   }

   if (!CanDrawNativeCutsceneColorGradePass(game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneColorGradePass: missing input/output resources");
      return;
   }

   if (!ApplyCutscenePostPassReplayState(ctx, game_device_data.cutscene_color_grade_replay_state, device_data.sampler_state_linear.get()))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneColorGradePass: replay state not cached");
      return;
   }

   D3D11_TEXTURE2D_DESC taa_desc;
   game_device_data.taa_output_texture->GetDesc(&taa_desc);
   ComPtr<ID3D11Device> native_device;
   ctx->GetDevice(native_device.put());
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device.get(), taa_desc, game_device_data.cutscene_color_grade_resource, game_device_data.cutscene_color_grade_srv, game_device_data.cutscene_color_grade_rtv);

   {
      constexpr bool do_safety_checks = false;
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
   }

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ID3D11ShaderResourceView* const input_srv = game_device_data.cutscene_gamma_srv.get();
   ctx->PSSetShaderResources(0, 1, &input_srv);

   ID3D11SamplerState* const linear_sampler = device_data.sampler_state_linear.get();
   ctx->PSSetSamplers(0, 1, &linear_sampler);

   ID3D11RenderTargetView* const output_rtv = game_device_data.cutscene_color_grade_rtv.get();
   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   ctx->RSSetViewports(1, &viewport);

   ctx->IASetInputLayout(nullptr);
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   ctx->VSSetShader(vs_it->second.get(), nullptr, 0);
   ctx->PSSetShader(ps_it->second.get(), nullptr, 0);
   ctx->Draw(4, 0);
}

static bool CanDrawNativeCutsceneOverlayPrepPass(const GameDeviceDataGBFR& game_device_data)
{
   return game_device_data.cutscene_overlay_prep_replay_state.valid;
}

static void DrawNativeCutsceneOverlayPrepPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   if (!ApplyCutsceneOverlayPrepReplayState(ctx, device_data, game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneOverlayPrepPass: replay state not cached");
      return;
   }

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ID3D11ShaderResourceView* srv = game_device_data.cutscene_color_grade_srv.get();
   if (srv == nullptr)
   {
      srv = game_device_data.cutscene_gamma_srv.get();
   }
   if (srv == nullptr)
   {
      srv = game_device_data.cutscene_intermediate_srv.get();
   }
   ctx->PSSetShaderResources(0, 1, &srv);

   D3D11_TEXTURE2D_DESC taa_desc;
   game_device_data.taa_output_texture->GetDesc(&taa_desc);
   ComPtr<ID3D11Device> native_device;
   ctx->GetDevice(native_device.put());
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device.get(), taa_desc, game_device_data.cutscene_overlay_prep_resource, game_device_data.cutscene_overlay_prep_srv, game_device_data.cutscene_overlay_prep_rtv);

   ID3D11RenderTargetView* const rtv = game_device_data.cutscene_overlay_prep_rtv.get();
   ctx->OMSetRenderTargets(1, &rtv, nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   ctx->RSSetViewports(1, &viewport);

   ctx->Draw(4, 0);
}

static bool CanDrawNativeCutsceneOverlayPass(const GameDeviceDataGBFR& game_device_data)
{
   return game_device_data.cutscene_overlay_prep_srv.get() != nullptr &&
          game_device_data.cutscene_overlay_replay_state.valid;
}

static void DrawNativeCutsceneOverlayPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11RenderTargetView* output_rtv)
{

   if (!ApplyCutsceneOverlayReplayState(ctx, device_data, game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneOverlayPass: replay state not cached");
      return;
   }

   // Bind Luma constant buffers (LumaSettings needed for DisplayMode branch in the shader).
   {
      constexpr bool do_safety_checks = false;
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
   }

   // Unbind current RTV first to avoid resource being simultaneously bound as RTV and SRV.
   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   // t0 = output of CutsceneOverlayPrep (prepared/transformed scene, loaded by integer pixel coord).
   // t1 = the overlay image asset captured from the original draw at ps_shader_resource_slot1
   //      (game-managed texture, stable across frames, sampled by UV).
   ID3D11ShaderResourceView* const prep_srv = game_device_data.cutscene_overlay_prep_srv.get();
   ID3D11ShaderResourceView* const overlay_img_srv = game_device_data.cutscene_overlay_replay_state.ps_shader_resource_slot1.get();
   ID3D11ShaderResourceView* srv_array[2] = {prep_srv, overlay_img_srv};
   ctx->PSSetShaderResources(0, 2, srv_array);

   ctx->OMSetRenderTargets(1, &output_rtv, nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   ctx->RSSetViewports(1, &viewport);

   ctx->Draw(4, 0);
}