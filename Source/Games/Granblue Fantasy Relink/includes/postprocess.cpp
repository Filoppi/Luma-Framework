#if TEST || DEVELOPMENT
static void LogExpectedCustomDrawSkipped(const char* pass_name, const std::string& reason)
{
   std::string msg = "[GBFR][TEST] Expected custom draw skipped: ";
   msg += pass_name;
   msg += " | reason: ";
   msg += reason;
   reshade::log::message(reshade::log::level::warning, msg.c_str());
}
#endif

static bool CreateOrRecreateOutlineTextureIfNeeded(GameDeviceDataGBFR& game_device_data, ID3D11Device* native_device, D3D11_TEXTURE2D_DESC desc)
{
   desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
   desc.MiscFlags = 0;
   desc.CPUAccessFlags = 0;
   desc.Usage = D3D11_USAGE_DEFAULT;

   if (CreateOrRecreateTextureIfNeeded(game_device_data, native_device, desc, game_device_data.outline_resource))
   {
      game_device_data.outline_srv = nullptr;
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = desc.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      srv_desc.Texture2D.MostDetailedMip = 0;
      HRESULT hr = native_device->CreateShaderResourceView(game_device_data.outline_resource.get(), &srv_desc, game_device_data.outline_srv.put());
      if (FAILED(hr) || game_device_data.outline_srv.get() == nullptr)
      {
         ASSERT_ONCE_MSG(false, "CreateOrRecreateOutlineTextureIfNeeded: failed to create SRV");
         return false;
      }

      game_device_data.outline_uav = nullptr;
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = desc.Format;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      uav_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateUnorderedAccessView(game_device_data.outline_resource.get(), &uav_desc, game_device_data.outline_uav.put());
      if (FAILED(hr) || game_device_data.outline_uav.get() == nullptr)
      {
         ASSERT_ONCE_MSG(false, "CreateOrRecreateOutlineTextureIfNeeded: failed to create UAV");
         return false;
      }
      return true;
   }

   return game_device_data.outline_srv.get() != nullptr && game_device_data.outline_uav.get() != nullptr;
}

static void CaptureOutlineReplayState(ID3D11DeviceContext* context, GameDeviceDataGBFR& game_device_data)
{
   auto& replay_state = game_device_data.outline_replay_state;
   replay_state.Reset();

   context->CSGetShader(replay_state.compute_shader.put(), nullptr, nullptr);

   ID3D11Buffer* cs_constant_buffers[GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount] = {};
   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (ctx1)
   {
      ctx1->CSGetConstantBuffers1(
         0,
         GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount,
         &cs_constant_buffers[0],
         replay_state.cs_constant_buffer_first_constants.data(),
         replay_state.cs_constant_buffer_num_constants.data());
   }
   else
   {
      context->CSGetConstantBuffers(0, GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount, &cs_constant_buffers[0]);
   }
   for (UINT i = 0; i < GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount; ++i)
   {
      replay_state.cs_constant_buffers[i] = cs_constant_buffers[i];
   }

   ID3D11SamplerState* cs_samplers[GameDeviceDataGBFR::OutlineReplayState::kCapturedSamplerCount] = {};
   context->CSGetSamplers(0, GameDeviceDataGBFR::OutlineReplayState::kCapturedSamplerCount, &cs_samplers[0]);
   for (UINT i = 0; i < GameDeviceDataGBFR::OutlineReplayState::kCapturedSamplerCount; ++i)
   {
      replay_state.cs_samplers[i] = cs_samplers[i];
   }

   context->CSGetShaderResources(2, 1, replay_state.cs_depth_srv.put());
   context->CSGetShaderResources(25, 1, replay_state.cs_stencil_srv.put());

   replay_state.valid = replay_state.compute_shader.get() != nullptr &&
                        replay_state.cs_depth_srv.get() != nullptr &&
                        replay_state.cs_stencil_srv.get() != nullptr;
}

static bool CanDrawNativeOutlinePass(ID3D11ShaderResourceView* input_color_srv, const GameDeviceDataGBFR& game_device_data)
{
   return game_device_data.outline_pending.load(std::memory_order_acquire) &&
          game_device_data.outline_replay_state.valid &&
          input_color_srv != nullptr;
}

static bool DrawNativeOutlinePass(
   ID3D11DeviceContext* context,
   const DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_color_srv)
{
   if (!CanDrawNativeOutlinePass(input_color_srv, game_device_data))
   {
      return false;
   }

   const auto& replay_state = game_device_data.outline_replay_state;
   ComPtr<ID3D11Device> native_device;
   context->GetDevice(native_device.put());
   if (!native_device)
   {
      return false;
   }

   ComPtr<ID3D11Resource> input_resource;
   input_color_srv->GetResource(input_resource.put());
   ComPtr<ID3D11Texture2D> input_texture;
   if (!input_resource || FAILED(input_resource->QueryInterface(input_texture.put())) || !input_texture)
   {
      return false;
   }

   D3D11_TEXTURE2D_DESC output_desc = {};
   input_texture->GetDesc(&output_desc);
   if (!CreateOrRecreateOutlineTextureIfNeeded(game_device_data, native_device.get(), output_desc))
   {
      return false;
   }

   // Preserve source color so replay never returns a black frame if dispatch is partially/fully culled.
   if (game_device_data.outline_resource && input_resource && game_device_data.outline_resource.get() != input_resource.get())
   {
      context->CopyResource(game_device_data.outline_resource.get(), input_resource.get());
   }

   context->CSSetShader(replay_state.compute_shader.get(), nullptr, 0);

   ID3D11Buffer* cs_constant_buffers[GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount] = {};
   for (UINT i = 0; i < GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount; ++i)
   {
      cs_constant_buffers[i] = replay_state.cs_constant_buffers[i].get();
   }
   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (ctx1)
   {
      ctx1->CSSetConstantBuffers1(
         0,
         GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount,
         &cs_constant_buffers[0],
         replay_state.cs_constant_buffer_first_constants.data(),
         replay_state.cs_constant_buffer_num_constants.data());
   }
   else
   {
      context->CSSetConstantBuffers(0, GameDeviceDataGBFR::OutlineReplayState::kCapturedConstantBufferCount, &cs_constant_buffers[0]);
   }

   ID3D11SamplerState* cs_samplers[GameDeviceDataGBFR::OutlineReplayState::kCapturedSamplerCount] = {};
   for (UINT i = 0; i < GameDeviceDataGBFR::OutlineReplayState::kCapturedSamplerCount; ++i)
   {
      cs_samplers[i] = replay_state.cs_samplers[i].get();
   }
   context->CSSetSamplers(0, GameDeviceDataGBFR::OutlineReplayState::kCapturedSamplerCount, &cs_samplers[0]);

   ID3D11ShaderResourceView* cs_srvs[26] = {};
   cs_srvs[0] = input_color_srv;
   cs_srvs[2] = replay_state.cs_depth_srv.get();
   cs_srvs[25] = replay_state.cs_stencil_srv.get();
   context->CSSetShaderResources(0, 26, &cs_srvs[0]);

   ID3D11UnorderedAccessView* output_uav = game_device_data.outline_uav.get();
   UINT initial_counts = 0;
   context->CSSetUnorderedAccessViews(0, 1, &output_uav, &initial_counts);

   const UINT dispatch_x = static_cast<UINT>((device_data.output_resolution.x + 15.0f) / 16.0f);
   const UINT dispatch_y = static_cast<UINT>((device_data.output_resolution.y + 15.0f) / 16.0f);
   context->Dispatch(dispatch_x, dispatch_y, 1);

   ID3D11UnorderedAccessView* null_uav = nullptr;
   context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

   ID3D11ShaderResourceView* null_srv = nullptr;
   context->CSSetShaderResources(0, 1, &null_srv);
   context->CSSetShaderResources(2, 1, &null_srv);
   context->CSSetShaderResources(25, 1, &null_srv);

   return true;
}

