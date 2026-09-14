#define GAME_MAFIA_III 1

#include "..\..\Core\core.hpp"

#if 0
bool IsProjectionMatrix(const DirectX::XMMATRIX& m, float tolerance = 1e-5f)
{
   DirectX::XMFLOAT4X4 mat;
   DirectX::XMStoreFloat4x4(&mat, m);

   auto approx = [&](float a, float b) { return std::fabs(a - b) < tolerance; };
#if 1
   if (mat._11 <= 0.5f || mat._11 >= 2.0f || !approx(mat._12, 0.0f) /*|| mat._13 != 0.0f*/ || !approx(mat._14, 0.0f))
      return false;
   if (mat._22 <= 0.5f || mat._22 >= 2.0f || !approx(mat._21, 0.0f) /*|| mat._31 != 0.0f*/ || !approx(mat._41, 0.0f))
      return false;
   if (!Math::AlmostEqual(std::abs(mat._33), 1.0f, 0.02f)) // Depth encode
      return false;
   if (!approx(mat._44, 0.0f))
      return false;
   if (mat._43 == 0.0f)
      return false;
   if (!approx(mat._34, 1.0f) && !approx(mat._34, -1.0f))
      return false;
#else
   // Perspective projection always has m[3][2] == -1 (within tolerance)
   if (!approx(mat._43, -1.0f))
      return false;

   // Typical projection has m[3][3] == 0
   if (!approx(mat._44, 0.0f))
      return false;

   // f_x and f_y (scales) must be > 0
   //if (!(mat._11 > 0.0f && mat._22 > 0.0f))
   //   return false;

   // Depth mapping: m[2][2] between 0 and 1-ish (depends on near/far planes)
   if (!(mat._33 > -10.0f && mat._33 < 10.0f))
      return false;

   // Allow jitter offsets: m[3][0], m[3][1] can be small non-zero
   // Other off-diagonal terms should be ~0
   if (!approx(mat._12, 0.0f) || !approx(mat._21, 0.0f))
      return false;
#endif

   return true;
}

// TODO: move these to DLSS and Matrix files...
// Helper: create a standard row-major D3D-style perspective projection
DirectX::XMMATRIX CreateProjection(float fovY, float aspect, float nearZ, float farZ)
{
   float yScale = 1.0f / tanf(fovY * 0.5f);
   float xScale = yScale / aspect;

   DirectX::XMMATRIX P = {};
   P.r[0] = DirectX::XMVectorSet(xScale, 0, 0, 0);
   P.r[1] = DirectX::XMVectorSet(0, yScale, 0, 0);
   P.r[2] = DirectX::XMVectorSet(0, 0, farZ / (farZ - nearZ), 1.0f);
   P.r[3] = DirectX::XMVectorSet(0, 0, -nearZ * farZ / (farZ - nearZ), 0);
   return P;
}

// Apply subpixel jitter (in NDC units)
DirectX::XMMATRIX ApplyJitter(const DirectX::XMMATRIX& P, float jitterX, float jitterY, float width, float height)
{
   DirectX::XMMATRIX jittered = P;
   // Convert pixel jitter to NDC offset
   float offsetX = jitterX / width;
   float offsetY = jitterY / height;
   // In row-major D3D, offsets go into m[2][0] and m[2][1]
   jittered.r[2] = DirectX::XMVectorSet(offsetX + DirectX::XMVectorGetX(jittered.r[2]), offsetY + DirectX::XMVectorGetY(jittered.r[2]), DirectX::XMVectorGetZ(jittered.r[2]), DirectX::XMVectorGetW(jittered.r[2]));
   return jittered;
}

bool IsViewMatrix(const DirectX::XMMATRIX& m, float tol = 1e-5f)
{
   DirectX::XMFLOAT4X4 mat;
   DirectX::XMStoreFloat4x4(&mat, m);
   auto approx = [&](float a, float b) { return std::abs(a - b) < tol; };

   // Check perspective terms
   if (!approx(mat._14, 0.0f) || !approx(mat._24, 0.0f) || !approx(mat._34, 0.0f))
      return false;

   // Bottom-right
   if (!approx(mat._44, 1.0f))
      return false;

   // Optional: check that upper-left 3x3 is orthogonal
   DirectX::XMVECTOR row0 = DirectX::XMVectorSet(mat._11, mat._12, mat._13, 0.0f);
   DirectX::XMVECTOR row1 = DirectX::XMVectorSet(mat._21, mat._22, mat._23, 0.0f);
   DirectX::XMVECTOR row2 = DirectX::XMVectorSet(mat._31, mat._32, mat._33, 0.0f);

   float dot01 = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(row0, row1)));
   float dot02 = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(row0, row2)));
   float dot12 = std::abs(DirectX::XMVectorGetX(DirectX::XMVector3Dot(row1, row2)));

   if (dot01 > tol || dot02 > tol || dot12 > tol)
      return false; // not orthogonal

   return true;
}

// Reproject a previous-frame NDC position into the current frame's NDC.
// `prevVP` and `currVP` are row-major here; flip if you use column-major.
// inNdc: [-1..1] range. Returns [-1..1].
inline DirectX::XMFLOAT2 ReprojectPrevNdcToCurrNdc(
   const DirectX::XMMATRIX& prevVP,
   const DirectX::XMMATRIX& currVP,
   const DirectX::XMFLOAT2 inNdc,
   float z_mode = 1.0f /* 1 ~ far plane direction proxy */)
{
   // Build prev->curr clip transform: currVP * inverse(prevVP)
   const DirectX::XMMATRIX prevToCurr = DirectX::XMMatrixMultiply(currVP, DirectX::XMMatrixInverse(nullptr, prevVP));

   // Treat skybox/infinite depth as a *direction* through projection.
   // A good, stable proxy is a homogeneous *direction* with w=0.
   // Start from prev-frame clip coords corresponding to NDC=(x,y) on the far direction.
   DirectX::XMVECTOR prevClipDir = DirectX::XMVectorSet(inNdc.x, inNdc.y, z_mode, 0.0f); // w=0 -> direction

   // Transform to current frame clip
   DirectX::XMVECTOR currClip = DirectX::XMVector4Transform(prevClipDir, prevToCurr);

   // Perspective divide to NDC
   // If w happens to be ~0 (rare for sensible camera states), fall back to z_mode as w.
   float x = DirectX::XMVectorGetX(currClip);
   float y = DirectX::XMVectorGetY(currClip);
   float w = DirectX::XMVectorGetW(currClip);
   if (fabsf(w) < 1e-6f) w = z_mode;

   return { x / w, y / w };
}

inline DirectX::XMFLOAT2 ExtractUvJitterDelta_FromMatrices_Skybox(
   const DirectX::XMMATRIX& prevVP,
   const DirectX::XMMATRIX& currVP)
{
   // Map the prev-frame NDC origin (0,0) as a *direction* to curr-frame NDC
   const DirectX::XMFLOAT2 prevNdcOrigin = { 0.0f, 0.0f };
   const auto currNdc = ReprojectPrevNdcToCurrNdc(prevVP, currVP, prevNdcOrigin);

   // If there were *only* jitter changes, currNdc would be a pure translation in NDC.
   // Convert that to UV-space delta:
   // NDC delta -> UV delta : delta uv = delta ndc * 0.5
   return { currNdc.x * 0.5f, currNdc.y * 0.5f };
}
#endif

// Apply rotation (yaw/pitch/roll), translation, and FoV tweak
DirectX::XMMATRIX ModifyViewProjection(
    DirectX::XMMATRIX matrix,
    float yaw, float pitch, float roll,
    float x, float y, float z,
    float fov_scale /* 1.0 = unchanged, >1 = wider FoV, <1 = narrower */)
{
   // --- 1. Build extra world-space camera transform (inverse of user motion) ---
   // Since we can't split view/proj, treat motion as a world->world transform
   DirectX::XMMATRIX rot = DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
   DirectX::XMMATRIX trans = DirectX::XMMatrixTranslation(x, y, z);

   if (test_index != 17)
   {
      matrix = XMMatrixMultiply(matrix, rot);
      for (uint8_t i = 0; i < 4; i++)
      {
         matrix.r[0].m128_f32[i] += x * matrix.r[3].m128_f32[i];
         matrix.r[1].m128_f32[i] += y * matrix.r[3].m128_f32[i];
         matrix.r[2].m128_f32[i] += z * matrix.r[3].m128_f32[i];
      }
      DirectX::XMMATRIX proj_mod = DirectX::XMMatrixScaling(fov_scale, fov_scale, 1.f);
      return XMMatrixMultiply(proj_mod, matrix);
   }

   // This acts like moving the *camera* in world space
   DirectX::XMMATRIX world_mod = rot * trans;

   // --- 2. Projection-space tweak (FoV) ---
   // Approximate by scaling x/y in clip space
   DirectX::XMMATRIX proj_mod = DirectX::XMMatrixScaling(fov_scale, fov_scale, 1.f);

   // --- 3. Combine ---
   // Note row-major D3D style: mul(lhs, rhs) means lhs * rhs
   return XMMatrixMultiply(proj_mod, XMMatrixMultiply(matrix, world_mod));
}

namespace
{
   ShaderHashesList shader_hashes_LinearizeDepth;
   ShaderHashesList shader_hashes_ShadowMapProjections;
   ShaderHashesList shader_hashes_PreTAAFogNearMask;
   ShaderHashesList shader_hashes_PreTAACopy;
   ShaderHashesList shader_hashes_TAA; // Temporal AA
   ShaderHashesList shader_hashes_AA; // Non temporal AA
   ShaderHashesList shader_hashes_Tonemap;
   ShaderHashesList shader_hashes_PostAAPostProcess;
   ShaderHashesList shader_hashes_EncodeMotionVectors;
   ShaderHashesList shader_hashes_MotionBlur;
   ShaderHashesList shader_hashes_DownscaleMotionVectors;
   ShaderHashesList shader_hashes_Vignette_UI;
   ShaderHashesList shader_hashes_Sprite_UI;
   ShaderHashesList shader_hashes_3D_UI;

   // User settings:
   bool enable_luts_normalization = true;
   float luts_strength = 1.0;
   float luts_yellow_filter_removal = 0.0;
   float sharpening = 0.0;
   bool enable_vignette = true;
   bool allow_motion_blur = true;
   bool taa_enabled = true; // Guess it's enabled as it will be unless the user edited the binary

   // User live settings:
   bool hide_gameplay_ui = false;
   bool enable_camera_mode = false;
   float3 camera_mode_translation = {};
   float3 camera_mode_rotation = {};
   float camera_mode_fov_scale = 1.0;

   static std::vector<std::byte*> pattern_1_addresses;
   static std::vector<std::byte*> pattern_2_addresses;
   static std::vector<std::byte*> pattern_3_addresses;

#if DEVELOPMENT //TODOFT5: delete
   std::map<ID3D11Buffer*, void*> res_map;
   std::vector<DirectX::XMMATRIX> prevs;
   std::vector<std::array<float, 4>> prevs2;
#endif

   // TODO: move to luma shared tools?
   bool IsRTSwapchain(ID3D11DeviceContext* native_device_context, const DeviceData& device_data)
   {
      bool is_rt_swapchain = false;
      com_ptr<ID3D11RenderTargetView> render_target_view;
      native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);
      if (render_target_view)
      {
         com_ptr<ID3D11Resource> rt_resource;
         render_target_view->GetResource(&rt_resource);
         is_rt_swapchain = device_data.back_buffers.contains((uint64_t)rt_resource.get()); // At least one of the two won't be nullptr
      }
      return is_rt_swapchain;
   }
}

