#pragma once
// =============================================================================
#include <cfloat>
#include <d3d11.h>
#include "log.hpp"

// Extract the exposure texture from the autoexposure pass
// FFXV autoexposure uses slot 0 for the exposure texture
// Returns true if the exposure texture was successfully extracted
static bool ExtractExposureTexture(
   ID3D11Device* native_device,
   ID3D11DeviceContext* native_device_context,
   GameDeviceDataFFXV& game_device_data)
{
   // Get the SRV from slot 0 (exposure texture in autoexposure pass)
   ComPtr<ID3D11ShaderResourceView> exposure_srv;
   native_device_context->CSGetShaderResources(0, 1, exposure_srv.put());

   if (exposure_srv.get() == nullptr)
      return false;

   // Extract the underlying resource
   ComPtr<ID3D11Resource> exposure_resource;
   exposure_srv->GetResource(exposure_resource.put());
   if (exposure_resource.get() == nullptr)
      return false;
   // get description, create new texture if needed with same desc and copy data
   if (game_device_data.exposure_texture.get() == nullptr)
   {
      ComPtr<ID3D11Texture2D> exposure_texture;
      HRESULT hr = exposure_resource->QueryInterface(exposure_texture.put());
      if (FAILED(hr))
      {
         return false;
      }
      D3D11_TEXTURE2D_DESC exposure_texture_desc;
      exposure_texture->GetDesc(&exposure_texture_desc);
      hr = native_device->CreateTexture2D(&exposure_texture_desc, nullptr, game_device_data.exposure_texture.put());
      if (FAILED(hr))
      {
         return false;
      }
      Log_Debug(
         reshade::log::level::info,
         std::format("Created exposure texture: {}x{}, format={}",
            exposure_texture_desc.Width,
            exposure_texture_desc.Height,
            static_cast<uint32_t>(exposure_texture_desc.Format)));
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
   ID3D11ShaderResourceView** out_velocity_srv = nullptr)
{
   Log_Debug(
      reshade::log::level::info,
      "Extracting TAA shader resources");
   // Get all pixel shader resources
   ComPtr<ID3D11ShaderResourceView> ps_shader_resources[16];
   native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

   // Validate that required SRVs are present
   if (!ps_shader_resources[0].get() || !ps_shader_resources[3].get() || !ps_shader_resources[6].get())
      return false;

   // Extract resources from known FFXV TAA slots and store in game_device_data
   game_device_data.sr_source_color = nullptr;
   ps_shader_resources[0]->GetResource(game_device_data.sr_source_color.put());

   game_device_data.depth_buffer = nullptr;
   ps_shader_resources[3]->GetResource(game_device_data.depth_buffer.put());

   // Store the original motion vectors resource (before decode)
   ComPtr<ID3D11Resource> original_velocity;
   ps_shader_resources[6]->GetResource(original_velocity.put());

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

   Log_Debug(
      reshade::log::level::info,
      std::format("Extracted TAA shader resources: source_color={}, depth_buffer={}, velocity={}",
         (game_device_data.sr_source_color.get() != nullptr) ? "yes" : "no",
         (game_device_data.depth_buffer.get() != nullptr) ? "yes" : "no",
         (original_velocity.get() != nullptr) ? "yes" : "no"));

   // Validate that all resources were successfully extracted
   return game_device_data.sr_source_color.get() != nullptr &&
          game_device_data.depth_buffer.get() != nullptr &&
          original_velocity.get() != nullptr;
}

// Setup DLSS/FSR output texture
// Modifies device_data.sr_output_color as needed
// Returns the output texture and whether it supports UAV
// When target_resolution is provided and differs from the RTV size, the SR output
// is created at target_resolution (upscaling mode) instead of matching the RTV.
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
   ComPtr<ID3D11Resource> output_color_resource;
   output_rtv->GetResource(output_color_resource.put());

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
   {
      D3D11_TEXTURE2D_DESC sr_out_desc;
      device_data.sr_output_color->GetDesc(&sr_out_desc);
      Log_Debug(
         reshade::log::level::info,
         std::format("SR output texture: RTV(input)={}x{} -> sr_output_color={}x{}, format={}, supports UAV={}",
            out_texture_desc.Width, out_texture_desc.Height,
            sr_out_desc.Width, sr_out_desc.Height,
            static_cast<uint32_t>(sr_out_desc.Format),
            out_supports_uav ? "yes" : "no"));
   }
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

   ComPtr<ID3D11Resource> velocity_resource;
   velocity_srv->GetResource(velocity_resource.put());

   if (!velocity_resource.get())
      return false;

   ComPtr<ID3D11Texture2D> velocity_texture;
   HRESULT hr = velocity_resource->QueryInterface(velocity_texture.put());
   if (FAILED(hr))
      return false;

   D3D11_TEXTURE2D_DESC velocity_desc;
   velocity_texture->GetDesc(&velocity_desc);

   // Check if we need to recreate the motion vectors texture
   bool needs_recreate = !game_device_data.sr_motion_vectors;
   if (!needs_recreate)
   {
      ComPtr<ID3D11Texture2D> existing_mv_texture;
      hr = game_device_data.sr_motion_vectors->QueryInterface(existing_mv_texture.put());
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
      mv_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

      game_device_data.sr_motion_vectors_uav = nullptr;
      game_device_data.sr_motion_vectors = nullptr;

      ComPtr<ID3D11Texture2D> mv_texture;
      hr = native_device->CreateTexture2D(&mv_desc, nullptr, mv_texture.put());
      if (FAILED(hr))
         return false;

      hr = mv_texture->QueryInterface(game_device_data.sr_motion_vectors.put());
      if (FAILED(hr))
         return false;

      hr = native_device->CreateUnorderedAccessView(game_device_data.sr_motion_vectors.get(), nullptr, game_device_data.sr_motion_vectors_uav.put());
      if (FAILED(hr))
      {
         game_device_data.sr_motion_vectors = nullptr;
         return false;
      }
   }
   // MV texture is always created at velocity source dimensions with R32G32_FLOAT
   Log_Debug(
      reshade::log::level::info,
      std::format("Motion vector decode target: source={}x{}, fmt={} -> output={}x{}, fmt=R32G32_FLOAT",
         velocity_desc.Width, velocity_desc.Height,
         static_cast<uint32_t>(velocity_desc.Format),
         velocity_desc.Width, velocity_desc.Height));
   return game_device_data.sr_motion_vectors_uav.get() != nullptr;
}