static void CaptureCutsceneOverlayModulateReplayState(ID3D11DeviceContext* context, GameDeviceDataGBFR& game_device_data)
{
   auto& replay_state = game_device_data.cutscene_overlay_modulate_replay_state;
   replay_state.Reset();

   context->VSGetShader(replay_state.vertex_shader.put(), nullptr, nullptr);
   context->PSGetShader(replay_state.pixel_shader.put(), nullptr, nullptr);
   context->IAGetInputLayout(replay_state.input_layout.put());

   ID3D11Buffer* vertex_buffers[GameDeviceDataGBFR::CutsceneOverlayModulateReplayState::kCapturedVertexBufferCount] = {};
   context->IAGetVertexBuffers(
      0,
      GameDeviceDataGBFR::CutsceneOverlayModulateReplayState::kCapturedVertexBufferCount,
      &vertex_buffers[0],
      replay_state.vertex_buffer_strides.data(),
      replay_state.vertex_buffer_offsets.data());

   for (UINT i = 0; i < GameDeviceDataGBFR::CutsceneOverlayModulateReplayState::kCapturedVertexBufferCount; ++i)
   {
      replay_state.vertex_buffers[i] = vertex_buffers[i];
   }
   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());

   if (ctx1)
   {
      ctx1->VSGetConstantBuffers1(1, 1, replay_state.vs_constant_buffer_slot1.buffer.put(), &replay_state.vs_constant_buffer_slot1.first_constant, &replay_state.vs_constant_buffer_slot1.num_constants);
   }

   replay_state.valid = replay_state.vertex_shader.get() != nullptr &&
                        replay_state.pixel_shader.get() != nullptr &&
                        ctx1 != nullptr &&
                        replay_state.vs_constant_buffer_slot1.buffer.get() != nullptr;
}

static bool ApplyCutsceneOverlayModulateReplayState(ID3D11DeviceContext* context, const DeviceData& device_data, const GameDeviceDataGBFR& game_device_data)
{
   const auto& replay_state = game_device_data.cutscene_overlay_modulate_replay_state;
   if (!replay_state.valid)
   {
      return false;
   }

   context->VSSetShader(replay_state.vertex_shader.get(), nullptr, 0);
   context->PSSetShader(replay_state.pixel_shader.get(), nullptr, 0);
   context->IASetInputLayout(replay_state.input_layout.get());

   ID3D11Buffer* vertex_buffers[GameDeviceDataGBFR::CutsceneOverlayModulateReplayState::kCapturedVertexBufferCount] = {};
   for (UINT i = 0; i < GameDeviceDataGBFR::CutsceneOverlayModulateReplayState::kCapturedVertexBufferCount; ++i)
   {
      vertex_buffers[i] = replay_state.vertex_buffers[i].get();
   }
   context->IASetVertexBuffers(
      0,
      GameDeviceDataGBFR::CutsceneOverlayModulateReplayState::kCapturedVertexBufferCount,
      &vertex_buffers[0],
      replay_state.vertex_buffer_strides.data(),
      replay_state.vertex_buffer_offsets.data());

   ID3D11Buffer* const vs_buffer = replay_state.vs_constant_buffer_slot1.buffer.get();
   if (vs_buffer == nullptr)
   {
      return false;
   }

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (!ctx1)
   {
      return false;
   }

   ctx1->VSSetConstantBuffers1(1, 1, &vs_buffer, &replay_state.vs_constant_buffer_slot1.first_constant, &replay_state.vs_constant_buffer_slot1.num_constants);

   D3D11_VIEWPORT viewport = {};
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   viewport.MaxDepth = 1.0f;
   context->RSSetViewports(1, &viewport);

   ID3D11SamplerState* const sampler_state = device_data.sampler_state_linear.get();
   context->PSSetSamplers(0, 1, &sampler_state);
   return true;
}

