// ---------------------------------------------------------------------------
// UI-phase detection and background upscaling for Granblue Fantasy Relink.
//
// scaled_texture (RGBA16F, output resolution) is pre-allocated in OnInitSwapchain
// and kept valid for the lifetime of a swapchain. When the first UI draw is
// detected on a deferred context, the current scene render target is bilinearly
// copied into scaled_texture. All subsequent UI draws on that context are
// redirected to scaled_texture. The output shader always reads from scaled_texture
// with identity UVs — no conditional gating needed because the output command list
// always executes after the UI command list.
// ---------------------------------------------------------------------------

// Creates / recreates scaled_texture at output resolution (DXGI_FORMAT_R16G16B16A16_FLOAT).
// The swapchain is upgraded to scRGB / RGBA16F and scaled_texture is copied to the back
// buffer, so they must match. No-op if size and format are already correct.
static void EnsureScaledTexture(DeviceData& device_data, GameDeviceDataGBFR& game_device_data)
{
   const UINT out_w = static_cast<UINT>(device_data.output_resolution.x);
   const UINT out_h = static_cast<UINT>(device_data.output_resolution.y);
   if (!out_w || !out_h)
      return;

   D3D11_TEXTURE2D_DESC desc = {};
   desc.Width = out_w;
   desc.Height = out_h;
   desc.MipLevels = 1;
   desc.ArraySize = 1;
   desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
   desc.SampleDesc.Count = 1;
   desc.Usage = D3D11_USAGE_DEFAULT;
   desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

   auto& ui_scale = game_device_data.ui_scale;
   CreateOrRecreateTextureIfNeeded(
      game_device_data, device_data.native_device, desc,
      ui_scale.scaled_texture, ui_scale.scaled_texture_srv, ui_scale.scaled_texture_rtv);
}

// Detects the UI phase by blend state / sampler / depth / SRV signature.
// On first detection, copies the scene to an output-resolution texture with
// bilinear filtering and redirects the render target + viewport.
// Returns true when the draw is part of the UI phase.
static bool DetectUIPhase(DeviceData& device_data, ID3D11DeviceContext* ctx, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
{
   auto& game_device_data = *static_cast<GameDeviceDataGBFR*>(device_data.game);
   auto& ui_scale = game_device_data.ui_scale;

   const ID3D11DeviceContext* ui_ctx = game_device_data.ui_scale.ui_detected_context.load(std::memory_order_acquire);
   if (ui_ctx != nullptr)
   {
      if (ui_ctx == ctx)
         return true;
      // Different context — fall through to check blend state.
   }
   if (!original_shader_hashes.Contains(shader_hashes_UIBackgroundDownscale))
      return false;

   // --- Blend state check (any alpha blending) ---
   com_ptr<ID3D11BlendState> blend_state;
   ctx->OMGetBlendState(&blend_state, nullptr, nullptr);
   if (!blend_state)
      return false;

   D3D11_BLEND_DESC bd;
   blend_state->GetDesc(&bd);
   if (!bd.RenderTarget[0].BlendEnable)
      return false;

   // --- Sampler check (bilinear) ---
   com_ptr<ID3D11SamplerState> sampler_state;
   ctx->PSGetSamplers(0, 1, &sampler_state);
   if (!sampler_state)
      return false;
   D3D11_SAMPLER_DESC sd;
   sampler_state->GetDesc(&sd);
   if (sd.Filter != D3D11_FILTER_MIN_MAG_MIP_LINEAR)
      return false;

   // --- Depth disabled ---
   com_ptr<ID3D11DepthStencilState> depth_stencil_state;
   ctx->OMGetDepthStencilState(&depth_stencil_state, nullptr);
   if (!depth_stencil_state)
      return false;
   D3D11_DEPTH_STENCIL_DESC dsd;
   depth_stencil_state->GetDesc(&dsd);
   if (dsd.DepthEnable)
      return false;

   // --- SRV 0 format check (BC7, BC4, 1x1 RGBA8, or RGBA16F at output resolution) ---
   // The last case covers the pause-game UI path where the game copies a previous
   // scene frame (already at output resolution and in the upgraded RGBA16F format)
   // as the background before drawing UI on top.
   com_ptr<ID3D11ShaderResourceView> srv;
   ctx->PSGetShaderResources(0, 1, &srv);
   if (srv.get())
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
      srv->GetDesc(&srv_desc);
      const DXGI_FORMAT fmt = srv_desc.Format;
      bool srv_matches = false;
      if (fmt == DXGI_FORMAT_R16G16B16A16_FLOAT)
      {
         com_ptr<ID3D11Resource> srv_resource;
         srv->GetResource(&srv_resource);
         com_ptr<ID3D11Texture2D> srv_texture;
         srv_resource->QueryInterface(&srv_texture);

         if (srv_texture)
         {
            D3D11_TEXTURE2D_DESC tex_desc;
            srv_texture->GetDesc(&tex_desc);
            UINT width = static_cast<UINT>(device_data.output_resolution.x);
            UINT height = static_cast<UINT>(device_data.output_resolution.y);
            if (tex_desc.Width != width && tex_desc.Height != height)
               return false;
         }
      }
   }

   ASSERT_ONCE(ui_ctx == nullptr);

   game_device_data.ui_scale.ui_detected_context.store(ctx, std::memory_order_release);

   ui_scale.ui_phase_seen_this_frame = true;

   com_ptr<ID3D11RenderTargetView> ui_rtv;
   ctx->OMGetRenderTargets(1, &ui_rtv, nullptr);
   if (!ui_rtv)
   {
      return true;
   }

   com_ptr<ID3D11Resource> ui_rtv_resource;
   ui_rtv->GetResource(&ui_rtv_resource);
   com_ptr<ID3D11Texture2D> ui_rtv_texture;
   ui_rtv_resource->QueryInterface(&ui_rtv_texture);
   if (!ui_rtv_texture)
   {
      return true;
   }

   D3D11_TEXTURE2D_DESC ui_rtv_texture_desc;
   ui_rtv_texture->GetDesc(&ui_rtv_texture_desc);
   if (ui_rtv_texture_desc.Width == static_cast<UINT>(device_data.output_resolution.x) &&
       ui_rtv_texture_desc.Height == static_cast<UINT>(device_data.output_resolution.y))
   {
      return true;
   }
   ui_scale.ui_scaled_needed = true;

   // Store the original scene texture for later comparison.
   ui_scale.original_ui_rtv = ui_rtv.get();

   // Fall through to the redirect for this first UI draw.
   return true;
}