struct GameDeviceDataMafiaIII final : public GameDeviceData
{
   com_ptr<ID3D11Resource> motion_vectors;
   com_ptr<ID3D11Resource> depth;

   com_ptr<ID3D11Resource> last_motion_vectors;
   com_ptr<ID3D11RenderTargetView> last_motion_vectors_rtv;

   com_ptr<ID3D11Texture3D> corrected_lut_texture_3d;
   com_ptr<ID3D11ShaderResourceView> corrected_lut_srv;
   com_ptr<ID3D11UnorderedAccessView> corrected_lut_uav;

   // The post tonemapping resource, being ping ponged between SRVs and RTVs
   com_ptr<ID3D11Resource> post_processed_scene;

   DirectX::XMMATRIX prev_view_projection_mat;
   DirectX::XMMATRIX view_projection_mat;

   float2 taa_jitters = {};

   bool try_draw_dlss_next = false;

   bool has_drawn_taa = false;
   bool has_drawn_aa = false;
   bool has_drawn_tonemap = false;
   bool has_drawn_ui_pre_vignette = false;
   bool has_drawn_ui_vignette = false;

   bool found_per_view_globals = false;
   bool updated_per_view_globals_post_taa = false;

#if DEVELOPMENT
   bool has_cleared_motion_vectors = false;
#endif
};

class MafiaIII final : public Game
{
public:
   static const GameDeviceDataMafiaIII& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataMafiaIII*>(device_data.game);
   }
   static GameDeviceDataMafiaIII& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMafiaIII*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1'); // This game looks less crushed with sRGB, but we took precautions for that
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_LUMA", '1', true, false, "Allows disabling the mod's improvements to the game's look", 1},
         {"ENABLE_CHROMATIC_ABERRATION", '1', true, false, "Allows disabling the game's chromatic aberration", 1},
         {"ENABLE_FILM_GRAIN", '1', true, false, "Allows disabling the game's film grain effect (it's not always present)", 1},
         {"ENABLE_DITHERING", '1', true, false, "Allows disabling the game's dithering (it isn't particularly useful in HDR, but it can help with banding in the sky)", 1},
         {"ALLOW_AA", '1', true, false, "The game uses FXAA at the end, which wasn't really a good combination with TAA\nIf Luma's Super Resolution is used, this is already skipped\nThis is better off if sharpening is used", 1},
#if DEVELOPMENT || TEST
         {"STRETCH_ORIGINAL_TONEMAPPER", '0', true, false, "An alternative HDR implementation that doesn't look good", 1},
#endif
         {"ENABLE_SHARPENING", '1', true, false, "Native sharpening to combat the game's blurriness", 1},
         {"ENABLE_AUTO_HDR", '1', true, false, "Enables an SDR to HDR conversion for Videos and car's Rear View Mirror (HUD)", 1},
         {"FIX_VIDEOS_COLOR_SPACE", '1', true, false, "Videos were incorrectly decoded as BT.601 instead of BT.709, making them more red than intended", 1},
         {"ENABLE_CITY_LIGHTS_BOOST", '1', true, false, "Boost up all the transparent lights like lamp posts and car highlights etc, they look nicer in HDR", 1},
         {"ENABLE_LUT_EXTRAPOLATION", '1', true, false, "Use Luma's signature technique for expanding Color Grading LUTs from SDR to HDR,\nthis might better represent the look the game devs wanted to go for, and have a nice highlights rolloff", 1},
         {"EXPAND_COLOR_GAMUT", '1', true, false, "Do tonemapping in a wider color gamut, to minimize hue shifts and get more saturated shadow, though this can change the look of the game a bit", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      native_shaders_definitions.emplace(CompileTimeStringHash("Draw Sky Motion Vectors"), ShaderDefinition{ "Luma_DrawSkyMotionVectors", reshade::api::pipeline_subobject_type::pixel_shader });

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
#if DEVELOPMENT
         reshade::register_event<reshade::addon_event::clear_render_target_view>(MafiaIII::OnClearRenderTargetView);
#endif
         reshade::register_event<reshade::addon_event::map_buffer_region>(MafiaIII::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(MafiaIII::OnUnmapBufferRegion);
         reshade::register_event<reshade::addon_event::update_buffer_region>(MafiaIII::OnUpdateBufferRegion);

         HMODULE module_handle = GetModuleHandle(nullptr); // Handle to the current executable
         auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
         auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(module_handle) + dos_header->e_lfanew);

         std::byte* base = reinterpret_cast<std::byte*>(module_handle);
         std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;

         std::vector<std::byte> pattern;

         // Unknown author. From pcgw.
         pattern = { std::byte{0x60}, std::byte{0x81}, std::byte{0x00}, std::byte{0x00}, std::byte{0x10}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x10} };
         pattern_1_addresses = System::ScanMemoryForPattern(base, section_size, pattern);

         pattern = { std::byte{0x88}, std::byte{0x48}, std::byte{0x38}, std::byte{0x48}, std::byte{0x8B}, std::byte{0x4E}, std::byte{0x08}, std::byte{0x80}, std::byte{0x79}, std::byte{0x38}, std::byte{0x00}, std::byte{0x74} };
         pattern_2_addresses = System::ScanMemoryForPattern(base, section_size, pattern);
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMafiaIII;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
		auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();

      cb_luma_global_settings.GameSettings.InvOutputRes.x = 1.f / device_data.output_resolution.x;
      cb_luma_global_settings.GameSettings.InvOutputRes.y = 1.f / device_data.output_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if DEVELOPMENT || TEST
      ASSERT_ONCE((stages & reshade::api::shader_stage::compute) == 0); // TODO: test warning about unorm/float mismatch that DX spams, though it seems like there's no CS calls...? Maybe it was ReShade/ImGUI?

      if (original_shader_hashes.Contains(shader_hashes_Tonemap))
      {
         com_ptr<ID3D11RenderTargetView> render_target_view[2];
         native_device_context->OMGetRenderTargets(2, &render_target_view[0], nullptr);
         ASSERT_ONCE(render_target_view[1].get() == nullptr); // The second tonemapper param was actually used?
      }
#endif

      if (original_shader_hashes.Contains(shader_hashes_EncodeMotionVectors))
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(4, 1, &srv);
         ASSERT_ONCE(srv.get());
         if (srv.get())
         {
            game_device_data.motion_vectors = nullptr;
            srv->GetResource(&game_device_data.motion_vectors);
            if (game_device_data.last_motion_vectors != game_device_data.motion_vectors)
            {
               game_device_data.last_motion_vectors = game_device_data.motion_vectors;
               game_device_data.last_motion_vectors_rtv = nullptr;
               if (game_device_data.last_motion_vectors)
               {
                  native_device->CreateRenderTargetView(game_device_data.last_motion_vectors.get(), nullptr, &game_device_data.last_motion_vectors_rtv);
               }
            }
         }
         return DrawOrDispatchOverrideType::None;
      }

      // Note: this might happen twice in a frame! But it'd be the same resource.
      // It happens at random places, so we can't afford putting in order checks.
      // Just because there's the rear view mirror rendering before the main scene etc, we allow it to run multiple times and take the last depth.
      if (original_shader_hashes.Contains(shader_hashes_LinearizeDepth))
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv);
         ASSERT_ONCE(srv.get());
         if (srv.get())
         {
            game_device_data.depth = nullptr;
            srv->GetResource(&game_device_data.depth);
         }
         return DrawOrDispatchOverrideType::None;
      }

#if 0 // TODO: optimize... maybe we could store a flag on whether we are in a menu? We need to run the following checks anyway for AutoHDR etc
      if (!game_device_data.motion_vectors)
      {
         return DrawOrDispatchOverrideType::None; // Nothing else to do
      }
      // This runs fairly late in rendering so most passes have now passed!
      if (!game_device_data.depth)
      {
         return DrawOrDispatchOverrideType::None; // Nothing else to do
      }
#endif

      bool do_dlss = false;

      if (taa_enabled)
      {
         if (original_shader_hashes.Contains(shader_hashes_TAA))
         {
#if ENABLE_SR
            if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
            {
               // Skip following TAA passes
               if (!game_device_data.has_drawn_taa && !device_data.has_drawn_sr)
               {
                  do_dlss = true;
               }
            }
#endif // ENABLE_SR
            game_device_data.has_drawn_taa = true; // This happens 3 times, for different parts of the image
            device_data.taa_detected = true;
         }
      }
      // The game's TAA shaders don't run if TAA is disabled (which is essential for proper DLSS),
      // so hook to the calls just before it would have been
      else
      {
         if (original_shader_hashes.Contains(shader_hashes_PreTAAFogNearMask))
         {
            game_device_data.try_draw_dlss_next = true;
         }
         else
         {
            if (original_shader_hashes.Contains(shader_hashes_PreTAACopy) && game_device_data.try_draw_dlss_next)
            {
               // Finish up the current draw call, as we don't want to skip it
               native_device_context->Draw(6, 0); // TODO: verify

#if ENABLE_SR
               if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
               {
                  do_dlss = true;

                  // Pretend TAA has happened, because it will happen (with DLSS)
                  game_device_data.has_drawn_taa = true;
                  device_data.taa_detected = true;
               }
#endif // ENABLE_SR
            }
            game_device_data.try_draw_dlss_next = false;
         }
      }