static void CaptureCutsceneOverlayBlendReplayState(ID3D11DeviceContext* context, GameDeviceDataGBFR& game_device_data)
{
   auto& replay_state = game_device_data.cutscene_overlay_blend_replay_state;
   replay_state.Reset();

   context->VSGetShader(replay_state.vertex_shader.put(), nullptr, nullptr);
   context->PSGetShader(replay_state.pixel_shader.put(), nullptr, nullptr);
   context->IAGetInputLayout(replay_state.input_layout.put());

   ID3D11Buffer* vertex_buffers[GameDeviceDataGBFR::CutsceneOverlayBlendReplayState::kCapturedVertexBufferCount] = {};
   context->IAGetVertexBuffers(
      0,
      GameDeviceDataGBFR::CutsceneOverlayBlendReplayState::kCapturedVertexBufferCount,
      &vertex_buffers[0],
      replay_state.vertex_buffer_strides.data(),
      replay_state.vertex_buffer_offsets.data());

   for (UINT i = 0; i < GameDeviceDataGBFR::CutsceneOverlayBlendReplayState::kCapturedVertexBufferCount; ++i)
   {
      replay_state.vertex_buffers[i] = vertex_buffers[i];
   }

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());

   if (ctx1)
   {
      ctx1->VSGetConstantBuffers1(1, 1, replay_state.vs_constant_buffer_slot1.buffer.put(), &replay_state.vs_constant_buffer_slot1.first_constant, &replay_state.vs_constant_buffer_slot1.num_constants);
      ctx1->PSGetConstantBuffers1(1, 1, replay_state.ps_constant_buffer_slot1.buffer.put(), &replay_state.ps_constant_buffer_slot1.first_constant, &replay_state.ps_constant_buffer_slot1.num_constants);
   }
   context->PSGetShaderResources(1, 1, replay_state.ps_shader_resource_slot1.put());
   replay_state.valid = replay_state.vertex_shader.get() != nullptr &&
                        replay_state.pixel_shader.get() != nullptr &&
                        ctx1 != nullptr &&
                        replay_state.vs_constant_buffer_slot1.buffer.get() != nullptr &&
                        replay_state.ps_constant_buffer_slot1.buffer.get() != nullptr;
}

static bool ApplyCutsceneOverlayBlendReplayState(ID3D11DeviceContext* context, const DeviceData& device_data, const GameDeviceDataGBFR& game_device_data)
{
   const auto& replay_state = game_device_data.cutscene_overlay_blend_replay_state;
   if (!replay_state.valid)
   {
      return false;
   }

   context->VSSetShader(replay_state.vertex_shader.get(), nullptr, 0);
   context->PSSetShader(replay_state.pixel_shader.get(), nullptr, 0);
   context->IASetInputLayout(replay_state.input_layout.get());

   ID3D11Buffer* vertex_buffers[GameDeviceDataGBFR::CutsceneOverlayBlendReplayState::kCapturedVertexBufferCount] = {};
   for (UINT i = 0; i < GameDeviceDataGBFR::CutsceneOverlayBlendReplayState::kCapturedVertexBufferCount; ++i)
   {
      vertex_buffers[i] = replay_state.vertex_buffers[i].get();
   }
   context->IASetVertexBuffers(
      0,
      GameDeviceDataGBFR::CutsceneOverlayBlendReplayState::kCapturedVertexBufferCount,
      &vertex_buffers[0],
      replay_state.vertex_buffer_strides.data(),
      replay_state.vertex_buffer_offsets.data());

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (!ctx1)
   {
      return false;
   }

   ID3D11Buffer* const vs_constant_buffer = replay_state.vs_constant_buffer_slot1.buffer.get();
   if (vs_constant_buffer == nullptr)
   {
      return false;
   }
   ctx1->VSSetConstantBuffers1(1, 1, &vs_constant_buffer, &replay_state.vs_constant_buffer_slot1.first_constant, &replay_state.vs_constant_buffer_slot1.num_constants);

   ID3D11Buffer* const ps_constant_buffer = replay_state.ps_constant_buffer_slot1.buffer.get();
   if (ps_constant_buffer == nullptr)
   {
      return false;
   }
   ctx1->PSSetConstantBuffers1(1, 1, &ps_constant_buffer, &replay_state.ps_constant_buffer_slot1.first_constant, &replay_state.ps_constant_buffer_slot1.num_constants);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   context->RSSetViewports(1, &viewport);

   ID3D11SamplerState* const sampler_state = device_data.sampler_state_linear.get();
   context->PSSetSamplers(1, 1, &sampler_state);
   return true;
}

static void CaptureMotionBlurReplayState(ID3D11DeviceContext* context, GameDeviceDataGBFR& game_device_data, size_t pass_index)
{
   auto& replay_state = game_device_data.motion_blur_replay_states[pass_index];
   replay_state.Reset();

   context->VSGetShader(replay_state.vertex_shader.put(), nullptr, nullptr);
   context->PSGetShader(replay_state.pixel_shader.put(), nullptr, nullptr);
   context->IAGetInputLayout(replay_state.input_layout.put());
   context->IAGetPrimitiveTopology(&replay_state.primitive_topology);

   ID3D11Buffer* vertex_buffers[GameDeviceDataGBFR::MotionBlurReplayState::kCapturedVertexBufferCount] = {};
   context->IAGetVertexBuffers(
      0,
      GameDeviceDataGBFR::MotionBlurReplayState::kCapturedVertexBufferCount,
      &vertex_buffers[0],
      replay_state.vertex_buffer_strides.data(),
      replay_state.vertex_buffer_offsets.data());

   for (UINT i = 0; i < GameDeviceDataGBFR::MotionBlurReplayState::kCapturedVertexBufferCount; ++i)
   {
      replay_state.vertex_buffers[i] = vertex_buffers[i];
   }

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (ctx1)
   {
      ctx1->PSGetConstantBuffers1(
         10,
         1,
         replay_state.ps_cbuffer_b10.buffer.put(),
         &replay_state.ps_cbuffer_b10.first_constant,
         &replay_state.ps_cbuffer_b10.num_constants);
      ctx1->PSGetConstantBuffers1(
         12,
         1,
         replay_state.ps_cbuffer_b12.buffer.put(),
         &replay_state.ps_cbuffer_b12.first_constant,
         &replay_state.ps_cbuffer_b12.num_constants);
   }
   context->PSGetShaderResources(0, 1, replay_state.ps_srv_t0.put());
   context->PSGetShaderResources(1, 1, replay_state.ps_srv_t1.put());
   context->PSGetShaderResources(4, 1, replay_state.ps_srv_t4.put());
   context->PSGetShaderResources(5, 1, replay_state.ps_srv_t5.put());

   replay_state.valid = replay_state.vertex_shader.get() != nullptr &&
                        replay_state.pixel_shader.get() != nullptr &&
                        replay_state.ps_cbuffer_b10.buffer.get() != nullptr &&
                        replay_state.ps_cbuffer_b12.buffer.get() != nullptr &&
                        replay_state.ps_srv_t0.get() != nullptr &&
                        replay_state.ps_srv_t1.get() != nullptr &&
                        replay_state.ps_srv_t4.get() != nullptr &&
                        replay_state.ps_srv_t5.get() != nullptr;
}