// Redirects a UI draw to the upscaled texture:
//   - Swaps the render target if it still points to the original scene.
//   - Scales the viewport from render resolution to output resolution.
//   - Swaps any SRVs that reference the original scene texture.
static void RedirectUIDrawToScaledTarget(
   ID3D11DeviceContext* ctx,
   DeviceData& device_data,
   GameDeviceDataGBFR& game_device_data)
{
   auto& ui_scale = game_device_data.ui_scale;
   if (!ui_scale.scaled_texture_srv)
      return;

   if (!ui_scale.ui_scaled_needed)
      return;

   com_ptr<ID3D11RenderTargetView> current_rtv;
   ctx->OMGetRenderTargets(1, &current_rtv, nullptr);
   const bool is_detected_ui_rtv = AreViewsOfSameResource(ui_scale.original_ui_rtv.get(), current_rtv.get());

   // --- Render target redirect ---
   {
      if (is_detected_ui_rtv)
      {
         ID3D11RenderTargetView* const scaled_rtv = ui_scale.scaled_texture_rtv.get();
         ctx->OMSetRenderTargets(1, &scaled_rtv, nullptr);
      }
   }

   // --- Viewport redirect ---
   {
      // Only rewrite viewport when drawing to the same RTV detected at UI-phase entry.
      if (is_detected_ui_rtv)
      {
         D3D11_VIEWPORT vp;
         UINT num_vp = 1;
         ctx->RSGetViewports(&num_vp, &vp);
         const UINT render_w = static_cast<UINT>(device_data.render_resolution.x);
         const UINT render_h = static_cast<UINT>(device_data.render_resolution.y);
         if (static_cast<UINT>(vp.Width) == render_w && static_cast<UINT>(vp.Height) == render_h)
         {
            vp.Width = device_data.output_resolution.x;
            vp.Height = device_data.output_resolution.y;
            ctx->RSSetViewports(1, &vp);
         }
      }
   }

   // --- SRV redirect (slots 0–3) ---
   {
      static constexpr UINT kMaxSRVCheck = 4;
      com_ptr<ID3D11ShaderResourceView> srvs[kMaxSRVCheck];
      ctx->PSGetShaderResources(0, kMaxSRVCheck, reinterpret_cast<ID3D11ShaderResourceView**>(&srvs[0]));
      for (UINT i = 0; i < kMaxSRVCheck; ++i)
      {
         if (!srvs[i])
            continue;
         if (AreViewsOfSameResource(srvs[i].get(), ui_scale.original_ui_rtv.get()))
         {
            ID3D11ShaderResourceView* const scaled_srv = ui_scale.scaled_texture_srv.get();
            ctx->PSSetShaderResources(i, 1, &scaled_srv);
         }
      }
   }
}