#if ENABLE_SR
      if (do_dlss)
      {
         assert(device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && !device_data.has_drawn_sr);

         // 0 Raw Jitter Source Color (HDR/Linear)
         // 1 Previous TAA output (smooth)
         // 2 Encoded Motion Vectors (in a weird format, not usable by DLSS, but there's raw ones from previous passes)
         com_ptr<ID3D11ShaderResourceView> ps_shader_resources[3];
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), &ps_shader_resources[0]);

         ASSERT_ONCE(game_device_data.found_per_view_globals);

         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1 or 2
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
         const bool dlss_inputs_valid = ps_shader_resources[0].get() != nullptr && render_target_views[0].get() != nullptr;
         ASSERT_ONCE(dlss_inputs_valid);

         if (dlss_inputs_valid)
         {
            auto* sr_instance_data = device_data.GetSRInstanceData();
            ASSERT_ONCE(sr_instance_data);

            com_ptr<ID3D11Resource> output_color_resource;
            render_target_views[0]->GetResource(&output_color_resource);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT hr = output_color_resource->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));

            D3D11_TEXTURE2D_DESC taa_output_texture_desc;
            output_color->GetDesc(&taa_output_texture_desc);

            SR::SettingsData settings_data;
            settings_data.output_width = unsigned int(device_data.output_resolution.x + 0.5);
            settings_data.output_height = unsigned int(device_data.output_resolution.y + 0.5);
            settings_data.render_width = unsigned int(device_data.render_resolution.x + 0.5);
            settings_data.render_height = unsigned int(device_data.render_resolution.y + 0.5);
            settings_data.hdr = true;
            settings_data.inverted_depth = true;
            settings_data.mvs_jittered = true;
            settings_data.auto_exposure = true;
            // MVs in UV space, so we need to scale by the render resolution to transform to pixel space
            settings_data.mvs_x_scale = device_data.render_resolution.x;
            settings_data.mvs_y_scale = device_data.render_resolution.y;
            settings_data.render_preset = dlss_render_preset;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

            bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
            bool dlss_output_changed = false;

            constexpr bool dlss_use_native_uav = true;
            bool dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
            // Create a copy that supports Unordered Access if it wasn't already supported
            if (!dlss_output_supports_uav)
            {
               D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
               dlss_output_texture_desc.Width = std::lrintf(device_data.output_resolution.x);
               dlss_output_texture_desc.Height = std::lrintf(device_data.output_resolution.y);
               dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

               if (device_data.sr_output_color.get())
               {
                  D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                  device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                  dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
               }
               if (!device_data.sr_output_color.get() || dlss_output_changed)
               {
                  device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                  hr = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
               // Texture creation failed, we can't proceed with DLSS
               if (!device_data.sr_output_color.get())
               {
                  skip_dlss = true;
               }
            }
            else
            {
               ASSERT_ONCE(device_data.sr_output_color == nullptr);
               device_data.sr_output_color = output_color;
            }

            if (!skip_dlss)
            {
               com_ptr<ID3D11Resource> sr_source_color;
               ps_shader_resources[0]->GetResource(&sr_source_color);
#if 0
               com_ptr<ID3D11Resource> depth_buffer;
               if (depth_buffer == nullptr)
               {
                  depth_buffer = nullptr;
                  depth_stencil_view->GetResource(&depth_buffer);
               }
               ASSERT_ONCE(!depth_buffer.get());
#endif
               ASSERT_ONCE(game_device_data.motion_vectors.get() && game_device_data.depth);

               bool reset_dlss = device_data.force_reset_sr || dlss_output_changed;
               device_data.force_reset_sr = false;

               float dlss_pre_exposure = 0.f; // TODO: find exposure? It's t4 of the tonemap shader, mixed up with some other value just before, but the auto exposure is calculated earlier (not sure with what formula, some log stuff).
               float2 jitters = game_device_data.taa_jitters;
#if DEVELOPMENT
               //TODOFT: delete
               {
                  jitters.x *= cb_luma_global_settings.DevSettings[8];
                  jitters.y *= cb_luma_global_settings.DevSettings[9];
               }
#endif

               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = sr_source_color.get();
               draw_data.output_color = device_data.sr_output_color.get();
               draw_data.motion_vectors = game_device_data.motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth.get();
               draw_data.pre_exposure = dlss_pre_exposure;
#if 1
               draw_data.jitter_x = jitters.x;
               draw_data.jitter_y = jitters.y;
#else // TODO
               draw_data.jitter_x = jitters.x * device_data.render_resolution.x * -0.5f;
               draw_data.jitter_y = jitters.y * device_data.render_resolution.y * -0.5f;
#endif
               draw_data.reset = reset_dlss;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
               if (dlss_succeeded)
               {
                  device_data.has_drawn_sr = true;
               }

               if (device_data.has_drawn_sr)
               {
#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = "DLSS";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  if (!dlss_output_supports_uav)
                  {
                     native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                  }
                  else
                  {
                     device_data.sr_output_color = nullptr;
                  }

                  return DrawOrDispatchOverrideType::Replaced;
               }
               else
               {
                  device_data.force_reset_sr = true;
               }
            }
            if (dlss_output_supports_uav)
            {
               device_data.sr_output_color = nullptr;
            }
         }
         return DrawOrDispatchOverrideType::None;
      }
#endif // ENABLE_SR

      // There's probably faster ways of skipping it but this will do
      if (!allow_motion_blur && original_shader_hashes.Contains(shader_hashes_MotionBlur))
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv);
         ASSERT_ONCE(srv.get());
         if (srv.get())
         {
            com_ptr<ID3D11Resource> sr;
            srv->GetResource(&sr);

            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            ASSERT_ONCE(rtv.get());
            if (rtv.get())
            {
               com_ptr<ID3D11Resource> rt;
               rtv->GetResource(&rt);

               if (sr.get() && rt.get())
               {
                  native_device_context->CopyResource(rt.get(), sr.get());
                  return DrawOrDispatchOverrideType::Replaced;
               }
            }
         }
      }

#if 0 // TODO: delete
      if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_ShadowMapProjections))
      {
         uint32_t custom_data_1 = 0;
         uint32_t custom_data_2 = 0;
         // UV space (not NDC), because they are used as texcoord
         float custom_data_3 = game_device_data.taa_jitters.x / (float)device_data.render_resolution.x;
         float custom_data_4 = game_device_data.taa_jitters.y / (float)device_data.render_resolution.y;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, custom_data_2, custom_data_3, custom_data_4);
         updated_cbuffers = true;

         return DrawOrDispatchOverrideType::None;
      }
