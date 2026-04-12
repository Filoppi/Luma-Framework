// =============================================================================
#include <cfloat>

// Extract the exposure texture from the autoexposure pass
// FFXV autoexposure uses slot 0 for the exposure texture
// Returns true if the exposure texture was successfully extracted
static bool ExtractExposureTexture(
   ID3D11Device* native_device,
   ID3D11DeviceContext* native_device_context,
   GameDeviceDataFFXV& game_device_data)
{
   // Get the SRV from slot 0 (exposure texture in autoexposure pass)
   com_ptr<ID3D11ShaderResourceView> exposure_srv;
   native_device_context->CSGetShaderResources(0, 1, &exposure_srv);

   if (exposure_srv.get() == nullptr)
      return false;

   // Extract the underlying resource
   com_ptr<ID3D11Resource> exposure_resource;
   exposure_srv->GetResource(&exposure_resource);
   if (exposure_resource.get() == nullptr)
      return false;
   // get description, create new texture if needed with same desc and copy data
   if (game_device_data.exposure_texture.get() == nullptr)
   {
      com_ptr<ID3D11Texture2D> exposure_texture;
      HRESULT hr = exposure_resource->QueryInterface(&exposure_texture);
      if (FAILED(hr))
      {
         return false;
      }
      D3D11_TEXTURE2D_DESC exposure_texture_desc;
      exposure_texture->GetDesc(&exposure_texture_desc);
      hr = native_device->CreateTexture2D(&exposure_texture_desc, nullptr, &game_device_data.exposure_texture);
      if (FAILED(hr))
      {
         return false;
      }
#if DEVELOPMENT && DEBUG_LOG
      reshade::log::message(
         reshade::log::level::info,
         std::format("Created exposure texture: {}x{}, format={}",
            exposure_texture_desc.Width,
            exposure_texture_desc.Height,
            static_cast<uint32_t>(exposure_texture_desc.Format))
            .c_str());
#endif // DEVELOPMENT
   }
   native_device_context->CopyResource(game_device_data.exposure_texture.get(), exposure_resource.get());

   return true;
}