static bool ApplyMotionBlurReplayState(ID3D11DeviceContext* context, const DeviceData& device_data, const GameDeviceDataGBFR& game_device_data, size_t pass_index)
{
   const auto& replay_state = game_device_data.motion_blur_replay_states[pass_index];
   if (!replay_state.valid)
   {
      return false;
   }

   context->VSSetShader(replay_state.vertex_shader.get(), nullptr, 0);
   context->PSSetShader(replay_state.pixel_shader.get(), nullptr, 0);
   context->IASetInputLayout(replay_state.input_layout.get());

   ID3D11Buffer* vertex_buffers[GameDeviceDataGBFR::MotionBlurReplayState::kCapturedVertexBufferCount] = {};
   for (UINT i = 0; i < GameDeviceDataGBFR::MotionBlurReplayState::kCapturedVertexBufferCount; ++i)
   {
      vertex_buffers[i] = replay_state.vertex_buffers[i].get();
   }
   context->IASetVertexBuffers(
      0,
      GameDeviceDataGBFR::MotionBlurReplayState::kCapturedVertexBufferCount,
      &vertex_buffers[0],
      replay_state.vertex_buffer_strides.data(),
      replay_state.vertex_buffer_offsets.data());

   context->IASetPrimitiveTopology(replay_state.primitive_topology);

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (!ctx1)
   {
      return false;
   }

   ID3D11Buffer* cb10 = replay_state.ps_cbuffer_b10.buffer.get();
   ctx1->PSSetConstantBuffers1(
      10,
      1,
      &cb10,
      &replay_state.ps_cbuffer_b10.first_constant,
      &replay_state.ps_cbuffer_b10.num_constants);
   ID3D11Buffer* cb12 = replay_state.ps_cbuffer_b12.buffer.get();
   ctx1->PSSetConstantBuffers1(
      12,
      1,
      &cb12,
      &replay_state.ps_cbuffer_b12.first_constant,
      &replay_state.ps_cbuffer_b12.num_constants);

   ID3D11ShaderResourceView* srv_t0 = replay_state.ps_srv_t0.get();
   context->PSSetShaderResources(0, 1, &srv_t0);
   ID3D11ShaderResourceView* srv_t1 = replay_state.ps_srv_t1.get();
   context->PSSetShaderResources(1, 1, &srv_t1);
   ID3D11ShaderResourceView* srv_t4 = replay_state.ps_srv_t4.get();
   context->PSSetShaderResources(4, 1, &srv_t4);
   ID3D11ShaderResourceView* srv_t5 = replay_state.ps_srv_t5.get();
   context->PSSetShaderResources(5, 1, &srv_t5);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   viewport.MinDepth = 0.0f;
   viewport.MaxDepth = 1.0f;
   context->RSSetViewports(1, &viewport);

   return true;
}

static void PassThroughToRenderTarget(ID3D11DeviceContext* context, UINT src_srv_slot = 0)
{
   ComPtr<ID3D11RenderTargetView> rtv;
   context->OMGetRenderTargets(1, rtv.put(), nullptr);
   if (!rtv)
      return;

   ComPtr<ID3D11ShaderResourceView> src_srv;
   context->PSGetShaderResources(src_srv_slot, 1, src_srv.put());
   if (!src_srv)
      return;

   ComPtr<ID3D11Resource> dst, src;
   rtv->GetResource(dst.put());
   src_srv->GetResource(src.put());
   if (dst && src && dst != src)
   {
      context->CopyResource(dst.get(), src.get());
   }
}

static void PassThroughToComputeUAV(ID3D11DeviceContext* context, UINT src_srv_slot = 0, UINT dst_uav_slot = 0)
{
   ComPtr<ID3D11UnorderedAccessView> uav;
   context->CSGetUnorderedAccessViews(dst_uav_slot, 1, uav.put());
   if (!uav)
      return;

   ComPtr<ID3D11ShaderResourceView> src_srv;
   context->CSGetShaderResources(src_srv_slot, 1, src_srv.put());
   if (!src_srv)
      return;

   ComPtr<ID3D11Resource> dst, src;
   uav->GetResource(dst.put());
   src_srv->GetResource(src.put());
   if (dst && src && dst != src)
   {
      context->CopyResource(dst.get(), src.get());
   }
}

static void CaptureCutscenePostPassReplayState(ID3D11DeviceContext* context, GameDeviceDataGBFR::CutscenePostPassReplayState& replay_state)
{
   replay_state.Reset();

   context->VSGetShader(replay_state.vertex_shader.put(), nullptr, nullptr);
   context->PSGetShader(replay_state.pixel_shader.put(), nullptr, nullptr);

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (ctx1)
   {
      ctx1->PSGetConstantBuffers1(
         1,
         1,
         replay_state.ps_constant_buffer_slot1.buffer.put(),
         &replay_state.ps_constant_buffer_slot1.first_constant,
         &replay_state.ps_constant_buffer_slot1.num_constants);
   }

   context->PSGetSamplers(0, 1, replay_state.ps_sampler_slot0.put());

   replay_state.valid = replay_state.vertex_shader.get() != nullptr &&
                        replay_state.pixel_shader.get() != nullptr &&
                        replay_state.ps_constant_buffer_slot1.buffer.get() != nullptr;
}

static bool ApplyCutscenePostPassReplayState(
   ID3D11DeviceContext* context,
   const GameDeviceDataGBFR::CutscenePostPassReplayState& replay_state,
   ID3D11SamplerState* fallback_sampler)
{
   if (!replay_state.valid)
   {
      return false;
   }

   ComPtr<ID3D11DeviceContext1> ctx1;
   context->QueryInterface(ctx1.put());
   if (!ctx1)
   {
      return false;
   }

   ID3D11Buffer* const ps_constant_buffer = replay_state.ps_constant_buffer_slot1.buffer.get();
   ctx1->PSSetConstantBuffers1(
      1,
      1,
      &ps_constant_buffer,
      &replay_state.ps_constant_buffer_slot1.first_constant,
      &replay_state.ps_constant_buffer_slot1.num_constants);

   context->VSSetShader(replay_state.vertex_shader.get(), nullptr, 0);
   context->PSSetShader(replay_state.pixel_shader.get(), nullptr, 0);

   ID3D11SamplerState* sampler = replay_state.ps_sampler_slot0.get();
   if (sampler == nullptr)
   {
      sampler = fallback_sampler;
   }
   context->PSSetSamplers(0, 1, &sampler);

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

   {
      constexpr bool do_safety_checks = false;
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
   }
   ctx->OMSetRenderTargets(0, nullptr, nullptr);

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

static bool CanDrawNativeCutsceneGammaPass(ID3D11ShaderResourceView* input_srv, const GameDeviceDataGBFR& game_device_data)
{
   return input_srv != nullptr &&
          game_device_data.taa_output_texture.get() != nullptr &&
          game_device_data.cutscene_gamma_replay_state.valid;
}

static void DrawNativeCutsceneGammaPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_srv)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Cutscene Gamma"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneGammaPass: required shaders not found");
      return;
   }

   if (!CanDrawNativeCutsceneGammaPass(input_srv, game_device_data))
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