#endif

      // Make sure we dejitter motion vectors when sampling them, as they'd be jittered with Luma's TAA.
      // These are seemengly the only places that use MVs in the game when Luma's TAA is enabled (the MVs encode passes are for the native TAA, which is skipped).
      if (is_custom_pass && (original_shader_hashes.Contains(shader_hashes_DownscaleMotionVectors) || original_shader_hashes.Contains(shader_hashes_MotionBlur)))
      {
         bool is_motion_blur = true;
         // Make sure the downscale shader isn't shared with other types of downscale other than MVs
         if (original_shader_hashes.Contains(shader_hashes_DownscaleMotionVectors))
         {
            is_motion_blur = false;

            com_ptr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, &srv);
            if (srv.get())
            {
               com_ptr<ID3D11Resource> sr;
               srv->GetResource(&sr);

               is_motion_blur = sr.get() && sr.get() == game_device_data.motion_vectors;
            }

         }
         uint32_t custom_data_1 = 0;
         uint32_t custom_data_2 = 0;
         // We don't flip the Y as we had instead flipped it in the projection matrix, given that that was NDC, but this is UV space. Same for the 2x scaling.
         float custom_data_3 = is_motion_blur ? (-game_device_data.taa_jitters.x / (float)device_data.render_resolution.x) : 0.f;
         float custom_data_4 = is_motion_blur ? (-game_device_data.taa_jitters.y / (float)device_data.render_resolution.y) : 0.f;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, custom_data_2, custom_data_3, custom_data_4);
         updated_cbuffers = true;

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_Tonemap))
      {
         game_device_data.has_drawn_tonemap = true;
         // During cutscenes, AA runs before tonemap (which is the opposite of the usual situation), so we need to do display mapping (peak compression) in the tonemapper, and also convert to gamma space there (unless we implemented proper gamma composition!)
         if (game_device_data.has_drawn_aa)
         {
            device_data.has_drawn_main_post_processing = true;
         }

         bool normalize_lut = true;
         bool is_rt_swapchain = false;

         // Re-apply the original shader in case this is a tonemap call for the separate rear view mirror rendering (optional). It targets UNORM textures and we definitely don't care for making these HDR (it's slow and ugly anyway)
         com_ptr<ID3D11RenderTargetView> render_target_view;
         native_device_context->OMGetRenderTargets(1, &render_target_view, nullptr);
         if (render_target_view)
         {
            com_ptr<ID3D11Resource> rt_resource;
            render_target_view->GetResource(&rt_resource);
            if (rt_resource.get())
            {
               is_rt_swapchain = device_data.back_buffers.contains((uint64_t)rt_resource.get());

               com_ptr<ID3D11Texture2D> rt;
               HRESULT hr = rt_resource->QueryInterface(&rt);
               if (SUCCEEDED(hr) && rt)
               {
                  D3D11_TEXTURE2D_DESC desc;
                  rt->GetDesc(&desc);
                  is_custom_pass = desc.Width == UINT(device_data.output_resolution.x + 0.5) && desc.Height == UINT(device_data.output_resolution.y + 0.5);
                  if (!is_custom_pass)
                  {
                     normalize_lut = false;

                     const uint32_t original_shader_hash = original_shader_hashes.pixel_shaders[0];
                     const auto pipelines_pair = device_data.pipeline_caches_by_shader_hash.find(original_shader_hash);
                     if (pipelines_pair != device_data.pipeline_caches_by_shader_hash.end())
                     {
                        ASSERT_ONCE(pipelines_pair->second.size() == 1);
                        for (const CachedPipeline* cached_pipeline : pipelines_pair->second)
                        {
                           ID3D11PixelShader* native_ps = reinterpret_cast<ID3D11PixelShader*>(cached_pipeline->pipeline.handle);
                           native_device_context->PSSetShader(native_ps, nullptr, 0);
                           break;
                        }
                        return DrawOrDispatchOverrideType::None;
                     }

                     ASSERT_ONCE(false); // We shouldn't really get here, the shader wasn't found?
                  }
                  else
                  {
                     game_device_data.post_processed_scene = rt_resource;
                  }
               }
            }
         }

         if (is_custom_pass)
         {
            bool should_sharpen_and_tonemap = game_device_data.has_drawn_aa;
            bool should_gammify = is_rt_swapchain;
            ASSERT_ONCE(!is_rt_swapchain); // Possibly never the case

            uint32_t custom_data_1 = 0;
            custom_data_1 |= (should_sharpen_and_tonemap ? 1u : 0u) << 0; // set bit 0
            custom_data_1 |= (should_gammify ? 1u : 0u) << 1; // set bit 1
            uint32_t custom_data_2 = 0;
            float custom_data_3 = luts_strength;
            float custom_data_4 = luts_yellow_filter_removal;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, custom_data_2, custom_data_3, custom_data_4);
            updated_cbuffers = true;
         }

         // TODO: does this even ever do anything?
         if (enable_luts_normalization && normalize_lut)
         {
            bool lut_conversion_succeeded = false;

            com_ptr<ID3D11ShaderResourceView> lut_srv;
            native_device_context->PSGetShaderResources(9, 1, &lut_srv);
            if (lut_srv.get())
            {
               com_ptr<ID3D11Resource> lut_r;
               lut_srv->GetResource(&lut_r);

               com_ptr<ID3D11Texture3D> lut_texture_3d;
               HRESULT hr = lut_r ? lut_r->QueryInterface(&lut_texture_3d) : E_FAIL;
               if (SUCCEEDED(hr) && lut_texture_3d)
               {
#if DEVELOPMENT || TEST
                  D3D11_TEXTURE3D_DESC desc;
                  lut_texture_3d->GetDesc(&desc);
                  ASSERT_ONCE(desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS);
                  ASSERT_ONCE(desc.Height == 16 && desc.Width == 16 && desc.Depth == 16); // The compute shader would actually work anyway, but... does this ever happen? Doubt
                  ASSERT_ONCE((desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0); // Why is this one a render target? Do they blend LUTs over time sometimes then? They have a LUT mixer?
#endif

                  if (!game_device_data.corrected_lut_texture_3d)
                  {
                     game_device_data.corrected_lut_texture_3d = nullptr;
                     game_device_data.corrected_lut_srv = nullptr;
                     game_device_data.corrected_lut_uav = nullptr;
                     game_device_data.corrected_lut_texture_3d = CloneTexture<ID3D11Texture3D>(native_device, lut_texture_3d.get(), DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, D3D11_BIND_RENDER_TARGET, false, false, native_device_context);
                     if (game_device_data.corrected_lut_texture_3d)
                     {
                        hr = native_device->CreateShaderResourceView(game_device_data.corrected_lut_texture_3d.get(), nullptr, &game_device_data.corrected_lut_srv);
                        ASSERT_ONCE(SUCCEEDED(hr));
                        hr = native_device->CreateUnorderedAccessView(game_device_data.corrected_lut_texture_3d.get(), nullptr, &game_device_data.corrected_lut_uav);
                        ASSERT_ONCE(SUCCEEDED(hr));
                     }
                  }

                  if (game_device_data.corrected_lut_srv.get() && game_device_data.corrected_lut_uav.get())
                  {
                     DrawStateStack<DrawStateStackType::Compute> draw_state_stack;
                     draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

                     ID3D11ShaderResourceView* const lut_srv_const = lut_srv.get();
                     native_device_context->CSSetShaderResources(0, 1, &lut_srv_const);

                     ID3D11UnorderedAccessView* const corrected_lut_uav = game_device_data.corrected_lut_uav.get();
                     native_device_context->CSSetUnorderedAccessViews(0, 1, &corrected_lut_uav, nullptr);

                     native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Normalize LUT 3D")].get(), nullptr, 0);

                     ID3D11SamplerState* const sampler_state_linear = device_data.sampler_state_linear.get();
                     native_device_context->CSSetSamplers(0, 1, &sampler_state_linear);

                     native_device_context->Dispatch(8, 8, 8);

#if DEVELOPMENT
                     const std::shared_lock lock_trace(s_mutex_trace);
                     if (trace_running)
                     {
                        const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                        TraceDrawCallData trace_draw_call_data;
                        trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                        trace_draw_call_data.command_list = native_device_context;
                        trace_draw_call_data.custom_name = "Normalize 3D LUT";
                        // Re-use the RTV data for simplicity
                        GetResourceInfo(game_device_data.corrected_lut_texture_3d.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                        cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                     }
#endif

                     draw_state_stack.Restore(native_device_context);

                     // Replace the tonemapper LUT with the corrected one
                     ID3D11ShaderResourceView* const corrected_lut_srv_const = game_device_data.corrected_lut_srv.get();
                     native_device_context->PSSetShaderResources(9, 1, &corrected_lut_srv_const);
                     lut_conversion_succeeded = true;
                  }
               }
            }

            ASSERT_ONCE(lut_conversion_succeeded);
         }

         return DrawOrDispatchOverrideType::None;
      }

      // This seemengly always draws for the scene, even when there's no scene, or the game is paused, in a menu, video playback etc etc
      // The "is_custom_pass" is just an optimization, given it'd always be the case
      if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_AA))
      {
         bool is_sr_post_processed_scene = false;
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv); // It's always in slot 0
         if (srv)
         {
            com_ptr<ID3D11Resource> sr;
            srv->GetResource(&sr);
            is_sr_post_processed_scene = game_device_data.post_processed_scene == sr;
         }

         bool is_rt_swapchain = false;
         bool is_rt_swapchain_or_back_buffer = is_rt_swapchain;

         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (rtv)
         {
            com_ptr<ID3D11Resource> rt_resource;
            rtv->GetResource(&rt_resource);
            is_rt_swapchain = device_data.back_buffers.contains((uint64_t)rt_resource.get());
            is_rt_swapchain_or_back_buffer = is_rt_swapchain;
            // Make sure it's linear otherwise, otherwise we might still need to correct the missing sRGB view mismatch
            if (!is_rt_swapchain)
            {
               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv->GetDesc(&rtv_desc);
               ASSERT_ONCE(rtv_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT || rtv_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);

               com_ptr<ID3D11Texture2D> rt;
               HRESULT hr = rt_resource->QueryInterface(&rt);
               ASSERT_ONCE(SUCCEEDED(hr));

               D3D11_TEXTURE2D_DESC rt_desc;
               rt->GetDesc(&rt_desc);

               // Should be good enough? No need to check if the format is linear/HDR too
               is_rt_swapchain_or_back_buffer |= rt_desc.Width == uint(device_data.output_resolution.x + 0.5f) && rt_desc.Height == uint(device_data.output_resolution.y + 0.5f);
               ASSERT_ONCE(!is_rt_swapchain_or_back_buffer); // TODO: delete... for now just making sure this doesn't cause false positives beyond cutscenes
            }
            
            if (is_rt_swapchain_or_back_buffer)
            {
               game_device_data.has_drawn_aa = true;
            }
            if (is_rt_swapchain)
            {
               // If there's no depth, AA would be running immediately on a cleared black textures, every frame
               if (!game_device_data.depth || game_device_data.has_drawn_tonemap)
               {
                  device_data.has_drawn_main_post_processing = true;
               }
            }

            // The scene texture has now moved to this render target
            if (is_sr_post_processed_scene)
            {
               game_device_data.post_processed_scene == rt_resource;
            }
         }

         // Needed to branch on gamma conversions in the shaders.
         // If we are writing on the swapchain, then we need to convert to gamma space.
         uint32_t custom_data_1 = is_rt_swapchain ? 1 : 0;
         uint32_t custom_data_2 = is_rt_swapchain_or_back_buffer ? (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed) : 0;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, custom_data_2);
         updated_cbuffers = true;

         return DrawOrDispatchOverrideType::None;
      }

      if (original_shader_hashes.Contains(shader_hashes_PostAAPostProcess))
      {
         ASSERT_ONCE(IsRTSwapchain(native_device_context, device_data)); // Should always be the case unless these shaders are used for other things
         device_data.has_drawn_main_post_processing = true;
      }

      // Skip all post post processing draw calls if we hide the UI (it only runs when the scene in rendering).
      // NOTE: this will affect the pause menu and possible some fullscreen videos playback too.
      if (game_device_data.has_drawn_tonemap && device_data.has_drawn_main_post_processing && (hide_gameplay_ui || enable_camera_mode))
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      // Unless the user disabled UI through mods, the first fullscreen UI draw adds some unnecessary vignette at the edges of the screen
      if (!enable_vignette && !game_device_data.has_drawn_ui_vignette && device_data.has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_Vignette_UI))
      {
         com_ptr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(&depth_stencil_state, nullptr);
         D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = {};
         if (depth_stencil_state)
         {
            depth_stencil_state->GetDesc(&depth_stencil_desc);
         }
         // Depth test writes are 3D UI, and always come before, after that, vignette is the first one
         if (!depth_stencil_desc.DepthEnable)
         {
            bool is_rt_swapchain = IsRTSwapchain(native_device_context, device_data);
            if (is_rt_swapchain)
            {
               // Unsure what the first draw is for, it seems invisible usually, but it might be some layer they have to draw stuff
               if (!game_device_data.has_drawn_ui_pre_vignette)
               {
                  game_device_data.has_drawn_ui_pre_vignette = true;
                  return DrawOrDispatchOverrideType::None;
               }
               else
               {
                  // Note: we could verify with the viewport size, bound SRVs and blend state, but it just seems unlikely to trigger false positives
                  game_device_data.has_drawn_ui_vignette = true;
                  return DrawOrDispatchOverrideType::Skip; // Skip vignette
               }
            }
         }
      }

      if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_Sprite_UI))
      {
         bool is_sr_post_processed_scene = false;
         bool is_srv_linear = false;
         bool is_rtv_linear = false;
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0 ,1, &srv); // It's always in slot 0
         if (srv)
         {
            com_ptr<ID3D11Resource> sr_resource;
            srv->GetResource(&sr_resource);
            is_sr_post_processed_scene = game_device_data.post_processed_scene == sr_resource;

            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
            srv->GetDesc(&srv_desc);
            // We don't check for "DXGI_FORMAT_R11G11B10_FLOAT" etc as UI is always 8 bit (so it seems).
            // This will detect videos, that are seemengly the only UI sprites to draw from linear views.
            // Videos are usually 1920x1080 but not all of them.
            if (srv_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || srv_desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || srv_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
            {
               is_srv_linear = true;
            }
#if 0
            com_ptr<ID3D11Texture2D> sr;
            HRESULT hr = sr_resource->QueryInterface(&sr);
            ASSERT_ONCE(SUCCEEDED(hr));
            if (SUCCEEDED(hr))
            {
               D3D11_TEXTURE2D_DESC sr_desc;
               sr->GetDesc(&sr_desc);
            }
#endif
         }

         bool is_rt_swapchain = false;
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (rtv)
         {
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
            rtv->GetDesc(&rtv_desc);
            // Note: this works under the assumption we don't upgrade R8G8B8A8_UNORM/R8G8B8A8_TYPELESS to R16G16B16A16_FLOAT
            is_rtv_linear = rtv_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT || rtv_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || rtv_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || rtv_desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB || rtv_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

            com_ptr<ID3D11Resource> rt_resource;
            rtv->GetResource(&rt_resource);
            is_rt_swapchain = device_data.back_buffers.contains((uint64_t)rt_resource.get());
            // Make sure it's linear otherwise, otherwise we might still need to correct the missing sRGB view mismatch
            bool is_rt_swapchain_or_back_buffer = is_rt_swapchain;
            if (!is_rt_swapchain)
            {
               ASSERT_ONCE(rtv_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT || rtv_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);

               com_ptr<ID3D11Texture2D> rt;
               HRESULT hr = rt_resource->QueryInterface(&rt);
               ASSERT_ONCE(SUCCEEDED(hr));

               D3D11_TEXTURE2D_DESC rt_desc;
               rt->GetDesc(&rt_desc);

               // Should be good enough?
               is_rt_swapchain_or_back_buffer |= rt_desc.Width == uint(device_data.output_resolution.x + 0.5f) && rt_desc.Height == uint(device_data.output_resolution.y + 0.5f);
            }

            // The scene texture has now moved to this render target
            if (is_sr_post_processed_scene)
            {
               game_device_data.post_processed_scene = rt_resource;
            }
         }

         // Needed to branch on gamma conversions in the shaders
         uint32_t custom_data_1 = is_rt_swapchain ? 1 : 0;
         uint32_t custom_data_2 = is_sr_post_processed_scene ? 1 : 0;
         float custom_data_3 = is_srv_linear ? 1.f : 0.f;
         float custom_data_4 = is_rtv_linear ? 1.f : 0.f;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1, custom_data_2, custom_data_3, custom_data_4);
         updated_cbuffers = true;
         return DrawOrDispatchOverrideType::None;
      }

      // Fix up the 3D UI viewport if it has a mismatching aspect ratio.
      // The whole UI, the 2D and 3D one (the one mapped from world TO screen), gets constrained to ~4:3 if the game is booted at (wide) ultrawide resolutions (e.g. 32:9).
      // The proper solution is booting with a 16:9 res and then setting your UW res later, but in case you forgot, this at least fixes the 3D UI.
      // 2D would still be centered and cropped. Stretching it kinda works but then it's too stretched and hard to read.
      if (device_data.has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_3D_UI))
      {
         com_ptr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(&depth_stencil_state, nullptr);
         if (depth_stencil_state)
         {
            D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
            depth_stencil_state->GetDesc(&depth_stencil_desc);
            if (depth_stencil_desc.DepthEnable)
            {
               D3D11_VIEWPORT viewport;
               viewport.TopLeftX = 0.f;
               viewport.TopLeftY = 0.f;
               viewport.MinDepth = 0.f;
               viewport.MaxDepth = 1.f;
               viewport.Width = device_data.output_resolution.x;
               viewport.Height = device_data.output_resolution.y;
               native_device_context->RSSetViewports(1, &viewport);
            }
         }

         return DrawOrDispatchOverrideType::None;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

#if 0 // These almost all trigger, and it seems meaningless
      if (game_device_data.found_per_view_globals && device_data.has_drawn_main_post_processing)
      {
         ASSERT_ONCE(game_device_data.motion_vectors.get() && game_device_data.depth.get());
         ASSERT_ONCE(game_device_data.has_drawn_taa);
         ASSERT_ONCE(game_device_data.has_drawn_tonemap);
         ASSERT_ONCE(game_device_data.has_drawn_aa);
         ASSERT_ONCE(game_device_data.updated_per_view_globals_post_taa); // If this didn't happen, we'd risk jittering bloom etc (the workaround would be to cache a copy of the buffer data without jitters and manually setting it again after DLSS)
      }
#endif

      if (!game_device_data.has_drawn_taa)
      {
			device_data.force_reset_sr = true; // If the frame didn't draw the scene, DLSS needs to reset to prevent the old history from blending with the new scene
         device_data.taa_detected = false;
      }

      game_device_data.motion_vectors = nullptr;
      game_device_data.depth = nullptr;

      device_data.has_drawn_main_post_processing = false;
      device_data.has_drawn_sr = false;
      game_device_data.try_draw_dlss_next = false;
      //ASSERT_ONCE(game_device_data.found_per_view_globals);
      game_device_data.has_drawn_taa = false;
      game_device_data.has_drawn_tonemap = false;
      game_device_data.has_drawn_aa = false;
      game_device_data.found_per_view_globals = false;
      game_device_data.updated_per_view_globals_post_taa = false;
      game_device_data.has_drawn_ui_vignette = false;
      game_device_data.has_drawn_ui_pre_vignette = false;
      game_device_data.post_processed_scene = nullptr;

      game_device_data.prev_view_projection_mat = game_device_data.view_projection_mat;

#if DEVELOPMENT
      game_device_data.has_cleared_motion_vectors = false;
#endif

      // Update TAA jitters:
      int phases = 16; // Decent default for any modern TAA
      const int base_phases = 8; // For DLAA
      // We round to the cloest int, though maybe we should floor? Unclear. Both are probably fine.
      phases = (int)std::lrint(float(base_phases) * powf(float(device_data.output_resolution.y) / float(device_data.render_resolution.y), 2.f));
      int temporal_frame = cb_luma_global_settings.FrameIndex % phases;

      // Note: we add 1 to the temporal frame here to avoid a bias, given that Halton always returns 0 for 0
      game_device_data.taa_jitters.x = SR::HaltonSequence(temporal_frame, 2);
      game_device_data.taa_jitters.y = SR::HaltonSequence(temporal_frame, 3);

#if DEVELOPMENT
      //TODOFT: delete
      {
         game_device_data.taa_jitters.x *= cb_luma_global_settings.DevSettings[4];
         game_device_data.taa_jitters.y *= cb_luma_global_settings.DevSettings[5];
      }
#endif
      bool no_jitters = false;
      static bool force_native_taa_jitters = false; // The original TAA used depth jitters, but it doesn't seem able to properly handle horizontal and vertical jitters
      if ((device_data.sr_type == SR::Type::None || device_data.sr_suppressed) && !force_native_taa_jitters)
      {
         no_jitters = true;
         game_device_data.taa_jitters = {};
      }

      // To NDC space
      cb_luma_global_settings.GameSettings.CameraJitters = game_device_data.taa_jitters;
      cb_luma_global_settings.GameSettings.CameraJitters.x *= 2.f / device_data.render_resolution.x;
      cb_luma_global_settings.GameSettings.CameraJitters.y *= -2.f / device_data.render_resolution.y;
      device_data.cb_luma_global_settings_dirty = true;

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && !no_jitters)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "EnableLUTsNormalization", enable_luts_normalization);
      reshade::get_config_value(runtime, NAME, "LUTsStrength", luts_strength);
      reshade::get_config_value(runtime, NAME, "LUTsYellowFilterCorrection", luts_yellow_filter_removal);
      reshade::get_config_value(runtime, NAME, "Sharpening", sharpening);
      reshade::get_config_value(runtime, NAME, "MotionBlur", allow_motion_blur);
      reshade::get_config_value(runtime, NAME, "Vignette", enable_vignette);

      cb_luma_global_settings.GameSettings.Sharpening = sharpening; // "device_data.cb_luma_global_settings_dirty" should already be true at this point
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      memcpy(&data.GameData.ViewProjectionMatrix, &game_device_data.view_projection_mat, sizeof(game_device_data.view_projection_mat));
      memcpy(&data.GameData.PrevViewProjectionMatrix, &game_device_data.prev_view_projection_mat, sizeof(game_device_data.prev_view_projection_mat));
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::Checkbox("Enable Color Grading LUTs Range Normalization", &enable_luts_normalization))
      {
         reshade::set_config_value(runtime, NAME, "EnableLUTsNormalization", enable_luts_normalization);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Fixes potential raised blacks baked into Color Grading LUTs, without changing the appearance much");
      }
      DrawResetButton(enable_luts_normalization, true, "EnableLUTsNormalization", runtime); // Default to true even if it's not the way the original game was

      if (ImGui::SliderFloat("Color Grading LUTs Strength", &luts_strength, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "LUTsStrength", luts_strength);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Modulates the intensity of Color Grading LUTs, allowing to go for a more neutral look");
      }
      DrawResetButton(luts_strength, 1.f, "LUTsStrength", runtime);

      if (ImGui::SliderFloat("Color Grading LUTs Yellow Filter Correction", &luts_yellow_filter_removal, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "LUTsYellowFilterCorrection", luts_yellow_filter_removal);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Attempts to remove the yellow filter (or any color filter) that Color Grading LUTs had in this game");
      }
      DrawResetButton(luts_yellow_filter_removal, 0.f, "LUTsYellowFilterCorrection", runtime);

      if (ImGui::SliderFloat("Sharpening", &sharpening, 0.f, 1.f))
      {
         cb_luma_global_settings.GameSettings.Sharpening = sharpening;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(runtime, NAME, "Sharpening", sharpening);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Sharpening to fix up the blurryness of the game's native TAA");
      }
      DrawResetButton(sharpening, 0.f, "Sharpening", runtime);

      if (ImGui::Checkbox("Allow Motion Blur", &allow_motion_blur)) // Called "Allow" and not "Enable" because there's already a toggle in the game settings, this is an override
      {
         reshade::set_config_value(runtime, NAME, "MotionBlur", allow_motion_blur);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Allows quick Motion Blur toggling (it can be enabled in the game's settings as well)");
      }
      DrawResetButton(allow_motion_blur, true, "MotionBlur", runtime);

      if (ImGui::Checkbox("Vignette", &enable_vignette))
      {
         reshade::set_config_value(runtime, NAME, "Vignette", enable_vignette);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Hides the persistent Vignette effect the game had (in the HUD)");
      }
      DrawResetButton(enable_vignette, true, "Vignette", runtime);

      ImGui::NewLine();

      // This isn't serialized because it could cause issues/confusion if it's enabled on boot
      ImGui::Checkbox("Hide Gameplay UI", &hide_gameplay_ui);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Hides the whole UI outside of the main menu\nWARNING: this can cause confusion and isn't perfect (everything is hidden, even some non gameplay UI and Menus)");
      }
      DrawResetButton<decltype(hide_gameplay_ui), false>(hide_gameplay_ui, false, "Hide Gameplay UI", runtime);

      ImGui::NewLine();

      if (ImGui::TreeNode("Camera Mode"))
      {
         ImGui::Checkbox("Enable", &enable_camera_mode);
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Basic Camera Mode. Pause the game and enable it to be able to move the camera around and take screenshots. Works during in engine cutscenes too.\nWARNING: if you rotate the camera backwards, some geometry might not render");
         }
         DrawResetButton<decltype(enable_camera_mode), false>(enable_camera_mode, false, "Enable Camera Mode", runtime);

         ImGui::BeginDisabled(!enable_camera_mode);

         ImGui::SliderFloat3("Translation", &camera_mode_translation.x, -1.f, 1.f);
         DrawResetButton<decltype(camera_mode_translation), false>(camera_mode_translation, {}, "Translation", runtime);
         ImGui::SliderFloat3("Rotation", &camera_mode_rotation.x, -M_PI, M_PI);
         DrawResetButton<decltype(camera_mode_rotation), false>(camera_mode_rotation, {}, "Rotation", runtime);
         ImGui::SliderFloat("FoV Scale", &camera_mode_fov_scale, 0.1f, 10.f);
         DrawResetButton<decltype(camera_mode_fov_scale), false>(camera_mode_fov_scale, 1.f, "FoV Scale", runtime); // TODO: why doesn't reset work!? Or does it now?

         ImGui::EndDisabled();

         ImGui::TreePop();
      }

      // This happens during present so it should be safe
      if (!pattern_1_addresses.empty() && !pattern_2_addresses.empty() && (taa_enabled ? ImGui::Button("Disable TAA") : ImGui::Button("Enable TAA")))
      {
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_1_addresses[0], 1, PAGE_EXECUTE_READWRITE, &old_protect);
         if (success)
         {
            uint8_t replacement_data_1 = taa_enabled ? 0x00 : 0x60;
            std::memcpy(pattern_1_addresses[0], &replacement_data_1, 1);

            DWORD temp_protect;
            VirtualProtect(pattern_1_addresses[0], 1, old_protect, &temp_protect);

            success = VirtualProtect(pattern_2_addresses[0], 3, PAGE_EXECUTE_READWRITE, &old_protect);
            if (success)
            {
               std::array<uint8_t, 3> replacement_data_2 = taa_enabled ? std::array<uint8_t, 3>{ 0x90, 0x90, 0x90 } : std::array<uint8_t, 3>{ 0x88, 0x48, 0x38 };
               std::memcpy(pattern_2_addresses[0], replacement_data_2.data(), 3);

               VirtualProtect(pattern_2_addresses[0], 3, old_protect, &temp_protect);

               taa_enabled = !taa_enabled;
            }
            else
            {
               assert(false); // Only one of the two failed
            }
         }
      }
      static bool taa_enabled_1 = true;
      if (!pattern_1_addresses.empty()  && (taa_enabled_1 ? ImGui::Button("Disable TAA 1") : ImGui::Button("Enable TAA 1")))
      {
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_1_addresses[0], 2, PAGE_EXECUTE_READWRITE, &old_protect);
         if (success)
         {
            uint8_t replacement_data_1 = taa_enabled_1 ? 0x00 : 0x60;
            std::memcpy(pattern_1_addresses[0], &replacement_data_1, 1);

            DWORD temp_protect;
            VirtualProtect(pattern_1_addresses[0], 2, old_protect, &temp_protect);

            taa_enabled_1 = !taa_enabled_1;
            taa_enabled = !taa_enabled;
         }
      }
      static bool taa_enabled_2 = true;
      if (!pattern_2_addresses.empty() && (taa_enabled_2 ? ImGui::Button("Disable TAA 2") : ImGui::Button("Enable TAA 2")))
      {
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_2_addresses[0], 3, PAGE_EXECUTE_READWRITE, &old_protect);
         if (success)
         {
            std::array<uint8_t, 3> replacement_data_2 = taa_enabled_2 ? std::array<uint8_t, 3>{ 0x90, 0x90, 0x90 } : std::array<uint8_t, 3>{ 0x88, 0x48, 0x38 };
            std::memcpy(pattern_2_addresses[0], replacement_data_2.data(), 3);

            DWORD temp_protect;
            VirtualProtect(pattern_2_addresses[0], 3, old_protect, &temp_protect);

            taa_enabled_2 = !taa_enabled_2;
         }
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Works on the 2025 version of the Steam and GOG game. Use at your own risk if you have any other store version or patch.\nIf you've already applied a \"No TAA\" binary patch, this will fail to do anything.");
      }
   }

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      ImGui::NewLine();

      ImGui::Text("View Projection Matrix:");

      // XMMATRIX stores 4 XMVECTOR rows
      for (int i = 0; i < 4; i++)
      {
         DirectX::XMVECTOR row = game_device_data.view_projection_mat.r[i];
         float values[4];
         DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(values), row);
         ImGui::Text("%.10f %.10f %.10f %.10f", values[0], values[1], values[2], values[3]);
      }
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Mafia III\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.", "");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
         "\n\nMain:"
         "\nPumbo"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nOklab"
         "\nDICE (HDR tonemapper)"
         , "");
   }