// Extract shader resources from the TAA shader state and store in game_device_data
// FFXV TAA slots: source_color=0, depth=3, velocity=6
// Returns true if all required resources are present and valid
static bool ExtractTAAShaderResources(
   ID3D11Device* native_device,
   ID3D11DeviceContext* native_device_context,
   GameDeviceDataFFXV& game_device_data,
   ID3D11ShaderResourceView** out_depth_srv = nullptr,
   ID3D11ShaderResourceView** out_velocity_srv = nullptr,
   bool is_using_upscaling = false)
{
#if DEVELOPMENT && DEBUG_LOG
   reshade::log::message(
      reshade::log::level::info,
      "Extracting TAA shader resources");
#endif // DEVELOPMENT
   // Get all pixel shader resources
   com_ptr<ID3D11ShaderResourceView> ps_shader_resources[16];
   native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

   // Validate that required SRVs are present
   if (!ps_shader_resources[0].get() || !ps_shader_resources[3].get() || !ps_shader_resources[6].get())
      return false;

   // Extract resources from known FFXV TAA slots and store in game_device_data
   game_device_data.sr_source_color = nullptr;
   ps_shader_resources[0]->GetResource(&game_device_data.sr_source_color);

   game_device_data.depth_buffer = nullptr;
   ps_shader_resources[3]->GetResource(&game_device_data.depth_buffer);

   // Make a backup copy of the depth buffer if using upscaling
   if (is_using_upscaling && game_device_data.depth_buffer.get() != nullptr)
   {
      com_ptr<ID3D11Texture2D> depth_texture;
      HRESULT hr = game_device_data.depth_buffer->QueryInterface(&depth_texture);

      if (!SUCCEEDED(hr))
         return false;

      D3D11_TEXTURE2D_DESC depth_desc;
      depth_texture->GetDesc(&depth_desc);
      bool recreate_backup = game_device_data.sr_depth_backup.get() == nullptr;
      if (!recreate_backup)
      {
         D3D11_TEXTURE2D_DESC backup_desc;
         game_device_data.sr_depth_backup->GetDesc(&backup_desc);
         if (backup_desc.Width == depth_desc.Width &&
             backup_desc.Height == depth_desc.Height &&
             backup_desc.Format == depth_desc.Format)
         {
            recreate_backup = false;
         }
         else
         {
            recreate_backup = true;
         }
      }
      if (recreate_backup)
      {
         game_device_data.sr_depth_backup = nullptr;
         hr = native_device->CreateTexture2D(&depth_desc, nullptr, &game_device_data.sr_depth_backup);
         if (FAILED(hr))
         {
            return false;
         }
      }
      native_device_context->CopyResource(game_device_data.sr_depth_backup.get(), game_device_data.depth_buffer.get());
   }

   // Store the original motion vectors resource (before decode)
   com_ptr<ID3D11Resource> original_velocity;
   ps_shader_resources[6]->GetResource(&original_velocity);

   // Output SRVs for motion vector decoding if requested
   if (out_depth_srv)
   {
      *out_depth_srv = ps_shader_resources[3].get();
      if (*out_depth_srv)
         (*out_depth_srv)->AddRef();
   }
   if (out_velocity_srv)
   {
      *out_velocity_srv = ps_shader_resources[6].get();
      if (*out_velocity_srv)
         (*out_velocity_srv)->AddRef();
   }
#if DEVELOPMENT && DEBUG_LOG
   reshade::log::message(
      reshade::log::level::info,
      std::format("Extracted TAA shader resources: source_color={}, depth_buffer={}, velocity={}",
         (game_device_data.sr_source_color.get() != nullptr) ? "yes" : "no",
         (game_device_data.depth_buffer.get() != nullptr) ? "yes" : "no",
         (original_velocity.get() != nullptr) ? "yes" : "no")
         .c_str());

#endif // DEVELOPMENT

   // Validate that all resources were successfully extracted
   return game_device_data.sr_source_color.get() != nullptr &&
          game_device_data.depth_buffer.get() != nullptr &&
          original_velocity.get() != nullptr;
}