static bool CanDrawNativeCutsceneColorGradePass(ID3D11ShaderResourceView* input_srv, const GameDeviceDataGBFR& game_device_data)
{
   return input_srv != nullptr &&
          game_device_data.taa_output_texture.get() != nullptr &&
          game_device_data.cutscene_color_grade_replay_state.valid;
}

static void DrawNativeCutsceneColorGradePass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_srv)
{
   const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
   const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Cutscene ColorGrade"));
   if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second ||
       ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneColorGradePass: required shaders not found");
      return;
   }

   if (!CanDrawNativeCutsceneColorGradePass(input_srv, game_device_data))
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

static bool CanDrawNativeCutsceneOverlayModulatePass(ID3D11ShaderResourceView* input_srv, const GameDeviceDataGBFR& game_device_data)
{
   return input_srv != nullptr &&
          game_device_data.cutscene_overlay_modulate_replay_state.valid &&
          game_device_data.taa_output_texture.get() != nullptr;
}

static void DrawNativeCutsceneOverlayModulatePass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_srv)
{
   if (input_srv == nullptr)
   {
      return;
   }

   if (!ApplyCutsceneOverlayModulateReplayState(ctx, device_data, game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneOverlayModulatePass: replay state not cached");
      return;
   }

   ctx->OMSetRenderTargets(0, nullptr, nullptr);
   ctx->PSSetShaderResources(0, 1, &input_srv);

   D3D11_TEXTURE2D_DESC taa_desc;
   game_device_data.taa_output_texture->GetDesc(&taa_desc);
   ComPtr<ID3D11Device> native_device;
   ctx->GetDevice(native_device.put());
   CreateOrRecreateTextureIfNeeded(game_device_data, native_device.get(), taa_desc, game_device_data.cutscene_overlay_modulate_resource, game_device_data.cutscene_overlay_modulate_srv, game_device_data.cutscene_overlay_modulate_rtv);

   ID3D11RenderTargetView* const rtv = game_device_data.cutscene_overlay_modulate_rtv.get();
   ctx->OMSetRenderTargets(1, &rtv, nullptr);

   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = device_data.output_resolution.x;
   viewport.Height = device_data.output_resolution.y;
   ctx->RSSetViewports(1, &viewport);

   ctx->Draw(4, 0);
}

static bool CanDrawNativeCutsceneOverlayBlendPass(ID3D11ShaderResourceView* input_modulate_srv, const GameDeviceDataGBFR& game_device_data)
{
   return input_modulate_srv != nullptr &&
          game_device_data.cutscene_intermediate_rtv.get() != nullptr &&
          game_device_data.taa_output_texture.get() != nullptr &&
          game_device_data.cutscene_overlay_blend_replay_state.valid;
}

static void DrawNativeCutsceneOverlayBlendPass(
   ID3D11DeviceContext* ctx,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data,
   ID3D11ShaderResourceView* input_modulate_srv,
   ID3D11RenderTargetView* output_rtv)
{
   if (input_modulate_srv == nullptr || output_rtv == nullptr)
   {
      return;
   }

   if (!ApplyCutsceneOverlayBlendReplayState(ctx, device_data, game_device_data))
   {
      ASSERT_ONCE_MSG(false, "DrawNativeCutsceneOverlayBlendPass: replay state not cached");
      return;
   }

   {
      constexpr bool do_safety_checks = false;
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings, 0, 0, 0.f, 0.f, do_safety_checks);
      SetLumaConstantBuffers(ctx, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, 0.f, 0.f, do_safety_checks);
   }

   ctx->OMSetRenderTargets(0, nullptr, nullptr);

   ID3D11ShaderResourceView* const overlay_img_srv = game_device_data.cutscene_overlay_blend_replay_state.ps_shader_resource_slot1.get();
   ID3D11ShaderResourceView* srv_array[2] = {input_modulate_srv, overlay_img_srv};
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

static void RunLatePostProcessPasses(
   ID3D11DeviceContext* native_device_context,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
   draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
   compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

   const bool tonemap_after_taa = IsTonemapAfterTAAEnabled(true);
   const bool sr_enabled_this_frame = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;

   // TUP path: TAA partial_command_list has just executed so sr_source_color is valid.
   // DrawNativePreSREncodePass was intentionally deferred from recording time to here.
   if (render_scale != 1.f && sr_enabled_this_frame && cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
   {

      {
         ComPtr<ID3D11Device> native_device;
         native_device_context->GetDevice(native_device.put());

         const bool pre_sr_ok = DrawNativePreSREncodePass(native_device.get(), native_device_context, cmd_list_data, device_data, game_device_data);
#if TEST || DEVELOPMENT
         if (!pre_sr_ok)
         {
            std::string reason = "prerequisite missing:";
            const auto vs_it = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
            const auto ps_it = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Pre SR Encode"));
            if (vs_it == device_data.native_vertex_shaders.end() || !vs_it->second)
               reason += " copy_vs=missing;";
            if (ps_it == device_data.native_pixel_shaders.end() || !ps_it->second)
               reason += " pre_sr_encode_ps=missing;";
            if (!game_device_data.sr_source_color.get())
               reason += " sr_source_color=null;";
            if (!game_device_data.sr_source_color_srv.get())
               reason += " sr_source_color_srv=null;";
            LogExpectedCustomDrawSkipped("PreSREncode (TUP path)", reason);
         }
#endif
      }
   }

   if (sr_enabled_this_frame)
   {
      bool sr_settings_changed = false;
      auto* sr_instance_data = device_data.GetSRInstanceData();
      {
         SR::SettingsData settings_data;
         settings_data.output_width = static_cast<uint>(device_data.output_resolution.x);
         settings_data.output_height = static_cast<uint>(device_data.output_resolution.y);
         settings_data.render_width = static_cast<uint>(device_data.render_resolution.x);
         settings_data.render_height = static_cast<uint>(device_data.render_resolution.y);
         settings_data.dynamic_resolution = false;
         settings_data.hdr = cb_luma_global_settings.DisplayMode == DisplayModeType::HDR ? true : tonemap_after_taa;
         settings_data.auto_exposure = true;
         settings_data.inverted_depth = false;
         // Granblue MVs are unjittered (g_ProjectionOffset cancels jitter in the PS)
         settings_data.mvs_jittered = false;
         settings_data.mvs_x_scale = -(float)device_data.render_resolution.x;
         settings_data.mvs_y_scale = -(float)device_data.render_resolution.y;
         settings_data.render_preset = dlss_render_preset;

         sr_settings_changed = !game_device_data.sr_settings_valid || !(settings_data == game_device_data.last_sr_settings_data);
         const bool update_ok = sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
         if (update_ok)
         {
            game_device_data.last_sr_settings_data = settings_data;
            game_device_data.sr_settings_valid = true;
         }
         else
         {
            device_data.force_reset_sr = true;
         }
      }

      // Prepare SR draw data
      {
         bool reset_sr = device_data.force_reset_sr || game_device_data.output_changed || sr_settings_changed;
         device_data.force_reset_sr = false;
         float jitter_x = game_device_data.table_jitter.x;
         float jitter_y = game_device_data.table_jitter.y;
#if TEST || DEVELOPMENT
         if (!game_device_data.use_table_jitter_for_dlss)
         {
            jitter_x = game_device_data.jitter.x;
            jitter_y = game_device_data.jitter.y;
         }
#endif
         SR::SuperResolutionImpl::DrawData draw_data;
         draw_data.source_color = game_device_data.sr_source_color.get();
         draw_data.output_color = game_device_data.sr_output_color.get();
         draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
         draw_data.depth_buffer = game_device_data.depth_buffer.get();
         draw_data.pre_exposure = 0.0f;
         // Pass the engine's AdaptLuminance texture as an exposure hint to DLSS.
         // Only set when the tonemap intercept has already captured it this frame.
         // Note: DLSS expects pInExposureTexture to be a 1x1 texture; if AdaptLuminance
         // is a different size, DLSS may ignore it - auto_exposure=true provides a fallback.
         if (tonemap_after_taa)
         {
            draw_data.exposure = game_device_data.exposure_texture.get();
         }
         // Pass the frame jitter tracked by this module.
         draw_data.jitter_x = jitter_x;
         draw_data.jitter_y = jitter_y;
         draw_data.vert_fov = game_device_data.camera_fov;
         draw_data.far_plane = game_device_data.camera_far;
         draw_data.near_plane = game_device_data.camera_near;
         draw_data.reset = reset_sr;
         draw_data.render_width = device_data.render_resolution.x;
         draw_data.render_height = device_data.render_resolution.y;

         // Execute SR
#if TEST || DEVELOPMENT
         {
            std::string pre_draw_issues;
            if (!draw_data.source_color)
               pre_draw_issues += " source_color=null;";
            if (!draw_data.output_color)
               pre_draw_issues += " output_color=null;";
            if (!draw_data.motion_vectors)
               pre_draw_issues += " motion_vectors=null;";
            if (!draw_data.depth_buffer)
               pre_draw_issues += " depth_buffer=null;";
            if (!sr_instance_data)
               pre_draw_issues += " sr_instance_data=null;";
            if (!pre_draw_issues.empty())
            {
               LogExpectedCustomDrawSkipped("SR", "null inputs before Draw:" + pre_draw_issues);
            }
         }
#endif
         const bool draw_ok = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
         if (draw_ok)
         {
            device_data.has_drawn_sr = true;
         }
         else
         {
            device_data.has_drawn_sr = false;
            device_data.force_reset_sr = true;
#if TEST || DEVELOPMENT
            {
               std::string reason = "SR::Draw returned false; draw inputs:";
               if (!draw_data.source_color)
                  reason += " source_color=null;";
               if (!draw_data.output_color)
                  reason += " output_color=null;";
               if (!draw_data.motion_vectors)
                  reason += " motion_vectors=null;";
               if (!draw_data.depth_buffer)
                  reason += " depth_buffer=null;";
               if (!sr_instance_data)
                  reason += " sr_instance_data=null;";
               if (draw_data.render_width <= 0.f || draw_data.render_height <= 0.f)
                  reason += " render_dims_invalid(" + std::to_string(static_cast<int>(draw_data.render_width)) + "x" + std::to_string(static_cast<int>(draw_data.render_height)) + ");";
               LogExpectedCustomDrawSkipped("SR", reason);
            }
#endif
         }
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
               trace_draw_call_data.custom_name = device_data.sr_type == SR::Type::DLSS
                                                     ? "DLSS-SR"
                                                     : "FSR-SR";
               GetResourceInfo(game_device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
         }
#endif
         draw_state_stack.Restore(native_device_context);
         compute_state_stack.Restore(native_device_context);
      }
   }

   // Pipeline chain SRV threaded through late-replay passes (split -> PostSREncode -> MotionBlur -> Tonemap -> Cutscene -> UI)
   ID3D11ShaderResourceView* pipeline_color_srv = GetPostAAColorInputSRV(device_data, game_device_data);

   auto run_chained_color_pass = [&](const char* pass_name, auto&& draw_fn) -> bool
   {
      if (!pipeline_color_srv)
      {
#if TEST || DEVELOPMENT
         LogExpectedCustomDrawSkipped(pass_name, "pipeline_color_srv=null");
#endif
         return false;
      }

      ID3D11ShaderResourceView* next_srv = nullptr;
      if (!draw_fn(pipeline_color_srv, next_srv))
      {
         return false;
      }

      if (!next_srv)
      {
#if TEST || DEVELOPMENT
         LogExpectedCustomDrawSkipped(pass_name, "draw succeeded but output SRV is null");
#endif
         return false;
      }

      pipeline_color_srv = next_srv;
      return true;
   };

   // PostSREncode normalizes color space: G->L if no HDR-SR, else L->G if HDR-SR, else skip
   {
      if (pipeline_color_srv)
      {
         const bool post_sr_ok = run_chained_color_pass("PostSREncode", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            // Use pre-DLSS input for alpha extraction if SR was executed.
            ID3D11ShaderResourceView* alpha_source_srv = nullptr;
            if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.has_drawn_sr)
            {
               alpha_source_srv = game_device_data.sr_source_color_srv.get();
            }

            if (!DrawNativePostSREncodePass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv, alpha_source_srv))
            {
               return false;
            }

            out_srv = game_device_data.post_sr_encode_srv.get();
            return true; });
#if TEST || DEVELOPMENT
         if (!post_sr_ok)
         {
            std::string reason = "DrawNativePostSREncodePass failed:";
            {
               const auto vs_chk = device_data.native_vertex_shaders.find(CompileTimeStringHash("Copy VS"));
               if (vs_chk == device_data.native_vertex_shaders.end() || !vs_chk->second)
                  reason += " copy_vs=missing;";
            }
            {
               const auto ps_chk = device_data.native_pixel_shaders.find(CompileTimeStringHash("GBFR Post SR Encode"));
               if (ps_chk == device_data.native_pixel_shaders.end() || !ps_chk->second)
                  reason += " post_sr_encode_ps=missing;";
            }
            if (!pipeline_color_srv)
               reason += " input_srv=null;";
            LogExpectedCustomDrawSkipped("PostSREncode", reason);
         }
#endif
      }
#if TEST || DEVELOPMENT
      else
      {
         LogExpectedCustomDrawSkipped("PostSREncode", "should_run=true but pipeline_color_srv is null");
      }
#endif
   }

   if (game_device_data.motion_blur_pending.load(std::memory_order_acquire))
   {
      const bool use_sr_input = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
      const bool after_sr = !use_sr_input || device_data.has_drawn_sr;
      if (after_sr && CanDrawNativeMotionBlurPass(pipeline_color_srv, game_device_data))
      {
         const bool motion_blur_ok = run_chained_color_pass("MotionBlur", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            DrawNativeMotionBlurPass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv);
            if (!game_device_data.motion_blur_output_ready)
            {
               return false;
            }

            out_srv = game_device_data.motion_blur_output_srv.get();
            return true; });