#if DEVELOPMENT
   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      ID3D11Resource* target_resource = reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(rtv).handle);
      ASSERT_ONCE(game_device_data.last_motion_vectors.get() != target_resource || (color[0] == 0.f && color[1] == 0.f));

      // TODOFT: fix... MVs are cleared with the camera movement, however, that is not jittered, and has a default value of non 0... so it's not right, we should re-do it ourselves?
      if (game_device_data.last_motion_vectors.get() == target_resource)
      {
         game_device_data.has_cleared_motion_vectors = true;

         if (prevs2.size() >= 500)
            prevs2.erase(prevs2.begin());
         prevs2.push_back({ color[0], color[1], color[2], color[3] });
         // Note: we could skip this to get some performance back but whatever
      }
      return false;
   }
#endif

	static constexpr uint32_t CBPerViewGlobal_buffer_size = 2048;

#if DEVELOPMENT
#pragma optimize("", off) //TODOFT
#endif
	static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
	{
		ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
		ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
		//auto& game_device_data = GetGameDeviceData(device_data);

		if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
		{
			D3D11_BUFFER_DESC buffer_desc;
			buffer->GetDesc(&buffer_desc);

			// There seems to only ever be one buffer type of this size, but it's not guaranteed (we might have found more, but it doesn't matter, they are discarded later)...
			// They seemingly all happen on the same thread.
			// Some how these are not marked as "D3D11_BIND_CONSTANT_BUFFER", probably because it copies them over to some other buffer later?
			if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size)
			{
				device_data.cb_per_view_global_buffer = buffer;
#if DEVELOPMENT
				ASSERT_ONCE(buffer_desc.Usage == D3D11_USAGE_DYNAMIC && buffer_desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && buffer_desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE && buffer_desc.MiscFlags == 0 && buffer_desc.StructureByteStride == 0);
#endif // DEVELOPMENT
				ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
				device_data.cb_per_view_global_buffer_map_data = *data;
			}
#if 0
         res_map[buffer] = *data;
#endif
		}
	}

	static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
	{
      DeviceData& device_data = *device->get_private_data<DeviceData>();
		auto& game_device_data = GetGameDeviceData(device_data);
		ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
		ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
		bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;
		ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);