// Setup DLSS/FSR output texture
// Modifies device_data.sr_output_color as needed
// Returns the output texture and whether it supports UAV
static bool SetupSROutput(
   ID3D11Device* native_device,
   DeviceData& device_data,
   ID3D11RenderTargetView* output_rtv,
   com_ptr<ID3D11Texture2D>& out_output_color,
   D3D11_TEXTURE2D_DESC& out_texture_desc,
   bool& out_supports_uav,
   bool& out_output_changed)
{
   out_output_changed = false;
   out_supports_uav = false;

   if (!output_rtv)
      return false;

   // Get output texture from render target
   com_ptr<ID3D11Resource> output_color_resource;
   output_rtv->GetResource(&output_color_resource);

   HRESULT hr = output_color_resource->QueryInterface(&out_output_color);
   if (FAILED(hr))
      return false;

   out_output_color->GetDesc(&out_texture_desc);

   // Check if output supports UAV
   constexpr bool use_native_uav = false; // Force intermediate texture to prevent output corruption
   out_supports_uav = use_native_uav && (out_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;

   // Get SR instance data for min resolution check
   auto* sr_instance_data = device_data.GetSRInstanceData();
   if (sr_instance_data)
   {
      if (out_texture_desc.Width < sr_instance_data->min_resolution ||
          out_texture_desc.Height < sr_instance_data->min_resolution)
         return false;
   }

   // Create or reuse output texture if needed
   if (!out_supports_uav)
   {
      D3D11_TEXTURE2D_DESC dlss_output_desc = out_texture_desc;
      dlss_output_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

      if (device_data.sr_output_color.get())
      {
         D3D11_TEXTURE2D_DESC prev_desc;
         device_data.sr_output_color->GetDesc(&prev_desc);
         out_output_changed = prev_desc.Width != dlss_output_desc.Width ||
                              prev_desc.Height != dlss_output_desc.Height ||
                              prev_desc.Format != dlss_output_desc.Format;
      }

      if (!device_data.sr_output_color.get() || out_output_changed)
      {
         device_data.sr_output_color = nullptr;
         hr = native_device->CreateTexture2D(&dlss_output_desc, nullptr, &device_data.sr_output_color);
         if (FAILED(hr))
            return false;
      }

      if (!device_data.sr_output_color.get())
         return false;
   }
   else
   {
      device_data.sr_output_color = out_output_color;
   }
#if DEVELOPMENT && DEBUG_LOG
   reshade::log::message(
      reshade::log::level::info,
      std::format("SR output texture: {}x{}, format={}, supports UAV={}",
         out_texture_desc.Width,
         out_texture_desc.Height,
         static_cast<uint32_t>(out_texture_desc.Format),
         out_supports_uav ? "yes" : "no")
         .c_str());
#endif // DEVELOPMENT
   return true;
}

// Create or update the motion vector decode render target
// Stores result in game_device_data.sr_motion_vectors and sr_motion_vectors_rtv
static bool SetupMotionVectorDecodeTarget(
   ID3D11Device* native_device,
   GameDeviceDataFFXV& game_device_data,
   ID3D11ShaderResourceView* velocity_srv)
{
   if (!velocity_srv)
      return false;

   com_ptr<ID3D11Resource> velocity_resource;
   velocity_srv->GetResource(&velocity_resource);

   if (!velocity_resource.get())
      return false;

   com_ptr<ID3D11Texture2D> velocity_texture;
   HRESULT hr = velocity_resource->QueryInterface(&velocity_texture);
   if (FAILED(hr))
      return false;

   D3D11_TEXTURE2D_DESC velocity_desc;
   velocity_texture->GetDesc(&velocity_desc);

   // Check if we need to recreate the motion vectors texture
   bool needs_recreate = !game_device_data.sr_motion_vectors.get();
   if (!needs_recreate && game_device_data.sr_motion_vectors.get())
   {
      com_ptr<ID3D11Texture2D> existing_mv_texture;
      hr = game_device_data.sr_motion_vectors->QueryInterface(&existing_mv_texture);
      if (SUCCEEDED(hr))
      {
         D3D11_TEXTURE2D_DESC existing_desc;
         existing_mv_texture->GetDesc(&existing_desc);
         needs_recreate = existing_desc.Width != velocity_desc.Width ||
                          existing_desc.Height != velocity_desc.Height ||
                          existing_desc.Format != DXGI_FORMAT_R32G32_FLOAT;
      }
   }

   if (needs_recreate)
   {
      // Create new motion vectors texture with R32G32 for higher precision
      D3D11_TEXTURE2D_DESC mv_desc = velocity_desc;
      mv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
      mv_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

      game_device_data.sr_motion_vectors_rtv = nullptr;
      game_device_data.sr_motion_vectors = nullptr;

      com_ptr<ID3D11Texture2D> mv_texture;
      hr = native_device->CreateTexture2D(&mv_desc, nullptr, &mv_texture);
      if (FAILED(hr))
         return false;

      hr = mv_texture->QueryInterface(&game_device_data.sr_motion_vectors);
      if (FAILED(hr))
         return false;

      hr = native_device->CreateRenderTargetView(game_device_data.sr_motion_vectors.get(), nullptr, &game_device_data.sr_motion_vectors_rtv);
      if (FAILED(hr))
      {
         game_device_data.sr_motion_vectors = nullptr;
         return false;
      }
   }
#if DEVELOPMENT && DEBUG_LOG
   reshade::log::message(
      reshade::log::level::info,
      std::format("Motion vector decode target: {}x{}, format={}",
         velocity_desc.Width,
         velocity_desc.Height,
         static_cast<uint32_t>(DXGI_FORMAT_R32G32_FLOAT))
         .c_str());
#endif // DEVELOPMENT
   return game_device_data.sr_motion_vectors_rtv.get() != nullptr;
}

// Decode motion vectors using the custom shader
// Renders the motion vector decode shader to sr_motion_vectors_rtv
static void DecodeMotionVectors(
   ID3D11DeviceContext* native_device_context,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   ID3D11ShaderResourceView* depth_srv,
   ID3D11ShaderResourceView* velocity_srv,
   ID3D11RenderTargetView* output_rtv)
{
   // Get render target dimensions for viewport setup
   com_ptr<ID3D11Resource> rtv_resource;
   output_rtv->GetResource(&rtv_resource);
   com_ptr<ID3D11Texture2D> rtv_texture;
   rtv_resource->QueryInterface(&rtv_texture);
   D3D11_TEXTURE2D_DESC rtv_desc;
   rtv_texture->GetDesc(&rtv_desc);

   // Set viewport to match render target
   D3D11_VIEWPORT viewport = {};
   viewport.TopLeftX = 0.0f;
   viewport.TopLeftY = 0.0f;
   viewport.Width = static_cast<float>(rtv_desc.Width);
   viewport.Height = static_cast<float>(rtv_desc.Height);
   viewport.MinDepth = 0.0f;
   viewport.MaxDepth = 1.0f;
   native_device_context->RSSetViewports(1, &viewport);

   // Set Luma constant buffers (LumaData contains motion matrix and jitter pixels used by the decode shader)
   SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
   SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);

   // Bind depth texture to slot 0 and velocity texture to slot 1
   native_device_context->PSSetShaderResources(0, 1, &depth_srv);
   native_device_context->PSSetShaderResources(1, 1, &velocity_srv);

   // Set up the pipeline for motion vector decoding
   // Use our custom Fullscreen VS that outputs TEXCOORD0 (UV coordinates) which the decode PS expects
   native_device_context->VSSetShader(device_data.native_vertex_shaders[CompileTimeStringHash("Fullscreen VS")].get(), nullptr, 0);
   native_device_context->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Decode MVs PS")].get(), nullptr, 0);
   native_device_context->CSSetShader(nullptr, nullptr, 0);
   native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

   // Set point sampler for accurate texture sampling
   ID3D11SamplerState* const sampler_state_point = device_data.sampler_state_point.get();
   native_device_context->PSSetSamplers(0, 1, &sampler_state_point);

   // Render to the motion vectors render target
   native_device_context->OMSetRenderTargets(1, &output_rtv, nullptr);
   native_device_context->Draw(4, 0);

   // Explicitly unbind the render target
   ID3D11RenderTargetView* null_rtv = nullptr;
   native_device_context->OMSetRenderTargets(1, &null_rtv, nullptr);
}