#if TEST || DEVELOPMENT
         if (!motion_blur_ok)
         {
            LogExpectedCustomDrawSkipped("MotionBlur", "DrawNativeMotionBlurPass did not produce output (motion_blur_output_ready=false)");
         }
#endif
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason;
         if (!after_sr)
         {
            reason = "pending replay, but SR output is not ready yet";
         }
         else
         {
            reason = "prerequisite missing:";
            if (!pipeline_color_srv)
               reason += " pipeline_color_srv=null;";
            if (!game_device_data.motion_blur_first_pass_seen)
               reason += " first_pass_not_seen;";
            if (!game_device_data.motion_blur_second_pass_seen)
               reason += " second_pass_not_seen;";
            if (!game_device_data.motion_blur_replay_states[0].valid)
               reason += " replay_state0_invalid;";
            if (!game_device_data.motion_blur_replay_states[1].valid)
               reason += " replay_state1_invalid;";
            if (!game_device_data.taa_output_texture.get())
               reason += " taa_output_texture=null;";
         }
         LogExpectedCustomDrawSkipped("MotionBlur", reason);
      }
#endif
      // Never stall tonemap if motion blur replay is unavailable in this frame.
      game_device_data.motion_blur_pending.store(false, std::memory_order_release);
      game_device_data.motion_blur_seen = false;
      game_device_data.motion_blur_first_pass_seen = false;
      game_device_data.motion_blur_second_pass_seen = false;
      game_device_data.motion_blur_output_ready = false;
      game_device_data.motion_blur_invocation_count = 0;
      game_device_data.motion_blur_replay_states[0].Reset();
      game_device_data.motion_blur_replay_states[1].Reset();
   }

   // Tonemap writes to cutscene_intermediate_rtv for UIEncode consumption
   if (game_device_data.tonemap_draw_pending.load(std::memory_order_acquire))
   {
      const bool use_sr_input = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
      const bool after_sr = !use_sr_input || device_data.has_drawn_sr;
      const bool ready = after_sr && CanDrawNativeTonemapPass(pipeline_color_srv, game_device_data);
      if (ready)
      {
         const bool tonemap_ok = run_chained_color_pass("Tonemap", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            DrawNativeTonemapPass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv);
            out_srv = game_device_data.cutscene_intermediate_srv.get();
            return true; });
         game_device_data.tonemap_draw_pending.store(false, std::memory_order_release);
         game_device_data.exposure_texture = nullptr;
         game_device_data.exposure_texture_srv = nullptr;
         game_device_data.bloom_texture_srv = nullptr;