#if 0
      auto it = res_map.find(buffer);
      if (it != res_map.end() && it->second != nullptr)
      {
         static bool do_it = false;
         static size_t step = 4; // float4
         if (do_it)
         {
            D3D11_BUFFER_DESC buffer_desc;
            buffer->GetDesc(&buffer_desc);
            size_t float_count = buffer_desc.ByteWidth / sizeof(float);
            float_count -= 16; // Remove the size of a 4x4 matrix
            for (size_t i = 0; i < float_count; i += step)
            {
               float* ptr = (float*)(it->second);
               DirectX::XMMATRIX* projection_matrix = (DirectX::XMMATRIX*)(ptr + i);
               (!IsProjectionMatrix(*projection_matrix));
               (!IsViewMatrix(*projection_matrix));
            }
         }
         it->second = nullptr;
      }
#endif

		if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr)
		{
			float4(&float_data)[CBPerViewGlobal_buffer_size / sizeof(float4)] = *((float4(*)[CBPerViewGlobal_buffer_size / sizeof(float4)])device_data.cb_per_view_global_buffer_map_data);
         			
			bool is_valid_cbuffer = true
				//&& float_data[22].x == 0.f && float_data[22].y == 0.f
				&& float_data[10].x == device_data.render_resolution.x && float_data[10].y == device_data.render_resolution.y
				&& float_data[10].z == 1.f / device_data.render_resolution.x && float_data[10].w == 1.f / device_data.render_resolution.y;
         // Dont't jitter after TAA applied
         if (is_valid_cbuffer && game_device_data.has_drawn_taa)
         {
            game_device_data.updated_per_view_globals_post_taa = true;
            // TODO: is the buffer already overwritten at least once after TAA? Otherwise
         }
			if (is_valid_cbuffer && !game_device_data.has_drawn_taa)
			{
            // We write them even if DLSS is off, just because... We don't have to as our additive jitters might be 0
            if (test_index != 17)
            {
               DirectX::XMMATRIX view_projection_mat;
               view_projection_mat = DirectX::XMLoadFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&float_data[22]));
               //memcpy(&view_projection_mat, &float_data[22], sizeof(DirectX::XMMATRIX));

#if DEVELOPMENT
               if (prevs.size() >= 250)
                  prevs.erase(prevs.begin()); // erase first element
               prevs.push_back(view_projection_mat);
#endif

               DirectX::XMMATRIX view_projection_mat_2;
               memcpy(&view_projection_mat_2, &float_data[26], sizeof(DirectX::XMMATRIX));

               DirectX::XMMATRIX view_projection_mat_3;
               memcpy(&view_projection_mat_3, &float_data[30], sizeof(DirectX::XMMATRIX));

               if (test_index == 15) // bad!
               view_projection_mat = DirectX::XMMatrixTranspose(view_projection_mat);

               if (enable_camera_mode)
               {
                  view_projection_mat = ModifyViewProjection(view_projection_mat, camera_mode_rotation.z, -camera_mode_rotation.y, -camera_mode_rotation.x, -camera_mode_translation.x, -camera_mode_translation.y, camera_mode_translation.z, camera_mode_fov_scale);
               }

               //TODOFT5: restore the scale by 2??? Everything gets jittery if we do that.
               // TODO: remove the game's depth jitter that was already there... DLSS won't handle it?
               // Note: row major! GPU ready.
               // Multiply by 2 to make it NDC clip space (-1|+1), which represents the whole pixel, without leaking into the adjacent ones.
               // We couldn't find the projection matrix, apparently this game only ever passes view projection matrices to vertex shaders,
               // as that's more optimized, so we simply sum in the jitters in screen space, this will directly go to influence SV_Position as output of the shader (which has a -1|1 range).
               // 
               // The game's native TAA only jittered depth. We are seemengly unable to remove these jitters are we don't have separate projection and view matrices,
               // but only a pre-multiplied one (it makes sense, the game pre-multiplied them as optimization).
               // The good thing however, is that the jitters are included in the depth and motion vectors (Edit: that's probably not true!!!), so DLSS should be able to reconstruct them anyway, even if they ultimately just hurt quality.
               float2 jitters = game_device_data.taa_jitters;
               jitters.x = (jitters.x * +2.f) / (float)device_data.render_resolution.x;
               jitters.y = (jitters.y * -2.f) / (float)device_data.render_resolution.y;
#if DEVELOPMENT
               //TODOFT: delete
               {
                  jitters.x *= cb_luma_global_settings.DevSettings[6];
                  jitters.y *= cb_luma_global_settings.DevSettings[7];
               }
               //if (cb_luma_global_settings.DevSettings[0] > 0.0)
               //{
               //   view_projection_mat.r[0].m128_f32[3] *= 1.f + cb_luma_global_settings.DevSettings[0];
               //}
               //else
#endif
#if 0
               DirectX::XMMATRIX viewProj;    // your existing view-projection matrix
               DirectX::XMVECTOR jitterX = DirectX::XMVectorReplicate(jitters.x);
               DirectX::XMVECTOR jitterY = DirectX::XMVectorReplicate(jitters.y);

               // Extract rows as XMVECTOR
               DirectX::XMVECTOR row0 = view_projection_mat.r[0];
               DirectX::XMVECTOR row1 = view_projection_mat.r[1];
               DirectX::XMVECTOR row3 = view_projection_mat.r[3];

               // Add perspective-correct jitter
               row0 = DirectX::XMVectorAdd(row0, DirectX::XMVectorMultiply(jitterX, row3));
               row1 = DirectX::XMVectorAdd(row1, DirectX::XMVectorMultiply(jitterY, row3));

               // Reassemble the matrix
               view_projection_mat.r[0] = row0;
               view_projection_mat.r[1] = row1;
#elif 1
               //if (test_index != 16)
               //{
               //   view_projection_mat.r[3].m128_f32
               //   //The last column of vp matrix x02, x12,x22,x32
               //   //Multiply each element of that column with Jx add each to Col0, and multply with Jy and add to Col1
               //}
               //else
               if (test_index != 16)
               {
                  for (uint8_t i = 0; i < 4; i++)
                  {
                     view_projection_mat.r[0].m128_f32[i] += jitters.x * view_projection_mat.r[3].m128_f32[i];
                     view_projection_mat.r[1].m128_f32[i] += jitters.y * view_projection_mat.r[3].m128_f32[i];
                  }
               }
               else
               {
                  for (uint8_t i = 0; i < 4; i++)
                  {
                     view_projection_mat.r[0].m128_f32[i] += jitters.x * view_projection_mat.r[3].m128_f32[i];
                     view_projection_mat.r[1].m128_f32[i] += jitters.y * view_projection_mat.r[3].m128_f32[i];
                  }
               }
#elif 1
               DirectX::XMMATRIX prev_view_projection_mat = view_projection_mat; // No need to copy it...
               // Row 0 += jitterX * Row 3
               view_projection_mat.r[0] = DirectX::XMVectorAdd(prev_view_projection_mat.r[0], DirectX::XMVectorMultiply(DirectX::XMVectorReplicate(jitters.x), prev_view_projection_mat.r[3]));
               // Row 1 += jitterY * Row 3
               view_projection_mat.r[1] = DirectX::XMVectorAdd(prev_view_projection_mat.r[1], DirectX::XMVectorMultiply(DirectX::XMVectorReplicate(jitters.y), prev_view_projection_mat.r[3]));
               // Row 0 += jitterX * Row 3
               //view_projection_mat.r[0] = prev_view_projection_mat.r[0] + (jitters.x * prev_view_projection_mat.r[3]);
               // Row 1 += jitterY * Row 3
               //view_projection_mat.r[1] = prev_view_projection_mat.r[1] + (jitters.y * prev_view_projection_mat.r[3]);
#elif 1
               view_projection_mat.r[0].m128_f32[3] *= 1.f + ((jitters.x * 2.f) / (float)device_data.render_resolution.x); // row 0, w component (_14)
               view_projection_mat.r[1].m128_f32[3] *= 1.f + ((jitters.y * -2.f) / (float)device_data.render_resolution.y); // row 1, w component (_24)
#else
               view_projection_mat.r[0].m128_f32[3] += (jitters.x * 2.f) / (float)device_data.render_resolution.x; // row 0, w component (_14)
               view_projection_mat.r[1].m128_f32[3] += (jitters.y * -2.f) / (float)device_data.render_resolution.y; // row 1, w component (_24)
#endif

               view_projection_mat_2.r[0].m128_f32[3] += (game_device_data.taa_jitters.x * 2.f) / (float)device_data.render_resolution.x;
               view_projection_mat_2.r[1].m128_f32[3] += (game_device_data.taa_jitters.y * -2.f) / (float)device_data.render_resolution.y;

               view_projection_mat_3.r[0].m128_f32[3] += (game_device_data.taa_jitters.x * 2.f) / (float)device_data.render_resolution.x;
               view_projection_mat_3.r[1].m128_f32[3] += (game_device_data.taa_jitters.y * -2.f) / (float)device_data.render_resolution.y;

               if (test_index == 15)
               view_projection_mat = DirectX::XMMatrixTranspose(view_projection_mat);

               DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(&float_data[22]), view_projection_mat);
               //memcpy(&float_data[22], &view_projection_mat, sizeof(DirectX::XMMATRIX));