static void CheckAndExtractTAABuffer(reshade::api::device* device, reshade::api::resource resource)
{
   ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
   ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
   DeviceData& device_data = *device->get_private_data<DeviceData>();
   auto& game_device_data = GetGameDeviceData(device_data);
   cbTemporalAA* taa_cb_data = reinterpret_cast<cbTemporalAA*>(game_device_data.cb_taa_buffer_map_data);
   int32_t* int_data = reinterpret_cast<int32_t*>(game_device_data.cb_taa_buffer_map_data);
   // Sanity check to make sure is the right cbuffer
   // check screensize has same aspect ratio as device_data.output_resolution
   // check screen size zw = 1/width, zh = 1/height
   // check bools in raw data are either 0 or 1
   // check jitters not both 0 and smaller than 0.5 in abs value
   const float aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;
   const float cb_aspect_ratio = taa_cb_data->g_screenSize.x / taa_cb_data->g_screenSize.y;
   const bool aspect_ratio_match = std::abs(aspect_ratio - cb_aspect_ratio) < FLT_EPSILON;
   const bool inverse_w_match = taa_cb_data->g_screenSize.z - (1.f / taa_cb_data->g_screenSize.x) < FLT_EPSILON;
   const bool inverse_h_match = taa_cb_data->g_screenSize.w - (1.f / taa_cb_data->g_screenSize.y) < FLT_EPSILON;
   const bool bools_valid = (int_data[196 / sizeof(int32_t)] == 0 || int_data[196 / sizeof(int32_t)] == 1) &&
                            (int_data[200 / sizeof(int32_t)] == 0 || int_data[200 / sizeof(int32_t)] == 1) &&
                            (int_data[204 / sizeof(int32_t)] == 0 || int_data[204 / sizeof(int32_t)] == 1);
   const bool jitters_valid = (std::abs(taa_cb_data->g_uvJitterOffset.x) < 0.5f && std::abs(taa_cb_data->g_uvJitterOffset.y) < 0.5f) &&
                              (std::abs(taa_cb_data->g_uvJitterOffset.x) > 0.f || std::abs(taa_cb_data->g_uvJitterOffset.y) > 0.f);
   if (aspect_ratio_match && inverse_w_match && inverse_h_match && bools_valid && jitters_valid)
   {
      game_device_data.found_taa_cb = true;
#if DEVELOPMENT && DEBUG_LOG
      reshade::log::message(
         reshade::log::level::info,
         "Found TAA constant buffer at size 256 bytes");
#endif // DEVELOPMENT
      // Store a copy of the cbuffer data
      if (!game_device_data.taa_cb_data)
      {
         game_device_data.taa_cb_data = std::make_unique<cbTemporalAA>();
      }
      std::memcpy(game_device_data.taa_cb_data.get(), taa_cb_data, sizeof(cbTemporalAA));

      // Store jitters for SR - convert from UV space to pixel space
      // DLSS/FSR expect jitters in pixel space (typically -0.5 to 0.5 range scaled by resolution)
      projection_jitters.x = taa_cb_data->g_uvJitterOffset.x * taa_cb_data->g_screenSize.x;
      projection_jitters.y = taa_cb_data->g_uvJitterOffset.y * taa_cb_data->g_screenSize.y;

      // Store the render resolution from TAA cbuffer
      device_data.render_resolution = {taa_cb_data->g_screenSize.x, taa_cb_data->g_screenSize.y};

      // Check if game is using upscaling (render resolution < output resolution)
      // The swapchain resolution is stored in device_data.output_resolution
      const float render_width = taa_cb_data->g_screenSize.x;
      const float render_height = taa_cb_data->g_screenSize.y;
      const float output_width = device_data.output_resolution.x;
      const float output_height = device_data.output_resolution.y;

      // Consider upscaling active if render resolution is notably smaller than output
      // Use a small epsilon to avoid floating point issues
      game_device_data.is_using_upscaling = (render_width < output_width - FLT_EPSILON) || (render_height < output_height - FLT_EPSILON);
#if DEVELOPMENT && DEBUG_LOG
      if (game_device_data.is_using_upscaling)
      {
         reshade::log::message(
            reshade::log::level::info,
            std::format("FFXV upscaling detected: render {}x{} -> output {}x{}",
               (int)render_width, (int)render_height, (int)output_width, (int)output_height)
               .c_str());
      }
#endif
   }
   else
   {
#if DEVELOPMENT && DEBUG_LOG
      reshade::log::message(
         reshade::log::level::info,
         "Rejected TAA constant buffer candidate");
#endif // DEVELOPMENT
   }
   game_device_data.cb_taa_buffer_map_data = nullptr;
   game_device_data.cb_taa_buffer = nullptr;
}