#if TEST || DEVELOPMENT
         if (!tonemap_ok)
         {
            LogExpectedCustomDrawSkipped("Tonemap", "DrawNativeTonemapPass did not produce output SRV");
         }
#endif
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason;
         if (!after_sr)
         {
            reason = "pending replay, but SR output is not ready yet";
         }
         else
         {
            reason = "prerequisite missing:";
            if (!pipeline_color_srv)
               reason += " pipeline_color_srv=null;";
            if (!game_device_data.exposure_texture_srv.get())
               reason += " exposure_texture_srv=null;";
            if (!game_device_data.bloom_texture_srv.get())
               reason += " bloom_texture_srv=null;";
            if (!game_device_data.taa_output_texture.get())
               reason += " taa_output_texture=null;";
         }
         LogExpectedCustomDrawSkipped("Tonemap", reason);
      }
#endif
   }

   if (game_device_data.cutscene_gamma_pending.load(std::memory_order_acquire))
   {
      if (CanDrawNativeCutsceneGammaPass(pipeline_color_srv, game_device_data))
      {
         run_chained_color_pass("CutsceneGamma", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            DrawNativeCutsceneGammaPass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv);
            out_srv = game_device_data.cutscene_gamma_srv.get();
            return true; });
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason = "prerequisite missing:";
         if (!pipeline_color_srv)
            reason += " pipeline_color_srv=null;";
         if (!game_device_data.taa_output_texture.get())
            reason += " taa_output_texture=null;";
         if (!game_device_data.cutscene_gamma_replay_state.valid)
            reason += " cutscene_gamma_replay_state_invalid;";
         LogExpectedCustomDrawSkipped("CutsceneGamma", reason);
      }