// Decode motion vectors using the custom shader
// Renders the motion vector decode shader to sr_motion_vectors_uav
static void DecodeMotionVectors(
   ID3D11DeviceContext* native_device_context,
   CommandListData& cmd_list_data,
   DeviceData& device_data,
   ID3D11ShaderResourceView* depth_srv,
   ID3D11ShaderResourceView* velocity_srv,
   ID3D11UnorderedAccessView* output_uav)
{
   // Get dispatch dimensions from the output UAV resource
   ComPtr<ID3D11Resource> uav_resource;
   output_uav->GetResource(uav_resource.put());
   ComPtr<ID3D11Texture2D> uav_texture;
   uav_resource->QueryInterface(uav_texture.put());
   D3D11_TEXTURE2D_DESC uav_desc;
   uav_texture->GetDesc(&uav_desc);

   auto cs_it = device_data.native_compute_shaders.find(CompileTimeStringHash("Decode MVs CS"));
   if (cs_it == device_data.native_compute_shaders.end() || !cs_it->second.get())
   {
      Log_Debug(
         reshade::log::level::warning,
         "Decode MVs compute shader not found - cannot decode motion vectors");
      return;
   }
   ComPtr<ID3D11Buffer> taa_cb_buffer;
   native_device_context->PSGetConstantBuffers(0, 1, taa_cb_buffer.put());

   native_device_context->CSSetConstantBuffers(0, 1, &taa_cb_buffer);

   // Bind depth texture to slot 0 and velocity texture to slot 1
   native_device_context->CSSetShaderResources(0, 1, &velocity_srv);
   native_device_context->CSSetShaderResources(1, 1, &depth_srv);

   // Set up the pipeline for motion vector decoding
   // Use our custom Fullscreen VS that outputs TEXCOORD0 (UV coordinates) which the decode PS expects
   native_device_context->CSSetShader(cs_it->second.get(), nullptr, 0);

   // Render to the motion vectors unordered access view
   native_device_context->OMSetRenderTargets(0, nullptr, nullptr);
   native_device_context->CSSetUnorderedAccessViews(0, 1, &output_uav, nullptr);

   // native_device_context->Draw(4, 0);
   uint dispatch_x = (uav_desc.Width + 7) / 8;
   uint dispatch_y = (uav_desc.Height + 7) / 8;
   native_device_context->Dispatch(dispatch_x, dispatch_y, 1);
   ID3D11UnorderedAccessView* null_uav = nullptr;
   native_device_context->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
}