static void CaptureOutputReplayState(ID3D11DeviceContext* ctx, GBFROutputReplayState& state)
{
   ID3D11DeviceContext1* ctx1 = nullptr;
   if (FAILED(ctx->QueryInterface(&ctx1)))
      return;
   state.Reset();
   ctx->VSGetShader(state.vs.put(), nullptr, nullptr);
   ctx->PSGetShader(state.ps.put(), nullptr, nullptr);
   ctx->OMGetRenderTargets(1, state.rtv.put(), nullptr);
   UINT num_viewports = 1;
   ctx->RSGetViewports(&num_viewports, &state.viewport);
   ctx->PSGetShaderResources(0, 1, state.original_t0_srv.put());
   ctx1->PSGetConstantBuffers1(1, 1, state.original_b1_cb.buffer.put(), &state.original_b1_cb.first_constant, &state.original_b1_cb.num_constants);
   ctx->RSGetState(state.rs_state.put());
   ctx->OMGetBlendState(state.blend_state.put(), state.blend_factor, &state.sample_mask);
   ctx->IAGetInputLayout(state.input_layout.put());
   {
      ID3D11Buffer* vbs[2] = {};
      ctx->IAGetVertexBuffers(0, 2, vbs, state.vb_strides, state.vb_offsets);
      state.vertex_buffers[0].attach(vbs[0]);
      state.vertex_buffers[1].attach(vbs[1]);
   }
   ctx->PSGetSamplers(0, 1, state.ps_sampler.put());
   state.valid = state.vs && state.ps && state.rtv;
}

// Replays the Output shader draw in the immediate context.
// If the UI phase ran this frame, reads from scaled_texture (SR output + composited UI).
// If not (cutscene, photo mode), replays with the game's original captured source.
// DC_UI is guaranteed complete by the time this is called, so ui_phase_seen_this_frame
// is safe to read here without a race.
static void ReplayOutputDraw(
   ID3D11DeviceContext* ctx,
   GameDeviceDataGBFR& game_device_data)
{
   auto& ui_scale = game_device_data.ui_scale;
   if (!ui_scale.output_replay_state.valid)
      return;
   ID3D11DeviceContext1* ctx1 = nullptr;
   if (FAILED(ctx->QueryInterface(&ctx1)))
      return;

   auto& state = ui_scale.output_replay_state;
   ctx->VSSetShader(state.vs.get(), nullptr, 0);
   ctx->PSSetShader(state.ps.get(), nullptr, 0);
   ctx->GSSetShader(nullptr, nullptr, 0);
   ctx->HSSetShader(nullptr, nullptr, 0);
   ctx->DSSetShader(nullptr, nullptr, 0);

   ctx->RSSetState(state.rs_state.get());
   ctx->OMSetBlendState(state.blend_state.get(), state.blend_factor, state.sample_mask);

   ctx->OMSetRenderTargets(0, nullptr, nullptr);
   ID3D11RenderTargetView* const rtv = state.rtv.get();
   ctx->OMSetRenderTargets(1, &rtv, nullptr);
   ctx->RSSetViewports(1, &state.viewport);

   ctx->IASetInputLayout(state.input_layout.get());
   {
      ID3D11Buffer* vbs[2] = {state.vertex_buffers[0].get(), state.vertex_buffers[1].get()};
      ctx->IASetVertexBuffers(0, 2, vbs, state.vb_strides, state.vb_offsets);
   }
   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

   ID3D11Buffer* const cb = state.original_b1_cb.buffer.get();
   ctx1->PSSetConstantBuffers1(1, 1, &cb, &state.original_b1_cb.first_constant, &state.original_b1_cb.num_constants);

   if (ui_scale.ui_phase_seen_this_frame && ui_scale.ui_scaled_needed)
   {
      // UI drew onto scaled_texture — present the composited result.
      ID3D11ShaderResourceView* const scaled_srv = ui_scale.scaled_texture_srv.get();
      ctx->PSSetShaderResources(0, 1, &scaled_srv);
   }
   else
   {
      // Game's original source — preserve the captured ring-buffer offset and scale.
      ID3D11Buffer* const cb = state.original_b1_cb.buffer.get();
      ctx1->PSSetConstantBuffers1(1, 1, &cb, &state.original_b1_cb.first_constant, &state.original_b1_cb.num_constants);
      ID3D11ShaderResourceView* const srv = ui_scale.output_replay_state.original_t0_srv.get();
      ctx->PSSetShaderResources(0, 1, &srv);
   }

   {
      ID3D11SamplerState* const st = state.ps_sampler.get();
      ctx->PSSetSamplers(0, 1, &st);
   }

   ctx->Draw(4, 0);
}