#endif
      game_device_data.cutscene_gamma_pending.store(false, std::memory_order_release);
      game_device_data.cutscene_gamma_replay_state.Reset();
   }

   if (game_device_data.cutscene_color_grade_pending.load(std::memory_order_acquire))
   {
      if (CanDrawNativeCutsceneColorGradePass(pipeline_color_srv, game_device_data))
      {
         run_chained_color_pass("CutsceneColorGrade", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            DrawNativeCutsceneColorGradePass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv);
            out_srv = game_device_data.cutscene_color_grade_srv.get();
            return true; });
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason = "prerequisite missing:";
         if (!pipeline_color_srv)
            reason += " pipeline_color_srv=null;";
         if (!game_device_data.taa_output_texture.get())
            reason += " taa_output_texture=null;";
         if (!game_device_data.cutscene_color_grade_replay_state.valid)
            reason += " cutscene_color_grade_replay_state_invalid;";
         LogExpectedCustomDrawSkipped("CutsceneColorGrade", reason);
      }
#endif
      game_device_data.cutscene_color_grade_pending.store(false, std::memory_order_release);
      game_device_data.cutscene_color_grade_replay_state.Reset();
   }

   if (game_device_data.cutscene_overlay_modulate_pending.load(std::memory_order_acquire))
   {
      if (CanDrawNativeCutsceneOverlayModulatePass(pipeline_color_srv, game_device_data))
      {
         const bool overlay_modulate_ok = run_chained_color_pass("CutsceneOverlayModulate", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            DrawNativeCutsceneOverlayModulatePass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv);
            out_srv = game_device_data.cutscene_overlay_modulate_srv.get();
            return true; });
#if TEST || DEVELOPMENT
         if (!overlay_modulate_ok)
         {
            LogExpectedCustomDrawSkipped("CutsceneOverlayModulate", "DrawNativeCutsceneOverlayModulatePass did not produce output SRV");
         }
#endif
         game_device_data.cutscene_overlay_modulate_pending.store(false, std::memory_order_release);
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason = "prerequisite missing:";
         if (!pipeline_color_srv)
            reason += " pipeline_color_srv=null;";
         if (!game_device_data.taa_output_texture.get())
            reason += " taa_output_texture=null;";
         if (!game_device_data.cutscene_overlay_modulate_replay_state.valid)
            reason += " cutscene_overlay_modulate_replay_state_invalid;";
         LogExpectedCustomDrawSkipped("CutsceneOverlayModulate", reason);
      }
#endif
      game_device_data.cutscene_overlay_modulate_replay_state.Reset();
   }

   if (game_device_data.cutscene_overlay_blend_pending.load(std::memory_order_acquire))
   {
      if (CanDrawNativeCutsceneOverlayBlendPass(pipeline_color_srv, game_device_data))
      {
         run_chained_color_pass("CutsceneOverlayBlend", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            DrawNativeCutsceneOverlayBlendPass(native_device_context, cmd_list_data, device_data, game_device_data, input_srv, game_device_data.cutscene_intermediate_rtv.get());
            out_srv = game_device_data.cutscene_intermediate_srv.get();
            return true; });
         game_device_data.cutscene_overlay_blend_pending.store(false, std::memory_order_release);
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason = "prerequisite missing:";
         if (!pipeline_color_srv)
            reason += " pipeline_color_srv=null;";
         if (!game_device_data.cutscene_intermediate_rtv.get())
            reason += " cutscene_intermediate_rtv=null;";
         if (!game_device_data.taa_output_texture.get())
            reason += " taa_output_texture=null;";
         if (!game_device_data.cutscene_overlay_blend_replay_state.valid)
            reason += " cutscene_overlay_blend_replay_state_invalid;";
         LogExpectedCustomDrawSkipped("CutsceneOverlayBlend", reason);
      }
#endif

      game_device_data.cutscene_overlay_blend_replay_state.Reset();
   }

   if (game_device_data.outline_pending.load(std::memory_order_acquire))
   {
      if (CanDrawNativeOutlinePass(pipeline_color_srv, game_device_data))
      {
         const bool outline_ok = run_chained_color_pass("OutlineCS", [&](ID3D11ShaderResourceView* input_srv, ID3D11ShaderResourceView*& out_srv) -> bool
            {
            if (!DrawNativeOutlinePass(native_device_context, device_data, game_device_data, input_srv))
            {
               return false;
            }

            out_srv = game_device_data.outline_srv.get();
            return true; });
#if TEST || DEVELOPMENT
         if (!outline_ok)
         {
            LogExpectedCustomDrawSkipped("OutlineCS", "DrawNativeOutlinePass failed");
         }
#endif
      }
#if TEST || DEVELOPMENT
      else
      {
         std::string reason = "prerequisite missing:";
         if (!pipeline_color_srv)
            reason += " pipeline_color_srv=null;";
         if (!game_device_data.outline_replay_state.valid)
            reason += " outline_replay_state_invalid;";
         if (!game_device_data.taa_output_texture.get())
            reason += " taa_output_texture=null;";
         LogExpectedCustomDrawSkipped("OutlineCS", reason);
      }
#endif
      game_device_data.outline_pending.store(false, std::memory_order_release);
      game_device_data.outline_replay_state.Reset();
   }

   {
      bool can_draw_ui_encode = CanDrawNativeUIEncodePass(pipeline_color_srv, game_device_data);
      const bool sr_enabled = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#if TEST || DEVELOPMENT
      if (sr_enabled)
      {
         if (!game_device_data.pre_sr_encode_srv.get() || !device_data.has_drawn_sr)
         {
            std::string reason = "SR enabled but prerequisite missing:";
            if (!game_device_data.pre_sr_encode_srv.get())
               reason += " pre_sr_encode_srv=null;";
            if (!device_data.has_drawn_sr)
               reason += " sr_not_drawn;";
            LogExpectedCustomDrawSkipped("UIEncode (SR composite inputs)", reason);
         }
      }
#endif

#if TEST || DEVELOPMENT
      if (!can_draw_ui_encode)
      {
         std::string reason = "prerequisite missing:";
         if (!pipeline_color_srv)
            reason += " pipeline_color_srv=null;";
         if (!game_device_data.taa_output_texture_rtv.get())
            reason += " taa_output_texture_rtv=null;";
         LogExpectedCustomDrawSkipped("UIEncode", reason);
      }
#endif

      if (can_draw_ui_encode)
      {
         DrawNativeUIEncodePass(native_device_context, cmd_list_data, device_data, game_device_data, pipeline_color_srv);
      }
   }

   draw_state_stack.Restore(native_device_context);
   compute_state_stack.Restore(native_device_context);
}