#if DEVELOPMENT
               if (cb_luma_global_settings.DevSettings[1] > 0)
                  memcpy(&float_data[26], &view_projection_mat, sizeof(DirectX::XMMATRIX)); // TODO

               //memcpy(&float_data[30], &view_projection_mat, sizeof(DirectX::XMMATRIX)); // This makes a mess
#endif

#if 0
               DirectX::XMMATRIX V_existing;
               memcpy(&V_existing, &float_data[62], sizeof(DirectX::XMMATRIX));
               DirectX::XMMATRIX V_inv = DirectX::XMMatrixInverse(nullptr, V_existing);
               DirectX::XMMATRIX V_existing2;
               memcpy(&V_existing2, &float_data[66], sizeof(DirectX::XMMATRIX));
               DirectX::XMMATRIX V_inv2 = DirectX::XMMatrixInverse(nullptr, V_existing2);
               DirectX::XMMATRIX V_existing3;
               memcpy(&V_existing3, &float_data[70], sizeof(DirectX::XMMATRIX));
               DirectX::XMMATRIX V_inv3 = DirectX::XMMatrixInverse(nullptr, V_existing3);

               // Multiply by VP to get projection
               DirectX::XMMATRIX P_found = DirectX::XMMatrixMultiply(V_inv, view_projection_mat);
               DirectX::XMMATRIX P_found2 = DirectX::XMMatrixMultiply(V_inv2, view_projection_mat);
               DirectX::XMMATRIX P_found3 = DirectX::XMMatrixMultiply(V_inv3, view_projection_mat);

               // Row-major: first column = r0, r1, r2; second column = r0, r1, r2
               DirectX::XMVECTOR col0 = DirectX::XMVectorSet(view_projection_mat.r[0].m128_f32[0], view_projection_mat.r[1].m128_f32[0], view_projection_mat.r[2].m128_f32[0], 0.0f);
               DirectX::XMVECTOR col1 = DirectX::XMVectorSet(view_projection_mat.r[0].m128_f32[1], view_projection_mat.r[1].m128_f32[1], view_projection_mat.r[2].m128_f32[1], 0.0f);

               float f_x = 1.0f / DirectX::XMVectorGetX(DirectX::XMVector3Length(col0));
               float f_y = 1.0f / DirectX::XMVectorGetX(DirectX::XMVector3Length(col1));

               float fov_x = 2.0f * atan(1.0f / f_x);
               float fov_y = 2.0f * atan(1.0f / f_y);

               float fovY = 2.0f * atanf(1.0f / float_data[44].y);
               if (cb_luma_global_settings.DevSettings[6])
                  fovY = 2.0f * atanf(1.0f / float_data[42].x);
               float aspect = device_data.render_resolution.x / device_data.render_resolution.y;
               float nearZ = float_data[1].x;
               float farZ = float_data[1].y;
               DirectX::XMMATRIX P = CreateProjection(fovY, aspect, nearZ, farZ);

               //  Extract View
               DirectX::XMMATRIX P_inv = DirectX::XMMatrixInverse(nullptr, P);
               DirectX::XMMATRIX V = P_inv * view_projection_mat; // row-major: V = P^-1 * VP

               //  Apply jitter
               float jitterX = cb_luma_global_settings.DevSettings[7] * 2.f - 1.0; // example subpixel jitter in pixels
               float jitterY = cb_luma_global_settings.DevSettings[8] * 2.f - 1.0;
               float screenWidth = device_data.render_resolution.x;
               float screenHeight = device_data.render_resolution.y;

               // Apply subpixel jitter (in NDC units)
               DirectX::XMMATRIX P_jittered = ApplyJitter(P, jitterX, jitterY, screenWidth, screenHeight);

               //  Recombine to get jittered VP
               DirectX::XMMATRIX VP_jittered = P_jittered * V;

               memcpy(&float_data[22], &VP_jittered, sizeof(DirectX::XMMATRIX));
#elif 0
               float_data[22].x = cb_luma_global_settings.DevSettings[7] * 2.f - 1.0;
               float_data[22].y = cb_luma_global_settings.DevSettings[8] * 2.f - 1.0;
               float_data[22].z *= cb_luma_global_settings.DevSettings[9];
#endif
            }

            if (!game_device_data.found_per_view_globals)
            {
               game_device_data.found_per_view_globals = true;

               memcpy(&game_device_data.view_projection_mat, &float_data[22], sizeof(DirectX::XMMATRIX));

               if (sr_user_type != SR::UserType::None && game_device_data.last_motion_vectors_rtv.get()) // TODO: do it for non DLSS SR path too?
               {
#if DEVELOPMENT
                  ASSERT_ONCE(game_device_data.has_cleared_motion_vectors); // Might happen in menus?
#endif

                  // Draw motion vectors for the sky, given that they don't have a separate draw call for it.
                  // We know the first time this is called, the jitters and view projection matrix are already final
                  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());

                  DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
                  draw_state_stack.Cache(device_data.primary_command_list.get(), device_data.uav_max_count);

                  CommandListData& cmd_list_data = *device_data.primary_command_list_data; // The game always uses the immediate device context (primary command list)
#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = device_data.primary_command_list.get();
                     trace_draw_call_data.custom_name = "Draw Sky Motion Vectors";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                  }
#endif

                  com_ptr<ID3D11GeometryShader> gs;
                  com_ptr<ID3D11ClassInstance> gs_i; // TODO: delete once we know it's nullptr (it seems to be)
                  device_data.primary_command_list->GSGetShader(&gs, &gs_i, 0);
                  ASSERT_ONCE(gs_i.get() == nullptr);

                  device_data.primary_command_list->GSSetShader(nullptr, nullptr, 0);

                  SetLumaConstantBuffers(device_data.primary_command_list.get(), cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
                  DrawCustomPixelShader(device_data.primary_command_list.get(), nullptr, nullptr, device_data.sampler_state_linear.get(), device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(), device_data.native_pixel_shaders[CompileTimeStringHash("Draw Sky Motion Vectors")].get(), nullptr, game_device_data.last_motion_vectors_rtv.get(), device_data.render_resolution.x, device_data.render_resolution.y, false);

                  device_data.primary_command_list->GSSetShader(gs.get(), nullptr, 0);

                  draw_state_stack.Restore(device_data.primary_command_list.get());
               }
            }
			}

			device_data.cb_per_view_global_buffer_map_data = nullptr;
			device_data.cb_per_view_global_buffer = nullptr;
		}
	}

   // TODO: delete, not useful
	static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
	{
		ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
		DeviceData& device_data = *device->get_private_data<DeviceData>();
		//auto& game_device_data = GetGameDeviceData(*device->get_private_data<DeviceData>());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);

      D3D11_BUFFER_DESC buffer_desc;
      buffer->GetDesc(&buffer_desc);

		if (size == CBPerViewGlobal_buffer_size || buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size) {
			// It's not very nice to const cast, but we know for a fact this is dynamic memory, so it's probably fine to edit it (ReShade doesn't offer an interface for replacing it easily, and doesn't pass in the command list)
			//float4* mutable_float_data = reinterpret_cast<float4*>(const_cast<void*>(data));
			//const float4* float_data = reinterpret_cast<const float4*>(data);
         float4(&mutable_float_data)[CBPerViewGlobal_buffer_size / sizeof(float4)] = *((float4(*)[CBPerViewGlobal_buffer_size / sizeof(float4)])data);

         bool is_valid_cbuffer = true
            //&& mutable_float_data[22].x == 0.f && mutable_float_data[22].y == 0.f
            && mutable_float_data[10].x == device_data.render_resolution.x && mutable_float_data[10].y == device_data.render_resolution.y
            && mutable_float_data[10].z == 1.f / device_data.render_resolution.x && mutable_float_data[10].w == 1.f / device_data.render_resolution.y;
         if (is_valid_cbuffer)
         {
            ASSERT_ONCE(false); // When would this ever happen?
         }
		}
		return false;
	}