static void ExtractCameraData(GameDeviceDataFFXV& game_device_data, const void* view_cb_data_raw)
{
   Math::Matrix44 proj;
   std::memcpy(&proj, static_cast<const uint8_t*>(view_cb_data_raw) + offsetof(IView_Combined_cbView, Projection), sizeof(Math::Matrix44));

   // Extract FOV, Near, Far from Projection Matrix
   // FFXV likely uses DirectX Right-Handed (looking down -Z).
   // m11 = cot(fov/2) -> tan(fov/2) = 1 / m11 -> fov/2 = atan(1/m11) -> fov = 2 * atan(1/m11)

   float m11 = proj.m11;
   if (m11 != 0.0f)
   {
      game_device_data.camera_fov = 2.0f * std::atan(1.0f / m11);
   }

   // Near and Far planes extraction
   // Assuming DirectX Right-Handed Projection:
   // Z_ndc = (A * Z + B) / -Z   (where input Z is negative in View Space)
   // A = m22 = f / (n - f)      (approx -1 for f >> n)
   // B = m32 = n * f / (n - f)  (approx -n)
   float m22 = proj.m22;
   float m32 = proj.m32; // Index 14 (Col 3, Row 2) = M23 = B

   float n = 0.0f;
   float f = 0.0f;

   if (m22 != 0.0f)
   {
      // n = B / A
      n = m32 / m22;

      // f = B / (1 + A)
      if ((1.0f + m22) != 0.0f)
      {
         f = m32 / (1.0f + m22);
      }
   }

   game_device_data.camera_near = n;
   game_device_data.camera_far = f;

#if DEVELOPMENT && DEBUG_LOG
   reshade::log::message(
      reshade::log::level::info,
      std::format("Extracted Camera Data: FOV={:.4f}, Near={:.4f}, Far={:.4f}",
         game_device_data.camera_fov,
         game_device_data.camera_near,
         game_device_data.camera_far)
         .c_str());
#endif
}