static void CheckAndExtractTAABuffer(reshade::api::device* device, reshade::api::resource resource)
{
   DeviceData& device_data = *device->get_private_data<DeviceData>();
   auto& game_device_data = *static_cast<GameDeviceDataFFXV*>(device_data.game);
   cbTemporalAA* const taa_cb_data = reinterpret_cast<cbTemporalAA*>(game_device_data.cb_taa_buffer_map_data);
   // Sanity check to make sure is the right cbuffer
   // check screensize has same aspect ratio as device_data.output_resolution
   // check screen size zw = 1/width, zh = 1/height
   // check bools in raw data are either 0 or 1
   // check jitters not both 0 and smaller than 0.5 in abs value
   const float aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;
   const float cb_aspect_ratio = taa_cb_data->g_screenSize.x / taa_cb_data->g_screenSize.y;
   const bool aspect_ratio_match = std::abs(aspect_ratio - cb_aspect_ratio) < 0.01f;
   const bool inverse_w_match = taa_cb_data->g_screenSize.z - (1.f / taa_cb_data->g_screenSize.x) < FLT_EPSILON;
   const bool inverse_h_match = taa_cb_data->g_screenSize.w - (1.f / taa_cb_data->g_screenSize.y) < FLT_EPSILON;
   const bool bools_valid = (taa_cb_data->g_gamePaused <= 1u) &&
                            (taa_cb_data->g_hairUseAlphaTest <= 1u) &&
                            (taa_cb_data->g_waterResponsiveAA <= 1u);
   const bool jitters_valid = (std::abs(taa_cb_data->g_uvJitterOffset.x) < 0.5f && std::abs(taa_cb_data->g_uvJitterOffset.y) < 0.5f) &&
                              (std::abs(taa_cb_data->g_uvJitterOffset.x) > 0.f || std::abs(taa_cb_data->g_uvJitterOffset.y) > 0.f);
   if (aspect_ratio_match && inverse_w_match && inverse_h_match && bools_valid && jitters_valid)
   {
      game_device_data.found_taa_cb = true;
      Log_Debug(
         reshade::log::level::info,
         "Found TAA constant buffer at size 256 bytes");
      // Store a copy of the cbuffer data
      if (!game_device_data.taa_cb_data)
      {
         game_device_data.taa_cb_data = std::make_unique<cbTemporalAA>();
      }
      std::memcpy(game_device_data.taa_cb_data.get(), taa_cb_data, sizeof(cbTemporalAA));

      // Store jitters for SR - convert from UV space to pixel space
      // DLSS/FSR expect jitters in pixel space (typically -0.5 to 0.5 range scaled by resolution)
      game_device_data.taa_jitters.x = taa_cb_data->g_uvJitterOffset.x * taa_cb_data->g_screenSize.x;
      game_device_data.taa_jitters.y = taa_cb_data->g_uvJitterOffset.y * taa_cb_data->g_screenSize.y;

      // Store the render resolution from TAA cbuffer
      device_data.render_resolution = {taa_cb_data->g_screenSize.x, taa_cb_data->g_screenSize.y};
   }
   else
   {
      Log_Debug(
         reshade::log::level::info,
         "Rejected TAA constant buffer candidate");
   }
   game_device_data.cb_taa_buffer_map_data = nullptr;
   game_device_data.cb_taa_buffer = nullptr;
}