#if DEVELOPMENT
#pragma optimize("", on)
#endif
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Mafia III Luma mod");
      Globals::VERSION = 1;

      shader_hashes_LinearizeDepth.pixel_shaders.emplace(std::stoul("E4A815CF", nullptr, 16));

      shader_hashes_ShadowMapProjections.vertex_shaders.emplace(std::stoul("AC56CA1A", nullptr, 16));

      shader_hashes_PreTAAFogNearMask.vertex_shaders.emplace(std::stoul("1B8BCBA8", nullptr, 16));
      shader_hashes_PreTAAFogNearMask.pixel_shaders.emplace(std::stoul("FD1D320F", nullptr, 16));

      shader_hashes_PreTAACopy.vertex_shaders.emplace(std::stoul("6FDEE99B", nullptr, 16));
      shader_hashes_PreTAACopy.pixel_shaders.emplace(std::stoul("4671BB12", nullptr, 16));

      shader_hashes_TAA.pixel_shaders = { std::stoul("2200CBD7", nullptr, 16), std::stoul("E781A41B", nullptr, 16), std::stoul("A8D4D208", nullptr, 16) };

      shader_hashes_MotionBlur.pixel_shaders.emplace(std::stoul("E3CE19B0", nullptr, 16));

      shader_hashes_DownscaleMotionVectors.pixel_shaders.emplace(std::stoul("287063DE", nullptr, 16));

      shader_hashes_EncodeMotionVectors.pixel_shaders.emplace(std::stoul("B00E89BC", nullptr, 16));

      shader_hashes_AA.pixel_shaders.emplace(std::stoul("BFC6242E", nullptr, 16));
      shader_hashes_AA.pixel_shaders.emplace(std::stoul("6C3629F1", nullptr, 16));

      shader_hashes_Tonemap.pixel_shaders.emplace(std::stoul("F4D0E9C2", nullptr, 16));

      // Final post processes that run after AA and directly write on the swapchain
      shader_hashes_PostAAPostProcess.pixel_shaders.emplace(std::stoul("DA76D42E", nullptr, 16)); // Sniper Scope View
      shader_hashes_PostAAPostProcess.pixel_shaders.emplace(std::stoul("D4433701", nullptr, 16)); // Intel View

      // TODO: ... won't work, not reliable
      shader_hashes_Vignette_UI.vertex_shaders.emplace(std::stoul("5D9627AA", nullptr, 16));
      shader_hashes_Vignette_UI.pixel_shaders.emplace(std::stoul("22EE786B", nullptr, 16));

      // These play back movies and sometimes even the scene (during cutscenes)
      shader_hashes_Sprite_UI.pixel_shaders.emplace(std::stoul("6B4B9B6D", nullptr, 16));
      shader_hashes_Sprite_UI.pixel_shaders.emplace(std::stoul("2C052C85", nullptr, 16));
      shader_hashes_Sprite_UI.pixel_shaders.emplace(std::stoul("B6F720AE", nullptr, 16));

      shader_hashes_3D_UI.pixel_shaders.emplace(std::stoul("CDB35CB7", nullptr, 16));
      shader_hashes_3D_UI.pixel_shaders.emplace(std::stoul("22EE786B", nullptr, 16));
      shader_hashes_3D_UI.pixel_shaders.emplace(std::stoul("AAA3C7B5", nullptr, 16));

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("FD2925A4", nullptr, 16), "Clear");
      forced_shader_names.emplace(std::stoul("B00E89BC", nullptr, 16), "Encode Motion Vectors"); // Second output is 8bit UNORM, seemengly unused (with high quality settings at least). First output is R16G16B16A16F (dunno why), it's used by the 3 TAA shaders. SRV 4 is the raw proper MVs R16G16F.
      forced_shader_names.emplace(std::stoul("C3E123B6", nullptr, 16), "Downscale Encoded Motion Vectors"); // Downscales the R16G16B16A16F encoded MVs to half res. Output is seemengly unused (with high quality settings at least).
      forced_shader_names.emplace(std::stoul("2E9DF0A7", nullptr, 16), "Downscale Motion Vectors 1/2"); // Downscales the raw FLOAT motion vectors to half size (for motion blur)
      forced_shader_names.emplace(std::stoul("FB0E84FB", nullptr, 16), "Downscale Motion Vectors 1/4"); // Second downscale pass from 1/2 to 1/8
      forced_shader_names.emplace(std::stoul("9AD611CB", nullptr, 16), "Blur Downscaled Motion Vectors");
      forced_shader_names.emplace(std::stoul("E3CE19B0", nullptr, 16), "Apply Motion Blur");
      forced_shader_names.emplace(std::stoul("287063DE", nullptr, 16), "Downscale Type 1 (e.g. Bloom)");
      forced_shader_names.emplace(std::stoul("6AB07017", nullptr, 16), "Downscale Type 2 (e.g. Bloom)");
      forced_shader_names.emplace(std::stoul("B0227624", nullptr, 16), "Downscale Type 3 (e.g. Bloom)"); // Also used for non bloom
      forced_shader_names.emplace(std::stoul("5021911B", nullptr, 16), "Downscale Type 4");
      forced_shader_names.emplace(std::stoul("0390A051", nullptr, 16), "Downscale Type 5");
      forced_shader_names.emplace(std::stoul("DDCF04CE", nullptr, 16), "Copy Mip"); // Samples a mip and copies it on output
      forced_shader_names.emplace(std::stoul("BD2F44E6", nullptr, 16), "Compose Downscaled Blooms");
      forced_shader_names.emplace(std::stoul("3FF5913A", nullptr, 16), "Generate First Exposure Mip");
      forced_shader_names.emplace(std::stoul("65A0E902", nullptr, 16), "Downscale Exposure Mip");
      forced_shader_names.emplace(std::stoul("015409F3", nullptr, 16), "Downscale Last Exposure Mip");
      forced_shader_names.emplace(std::stoul("C6611DE0", nullptr, 16), "Mix Exposure");
      forced_shader_names.emplace(std::stoul("E4A815CF", nullptr, 16), "Linearize Depth");
      forced_shader_names.emplace(std::stoul("FD1D320F", nullptr, 16), "Fog + Near Mask"); // This somehow cuts out parts of the images that are snitched back together later
      forced_shader_names.emplace(std::stoul("4671BB12", nullptr, 16), "Copy");
      forced_shader_names.emplace(std::stoul("5A6EDCF9", nullptr, 16), "Hair"); // TODO: these aren't jittered?
      forced_shader_names.emplace(std::stoul("E2E37687", nullptr, 16), "Hair");
      forced_shader_names.emplace(std::stoul("36F3D36C", nullptr, 16), "Eye");
      forced_shader_names.emplace(std::stoul("F6D2567A", nullptr, 16), "Decal");
      forced_shader_names.emplace(std::stoul("121A4A96", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("F2746066", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("62674CDD", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("BA8BC257", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("2852B0B0", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("2A96E15A", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("71856B3B", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("6EC4573E", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("CC753AEC", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("B10C4067", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("333D8C48", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("D72D8625", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("60A25B05", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("C22FC4EF", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("B0843142", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("78F99AEA", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("9B84CEAD", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("D74A2022", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("8775A716", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("1D550058", nullptr, 16), "Shadow Map / Depth");
      forced_shader_names.emplace(std::stoul("C778EC2C", nullptr, 16), "Sun Shadow Map");
      forced_shader_names.emplace(std::stoul("DFFD53DC", nullptr, 16), "Sun Shadow Map");
      forced_shader_names.emplace(std::stoul("DFFD53DC", nullptr, 16), "Sun Shadow Map");
      forced_shader_names.emplace(std::stoul("E92CD44B", nullptr, 16), "Sun Shadow Map");
      forced_shader_names.emplace(std::stoul("0D6D0591", nullptr, 16), "Sun Shadow Map");
      forced_shader_names.emplace(std::stoul("D9388830", nullptr, 16), "Sun Shadow Map (x3)");
      forced_shader_names.emplace(std::stoul("01C8970C", nullptr, 16), "Some Shadow Map ?");
      forced_shader_names.emplace(std::stoul("A2C3D0F2", nullptr, 16), "Screen Space Reflections ?");
      forced_shader_names.emplace(std::stoul("685D5D10", nullptr, 16), "Screen Space Reflections");
      forced_shader_names.emplace(std::stoul("7E83AA58", nullptr, 16), "Screen Space Reflections");
      forced_shader_names.emplace(std::stoul("5F4BC362", nullptr, 16), "Sky Mask ?");
      forced_shader_names.emplace(std::stoul("B812B7FC", nullptr, 16), "Generation Clouds");
      forced_shader_names.emplace(std::stoul("4A74FD61", nullptr, 16), "Draw Sky, Clouds and Volumetrics");
      forced_shader_names.emplace(std::stoul("21562E2E", nullptr, 16), "Draw Some Clouds Stuff");
      forced_shader_names.emplace(std::stoul("35DFEC90", nullptr, 16), "Generate HQ Ambient Occlusion");
      forced_shader_names.emplace(std::stoul("26AB1E66", nullptr, 16), "Compose Light onto Scene"); // This also draws a second RT with a map of how much lighting there is per pixel, seemengly later used by hair to do approximate lighting
      forced_shader_names.emplace(std::stoul("6BC66DF6", nullptr, 16), "Compose Light onto Scene");
      forced_shader_names.emplace(std::stoul("654A690C", nullptr, 16), "Generate Fog Texture Step 3");
      forced_shader_names.emplace(std::stoul("74D45B78", nullptr, 16), "Compose Fog and Volumetrics Step 1"); // Very low res. 2D textures. Generates the screen space results.
      forced_shader_names.emplace(std::stoul("EB444145", nullptr, 16), "Compose Fog and Volumetrics Step 2");
      forced_shader_names.emplace(std::stoul("7A296760", nullptr, 16), "Depth Test");
      forced_shader_names.emplace(std::stoul("6FD15674", nullptr, 16), "Some Noise Map ?");
      forced_shader_names.emplace(std::stoul("B876B4FF", nullptr, 16), "Some Noise Map ?");
      forced_shader_names.emplace(std::stoul("2C64BF05", nullptr, 16), "City Light");
      forced_shader_names.emplace(std::stoul("6B4B9B6D", nullptr, 16), "UI Video with Film Grain");
      forced_shader_names.emplace(std::stoul("2C052C85", nullptr, 16), "UI Sprite with Color Filter");
      forced_shader_names.emplace(std::stoul("CDB35CB7", nullptr, 16), "UI Sprite"); // This is used as the brightness calibration text
      forced_shader_names.emplace(std::stoul("BC824E7F", nullptr, 16), "UI Color"); // This is used for black bars (etc?)

      cb_luma_dev_settings_set_from_code = true;

      cb_luma_dev_settings_names[1] = "Write View Projection Matrix";

      cb_luma_dev_settings_names[4] = "Global Jitters Scale X";
      cb_luma_dev_settings_default_value[4] = 1;
      cb_luma_dev_settings_min_value[4] = -2;
      cb_luma_dev_settings_max_value[4] = 2;
      cb_luma_dev_settings_names[5] = "Global Jitters Scale Y";
      cb_luma_dev_settings_default_value[5] = 1;
      cb_luma_dev_settings_min_value[5] = -2;
      cb_luma_dev_settings_max_value[5] = 2;

      cb_luma_dev_settings_names[6] = "CBuffer Jitters Scale X";
      cb_luma_dev_settings_default_value[6] = 1;
      cb_luma_dev_settings_min_value[6] = -2;
      cb_luma_dev_settings_max_value[6] = 2;
      cb_luma_dev_settings_names[7] = "CBuffer Jitters Scale Y";
      cb_luma_dev_settings_default_value[7] = 1;
      cb_luma_dev_settings_min_value[7] = -2;
      cb_luma_dev_settings_max_value[7] = 2;

      cb_luma_dev_settings_names[8] = "DLSS Jitters Scale X";
      cb_luma_dev_settings_default_value[8] = 1;
      cb_luma_dev_settings_min_value[8] = -2;
      cb_luma_dev_settings_max_value[8] = 2;
      cb_luma_dev_settings_names[9] = "DLSS Jitters Scale Y";
      cb_luma_dev_settings_default_value[9] = 1;
      cb_luma_dev_settings_min_value[9] = -2;
      cb_luma_dev_settings_max_value[9] = 2;

      cb_luma_global_settings.DevSettings[4] = 1.f;
      cb_luma_global_settings.DevSettings[5] = 1.f;
      cb_luma_global_settings.DevSettings[6] = 1.f;
      cb_luma_global_settings.DevSettings[7] = 1.f;
      cb_luma_global_settings.DevSettings[8] = 1.f;
      cb_luma_global_settings.DevSettings[9] = 1.f;
#endif

      // TODO: distribute DLSS with the build! Also test for crashes when going back to the menu! Also remove the memory patching library if not used. Also add compat with the NO TAA mod? Reduce fog?
      // Also fix when pressing square in the car, screen became darker!? Add photo mode and hide UI buttons.
      // Also.. test mirrors! Also check FOG... sometimes it's too much?
      // Sun and City Lights scale when zooming in!
      // Test debug layer for errors.

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      // Note that this game has an optional rear view mirror HUD, which has its own rendering, by default it renders to r8g8b8a8_typeless with an sRGB view. It seemengly follows the main rendering aspect ratio, or is ~32:9 anyway (low res)
      texture_upgrade_formats = {
#if 0 // Not needed really, swapchain is all we need, though the rest wouldn't hurt.
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_unorm_srgb,
            reshade::api::format::b8g8r8x8_typeless,
#endif
#if 1 // Most important format to upgrade
            reshade::api::format::r11g11b10_float,
#endif
      };
      // Optional, we manually upgrade LUTs, that seem to be fixed over time (they aren't render targets somehow, at least not always)
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;

      // The vanilla swapchain was last written by the game's AA with an sRGB view, so in linear.
      // The UI would be written to the swapchain with non sRGB views, so with texture upgrades, the UI is broken, but the game should be linear nonetheless.
      force_vanilla_swapchain_linear = swapchain_format_upgrade_type > TextureFormatUpgradesType::None && swapchain_upgrade_type == SwapchainUpgradeType::scRGB;

#if DEVELOPMENT // TODO: put this feature release too. But, they currently break some metal shiny stuff (e.g. truck in the opening scene)
      // Needed for DLSS mips in case there's jitters
      enable_samplers_upgrade = true;
#endif

      game = new MafiaIII();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
#if DEVELOPMENT
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(MafiaIII::OnClearRenderTargetView);
#endif
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(MafiaIII::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(MafiaIII::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(MafiaIII::OnUpdateBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}