static void CheckAndExtractPerViewGlobalsBuffer(reshade::api::device* device, reshade::api::resource resource, const void* data)
{
   ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
   ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
   DeviceData& device_data = *device->get_private_data<DeviceData>();
   auto& game_device_data = GetGameDeviceData(device_data);

   const uint8_t* view_cb_data_raw = reinterpret_cast<const uint8_t*>(data);

   reshade::log::message(
      reshade::log::level::info,
      "Checking projection matrix for Per-View Globals constant buffer");

   // Sanity check: check if Projection * InvProjection is Identity
   Math::Matrix44 proj, inv_proj;
   std::memcpy(&proj, view_cb_data_raw + offsetof(IView_Combined_cbView, Projection), sizeof(Math::Matrix44));
   std::memcpy(&inv_proj, view_cb_data_raw + offsetof(IView_Combined_cbView, InvProjection), sizeof(Math::Matrix44));

   reshade::log::message(
      reshade::log::level::info,
      "Done copying projection matrix for Per-View Globals constant buffer");
   Math::Matrix44 identity;
   identity.SetIdentity();

   // The matrix library seems to use row-major or has logic that handles multiplication order.
   // Assuming standard multiplication logic from the header (AxB).
   Math::Matrix44 product = proj * inv_proj;

   // Use a tolerance for float comparison
   bool is_identity = Math::MatrixAlmostEqual(product, identity, FLT_EPSILON);

   // Check viewport aspect ratio
   reshade::log::message(
      reshade::log::level::info,
      "Checking viewport aspect ratio for Per-View Globals constant buffer");

   IView_Combined_cbView::Viewport viewport;
   std::memcpy(&viewport, view_cb_data_raw + offsetof(IView_Combined_cbView, ViewPort), sizeof(IView_Combined_cbView::Viewport));

   bool viewport_match = false;
   if (viewport.Size.y > 0 && device_data.output_resolution.y > 0)
   {
      float viewport_aspect = static_cast<float>(viewport.Size.x) / static_cast<float>(viewport.Size.y);
      float output_aspect = static_cast<float>(device_data.output_resolution.x) / static_cast<float>(device_data.output_resolution.y);
      viewport_match = std::abs(viewport_aspect - output_aspect) < FLT_EPSILON;
   }

   if (is_identity && viewport_match)
   {
      game_device_data.found_per_view_globals = true;
      game_device_data.cached_view_buffer = buffer;

#if DEVELOPMENT && DEBUG_LOG
      reshade::log::message(
         reshade::log::level::info,
         "Found Per-View Globals constant buffer");
#endif

      ExtractCameraData(game_device_data, view_cb_data_raw);
      game_device_data.has_processed_view_buffer = true;
   }
   else
   {
#if DEVELOPMENT && DEBUG_LOG
      reshade::log::message(
         reshade::log::level::info,
         "Rejected Per-View Globals constant buffer candidate");
#endif // DEVELOPMENT
   }
}