static void ExtractCameraData(DeviceData& device_data, GameDeviceDataFFXV& game_device_data, const void* view_cb_data_raw)
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

   // get jitter from projection matrix to compare with taa jitter
   // NDC to screen space
   game_device_data.projection_jitters.x = proj.m20 * device_data.render_resolution.x * -0.5f;
   game_device_data.projection_jitters.y = proj.m21 * device_data.render_resolution.y * 0.5f;

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

   Log_Debug(
      reshade::log::level::info,
      std::format("Extracted Camera Data: FOV={:.4f}, Near={:.4f}, Far={:.4f}",
         game_device_data.camera_fov,
         game_device_data.camera_near,
         game_device_data.camera_far));
}

static void CheckAndExtractPerViewGlobalsBuffer(reshade::api::device* device, reshade::api::resource resource, const void* data)
{
   DeviceData& device_data = *device->get_private_data<DeviceData>();
   auto& game_device_data = *static_cast<GameDeviceDataFFXV*>(device_data.game);

   const uint8_t* view_cb_data_raw = reinterpret_cast<const uint8_t*>(data);

   Log_Debug(
      reshade::log::level::info,
      "Checking projection matrix for Per-View Globals constant buffer");

   // Check size of the viewport in cbuffer, make sure it has reasonable values
   int2 size = {
      *reinterpret_cast<const int*>(view_cb_data_raw + offsetof(IView_Combined_cbView, ViewPort.Size)),
      *reinterpret_cast<const int*>(view_cb_data_raw + offsetof(IView_Combined_cbView, ViewPort.Size) + sizeof(int))
   };
   int width = static_cast<int>(device_data.render_resolution.x);
   int height = static_cast<int>(device_data.render_resolution.y);
   if (size.x != width || size.y != height)
   {
      // check aspect ratio match with some tolerance to account for upscaling
      float aspect_ratio = static_cast<float>(size.x) / static_cast<float>(size.y);
      float output_aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;
      if (std::abs(aspect_ratio - output_aspect_ratio) > 0.01f)
      {
         Log_Debug(
            reshade::log::level::info,
            std::format("Rejected Per-View Globals candidate due to viewport size mismatch: cb_size={}x{}, output_size={}x{}",
               size.x, size.y, width, height));
         return;
      }
   }

   // Sanity check: check if Projection * InvProjection is Identity
   Math::Matrix44 proj, inv_proj;
   std::memcpy(&proj, view_cb_data_raw, sizeof(Math::Matrix44));
   std::memcpy(&inv_proj, view_cb_data_raw + offsetof(IView_Combined_cbView, InvProjection), sizeof(Math::Matrix44));

   Log_Debug(
      reshade::log::level::info,
      "Done copying projection matrix for Per-View Globals constant buffer");
   Math::Matrix44 identity;
   identity.SetIdentity();

   // The matrix library seems to use row-major or has logic that handles multiplication order.
   // Assuming standard multiplication logic from the header (AxB).
   Math::Matrix44 product = proj * inv_proj;

   // Use a tolerance for float comparison
   bool is_identity = Math::MatrixAlmostEqual(product, identity, 1e-4f);
   bool has_jitter = std::abs(proj.m20) > FLT_EPSILON || std::abs(proj.m21) > FLT_EPSILON;
   if (is_identity && has_jitter)
   {
      game_device_data.found_per_view_globals = true;

      Log_Debug(
         reshade::log::level::info,
         "Found Per-View Globals constant buffer");

      ExtractCameraData(device_data, game_device_data, view_cb_data_raw);
      game_device_data.has_processed_view_buffer = true;
   }
   else
   {
      Log_Debug(
         reshade::log::level::info,
         "Rejected Per-View Globals constant buffer candidate");
   }
}
