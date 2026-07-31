// TODO: rename mod to "Prey (2017)" if possible (VS project, shaders and code folder, define, mod name in c++ etc)
#define GAME_PREY 1

#define ENABLE_NGX 1
#define ENABLE_NVAPI 0
#define ENABLE_FIDELITY_SK 1

#include "..\..\Core\core.hpp"

#define ENABLE_NATIVE_PLUGIN 1

#include "includes/cbuffers.h"

#include "native plugin/NativePlugin.h"

struct GameDeviceDataPrey final : public GameDeviceData
{
   // Resources:

#if ENABLE_SR
   // SR
   com_ptr<ID3D11Texture2D> sr_motion_vectors;
   com_ptr<ID3D11RenderTargetView> sr_motion_vectors_rtv;
#endif // ENABLE_SR

   // GTAO
   com_ptr<ID3D11Texture2D> gtao_edges_texture;
   UINT gtao_edges_texture_width = 0;
   UINT gtao_edges_texture_height = 0;
   com_ptr<ID3D11RenderTargetView> gtao_edges_rtv;
   com_ptr<ID3D11ShaderResourceView> gtao_edges_srv;

   void CleanGTAOResources()
   {
      gtao_edges_texture = nullptr;
      gtao_edges_texture_width = 0;
      gtao_edges_texture_height = 0;
      gtao_edges_rtv = nullptr;
      gtao_edges_srv = nullptr;
   }

   // SSR
   com_ptr<ID3D11Texture2D> ssr_texture;
   com_ptr<ID3D11ShaderResourceView> ssr_srv;
   com_ptr<ID3D11Texture2D> ssr_diffuse_texture;
   UINT ssr_diffuse_texture_width = 0;
   UINT ssr_diffuse_texture_height = 0;
   com_ptr<ID3D11RenderTargetView> ssr_diffuse_rtv;
   com_ptr<ID3D11ShaderResourceView> ssr_diffuse_srv;

   void CleanSSRResources()
   {
      ssr_texture = nullptr;
      ssr_srv = nullptr;
      ssr_diffuse_texture = nullptr;
      ssr_diffuse_texture_width = 0;
      ssr_diffuse_texture_height = 0;
      ssr_diffuse_rtv = nullptr;
      ssr_diffuse_srv = nullptr;
   }

   // Lens Distortion
   com_ptr<ID3D11SamplerState> lens_distortion_sampler_state;
   com_ptr<ID3D11Texture2D> lens_distortion_texture;
   com_ptr<ID3D11ShaderResourceView> lens_distortion_srv;
   com_ptr<ID3D11Resource> lens_distortion_rtvs_resources[2];
   com_ptr<ID3D11RenderTargetView> lens_distortion_rtvs[2];
   com_ptr<ID3D11ShaderResourceView> lens_distortion_srvs[2];
   size_t lens_distortion_rtv_index = -1;
   bool lens_distortion_rtv_found = false;
   UINT lens_distortion_texture_width = 0;
   UINT lens_distortion_texture_height = 0;
   DXGI_FORMAT lens_distortion_texture_format = DXGI_FORMAT_UNKNOWN;

   void CleanLensDistortionResources()
   {
      // "lens_distortion_sampler_state" is peristent (not much point in clearing it)
      lens_distortion_texture = nullptr;
      lens_distortion_srv = nullptr;
      lens_distortion_rtvs_resources[0] = nullptr;
      lens_distortion_rtvs_resources[1] = nullptr;
      lens_distortion_rtvs[0] = nullptr;
      lens_distortion_rtvs[1] = nullptr;
      lens_distortion_srvs[0] = nullptr;
      lens_distortion_srvs[1] = nullptr;
      lens_distortion_rtv_index = -1;
      lens_distortion_rtv_found = false;
      lens_distortion_texture_width = 0;
      lens_distortion_texture_height = 0;
      lens_distortion_texture_format = DXGI_FORMAT_UNKNOWN;
   }

   // Tells if the scene is still being rendered
   std::atomic<bool> has_drawn_composed_gbuffers = false;
   std::atomic<bool> has_drawn_upscaling = false;
   std::atomic<bool> has_drawn_motion_blur = false;
   bool has_drawn_motion_blur_previous = false;
   std::atomic<bool> has_drawn_tonemapping = false;
   std::atomic<bool> has_drawn_ssr = false;
   std::atomic<ID3D11DeviceContext*> ssr_command_list = nullptr;
   std::atomic<bool> has_drawn_ssr_blend = false;
   std::atomic<bool> has_drawn_ssao = false;
   std::atomic<bool> has_drawn_ssao_denoise = false;

   std::atomic<bool> found_per_view_globals = false;
   //TODOFT: remove "Prey" from these variables names
   // Whether the rendering resolution was scaled in this frame (different from the output resolution)
   std::atomic<bool> prey_drs_active = false;
   std::atomic<bool> prey_drs_detected = false;
   std::atomic<bool> prey_taa_active = false; // Instant version of "taa_detected".
   // Index 0 is one frame ago, index 1 is two frames ago
   bool previous_prey_taa_active[2] = { false, false };
};

namespace
{
   bool tonemap_ui_background = true;
   constexpr float tonemap_ui_background_amount = 0.25;

   RE::ETEX_Format HDR_textures_upgrade_confirmed_format = ENABLE_NATIVE_PLUGIN ? RE::ETEX_Format::eTF_R16G16B16A16F : RE::ETEX_Format::eTF_R11G11B10F; // Native hooks and vanilla game start with these
   RE::ETEX_Format HDR_textures_upgrade_requested_format = RE::ETEX_Format::eTF_R16G16B16A16F;

   constexpr bool force_motion_vectors_jittered = true;

   // Directly from cbuffer (so these are transposed)
   Matrix44F projection_matrix;
   Matrix44F nearest_projection_matrix; // For first person weapons (view model)
   Matrix44F previous_projection_matrix;
   Matrix44F previous_nearest_projection_matrix;
   Matrix44F reprojection_matrix;
   float2 previous_projection_jitters = { 0, 0 };
   float2 projection_jitters = { 0, 0 };
   CBPerViewGlobal cb_per_view_global = { };
   CBPerViewGlobal cb_per_view_global_previous = cb_per_view_global;

   ShaderHashesList shader_hashes_TiledShadingTiledDeferredShading;
   uint32_t shader_hash_DeferredShadingSSRRaytrace;
   uint32_t shader_hash_DeferredShadingSSReflectionComp;
   uint32_t shader_hash_PostEffectsGaussBlurBilinear;
   uint32_t shader_hash_PostEffectsTextureToTextureResampled;
   ShaderHashesList shader_hashes_MotionBlur;
   ShaderHashesList shader_hashes_HDRPostProcessHDRFinalScene;
   ShaderHashesList shader_hashes_HDRPostProcessHDRFinalScene_Sunshafts;
   ShaderHashesList shader_hashes_SMAA_EdgeDetection;
   ShaderHashesList shader_hashes_PostAA;
   ShaderHashesList shader_hashes_PostAA_TAA;
   ShaderHashesList shader_hashes_PostAAComposites;
   uint32_t shader_hash_PostAAUpscaleImage;
   ShaderHashesList shader_hashes_LensOptics;
   ShaderHashesList shader_hashes_DirOccPass;
   ShaderHashesList shader_hashes_SSDO_Blur;

#if DEVELOPMENT
   std::vector<std::string> cb_per_view_globals_last_drawn_shader; // Not exactly thread safe but it's fine...
   std::vector<CBPerViewGlobal> cb_per_view_globals;
   std::vector<CBPerViewGlobal> cb_per_view_globals_previous;
#endif

   // Dev or User settings:
#if DEVELOPMENT
   float sr_custom_pre_exposure = 0.f; // Ignored at 0
   int force_taa_jitter_phases = 0; // Ignored if 0 (automatic mode), set to 1 to basically disable jitters
   bool disable_taa_jitters = false;
   RE::ETEX_Format LDR_textures_upgrade_confirmed_format = ENABLE_NATIVE_PLUGIN ? RE::ETEX_Format::eTF_R16G16B16A16F : RE::ETEX_Format::eTF_R8G8B8A8; // Native hooks and vanilla game start with these
   RE::ETEX_Format LDR_textures_upgrade_requested_format = RE::ETEX_Format::eTF_R16G16B16A16F;
#endif

   constexpr uint32_t TONEMAP_TYPE_HASH = char_ptr_crc32("TONEMAP_TYPE");
   constexpr uint32_t EXPAND_COLOR_GAMUT_HASH = char_ptr_crc32("EXPAND_COLOR_GAMUT");
   constexpr uint32_t ENABLE_LUT_EXTRAPOLATION_HASH = char_ptr_crc32("ENABLE_LUT_EXTRAPOLATION");
   constexpr uint32_t AUTO_HDR_VIDEOS_HASH = char_ptr_crc32("AUTO_HDR_VIDEOS");

   constexpr uint32_t SSAO_TYPE_HASH = char_ptr_crc32("SSAO_TYPE");
   constexpr uint32_t FORCE_MOTION_VECTORS_JITTERED_HASH = char_ptr_crc32("FORCE_MOTION_VECTORS_JITTERED"); // "DEVELOPMENT" only

#if DEVELOPMENT
   std::thread::id global_cbuffer_thread_id;
#endif
}

class Prey final : public Game
{
public:
   static const GameDeviceDataPrey& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataPrey*>(device_data.game);
   }
   static GameDeviceDataPrey& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataPrey*>(device_data.game);
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      // Hardcoding "Globals::NAME" here:
      if (GetModuleHandle(TEXT("PreyDll.dll")) == NULL)
      {
         MessageBoxA(game_window, "You are trying to use \"Prey Luma\" on a game that is not \"Prey (2017)\".\nThe mod will still run but probably crash.", NAME, MB_SETFOREGROUND);
      }

      std::filesystem::path luma_old_shaders_path = file_path.parent_path().append("Prey-Luma\\");
      std::filesystem::path luma_old_bin_path_1 = file_path.parent_path().append("Prey-Luma-ReShade.addon");
      std::filesystem::path luma_old_bin_path_2 = file_path.parent_path().append("Prey-Luma-ReShade.asi");

      if (std::filesystem::is_regular_file(luma_old_bin_path_1) || std::filesystem::is_regular_file(luma_old_bin_path_2) || std::filesystem::is_directory(luma_old_shaders_path))
      {
         MessageBoxA(game_window, "An old version of \"Prey Luma\" was found, we will now try to delete it.\nBack it up before continuing if needed.", NAME, MB_SETFOREGROUND);
      }

      bool removal_failed = false;
      if (std::filesystem::is_regular_file(luma_old_bin_path_1))
      {
         bool removed = false;
         try
         {
            removed = std::filesystem::remove(luma_old_bin_path_1);
         }
         catch (const std::exception&) { }
         if (!removed) removal_failed = true;
      }
      if (std::filesystem::is_regular_file(luma_old_bin_path_2))
      {
         bool removed = false;
         try
         {
            removed = std::filesystem::remove(luma_old_bin_path_2);
         }
         catch (const std::exception&) { }
         if (!removed) removal_failed = true;
      }
      if (std::filesystem::is_directory(luma_old_shaders_path))
      {
         bool removed = false;
         try
         {
            removed = std::filesystem::remove_all(luma_old_shaders_path);
         }
         catch (const std::exception&) {}
         if (!removed) removal_failed = true;
      }
      if (removal_failed)
      {
         auto ret = MessageBoxA(game_window, "Some of the old \"Prey Luma\" files failed to be deleted, it's possible that two versions of the mod will load at once and cause conflicts.\nWould you like to abort the game to manually delete them before running it again?", NAME, MB_SETFOREGROUND | MB_YESNO);
         if (ret == IDYES)
         {
            exit(0);
         }
      }

#if ENABLE_NATIVE_PLUGIN
      if (!failed)
      {
         // Initialize the "native plugin" (our code hooks/patches)
         NativePlugin::Init(NAME, Globals::VERSION);
      }
#endif // ENABLE_NATIVE_PLUGIN

      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(Prey::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(Prey::OnUnmapBufferRegion);
      }
   }

   void OnInit(bool async) override
   {
      // These need to be here as they might not exist before this yet (it's not fully clear why)
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetTooltip("0 - SDR Gamma space\n1 - Linear space\n2 - Linear space until UI (then gamma space)\n\nSelect \"2\" if you want the UI to look exactly like it did in Vanilla\nSelect \"1\" for the highest possible quality (e.g. color accuracy, banding, DLSS/FSR)");
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetValueFixed(false);
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('1'); // Gamma correction happens within LUT sampling in Prey, turning this setting off will have unexpected results (it's not meant to be changed, the off mode is not implemented)
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('1');

      native_shaders_definitions.emplace(CompileTimeStringHash("Perfect Perspective"), ShaderDefinition{ "Luma_PerfectPerspective", reshade::api::pipeline_subobject_type::pixel_shader });
   }

   // Prey seems to create new devices when resetting the game settings (either manually from the menu, or on boot in case the config had invalid ones), but then the new devices might not actually be used?
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataPrey;
      auto& game_device_data = GetGameDeviceData(device_data);

      D3D11_SAMPLER_DESC sampler_desc = {};
      // The original "Perfect Perspective" implementation uses this
      sampler_desc.Filter = D3D11_FILTER_ANISOTROPIC;
      // "BorderColor" is defaulted to black
      sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
      sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
      sampler_desc.MaxAnisotropy = D3D11_REQ_MAXANISOTROPY;
      sampler_desc.MinLOD = 0;
      sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
      HRESULT hr = native_device->CreateSamplerState(&sampler_desc, &game_device_data.lens_distortion_sampler_state);
      assert(SUCCEEDED(hr));
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
#if DEVELOPMENT
      {
         const std::unique_lock lock_trace(s_mutex_trace);
         trace_count = 0;
      }
#endif
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      HDR_textures_upgrade_confirmed_format = HDR_textures_upgrade_requested_format;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      const bool had_drawn_main_post_processing = device_data.has_drawn_main_post_processing;
      const bool had_drawn_upscaling = game_device_data.has_drawn_upscaling;

      // GBuffers composition
      if (!game_device_data.has_drawn_composed_gbuffers && original_shader_hashes.Contains(shader_hashes_TiledShadingTiledDeferredShading))
      {
         game_device_data.has_drawn_composed_gbuffers = true;
      }

      // SSR
      if (!game_device_data.has_drawn_composed_gbuffers && !game_device_data.has_drawn_ssr && original_shader_hashes.Contains(shader_hash_DeferredShadingSSRRaytrace, reshade::api::shader_stage::pixel))
      {
         game_device_data.has_drawn_ssr = true;
         // There's no need to ever skip this added render target, the performance cost is tiny
         if (is_custom_pass)
         {
            uint2 ssr_diffuse_target_resolution = { (UINT)device_data.output_resolution.x, (UINT)device_data.output_resolution.y };

            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            DXGI_FORMAT ssr_texture_format = DXGI_FORMAT_UNKNOWN;

            // See the same code for SSDO (GTAO), the render target resolution is handled in a similar way, based on "r_arkssr" and "r_SSReflHalfRes", but this one can actually draw to a lower (halved) resolution render target (when selecting the half res SSR quality from the menu)
            bool ssr_texture_changed = false;
            if (rtvs[0])
            {
               com_ptr<ID3D11Resource> render_target_resource;
               rtvs[0]->GetResource(&render_target_resource);
               if (render_target_resource)
               {
                  ID3D11Texture2D* prev_ssr_texture = game_device_data.ssr_texture.get();
                  game_device_data.ssr_texture = nullptr;
                  render_target_resource->QueryInterface(&game_device_data.ssr_texture);
                  ssr_texture_changed = prev_ssr_texture != game_device_data.ssr_texture.get();
                  if (game_device_data.ssr_texture)
                  {
                     D3D11_TEXTURE2D_DESC render_target_texture_2d_desc;
                     game_device_data.ssr_texture->GetDesc(&render_target_texture_2d_desc);
                     ssr_diffuse_target_resolution.x = render_target_texture_2d_desc.Width;
                     ssr_diffuse_target_resolution.y = render_target_texture_2d_desc.Height;
                     ssr_texture_format = render_target_texture_2d_desc.Format;
                  }
               }
            }
            if (!game_device_data.ssr_diffuse_texture.get() || game_device_data.ssr_diffuse_texture_width != ssr_diffuse_target_resolution.x || game_device_data.ssr_diffuse_texture_height != ssr_diffuse_target_resolution.y || ssr_texture_changed)
            {
               game_device_data.ssr_diffuse_texture_width = ssr_diffuse_target_resolution.x;
               game_device_data.ssr_diffuse_texture_height = ssr_diffuse_target_resolution.y;

               D3D11_TEXTURE2D_DESC texture_desc;
               texture_desc.Width = game_device_data.ssr_diffuse_texture_width;
               texture_desc.Height = game_device_data.ssr_diffuse_texture_height;
               texture_desc.MipLevels = 1;
               texture_desc.ArraySize = 1;
               texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R8_UNORM;
               texture_desc.SampleDesc.Count = 1;
               texture_desc.SampleDesc.Quality = 0;
               texture_desc.Usage = D3D11_USAGE_DEFAULT;
               texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
               texture_desc.CPUAccessFlags = 0;
               texture_desc.MiscFlags = 0;

               game_device_data.ssr_diffuse_texture = nullptr;
               HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &game_device_data.ssr_diffuse_texture);
               assert(SUCCEEDED(hr));

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv_desc.Format = texture_desc.Format;
               rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
               rtv_desc.Texture2D.MipSlice = 0;

               game_device_data.ssr_diffuse_rtv = nullptr;
               hr = native_device->CreateRenderTargetView(game_device_data.ssr_diffuse_texture.get(), &rtv_desc, &game_device_data.ssr_diffuse_rtv);
               assert(SUCCEEDED(hr));

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
               srv_desc.Format = texture_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MipLevels = 1;
               srv_desc.Texture2D.MostDetailedMip = 0;

               game_device_data.ssr_diffuse_srv = nullptr;
               hr = native_device->CreateShaderResourceView(game_device_data.ssr_diffuse_texture.get(), &srv_desc, &game_device_data.ssr_diffuse_srv);
               assert(SUCCEEDED(hr));

               if (game_device_data.ssr_texture)
               {
                  srv_desc.Format = ssr_texture_format;

                  game_device_data.ssr_srv = nullptr;
                  // Cache the main (first) SSR texture for later retrieval in the SSR blend shader, given it only had access to mip mapped versions of it
                  hr = native_device->CreateShaderResourceView(game_device_data.ssr_texture.get(), &srv_desc, &game_device_data.ssr_srv);
                  assert(SUCCEEDED(hr));
               }
            }

            // Add a second render target to store how "diffuse" reflections need to be, based on the ray travel distance from the relfection point (and the specularity etc).
            // We need to cache and restore all the RTs as the game uses a push and pop mechanism that tracks them closely, so any changes in state can break them.
            com_ptr<ID3D11RenderTargetView> rtv1 = rtvs[1];
            rtvs[1] = game_device_data.ssr_diffuse_rtv.get();
            ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(rtvs[0]); // We can't use "com_ptr"'s "T **operator&()" as it asserts if the object isn't null, even if the reference would be const
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, dsv.get());

#if DEVELOPMENT // Currently we'd only ever need these in development modes to make tweaks, or for in development code paths that are still disabled
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
#endif

            native_device_context->Draw(3, 0);

            rtvs[1] = rtv1;
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, dsv.get());

            ASSERT_ONCE(game_device_data.ssr_command_list == nullptr);
            game_device_data.ssr_command_list = native_device_context; // To make sure we only fix mip map draw calls from the same command list (more can run at the same time in different threads)

            return DrawOrDispatchOverrideType::Replaced;
         }
         else if (game_device_data.ssr_texture.get() || game_device_data.ssr_diffuse_texture.get())
         {
            game_device_data.CleanSSRResources();
         }
      }
      if (game_device_data.has_drawn_ssr && !game_device_data.has_drawn_ssr_blend && native_device_context == game_device_data.ssr_command_list && is_custom_pass && (original_shader_hashes.Contains(shader_hash_PostEffectsGaussBlurBilinear, reshade::api::shader_stage::pixel) || original_shader_hashes.Contains(shader_hash_PostEffectsTextureToTextureResampled, reshade::api::shader_stage::pixel)))
      {
         uint32_t custom_data = 1; // This value will make the SSR mip map generation and blurring shaders take choices specifically designed for SSR
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data);
         updated_cbuffers = true;
         return DrawOrDispatchOverrideType::None;
      }
      if (game_device_data.has_drawn_ssr && !game_device_data.has_drawn_ssr_blend && original_shader_hashes.Contains(shader_hash_DeferredShadingSSReflectionComp, reshade::api::shader_stage::pixel))
      {
         game_device_data.has_drawn_ssr_blend = true;
         game_device_data.ssr_command_list = nullptr;
         if (game_device_data.ssr_srv.get() || game_device_data.ssr_diffuse_srv.get())
         {
            ID3D11ShaderResourceView* const shader_resource_views_const[2] = { game_device_data.ssr_srv.get(), game_device_data.ssr_diffuse_srv.get() };
            native_device_context->PSSetShaderResources(5, 2, &shader_resource_views_const[0]); //TODOFT: unbind these later? Not particularly needed
         }
         updated_cbuffers = true; // No need to update them actually, they are used by this shader
         return DrawOrDispatchOverrideType::None;
      }

      // Pre AA primary post process (HDR to SDR/HDR tonemapping, color grading, sun shafts etc)
      if (game_device_data.has_drawn_composed_gbuffers && !game_device_data.has_drawn_tonemapping && original_shader_hashes.Contains(shader_hashes_HDRPostProcessHDRFinalScene))
      {
         game_device_data.has_drawn_tonemapping = true;
      }

      // Motion Blur
      // Note: this doesn't always run, it's based on a user setting!
      if (game_device_data.has_drawn_composed_gbuffers && !game_device_data.has_drawn_motion_blur && original_shader_hashes.Contains(shader_hashes_MotionBlur))
      {
         game_device_data.has_drawn_motion_blur = true;
      }

      // SSAO
      if (!game_device_data.has_drawn_composed_gbuffers && !game_device_data.has_drawn_ssao && original_shader_hashes.Contains(shader_hashes_DirOccPass))
      {
         game_device_data.has_drawn_ssao = true;
         if (is_custom_pass && GetShaderDefineCompiledNumericalValue(SSAO_TYPE_HASH) >= 1) // If using GTAO
         {
            uint2 gtao_edges_target_resolution = { (UINT)device_data.output_resolution.x, (UINT)device_data.output_resolution.y }; // Note that the swapchain resolution can end up being changed with a delay? Or are we somehow missing resize events?

            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            // This is an optional extra check we can make to properly determine the resolution of our edges texture.
            // Unless "r_ssdoHalfRes" has a value of 3, then the RT would always have the same resolution as the final swapchain output.
            // Unfortunately this texture is sampled with a bilinear sampler by 0-1 UV coordinates, and also because DX11 doesn't allow two render targets to have a different resolution,
            // we'd either need to make this a UAV RW texture or to make sure it matches the original RT in resolution.
            // Given that that cvar isn't exposed to the official game settings and doesn't seem to be enableable even through config, this is disabled to save performance.
#if DEVELOPMENT
            if (rtvs[0])
            {
               com_ptr<ID3D11Resource> render_target_resource;
               rtvs[0]->GetResource(&render_target_resource);
               if (render_target_resource)
               {
                  com_ptr<ID3D11Texture2D> render_target_texture_2d;
                  render_target_resource->QueryInterface(&render_target_texture_2d);
                  if (render_target_texture_2d)
                  {
                     D3D11_TEXTURE2D_DESC render_target_texture_2d_desc;
                     render_target_texture_2d->GetDesc(&render_target_texture_2d_desc);
                     ASSERT_ONCE(gtao_edges_target_resolution.x == render_target_texture_2d_desc.Width && gtao_edges_target_resolution.y == render_target_texture_2d_desc.Height);
                     gtao_edges_target_resolution.x = render_target_texture_2d_desc.Width;
                     gtao_edges_target_resolution.y = render_target_texture_2d_desc.Height;
                  }
               }
            }
#endif
            if (!game_device_data.gtao_edges_texture.get() || game_device_data.gtao_edges_texture_width != gtao_edges_target_resolution.x || game_device_data.gtao_edges_texture_height != gtao_edges_target_resolution.y)
            {
               game_device_data.gtao_edges_texture_width = gtao_edges_target_resolution.x;
               game_device_data.gtao_edges_texture_height = gtao_edges_target_resolution.y;

               D3D11_TEXTURE2D_DESC texture_desc;
               texture_desc.Width = game_device_data.gtao_edges_texture_width;
               texture_desc.Height = game_device_data.gtao_edges_texture_height;
               texture_desc.MipLevels = 1;
               texture_desc.ArraySize = 1;
               texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R8_UNORM; // The texture is encoded to this format
               texture_desc.SampleDesc.Count = 1;
               texture_desc.SampleDesc.Quality = 0;
               texture_desc.Usage = D3D11_USAGE_DEFAULT;
               texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
               texture_desc.CPUAccessFlags = 0;
               texture_desc.MiscFlags = 0;

               game_device_data.gtao_edges_texture = nullptr;
               HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &game_device_data.gtao_edges_texture);
               assert(SUCCEEDED(hr));

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv_desc.Format = texture_desc.Format;
               rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
               rtv_desc.Texture2D.MipSlice = 0;

               game_device_data.gtao_edges_rtv = nullptr;
               hr = native_device->CreateRenderTargetView(game_device_data.gtao_edges_texture.get(), &rtv_desc, &game_device_data.gtao_edges_rtv);
               assert(SUCCEEDED(hr));

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
               srv_desc.Format = texture_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MipLevels = 1;
               srv_desc.Texture2D.MostDetailedMip = 0;

               game_device_data.gtao_edges_srv = nullptr;
               hr = native_device->CreateShaderResourceView(game_device_data.gtao_edges_texture.get(), &srv_desc, &game_device_data.gtao_edges_srv);
               assert(SUCCEEDED(hr));
            }

            // Add a second render target (the depth edges) as it's needed by GTAO.
            // We need to cache and restore all the RTs as the game uses a push and pop mechanism that tracks them closely, so any changes in state can break them.
            com_ptr<ID3D11RenderTargetView> rtv1 = rtvs[1];
            rtvs[1] = game_device_data.gtao_edges_rtv.get();
            ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(rtvs[0]);
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, dsv.get());

            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);
            updated_cbuffers = true; // This is ignored anyway as we return true

            native_device_context->Draw(3, 0);

            rtvs[1] = rtv1;
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, dsv.get());

            return DrawOrDispatchOverrideType::Replaced;
         }
         else if (game_device_data.gtao_edges_texture.get())
         {
            game_device_data.CleanGTAOResources();
         }
      }
      if (game_device_data.has_drawn_ssao && !game_device_data.has_drawn_ssao_denoise && original_shader_hashes.Contains(shader_hashes_SSDO_Blur))
      {
         game_device_data.has_drawn_ssao_denoise = true;
         if (game_device_data.gtao_edges_srv.get())
         {
            ID3D11ShaderResourceView* const shader_resource_view_const = game_device_data.gtao_edges_srv.get();
            native_device_context->PSSetShaderResources(3, 1, &shader_resource_view_const); //TODOFT: unbind these later? Not particularly needed
         }
      }

      // Post AA secondary post process (film grain, vignette, lens optics etc)
      if (game_device_data.has_drawn_composed_gbuffers && !device_data.has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_PostAAComposites))
      {
         uint32_t custom_data = 0;

         // Do lens distortion just before the post AA composition, which draws film grain and other screen space effects
         if (is_custom_pass && cb_luma_global_settings.GameSettings.LensDistortion && device_data.native_pixel_shaders[CompileTimeStringHash("Perfect Perspective")].get())
         {
            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

            com_ptr<ID3D11ShaderResourceView> ps_srv;
            native_device_context->PSGetShaderResources(0, 1, &ps_srv);

            com_ptr<ID3D11PixelShader> ps;
            native_device_context->PSGetShader(&ps, nullptr, 0);

            uint2 lens_distortion_resolution = { (UINT)device_data.output_resolution.x, (UINT)device_data.output_resolution.y };
            DXGI_FORMAT lens_distortion_format = DXGI_FORMAT_UNKNOWN;

            com_ptr<ID3D11Resource> ps_srv_resource;
            if (ps_srv)
            {
               ps_srv->GetResource(&ps_srv_resource);
               if (ps_srv_resource)
               {
                  com_ptr<ID3D11Texture2D> ps_srv_texture_2d;
                  ps_srv_resource->QueryInterface(&ps_srv_texture_2d);
                  if (ps_srv_texture_2d)
                  {
                     D3D11_TEXTURE2D_DESC ps_srv_texture_2d_desc;
                     ps_srv_texture_2d->GetDesc(&ps_srv_texture_2d_desc);
                     lens_distortion_resolution.x = ps_srv_texture_2d_desc.Width;
                     lens_distortion_resolution.y = ps_srv_texture_2d_desc.Height;
                     lens_distortion_format = ps_srv_texture_2d_desc.Format; // Maybe we should take the format from the Shader Resource View instead, but in Prey they'd always match
                     ASSERT_ONCE(game_device_data.lens_distortion_texture_format != DXGI_FORMAT_R11G11B10_FLOAT); // We need a format that supports alpha as we store borders alpha information on it (all other possibly used formats have alpha)
                     // If these ever happened, we'd need to create the new texture acknoledging them
                     ASSERT_ONCE(lens_distortion_resolution.x == device_data.output_resolution.x && lens_distortion_resolution.y == device_data.output_resolution.y);
                     ASSERT_ONCE(ps_srv_texture_2d_desc.SampleDesc.Count == 1 && ps_srv_texture_2d_desc.SampleDesc.Quality == 0);
                  }
               }
            }
            ASSERT_ONCE(ps_srv_resource.get());

            // "Perfect Perspective" original implementation allowed to use mips (from 0 to 4 extra ones) to avoid shimmering at the edges of the screen if distortion was high, we don't distort that much, so we limit it to two extra mips
            // TODO: lower it to 1 and see if the quality would ever suffer from it?
            constexpr UINT lens_distortion_max_mip_levels = 2; // 1 for base/native mip only, 2 should be enough for our light default settings, if we ever allowed more distortion, we should increase it

            switch (lens_distortion_format)
            {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            {
               lens_distortion_format = DXGI_FORMAT_R8G8B8A8_UNORM;
               break;
            }
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            {
               lens_distortion_format = DXGI_FORMAT_B8G8R8A8_UNORM;
               break;
            }
            case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            {
               lens_distortion_format = DXGI_FORMAT_B8G8R8X8_UNORM;
               break;
            }
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            {
               lens_distortion_format = DXGI_FORMAT_R10G10B10A2_UNORM;
               break;
            }
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            {
               lens_distortion_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
               break;
            }
            }

            if (!game_device_data.lens_distortion_texture.get() || game_device_data.lens_distortion_texture_width != lens_distortion_resolution.x || game_device_data.lens_distortion_texture_height != lens_distortion_resolution.y || game_device_data.lens_distortion_texture_format != lens_distortion_format)
            {
               game_device_data.lens_distortion_texture_width = lens_distortion_resolution.x; // Note that at this point, the textures might still be using a restricted area of their total size to do dynamic resolution scaling
               game_device_data.lens_distortion_texture_height = lens_distortion_resolution.y;
               game_device_data.lens_distortion_texture_format = lens_distortion_format; // We take whatever format we had previously, the lens distortion doesn't adjust by image encoding (gamma), whatever that was (usually linear)

               D3D11_TEXTURE2D_DESC texture_desc;
               texture_desc.Width = game_device_data.lens_distortion_texture_width;
               texture_desc.Height = game_device_data.lens_distortion_texture_height;
               texture_desc.MipLevels = lens_distortion_max_mip_levels;
               texture_desc.ArraySize = 1;
               texture_desc.Format = game_device_data.lens_distortion_texture_format;
               texture_desc.SampleDesc.Count = 1;
               texture_desc.SampleDesc.Quality = 0;
               texture_desc.Usage = D3D11_USAGE_DEFAULT;
               texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
               texture_desc.CPUAccessFlags = 0;
               texture_desc.MiscFlags = 0;
               if (lens_distortion_max_mip_levels > 1)
               {
                  texture_desc.BindFlags |= D3D11_BIND_RENDER_TARGET; // RT required by "D3D11_RESOURCE_MISC_GENERATE_MIPS"
                  texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
               }

               game_device_data.lens_distortion_texture = nullptr;
               HRESULT hr = native_device->CreateTexture2D(&texture_desc, nullptr, &game_device_data.lens_distortion_texture);
               assert(SUCCEEDED(hr));

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
               srv_desc.Format = texture_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MipLevels = lens_distortion_max_mip_levels;
               srv_desc.Texture2D.MostDetailedMip = 0;

               game_device_data.lens_distortion_srv = nullptr;
               hr = native_device->CreateShaderResourceView(game_device_data.lens_distortion_texture.get(), &srv_desc, &game_device_data.lens_distortion_srv);
               assert(SUCCEEDED(hr));
            }

            // Clear the SRV and RTV so the copy resource functions below work as expected (this doesn't seem to be necessary)
            ID3D11ShaderResourceView* const emptry_srv = nullptr;
            native_device_context->PSSetShaderResources(0, 1, &emptry_srv); // This crashes if fed nullptr directly
            native_device_context->OMSetRenderTargets(0, nullptr, nullptr);

            if (!game_device_data.lens_distortion_rtv_found)
            {
               size_t prev_lens_distortion_rtv_index = game_device_data.lens_distortion_rtv_index;
               game_device_data.lens_distortion_rtv_index = -1;

               // We can't properly and safely detect the original AA SRVs anymore at this point
               game_device_data.lens_distortion_srvs[0] = nullptr;
               game_device_data.lens_distortion_srvs[1] = nullptr;

               if (game_device_data.lens_distortion_rtvs_resources[0].get() == ps_srv_resource.get())
               {
                  game_device_data.lens_distortion_rtv_index = 0;
               }
               else if (game_device_data.lens_distortion_rtvs_resources[1].get() == ps_srv_resource.get())
               {
                  game_device_data.lens_distortion_rtv_index = 1;
               }
               else
               {
                  // Use index 0 if it's available (empty) or if they are both taken (we already replace the oldest if we can)
                  game_device_data.lens_distortion_rtv_index = game_device_data.lens_distortion_rtvs_resources[0].get() == nullptr ? 0 : (game_device_data.lens_distortion_rtvs_resources[1].get() == nullptr ? 1 : 0);

                  // We could re-use the RTV created by the game for the previous pass, but it's hard to track so create a new one.
                  // Unless "FORCE_DLSS_SMAA_SLIMMED_DOWN_HISTORY" is true, TAA will ping pong between two textures.
                  // We keep a reference to textures that the game might have unreferenced here but it shouldn't matter.
                  game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index] = ps_srv_resource;

                  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
                  rtv_desc.Format = lens_distortion_format;
                  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                  rtv_desc.Texture2D.MipSlice = 0;

                  game_device_data.lens_distortion_rtvs[game_device_data.lens_distortion_rtv_index] = nullptr;
                  HRESULT hr = native_device->CreateRenderTargetView(game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index].get(), &rtv_desc, &game_device_data.lens_distortion_rtvs[game_device_data.lens_distortion_rtv_index]);
                  assert(SUCCEEDED(hr));
               }

               // If the index didn't change for over two frames, the user probably disabled TAA (or the SMAA versions that holds a history texture),
               // so we clean up the other one to avoid it persisting in memory.
               if (game_device_data.lens_distortion_rtv_index == prev_lens_distortion_rtv_index)
               {
                  game_device_data.lens_distortion_rtvs[game_device_data.lens_distortion_rtv_index == 0 ? 1 : 0] = nullptr;
                  game_device_data.lens_distortion_srvs[game_device_data.lens_distortion_rtv_index == 0 ? 1 : 0] = nullptr;
               }
            }
            else
            {
               ASSERT_ONCE(game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index].get() == ps_srv_resource.get()); // Something went wrong, this shouldn't happen
            }

            // This shouldn't last for more than a frame, as the users settings or rendering state could change
            game_device_data.lens_distortion_rtv_found = false;

            // We make a copy of the current "PostAAComposite" source texture (with mip maps), and set that as render target (we draw the lens distortion into it),
            // and replace the shader resource view it came from with the cloned mip mapped texture, then we restore the previous state and run "PostAAComposite" on the distorted texture as if nothing happened.
            // Theoretically we could do the distortion in-line in the "PostAAComposite" shader, but it doesn't work that great given that there's sharpening in there too (so we need the finalized texture to sample it from multiple places that already have distortion applied).
            if (lens_distortion_max_mip_levels > 1)
            {
               native_device_context->CopySubresourceRegion(game_device_data.lens_distortion_texture.get(), 0, 0, 0, 0, game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index].get(), 0, nullptr);
            }
            else
            {
               native_device_context->CopyResource(game_device_data.lens_distortion_texture.get(), game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index].get());
            }

            // If we are using high quality lens distortion, generate mips so we can distort with better quality at the edges of the screen, as bilinear sampling wouldn't be enough anymore (the distorted sampling UVs might cover an area greater than 4 source texels, so bilinear isn't enough anymore).
            // We have a flag to do this directly in CryEngine "FORCE_SMAA_MIPS" through our hooks, but it allocates a higher number of mips so this function would end up generating all of them, which we don't need.
            if (lens_distortion_max_mip_levels > 1)
            {
               native_device_context->GenerateMips(game_device_data.lens_distortion_srv.get());
            }

            // Replace the same SRV as it's now being used as RTV (they can't be in use as both at the same time)
            ID3D11ShaderResourceView* const lens_distortion_srv = game_device_data.lens_distortion_srv.get();
            native_device_context->PSSetShaderResources(0, 1, &lens_distortion_srv);

            // Swap the RTV and SRV if we can, because if we wrote on the same RT that TAA just wrote to, we'd pollute the next frame's AA, given it'd be blending with that resource (which would now have lens distortion).
            // By flip flopping the index, we always use the one that was the history of the current frame, and that won't have any usage in the next frame (it only uses 1 frame of history).
            // If we can't flip it, we are either in the first TAA frame, or we are not using TAA.
            // If we previous used TAA and then change to no AA or FXAA (flipflopless), we can still use the other index for this temporary write, as it'd simply be unused, but still exist in memory (and if not, we'll be keeping it alive for an extra while).
            // Note that when toggling lens distortion, gathering the data might be late by 1 frame so we might end up polluting our own TAA history with lens distortion (for a few frames, until it stops trailing behind).
            size_t flipped_index = game_device_data.lens_distortion_rtv_index == 0 ? 1 : 0;
            bool can_flip = game_device_data.lens_distortion_rtvs[flipped_index].get() && game_device_data.lens_distortion_srvs[flipped_index].get();
            size_t target_index = can_flip ? flipped_index : game_device_data.lens_distortion_rtv_index;
            ID3D11ShaderResourceView* const ps_srv_const = can_flip ? game_device_data.lens_distortion_srvs[game_device_data.lens_distortion_rtv_index].get() : ps_srv.get();

            com_ptr<ID3D11RenderTargetView> rtv0 = rtvs[0];
            rtvs[0] = game_device_data.lens_distortion_rtvs[target_index].get();
            ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(rtvs[0]);
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, dsv.get());

            native_device_context->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Perfect Perspective")].get(), nullptr, 0);

            // Add sampler in an unused slot (we don't need to clear this one)
            ID3D11SamplerState* const lens_distortion_sampler_state = game_device_data.lens_distortion_sampler_state.get();
            native_device_context->PSSetSamplers(10, 1, &lens_distortion_sampler_state);

            // We don't need to set send our cbuffers again as they'd already have the latest values, but... let's do it anyway
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data);
            updated_cbuffers = true;

            // In case DLSS upscaled earlier (it does)
            if (device_data.has_drawn_sr && game_device_data.prey_drs_active)
            {
               SetViewportFullscreen(native_device_context, lens_distortion_resolution);
            }

            // This should be the same draw type that the shader would have used.
            native_device_context->Draw(3, 0);

            native_device_context->PSSetShader(ps.get(), nullptr, 0);

            rtvs[0] = rtv0;
            native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, dsv.get());

            native_device_context->PSSetShaderResources(0, 1, &ps_srv_const);

            // Don't return, let the native "PostAAComposite" draw happen anyway!
         }
         else if (game_device_data.lens_distortion_texture.get() || game_device_data.lens_distortion_rtvs[0].get() || game_device_data.lens_distortion_rtvs[1].get())
         {
            game_device_data.CleanLensDistortionResources();
         }

         if (is_custom_pass && !updated_cbuffers)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data);
            updated_cbuffers = true;
         }

         // This is the last known pass that is guaranteed to run before UI draws in
         device_data.has_drawn_main_post_processing = true;
         // If DRS is not currently running, upscaling won't happen, pretend it did (it'd already be true if DLSS had run).
         // We might not want to set this flag before as we assume it to be turned to true around the time the original upscaling would have run.
         if (!game_device_data.prey_drs_active)
         {
            game_device_data.has_drawn_upscaling = true;
         }
      }

      // SMAA
      // If DLSS is guaranteed to be running instead of SMAA 2TX, we can skip the edge detection passes of SMAA 2TX (these also run other SMAA modes but then DLSS wouldn't run with these).
      // This check might engage one frame late after DLSS engages but it doesn't matter.
      // This is particularly useful because on every boot the game rejects the TAA user config setting (seemengly due to "r_AntialiasingMode" being clamped to 3 (SMAA 2TX)), so we'd waste performance if we didn't skip the passes (we still do).
      if (game_device_data.has_drawn_composed_gbuffers && !device_data.has_drawn_main_post_processing && original_shader_hashes.Contains(shader_hashes_SMAA_EdgeDetection) && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
      {
         return DrawOrDispatchOverrideType::Skip;
      }

      // Vanilla upscaling
      if (game_device_data.has_drawn_composed_gbuffers && !had_drawn_upscaling)
      {
         // Viewport is already fullscreen for this pass
         if (original_shader_hashes.Contains(shader_hash_PostAAUpscaleImage, reshade::api::shader_stage::pixel))
         {
            game_device_data.has_drawn_upscaling = true;
            assert(device_data.has_drawn_main_post_processing && game_device_data.prey_drs_active);
         }
         // Between DLSS SR and upscaling, force the viewport to the full render target resolution at all times, because we upscaled early.
         // Usually this matches the swapchain output resolution, but some lens optics passes actually draw on textures with a different resolution (independently of the game render/output res).
         else if (device_data.has_drawn_sr && game_device_data.prey_drs_active)
         {
            SetViewportFullscreen(native_device_context);
         }
      }

      // Native TAA
      // This pass always runs before our lens distortion, so mark the lens distortion RTV as found here to avoid having to find it again later
      if (game_device_data.has_drawn_composed_gbuffers && cb_luma_global_settings.GameSettings.LensDistortion && original_shader_hashes.Contains(shader_hashes_PostAA) && device_data.cloned_pipeline_count != 0 && device_data.native_pixel_shaders[CompileTimeStringHash("Perfect Perspective")].get())
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);

         ASSERT_ONCE(rtv.get());
         if (rtv.get())
         {
            com_ptr<ID3D11ShaderResourceView> ps_srv;
            native_device_context->PSGetShaderResources(1, 1, &ps_srv); // "PostAA_PreviousSceneTex" (not present in all AA methods)
            com_ptr<ID3D11Resource> ps_srv_resource;
            if (ps_srv)
            {
               ps_srv->GetResource(&ps_srv_resource);
            }
            com_ptr<ID3D11Resource> rtv_resource;
            rtv->GetResource(&rtv_resource);

            bool reset = ps_srv_resource.get() == nullptr || (game_device_data.lens_distortion_rtvs_resources[0].get() != ps_srv_resource.get() && game_device_data.lens_distortion_rtvs_resources[1].get() != ps_srv_resource.get());
            // SMAA 1TX and 2TX ping pong two textures on read write. FXAA and no AA only use one (they only need one, but might still flip flop). LUMA DLSS also only uses one.
            // 
            // If there's no TAA element ongoing, always use index 0.
            // If index 0 and 1 are taken by different RTs, and neither these RT resources match the current TAA shader resource, overwrite index 0 and 1 (reset) as the AA method could have changed.
            // If we previously (2 frames ago) used index 0 and now we find a matching resource as RT, use index 0 again.
            // If index 0 is taken by a different RT, and that RT resource matches the current TAA shader resource (implying RTs ping pong is still happening), use index 1.
            // In any other case, leave everything as it was, it's probably fine.
            if (reset)
            {
               game_device_data.lens_distortion_rtv_index = 0;
               game_device_data.lens_distortion_rtvs[0] = rtv.get();
               game_device_data.lens_distortion_rtvs[1] = nullptr;
               game_device_data.lens_distortion_srvs[0] = ps_srv.get(); // This SRV possibly belongs to a different resource than the RTV
               game_device_data.lens_distortion_srvs[1] = nullptr;
               game_device_data.lens_distortion_rtvs_resources[0] = rtv_resource.get();
               game_device_data.lens_distortion_rtvs_resources[1] = nullptr;
               game_device_data.lens_distortion_rtv_found = true; // Highlight that we have already found it before the actual lens distortion pass
            }
            else if (game_device_data.lens_distortion_rtvs_resources[0].get() == nullptr || game_device_data.lens_distortion_rtvs_resources[0].get() == rtv_resource.get())
            {
               game_device_data.lens_distortion_rtv_index = 0;
               game_device_data.lens_distortion_rtvs[game_device_data.lens_distortion_rtv_index] = rtv.get();
               game_device_data.lens_distortion_srvs[game_device_data.lens_distortion_rtv_index] = ps_srv.get();
               game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index] = rtv_resource.get();
               game_device_data.lens_distortion_rtv_found = true;
            }
            else if (game_device_data.lens_distortion_rtvs_resources[1].get() == nullptr || game_device_data.lens_distortion_rtvs_resources[1].get() == rtv_resource.get())
            {
               game_device_data.lens_distortion_rtv_index = 1;
               game_device_data.lens_distortion_rtvs[game_device_data.lens_distortion_rtv_index] = rtv.get();
               game_device_data.lens_distortion_srvs[game_device_data.lens_distortion_rtv_index] = ps_srv.get();
               game_device_data.lens_distortion_rtvs_resources[game_device_data.lens_distortion_rtv_index] = rtv_resource.get();
               game_device_data.lens_distortion_rtv_found = true;
            }
         }
      }

#if ENABLE_SR
      // SR/TAA
      // We do DLSS after some post processing (e.g. exposure, tonemap, color grading, bloom, blur, objects highlight, sun shafts, other possible AA forms, etc) because running it before post processing
      // would be harder (we'd need to collect more textures manually and manually skip all later AA steps), most importantly, that wouldn't work with the native dynamic resolution the game supports (without changing every single
      // texture sample coordinates in post processing). Even if it's after pp, it should still have enough quality.
      // We replace the "TAA"/"SMAA 2TX" pass (whichever of the ones in our supported passes list is run), ignoring whatever it would have done (the secondary texture it allocated is kept alive, even if we don't use it, we couldn't really destroy it from ReShade),
      // after there's a "composition" pass (film grain, sharpening, ...) and then an optional upscale pass, both of these are too late for DLSS to run.
      // 
      // Don't even try to run DLSS if we have no custom shaders loaded, we need them for DLSS to work properly (it might somewhat work even without them, but it's untested and unneeded)
      if (game_device_data.has_drawn_composed_gbuffers && is_custom_pass && device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && original_shader_hashes.Contains(shader_hashes_PostAA_TAA))
      {
         // TODO: add DLSS transparency mask (e.g. glass, decals, emissive) by caching the g-buffers before and after transparent stuff draws near the end?
         // TODO: add DLSS bias mask (to ignore animated textures) by marking up some shaders(materials)/textures hashes with it? DLSS is smart enough to not really need that
         //TODOFT4: move DLSS before tonemapping, depth of field, bloom and blur. It wouldn't be easy because exposure is calculated after blur in CryEngine,
         // but we could simply fall back on using DLSS Auto Exposure (even if that wouldn't match the actual value used by post processing...).
         // To achieve that, we need to add both DRS+DLSS scaling support to all shaders that run after DLSS, as DLSS would upscale the image before the final upscale pass (and native TAA would be skipped).
         // Sun shafts and lens optics effects would (actually, could) draw in native resolution after upscaling then.
         // Overall that solution has no downsides other than the difficulty of running multiple passes at a different resolution (which really isn't hard as we already have a set up for it).
         // DLSS currently clips all BT.2020 colors (there's no proper way around it).
         // Blur is kinda fine to be done before DLSS as blurry pixels have no detail anyway... (the only problem is that they'd ruin the recomposition after blur stops, but whatever).
         // Bloom is ok before and after (especially if we dejitter it if done b4).
         // Fix the "ENABLE_CAMERA_MOTION_BLUR" comment while making this change.
         // TODO: (idea) increase the number of Halton sequence phases when there's no camera rotation happening, in movement it can benefit from being lower, but when steady (or rotating the camera only, which conserves most of the TAA history),
         // a higher phase count can drastically improve the quality.

         auto* sr_instance_data = device_data.GetSRInstanceData();
         ASSERT_ONCE(sr_instance_data);

         ASSERT_ONCE(device_data.taa_detected); // Why did we get here without TAA enabled?
         com_ptr<ID3D11ShaderResourceView> ps_shader_resources[17];
         // 0 current color source
         // 1 previous color source (post TAA)
         // 2 depth (0-1 being camera origin - far)
         // 3 motion vectors (dynamic objects movement only, no camera movement (if not baked in the dynamic objects))
         // 16 device depth (inverted depth, used by stencil)
         native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources));

         com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);

         const bool dlss_inputs_valid = ps_shader_resources[0].get() != nullptr && ps_shader_resources[16].get() != nullptr && ps_shader_resources[3].get() != nullptr && render_target_views[0] != nullptr;
         ASSERT_ONCE(dlss_inputs_valid);
         if (dlss_inputs_valid)
         {
            // TODO: add FSR 3 state reset? probably not needed!

            com_ptr<ID3D11Resource> output_color_resource;
            render_target_views[0]->GetResource(&output_color_resource);
            com_ptr<ID3D11Texture2D> output_color;
            HRESULT hr = output_color_resource->QueryInterface(&output_color);
            ASSERT_ONCE(SUCCEEDED(hr));

            D3D11_TEXTURE2D_DESC taa_output_texture_desc;
            output_color->GetDesc(&taa_output_texture_desc);

#if FORCE_SMAA_MIPS // Define from the native plugin (not ever defined here!)
            ASSERT_ONCE(taa_output_texture_desc.MipLevels > 1); // To improve "Perfect Perspective" lens distortion
#endif

            ASSERT_ONCE(std::lrintf(device_data.output_resolution.x) == taa_output_texture_desc.Width && std::lrintf(device_data.output_resolution.y) == taa_output_texture_desc.Height);
            std::array<uint32_t, 2> dlss_render_resolution = FindClosestIntegerResolutionForAspectRatio((double)taa_output_texture_desc.Width * (double)device_data.sr_render_resolution_scale, (double)taa_output_texture_desc.Height * (double)device_data.sr_render_resolution_scale, (double)taa_output_texture_desc.Width / (double)taa_output_texture_desc.Height);
            // The "HDR" flag in DLSS SR actually means whether the color is in linear space or "sRGB gamma" (apparently not 2.2) (SDR) space, colors beyond 0-1 don't seem to be clipped either way
            bool dlss_hdr = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1; // we are assuming the value was always a number and not empty

#if TEST_SR
            com_ptr<ID3D11VertexShader> vs;
            com_ptr<ID3D11PixelShader> ps;
            native_device_context->VSGetShader(&vs, nullptr, 0);
            native_device_context->PSGetShader(&ps, nullptr, 0);
#endif // TEST_SR

            // TODO: we could do this async from the beginning of rendering (when we can detect res changes), to here, with a mutex, to avoid potential stutters when DRS first engages (same with creating DLSS textures?) or changes resolution? (we could allow for creating more than one DLSS feature???)
            // 
            // Our DLSS implementation picks a quality mode based on a fixed rendering resolution, but we scale it back in case we detected the game is running DRS, otherwise we run DLAA.
            // At lower quality modes (non DLAA), DLSS actually seems to allow for a wider input resolution range that it actually claims when queried for it, but if we declare a resolution scale below 50% here, we can get an hitch,
            // still, DLSS will keep working at any input resolution (or at least with a pretty big tolerance range).
            // This function doesn't alter the pipeline state (e.g. shaders, cbuffers, RTs, ...), if not, we need to move it to the "Present()" function
            SR::SettingsData settings_data;
				settings_data.output_width = taa_output_texture_desc.Width;
            settings_data.output_height = taa_output_texture_desc.Height;
            settings_data.render_width = dlss_render_resolution[0];
            settings_data.render_height = dlss_render_resolution[1];
            settings_data.dynamic_resolution = game_device_data.prey_drs_detected;
            settings_data.hdr = dlss_hdr;
            settings_data.inverted_depth = true;
            // We modified Prey to make sure this is the case (see "FORCE_MOTION_VECTORS_JITTERED").
            // Previously (dynamic objects) MVs were half jittered (with the current frame's jitters only), because they are rendered with g-buffers, on projection matrices that have jitters.
            // We could't remove these jitters properly when rendering the final motion vectors for DLSS (we tried...), so neither this flag on or off would have been correct.
            // Even if we managed to generate the final MVs without jitters included, it seemengly doesn't look any better anyway.
            settings_data.mvs_jittered = true;
            // MVs in UV space, so we need to scale by the render resolution to transform to pixel space
            settings_data.mvs_x_scale = static_cast<float>(dlss_render_resolution[0]);
            settings_data.mvs_y_scale = static_cast<float>(dlss_render_resolution[1]);
            settings_data.render_preset = dlss_render_preset;
            settings_data.auto_exposure = true;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

#if TEST_SR // Verify that DLSS/FSR never alter the pipeline state (it doesn't, not in the "SR::UpdateSettings()"
            com_ptr<ID3D11ShaderResourceView> ps_shader_resources_post[ARRAYSIZE(ps_shader_resources)];
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources_post), reinterpret_cast<ID3D11ShaderResourceView**>(ps_shader_resources_post));
            for (uint32_t i = 0; i < ARRAYSIZE(ps_shader_resources); i++)
            {
               ASSERT_ONCE(ps_shader_resources[i] == ps_shader_resources_post[i]);
            }

            com_ptr<ID3D11RenderTargetView> render_target_view_post;
            com_ptr<ID3D11DepthStencilView> depth_stencil_view_post;
            native_device_context->OMGetRenderTargets(1, &render_target_view_post, &depth_stencil_view_post);
            ASSERT_ONCE(render_target_views[0] == render_target_view_post && depth_stencil_view == depth_stencil_view_post);

            com_ptr<ID3D11VertexShader> vs_post;
            com_ptr<ID3D11PixelShader> ps_post;
            native_device_context->VSGetShader(&vs_post, nullptr, 0);
            native_device_context->PSGetShader(&ps_post, nullptr, 0);
            ASSERT_ONCE(vs == vs_post && ps == ps_post);
            vs = nullptr;
            ps = nullptr;
            vs_post = nullptr;
            ps_post = nullptr;
#endif // TEST_SR

            bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
            bool dlss_output_changed = false;
            constexpr bool dlss_use_native_uav = true;
            bool dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0; // Unify all of this upscaling function for all games
            if (!dlss_output_supports_uav)
            {
#if ENABLE_NATIVE_PLUGIN
               ASSERT_ONCE(!dlss_use_native_uav); // Should never happen anymore ("FORCE_DLSS_SMAA_UAV" is true)
#endif

               taa_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

               if (device_data.sr_output_color.get())
               {
                  D3D11_TEXTURE2D_DESC dlss_taa_output_texture_desc;
                  device_data.sr_output_color->GetDesc(&dlss_taa_output_texture_desc);
                  dlss_output_changed = dlss_taa_output_texture_desc.Width != taa_output_texture_desc.Width || dlss_taa_output_texture_desc.Height != taa_output_texture_desc.Height || dlss_taa_output_texture_desc.Format != taa_output_texture_desc.Format;
               }
               if (!device_data.sr_output_color.get() || dlss_output_changed)
               {
                  device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                  hr = native_device->CreateTexture2D(&taa_output_texture_desc, nullptr, &device_data.sr_output_color);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
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
               com_ptr<ID3D11Resource> source_color;
               ps_shader_resources[0]->GetResource(&source_color);
               com_ptr<ID3D11Resource> depth_buffer;
               ps_shader_resources[16]->GetResource(&depth_buffer);
               com_ptr<ID3D11Resource> object_velocity_buffer_temp;
               ps_shader_resources[3]->GetResource(&object_velocity_buffer_temp);
               com_ptr<ID3D11Texture2D> object_velocity_buffer;
               hr = object_velocity_buffer_temp->QueryInterface(&object_velocity_buffer);
               ASSERT_ONCE(SUCCEEDED(hr));

               // Generate motion vectors from the objects velocity buffer and the camera movement.
               // For the most past, these look great, especially with rotational camera movement. When there's location camera movement, thin lines do break a bit,
               // and that might be a precision issue with high resolution and jittered matrices not having high enough precision.
               // We take advantage of the state the game had set DX to, and simply swap the render target.
               {
                  D3D11_TEXTURE2D_DESC object_velocity_texture_desc;
                  object_velocity_buffer->GetDesc(&object_velocity_texture_desc);
                  ASSERT_ONCE((object_velocity_texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET);
#if 1 // Use the higher quality for MVs, the game's one were R16G16F. This has a ~1% cost on performance but helps with reducing shimmering on fine lines (stright lines looking segmented, like Bart's hair or Shark's teeth) when the camera is moving in a linear fashion. Generating MVs from the depth is still a limited technique so it can't be perfect.
                  object_velocity_texture_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
#else
                  object_velocity_texture_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
#endif

                  // Update the "dlss_output_changed" flag if we hadn't already (we wouldn't have had a previous copy to compare against above)
                  bool dlss_motion_vectors_changed = dlss_output_changed;
                  if (dlss_output_supports_uav)
                  {
                     if (game_device_data.sr_motion_vectors.get())
                     {
                        D3D11_TEXTURE2D_DESC dlss_motion_vectors_desc;
                        game_device_data.sr_motion_vectors->GetDesc(&dlss_motion_vectors_desc);
                        dlss_output_changed = dlss_motion_vectors_desc.Width != taa_output_texture_desc.Width || dlss_motion_vectors_desc.Height != taa_output_texture_desc.Height;
                        dlss_motion_vectors_changed = dlss_output_changed;
                     }
                  }
                  // We assume the conditions of this texture (and its render target view) changing are the same as "dlss_output_changed"
                  if (!game_device_data.sr_motion_vectors.get() || dlss_motion_vectors_changed)
                  {
                     game_device_data.sr_motion_vectors = nullptr; // Make sure we discard the previous one
                     hr = native_device->CreateTexture2D(&object_velocity_texture_desc, nullptr, &game_device_data.sr_motion_vectors);
                     ASSERT_ONCE(SUCCEEDED(hr));

                     D3D11_RENDER_TARGET_VIEW_DESC object_velocity_render_target_view_desc;
                     render_target_views[0]->GetDesc(&object_velocity_render_target_view_desc);
                     object_velocity_render_target_view_desc.Format = object_velocity_texture_desc.Format;
                     object_velocity_render_target_view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                     object_velocity_render_target_view_desc.Texture2D.MipSlice = 0;

                     game_device_data.sr_motion_vectors_rtv = nullptr; // Make sure we discard the previous one
                     native_device->CreateRenderTargetView(game_device_data.sr_motion_vectors.get(), &object_velocity_render_target_view_desc, &game_device_data.sr_motion_vectors_rtv);
                  }

                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaSettings);
                  SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData);

                  ID3D11RenderTargetView* const dlss_motion_vectors_rtv_const = game_device_data.sr_motion_vectors_rtv.get();
                  native_device_context->OMSetRenderTargets(1, &dlss_motion_vectors_rtv_const, depth_stencil_view.get());

                  // This should be the same draw type that the shader would have used if we went through with it (SMAA 2TX/TAA).
                  native_device_context->Draw(3, 0);
               }

               // Reset the render target, just to make sure there's no conflicts with the same texture being used as RWTexture UAV or Shader Resources
               native_device_context->OMSetRenderTargets(0, nullptr, nullptr);

               // Reset DLSS history if we did not draw motion blur (and we previously did). Based on CryEngine source code, mb is skipped on the first frame after scene cuts, so we want to re-use that information (this works even if MB was disabled).
               // Reset DLSS history if for one frame we had stopped tonemapping. This might include some scene cuts, but also triggers when entering full screen UI menus or videos and then leaving them (it shouldn't be a problem).
               // Reset DLSS history if the output resolution or format changed (just an extra safety mechanism, it might not actually be needed).
               bool reset_dlss = device_data.force_reset_sr || dlss_output_changed || !device_data.has_drawn_main_post_processing_previous || (game_device_data.has_drawn_motion_blur_previous && !game_device_data.has_drawn_motion_blur);
               device_data.force_reset_sr = false;

               uint32_t render_width_dlss = std::lrintf(device_data.render_resolution.x);
               uint32_t render_height_dlss = std::lrintf(device_data.render_resolution.y);

               // These configurations store the image already multiplied by paper white from the beginning of tonemapping, including at the time DLSS runs.
               // The other configurations run DLSS in "SDR" Gamma Space so we couldn't safely change the exposure.
               const bool dlss_use_paper_white_pre_exposure = GetShaderDefineCompiledNumericalValue(POST_PROCESS_SPACE_TYPE_HASH) >= 1;

               float dlss_pre_exposure = 0.f; // 0 means it's ignored
               if (dlss_use_paper_white_pre_exposure)
               {
#if 1 // Alternative that considers a value of 1 in the DLSS color textures to match the SDR output nits range (whatever that is)
                  dlss_pre_exposure = cb_luma_global_settings.ScenePaperWhite / default_paper_white;
#else // Alternative that considers a value of 1 in the DLSS color textures to match 203 nits
                  dlss_pre_exposure = cb_luma_global_settings.ScenePaperWhite / srgb_white_level;
#endif
                  dlss_pre_exposure *= device_data.sr_scene_pre_exposure;
               }
#if DEVELOPMENT
               if (sr_custom_pre_exposure > 0.f)
                  dlss_pre_exposure = sr_custom_pre_exposure;
#endif

               if (!sr_instance_data->automatically_restores_pipeline_state)
               {
                  // TODO: cache the state and restore it below (FSR)
               }

               // There doesn't seem to be a need to restore the DX state to whatever we had before (e.g. render targets, cbuffers, samplers, UAVs, texture shader resources, viewport, scissor rect, ...), CryEngine always sets everything it needs again for every pass.
               // DLSS internally keeps its own frames history, we don't need to do that ourselves (by feeding in an output buffer that was the previous frame's output, though we do have that if needed, it should be in ps_shader_resources[1]).
               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = source_color.get();
               draw_data.output_color = device_data.sr_output_color.get();
               draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
               draw_data.depth_buffer = depth_buffer.get();
               draw_data.pre_exposure = dlss_pre_exposure;
               draw_data.jitter_x = projection_jitters.x * static_cast<float>(render_width_dlss) * -0.5f;
               draw_data.jitter_y = projection_jitters.y * static_cast<float>(render_height_dlss) * 0.5f;
               draw_data.reset = reset_dlss;
               draw_data.render_width = render_width_dlss;
               draw_data.render_height = render_height_dlss;
               draw_data.near_plane = cb_per_view_global.CV_NearFarClipDist.x; // TODO: make sure the scale is in meters (seems to be?)
               draw_data.far_plane = cb_per_view_global.CV_NearFarClipDist.y;
               draw_data.vert_fov = atan(1.f / projection_matrix.m11) * 2.0;
               draw_data.frame_index = cb_luma_global_settings.FrameIndex;
               draw_data.time_delta = cb_per_view_global.CV_AnimGenParams.z - cb_per_view_global_previous.CV_AnimGenParams.z;

               if (sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data))
               {
                  device_data.has_drawn_sr = true;

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
               }

               // Fully reset the state of the RTs given that CryEngine is very delicate with it and uses some push and pop technique (simply resetting caching and resetting the first RT seemed fine for DLSS in case optimization is needed).
               // The fact that it could changes cbuffers or texture resources bindings or viewport seems fines.
               ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(render_target_views[0]);
               native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, depth_stencil_view.get());

               if (device_data.has_drawn_sr)
               {
                  if (!dlss_output_supports_uav)
                  {
                     native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                  }
                  // In this case it's not our business to keep alive this "external" texture
                  else
                  {
                     device_data.sr_output_color = nullptr;
                  }

                  return DrawOrDispatchOverrideType::Replaced; // "Cancel" the previously set draw call, DLSS has taken care of it
               }
               // SR Failed, suppress it for this frame and fall back on SMAA/TAA, hoping that anything before would have been rendered correctly for it already (otherwise it will start being correct in the next frame, given we suppress it (until manually toggled again, given that it'd likely keep failing))
               else
               {
                  ASSERT_ONCE(false);
                  cb_luma_global_settings.SRType = 0;
                  device_data.cb_luma_global_settings_dirty = true;
                  device_data.sr_suppressed = true;
                  device_data.force_reset_sr = true; // We missed frames so it's good to do this, it might also help prevent further errors
               }
            }
            if (dlss_output_supports_uav)
            {
               device_data.sr_output_color = nullptr;
            }
         }
      }
#endif // ENABLE_SR

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // We should have never gotten here with this state! Maybe it could happen if we loaded/unloaded a weird combinations of shaders (e.g. in case they failed to compile or something)
      ASSERT_ONCE(!game_device_data.lens_distortion_rtv_found);

      // Clean resources that are probably not needed anymore (e.g. if users disabled SSR and SSAO, we wouldn't get another chance to clean these up ever).
      // If users unloaded all shaders, these would automatically be cleared ir their rendering pass.
      // If users changed the output resolution, they would be automatically re-created in their rendering pass.
      if (game_device_data.has_drawn_composed_gbuffers)
      {
         // Check if some of them are valid just to avoid constant memory writes (not sure if it's a valid optimization)
         if (!game_device_data.has_drawn_ssr && (game_device_data.ssr_texture.get() || game_device_data.ssr_diffuse_texture.get()))
         {
            game_device_data.CleanSSRResources();
         }
         if (!game_device_data.has_drawn_ssao && game_device_data.gtao_edges_texture.get())
         {
            game_device_data.CleanGTAOResources();
         }
         if (!device_data.has_drawn_main_post_processing && (game_device_data.lens_distortion_texture.get() || game_device_data.lens_distortion_rtvs[0].get() || game_device_data.lens_distortion_rtvs[1].get())) // This seemengly can't happen
         {
            game_device_data.CleanLensDistortionResources();
         }
         if (game_device_data.lens_distortion_rtv_found) // Just for super extra safety
         {
            game_device_data.CleanLensDistortionResources();
         }
      }

      // Update all variables as this is on the only thing guaranteed to run once per frame:
      ASSERT_ONCE(!game_device_data.has_drawn_composed_gbuffers || game_device_data.found_per_view_globals); // We failed to find and assign global cbuffer 13 this frame (could it be that the scene is empty if this triggers?)
      ASSERT_ONCE(game_device_data.has_drawn_composed_gbuffers == device_data.has_drawn_main_post_processing); // Why is g-buffer composition drawing but post processing isn't? We don't expect this to ever happen as PP should always be on
      if (device_data.has_drawn_main_post_processing)
      {
         game_device_data.previous_prey_taa_active[1] = game_device_data.previous_prey_taa_active[0];
         game_device_data.previous_prey_taa_active[0] = game_device_data.prey_taa_active;
      }
      else
      {
         game_device_data.previous_prey_taa_active[1] = false;
         game_device_data.previous_prey_taa_active[0] = false;
         device_data.taa_detected = false;
         // Theoretically we turn this flag off one frame late (or well, at the end of the frame),
         // but then again, if no scene rendered, this flag wouldn't have been used for anything.
         if (cb_luma_global_settings.SRType > 0)
         {
            cb_luma_global_settings.SRType = 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
            device_data.cb_luma_global_settings_dirty = true;
         }
         device_data.sr_suppressed = false;
         // Reset DRS related values if there's a scene cut or loading screen or a menu, we have no way of telling if it's actually still enabled in the user settings.
         // Note that this could cause a micro stutter the next frame we use DLSS as it's gonna have to recreate its internal textures, but we have no way to tell if DRS is still active from the user, so we have to reset the DLSS mode at some point to allow DLAA to run again.
         if (!game_device_data.prey_drs_active)
         {
            device_data.sr_render_resolution_scale = 1.f;
            game_device_data.prey_drs_detected = false;
         }
         device_data.sr_scene_pre_exposure = 1.f;
      }
      game_device_data.has_drawn_ssao = false;
      game_device_data.has_drawn_ssao_denoise = false;
      game_device_data.has_drawn_ssr = false;
      game_device_data.has_drawn_ssr_blend = false;
      game_device_data.has_drawn_composed_gbuffers = false;
      game_device_data.has_drawn_motion_blur_previous = game_device_data.has_drawn_motion_blur;
      game_device_data.has_drawn_motion_blur = false;
      game_device_data.has_drawn_tonemapping = false;
      device_data.has_drawn_main_post_processing = false;
      game_device_data.has_drawn_upscaling = false;
      device_data.has_drawn_sr = false;
#if 1 // Not much need to reset this, but let's do it anyway (e.g. in case the game scene isn't currently rendering)
      game_device_data.prey_drs_active = false;
#endif
      game_device_data.found_per_view_globals = false;
      device_data.previous_render_resolution = device_data.render_resolution;
      previous_projection_matrix = projection_matrix;
      previous_nearest_projection_matrix = nearest_projection_matrix;
      previous_projection_jitters = projection_jitters;
      cb_per_view_global_previous = cb_per_view_global;
      reprojection_matrix.SetIdentity();
#if DEVELOPMENT
      cb_per_view_globals_last_drawn_shader.clear();
      cb_per_view_globals_previous = cb_per_view_globals;
      cb_per_view_globals.clear();
#endif // DEVELOPMENT

#if ENABLE_SR && ENABLE_NATIVE_PLUGIN
      // Update Halton sequence with the latest rendering resolution.
      // Theoretically we should do that at the beginning of the rendering pass, after picking the current frame resolution (in DRS, res can change almost every frame), and after knowing whether DLSS will be active,
      // but in reality there's probably little difference. Also, our implementation rounds it to the closest power of 2.
      // This won't do anything (these values are ignored by the game) unless "TAA" or "SMAA 2TX" are active.
#if DEVELOPMENT
      if (force_taa_jitter_phases > 0)
      {
         NativePlugin::SetHaltonSequencePhases(force_taa_jitter_phases);
      }
      else
#endif // DEVELOPMENT
      {
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
         {
            NativePlugin::SetHaltonSequencePhases(device_data.render_resolution.y, device_data.output_resolution.y);
         }
         // Restore the default value for the game's native TAA, though instead of going to "16" as "r_AntialiasingTAAPattern" "10" would do, we set the phase to 8, which is actually the game's default for TAA/SMAA 2TX, and more appropriate for its short history (4 works too and looks about the same, maybe better, as it's what SMAA defaulted to in CryEngine)
         else
         {
            NativePlugin::SetHaltonSequencePhases(8);
         }
      }
#endif // ENABLE_SR && ENABLE_NATIVE_PLUGIN
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      data.GameData.CameraJitters = projection_jitters; // TODO: pre-multiply these by float2(0.5, -0.5) (NDC to UV space) given that they are always used in UV space by shaders. It doesn't really matter as they end up as "mad" single instructions
      data.GameData.PreviousCameraJitters = previous_projection_jitters;
#if 0
      data.GameData.ViewProjectionMatrix = cb_per_view_global.CV_ViewProjMatr; // Note that this is not 100% thread safe as "CV_ViewProjMatr" is written from another thread
      data.GameData.PrevViewProjectionMatrix = cb_per_view_global_previous.CV_ViewProjMatr;
#endif
      data.GameData.ReprojectionMatrix = reprojection_matrix;
   }

   static bool IsValidGlobalCB(const void* global_buffer_data_ptr)
   {
      const CBPerViewGlobal& global_buffer_data = *((const CBPerViewGlobal*)global_buffer_data_ptr);

      //TODOFT: optimize and verify?
      // Is this the cbuffer we are looking for?
      // Note that even if it was, in the menu a lot of these parameters are uninitialized (usually zeroed around, with matrices being identity).
      // This check overall is a bit crazy, but there's ~0% chance that it will fail and accidentally use a buffer that isn't the global one (cb13)
      bool is_valid_cbuffer = true
         && global_buffer_data.CV_AnimGenParams.x >= 0.f && global_buffer_data.CV_AnimGenParams.y >= 0.f && global_buffer_data.CV_AnimGenParams.z >= 0.f && global_buffer_data.CV_AnimGenParams.w >= 0.f // These are either all four 0 or all four > 0
         && global_buffer_data.CV_CameraRightVector.w == 0.f
         && global_buffer_data.CV_CameraFrontVector.w == 0.f
         && global_buffer_data.CV_CameraUpVector.w == 0.f
         && global_buffer_data.CV_ScreenSize.x > 0.f && global_buffer_data.CV_ScreenSize.y > 0.f && global_buffer_data.CV_ScreenSize.z > 0.f && global_buffer_data.CV_ScreenSize.w > 0.f
         && AlmostEqual(global_buffer_data.CV_ScreenSize.x, global_buffer_data.CV_HPosScale.x * (0.5f / global_buffer_data.CV_ScreenSize.z), 0.5f) && AlmostEqual(global_buffer_data.CV_ScreenSize.y, global_buffer_data.CV_HPosScale.y * (0.5f / global_buffer_data.CV_ScreenSize.w), 0.5f)
         && global_buffer_data.CV_HPosScale.x > 0.f && global_buffer_data.CV_HPosScale.y > 0.f && global_buffer_data.CV_HPosScale.z > 0.f && global_buffer_data.CV_HPosScale.w > 0.f
         && global_buffer_data.CV_HPosScale.x <= 1.f && global_buffer_data.CV_HPosScale.y <= 1.f && global_buffer_data.CV_HPosScale.z <= 1.f && global_buffer_data.CV_HPosScale.w <= 1.f
         && global_buffer_data.CV_HPosClamp.x > 0.f && global_buffer_data.CV_HPosClamp.y > 0.f && global_buffer_data.CV_HPosClamp.z > 0.f && global_buffer_data.CV_HPosClamp.w > 0.f
         && global_buffer_data.CV_HPosClamp.x <= 1.f && global_buffer_data.CV_HPosClamp.y <= 1.f && global_buffer_data.CV_HPosClamp.z <= 1.f && global_buffer_data.CV_HPosClamp.w <= 1.f
         //&& MatrixAlmostEqual(global_buffer_data.CV_InvViewProj.GetTransposed(), global_buffer_data.CV_ViewProjMatr.GetTransposed().GetInverted(), 0.001f) // These checks fail, they need more investigation
         //&& MatrixAlmostEqual(global_buffer_data.CV_InvViewMatr.GetTransposed(), global_buffer_data.CV_ViewMatr.GetTransposed().GetInverted(), 0.001f)
         && (MatrixIsProjection(global_buffer_data.CV_PrevViewProjMatr.GetTransposed()) || MatrixIsIdentity(global_buffer_data.CV_PrevViewProjMatr)) // For shadow projection "CV_PrevViewProjMatr" is actually what its names says it is, instead of being the current projection matrix as in other passes
         && (MatrixIsProjection(global_buffer_data.CV_PrevViewProjNearestMatr.GetTransposed()) || MatrixIsIdentity(global_buffer_data.CV_PrevViewProjNearestMatr))
         && global_buffer_data.CV_SunLightDir.w == 1.f
         //&& global_buffer_data.CV_SunColor.w == 1.f // This is only approximately 1 (maybe not guaranteed, sometimes it's 0)
         && global_buffer_data.CV_SkyColor.w == 1.f
         && global_buffer_data.CV_DecalZFightingRemedy.w == 0.f
         && global_buffer_data.CV_PADDING0 == 0.f && global_buffer_data.CV_PADDING1 == 0.f
         ;
      if (!is_valid_cbuffer)
      {
         ASSERT_ONCE(false); // If this now never happened, we could remove the check... It actually does fail, on particles/transparent stuff
         return false;
      }

#if 0 // This happens, but it's not a problem
      char* global_buffer_data_ptr_cast = (char*)global_buffer_data_ptr;
      // Make sure that all extra memory is zero, as an extra check. This could easily be uninitialized memory though.
      ASSERT_ONCE(IsMemoryAllZero(&global_buffer_data_ptr_cast[sizeof(CBPerViewGlobal) - 1], CBPerViewGlobal_buffer_size - sizeof(CBPerViewGlobal)));
#endif

         ASSERT_ONCE((global_buffer_data.CV_DecalZFightingRemedy.x >= 0.9f && global_buffer_data.CV_DecalZFightingRemedy.x <= 1.f) || global_buffer_data.CV_DecalZFightingRemedy.x == 0.f);

      // Shadow maps and other things temporarily change the values in the global cbuffer,
      // like not use inverse depth (which affects the projection matrix, and thus many other matrices?),
      // use different render and output resolutions, etc etc.
      // We could also base our check on "CV_ProjRatio" (x and y) and "CV_FrustumPlaneEquation" and "CV_DecalZFightingRemedy" as these are also different for alternative views.
      // "CV_PrevViewProjMatr" is not a raw projection matrix when rendering shadow maps, so we can easily detect that.
      // Note: we can check if the matrix is identity to detect whether we are currently in a menu (the main menu?)
      bool is_custom_draw_version = !MatrixIsProjection(global_buffer_data.CV_PrevViewProjMatr.GetTransposed());
      if (is_custom_draw_version)
      {
         return false;
      }

      return true;
   }

   // Call this after reading the global cbuffer (index 13) memory (from CPU or GPU memory). This seemingly only happens in one thread.
   // This will update the "cb_per_view_global" values if the ptr is found to be the right type of buffer (and return true in that case),
   // correct some of its values, and cache information for other usage.
   // 
   // An alternative way of approaching this would be to cache all the address of buffers that are ever filled up through ::Map() calls,
   // then store a copy of each of their instances, and when one of these buffers is set to a shader stage, re-set the same cbuffer with our
   // modified and fixed up data. That is a bit slower but it would be more safe, as it would guarantee us 100% that the buffer we are changing is cbuffer 13.
   // Another alternative, after storing the buffers pointers, would be to at least skip the validity check, after we verified a buffer pointer is only used for the same type of rendering etc.
   // If we were looking for the value of only one buffer in particular, we can simply store the buffer pointers from the DX state in a specific draw call, and then check for following map calls to it.
   bool UpdateGlobalCB(const void* global_buffer_data_ptr, reshade::api::device* device) override
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      const CBPerViewGlobal& global_buffer_data = *((const CBPerViewGlobal*)global_buffer_data_ptr);

      float cb_output_resolution_x = std::round(0.5f / global_buffer_data.CV_ScreenSize.z); // Round here already as it would always meant to be integer
      float cb_output_resolution_y = std::round(0.5f / global_buffer_data.CV_ScreenSize.w);

      bool output_resolution_matches = AlmostEqual(device_data.output_resolution.x, cb_output_resolution_x, 0.5f) && AlmostEqual(device_data.output_resolution.y, cb_output_resolution_y, 0.5f);

#if DEVELOPMENT
      std::thread::id new_global_cbuffer_thread_id = std::this_thread::get_id();
      // Make sure this cbuffer is always updated in the same thread (forever)
      if (global_cbuffer_thread_id != std::thread::id())
      {
         ASSERT_ONCE(global_cbuffer_thread_id == new_global_cbuffer_thread_id);
      }
      global_cbuffer_thread_id = new_global_cbuffer_thread_id;
#endif

      // Copy the temporary buffer ptr into our persistent data
      cb_per_view_global = global_buffer_data;

      // Re-use the current cbuffer as the previous one if we didn't draw the scene in the frame before
      const CBPerViewGlobal& cb_per_view_global_actual_previous = device_data.has_drawn_main_post_processing_previous ? cb_per_view_global_previous : cb_per_view_global;

      auto current_projection_matrix = cb_per_view_global.CV_PrevViewProjMatr;
      auto current_nearest_projection_matrix = cb_per_view_global.CV_PrevViewProjNearestMatr;

      // Note that "taa_detected" would be one frame late here, but to avoid unexpectedly replacing proj matrices, we check it anyway  (the game always starts with a fade to black, so it's fine)
      bool replace_prev_projection_matrix = device_data.cloned_pipeline_count != 0 && ((device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected)
#if DEVELOPMENT || TEST
         || GetShaderDefineCompiledNumericalValue(FORCE_MOTION_VECTORS_JITTERED_HASH) >= 1
#else
         || force_motion_vectors_jittered
#endif
         );

      if (!device_data.has_drawn_main_post_processing_previous)
      {
         previous_projection_matrix = current_projection_matrix;
         previous_nearest_projection_matrix = current_nearest_projection_matrix;
      }

      // Fix up the "previous view projection matrices" as they had wrong data in Prey,
      // first of all, their name was "wrong", because it was meant to have the value of the previous projection matrix,
      // not the camera/view projection matrix, and second, it was actually always based on the current one,
      // so it would miss any changes in FOV and jitters (drastically lowering the quality of motion vectors).
      // After tonemapping, ignore fixing up these, because they'd be jitterless and we don't have a jitterless copy (they aren't used anyway!).
      // If in the previous frame we didn't render, we don't replace the matrix with the one from the last frame that was rendered,
      // because there's no guaranteed that it would match.
      // If AA is disabled, or if the current form of AA doesn't use jittered rendering, this doesn't really make a difference (but it's still better because it creates motion vectors based on the previous view matrix).
      // We've also tried to completely remove the jitters from here and the DLSS reprojection matrix below, and disabling "NVSDK_NGX_DLSS_Feature_Flags_MVJittered" in DLSS, but it doesn't seem to help.
      // Apparently we can also modulate the values in "CV_ViewProjMatr" etc to move the camera in game, but that would require a lot more to polish for (e.g.) a photo mode.
      if (replace_prev_projection_matrix && !game_device_data.has_drawn_tonemapping && device_data.has_drawn_main_post_processing_previous)
      {
         cb_per_view_global.CV_PrevViewProjMatr = previous_projection_matrix;
         cb_per_view_global.CV_PrevViewProjNearestMatr = previous_nearest_projection_matrix;
      }
#if DEVELOPMENT
      // Just for test.
      if (disable_taa_jitters)
      {
         current_projection_matrix.m02 = 0;
         current_projection_matrix.m12 = 0;
         current_nearest_projection_matrix.m02 = 0;
         current_nearest_projection_matrix.m12 = 0;
         cb_per_view_global.CV_PrevViewProjMatr.m02 = 0;
         cb_per_view_global.CV_PrevViewProjMatr.m12 = 0;
         cb_per_view_global.CV_PrevViewProjNearestMatr.m02 = 0;
         cb_per_view_global.CV_PrevViewProjNearestMatr.m12 = 0;
      }
#endif // DEVELOPMENT

      // Fix up the rendering scale for all passes after DLSS SR, as we upscaled before the game expected,
      // there's only post processing passes after it anyway (and lens optics shaders don't really read cbuffer 13 (we made sure of that), but still, some of their passes use custom resolutions).
      if (device_data.has_drawn_sr && game_device_data.prey_drs_active && !game_device_data.has_drawn_upscaling)
      {
         cb_per_view_global.CV_ScreenSize.x = cb_output_resolution_x;
         cb_per_view_global.CV_ScreenSize.y = cb_output_resolution_y;

         cb_per_view_global.CV_HPosScale.x = 1.f;
         cb_per_view_global.CV_HPosScale.y = 1.f;
         // Upgrade the ones from the previous frame too, because at this rendering phase they'd also have been full resolution, and these aren't used anyway
         cb_per_view_global.CV_HPosScale.z = cb_per_view_global.CV_HPosScale.x;
         cb_per_view_global.CV_HPosScale.w = cb_per_view_global.CV_HPosScale.y;

         // Clamp at the last texel center (half pixel offset) at the bottom right of the rendering (which is now equal to output) resolution area.
         // We could probably set these to 1 as well, and skip the last half texel, but that would make the behaviour different from when DRS is running.
         // Note that usually these would be set relative to the render target viewport resolution, not source texture resolution.
         cb_per_view_global.CV_HPosClamp.x = 1.f - cb_per_view_global.CV_ScreenSize.z;
         cb_per_view_global.CV_HPosClamp.y = 1.f - cb_per_view_global.CV_ScreenSize.w;
         cb_per_view_global.CV_HPosClamp.z = cb_per_view_global.CV_HPosClamp.x;
         cb_per_view_global.CV_HPosClamp.w = cb_per_view_global.CV_HPosClamp.y;
      }

      bool render_resolution_matches = AlmostEqual(device_data.render_resolution.x, cb_per_view_global.CV_ScreenSize.x, 0.5f) && AlmostEqual(device_data.render_resolution.y, cb_per_view_global.CV_ScreenSize.y, 0.5f);
      bool is_in_post_processing = game_device_data.has_drawn_composed_gbuffers || game_device_data.has_drawn_tonemapping || device_data.has_drawn_main_post_processing;

      // Update our cached data with information from the cbuffer.
      // After vanilla tonemapping (as soon as AA starts),
      // camera jitters are removed from the cbuffer projection matrices, and the render resolution is also set to 100% (after the upscaling pass),
      // so we want to ignore these cases. We stop at the gbuffer compositions draw, because that's the last know cbuffer 13 to have the perfect values we are looking for (that shader is always run, so it's reliable)!
      // A lot of passes are drawn on scaled down render targets and the cbuffer values would have been updated to reflect that (e.g. "CV_ScreenSize"), so ignore these cases.
      if (output_resolution_matches && (!game_device_data.found_per_view_globals ? true : (!render_resolution_matches && !is_in_post_processing)))
      {
#if DEVELOPMENT
         static float2 local_previous_render_resolution;
         if (!game_device_data.found_per_view_globals)
         {
            local_previous_render_resolution.x = cb_per_view_global.CV_ScreenSize.x;
            local_previous_render_resolution.y = cb_per_view_global.CV_ScreenSize.y;
         }
#endif // DEVELOPMENT

         //TODOFT: these have read/writes that are possibly not thread safe but they should never cause issues in actual usages of Prey
         device_data.render_resolution.x = cb_per_view_global.CV_ScreenSize.x;
         device_data.render_resolution.y = cb_per_view_global.CV_ScreenSize.y;
#if 0 // They should already match and the one we have would be more accurate anyway
         device_data.output_resolution.x = cb_output_resolution_x; // Round here already as it would always meant to be integer
         device_data.output_resolution.y = cb_output_resolution_y;
#endif

         auto previous_prey_drs_active = game_device_data.prey_drs_active.load();
         game_device_data.prey_drs_active = std::abs(device_data.render_resolution.x - device_data.output_resolution.x) >= 0.5f || std::abs(device_data.render_resolution.y - device_data.output_resolution.y) >= 0.5f;
         // Make sure this doesn't change within a frame (once we found DRS in a frame, we should never "lose" it again for that frame.
         // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
         ASSERT_ONCE(device_data.cloned_pipeline_count == 0 || !game_device_data.found_per_view_globals || !previous_prey_drs_active || (previous_prey_drs_active == game_device_data.prey_drs_active));

#if DEVELOPMENT
         // Make sure that our rendering resolution doesn't change randomly within the pipeline (it probably will, it seems to trigger during quick save loads, maybe for the very first draw call to clear buffers)
         const float2 previous_render_resolution = local_previous_render_resolution;
         ASSERT_ONCE(!device_data.has_drawn_main_post_processing_previous || !game_device_data.found_per_view_globals || !game_device_data.prey_drs_detected || (AlmostEqual(device_data.render_resolution.x, previous_render_resolution.x, 0.25f) && AlmostEqual(device_data.render_resolution.y, previous_render_resolution.y, 0.25f)));
#endif // DEVELOPMENT

         // Once we detect the user enabled DRS, we can't ever know it's been disabled because the game only occasionally drops to lower rendering resolutions, so we couldn't know if it was ever disabled
         if (game_device_data.prey_drs_active)
         {
            game_device_data.prey_drs_detected = true;

            float resolution_scale = device_data.render_resolution.y / device_data.output_resolution.y;
            // Lower the DLSS quality mode (which might introduce a stutter, or a slight blurring of the image as it resets the history),
            // but this will make DLSS not use DLAA and instead fall back on a quality mode that allows for a dynamic range of resolutions.
            // This isn't the exact rend resolution DLSS will be forced to use, but the center of a range it's gonna expect.
            // Unfortunately DLSS has a limited range of accepted resolutions per quality mode, and if you go beyond it, it fails to render (until in range again),
            // thus, we need to make sure the automatic DRS range of Prey is within the same range!
            // We couldn't change this resolution scale every frame as it's make DLSS stutter massively.
            // See CryEngine "osm_fbMinScale" cvar (config), that drives the min rend res scale, the DLSS rend scale should ideally be set to the same value, but it's fine if it's above it, given it's the target "average" dynamic resolution.
            // If CryEngine ever went below 50% render scale, we force DLSS into ultra performance mode (33%), as the range allowed by quality mode (67%) can't go below 50%. There will be a stutter (and history reset?) every time we swap back and forth, but at least it works...
            if (resolution_scale < 0.5f - FLT_EPSILON)
            {
#if 1 // Unfortunately no quality mode with a res scale below 0.5 supports dynamic resolution scaling, so we are forced to change the quality mode every frame or so (or at least, every time Prey changes DRS value, which might further slow down the DRS detection mechanism...)
               device_data.sr_render_resolution_scale = resolution_scale;
#else // If we do this, DLSS would fail if any resolution that didn't exactly match 33% render scale was used by the game
               device_data.sr_render_resolution_scale = 1.f / 3.f;
#endif
            }
            else
            {
               // This should pick quality or balanced mode, with a range from 100% to 50% resolution scale
               device_data.sr_render_resolution_scale = 1.f / 1.5f;
            }
         }
         // Reset to DLAA and try again (once), given that we can't go from a 1/3 to a 1 rend scale (e.g. in case DRS was disabled in the menu)
         else if (device_data.sr_suppressed && device_data.sr_render_resolution_scale != 1.f)
         {
            device_data.sr_render_resolution_scale = 1.f;
            device_data.sr_suppressed = false;
         }

         // NOTE: we could just save the first one we found, it should always be jittered and "correct".
         projection_matrix = current_projection_matrix;
         nearest_projection_matrix = current_nearest_projection_matrix;

         const auto projection_jitters_copy = projection_jitters;

         // These are called "m_vProjMatrixSubPixoffset" in CryEngine.
         // The matrix is transposed so we flip the matrix x and y indices.
         projection_jitters.x = current_projection_matrix(0, 2);
         projection_jitters.y = current_projection_matrix(1, 2);

#if DEVELOPMENT
         ASSERT_ONCE(disable_taa_jitters || (projection_jitters_copy.x == 0 && projection_jitters_copy.y == 0) || (projection_jitters.x != 0 || projection_jitters.y != 0)); // Once we found jitters, we should never cache matrices that don't have jitters anymore
#endif

         bool prey_taa_active_copy = game_device_data.prey_taa_active;
         // This is a reliable check to tell whether TAA is enabled. Jitters are "never" zero if they are enabled:
         // they can be if we use the "srand" method, but it would happen one in a billion years;
         // they could also be zero with Halton if the frame index was reset to zero (it is every x frames), but that happens very rarely, and for one frame only (we have two frames as tolerance).
         game_device_data.prey_taa_active = (std::abs(projection_jitters.x * device_data.render_resolution.x) >= 0.00075) || (std::abs(projection_jitters.y * device_data.render_resolution.y) >= 0.00075); //TODOFT: make calculations more accurate (the threshold), especially with higher Halton phases!
#if DEVELOPMENT
         game_device_data.prey_taa_active = game_device_data.prey_taa_active || disable_taa_jitters;
#endif // DEVELOPMENT
         // Make sure that once we detect that TAA was active within a frame, then it should never be detected as off in the same frame (it would mean we are reading a bad cbuffer 13 that we should have discarded).
         // Ignore this when we have no shaders loaded as it would always break due to the "has_drawn_tonemapping" check failing.
         ASSERT_ONCE(device_data.cloned_pipeline_count == 0 || !game_device_data.found_per_view_globals || !prey_taa_active_copy || (prey_taa_active_copy == game_device_data.prey_taa_active));
         if (prey_taa_active_copy != game_device_data.prey_taa_active && device_data.has_drawn_main_post_processing_previous) // TAA changed
         {
            // Detect if TAA was ever detected as on/off/on or off/on/off over 3 frames, because if that was so, our jitter "length" detection method isn't solid enough and we should do more (or add more tolernace to it),
            // this might even happen every x hours once the randomization triggers specific enough values, though all TAA modes have a pretty short cycle with fixed jitters,
            // so it should either happen quickly or never.
            bool middle_value_different = (game_device_data.prey_taa_active == game_device_data.previous_prey_taa_active[0]) != (game_device_data.prey_taa_active == game_device_data.previous_prey_taa_active[1]);
            ASSERT_ONCE(!middle_value_different);
         }
         bool drew_sr = cb_luma_global_settings.SRType > 0; // If this was true, SR would have been enabled and probably drew
         device_data.taa_detected = game_device_data.prey_taa_active || game_device_data.previous_prey_taa_active[0]; // This one has a two frames tolerance. We let it persist even if the game stopped drawing the 3D scene.
         cb_luma_global_settings.SRType = (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected) ? (uint(device_data.sr_type) + 1) : 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
         if (cb_luma_global_settings.SRType > 0 && !drew_sr)
         {
            device_data.cb_luma_global_settings_dirty = true;
            // Reset SR history when we toggle SR on and off manually, or when the user in the game changes the AA mode,
            // otherwise the history from the last time SR was active will be kept (SR implementations don't know time passes since it was last used).
            // We could also clear SR resources here when we know it's unused for a while, but it would possibly lead to stutters.
            device_data.force_reset_sr = true;
         }

         if (!custom_texture_mip_lod_bias_offset)
         {
            std::shared_lock shared_lock_samplers(s_mutex_samplers);

            const auto prev_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;
            if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected && device_data.cloned_pipeline_count != 0)
            {
               device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y);
            }
            else
            {
               // Reset to best fallback value.
               // This bias offset replaces the value from the game (see "samplers_upgrade_mode" 5), which was based on the "r_AntialiasingTSAAMipBias" cvar for most textures (it doesn't apply to all the ones that would benefit from it, and still applies to ones that exhibit moire patterns),
               // but only if TAA was engaged (not SMAA or SMAA+TAA) (it might persist on SMAA after once using TAA, due to a bug).
               // Prey defaults that to 0 but Luma's configs set it to -1.
               device_data.texture_mip_lod_bias_offset = device_data.taa_detected ? -1.f : 0.f;
            }
            const auto new_texture_mip_lod_bias_offset = device_data.texture_mip_lod_bias_offset;

            bool texture_mip_lod_bias_offset_changed = prev_texture_mip_lod_bias_offset != new_texture_mip_lod_bias_offset;
            // Re-create all samplers immediately here instead of doing it at the end of the frame.
            // This allows us to avoid possible (but very unlikely) hitches that could happen if we re-created a new sampler for a new resolution later on when samplers descriptors are set.
            // It also allows us to use the right samplers for this frame's resolution.
            if (texture_mip_lod_bias_offset_changed)
            {
               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               for (auto& samplers_handle : device_data.custom_sampler_by_original_sampler)
               {
                  if (samplers_handle.second.contains(new_texture_mip_lod_bias_offset)) continue; // Skip "resolutions" that already got their custom samplers created
                  ID3D11SamplerState* native_sampler = reinterpret_cast<ID3D11SamplerState*>(samplers_handle.first);
                  shared_lock_samplers.unlock(); // This is fine!
                  {
                     D3D11_SAMPLER_DESC native_desc;
                     native_sampler->GetDesc(&native_desc);
                     com_ptr<ID3D11SamplerState> custom_sampler = CreateCustomSampler(device_data, native_device, native_desc);
                     const std::unique_lock unique_lock_samplers(s_mutex_samplers);
                     samplers_handle.second[new_texture_mip_lod_bias_offset] = custom_sampler;
                  }
                  shared_lock_samplers.lock();
               }
            }
         }

         if (!device_data.has_drawn_main_post_processing_previous)
         {
            device_data.previous_render_resolution = device_data.render_resolution;
            previous_projection_jitters = projection_jitters;

            // Set it to the latest value (ignoring the actual history)
            game_device_data.previous_prey_taa_active[0] = game_device_data.prey_taa_active;
            game_device_data.previous_prey_taa_active[1] = game_device_data.prey_taa_active;
         }

         // This only needs to be calculated once, before or after G-buffers are composed, but before late post processing (TM) starts, as it's for TAA (at the end of PP), and last post processing clears the jitters from the matrices
         if (!game_device_data.has_drawn_composed_gbuffers)
         {
            // NDC to UV space (y is flipped)
            const Matrix44D mScaleBias1 = Matrix44D(
               0.5, 0, 0, 0,
               0, -0.5, 0, 0,
               0, 0, 1, 0,
               0.5, 0.5, 0, 1);
            // UV to NDC space (y is flipped)
            const Matrix44D mScaleBias2 = Matrix44D(
               2.0, 0, 0, 0,
               0, -2.0, 0, 0,
               0, 0, 1, 0,
               -1.0, 1.0, 0, 1);

#if 0 // Not needed anymore, but here in case
            const Matrix44F mViewProjPrev = Matrix44D(cb_per_view_global_actual_previous.CV_ViewMatr.GetTransposed()) * projection_matrix_native * Matrix44D(mScaleBias1);
#endif
            // TODO: we don't need this until we do DLSS later on, once.
            // We calculate all in double for extra precision (this stuff is delicate)
            Matrix44D projection_matrix_native = current_projection_matrix.GetTransposed();
            Matrix44D previous_projection_matrix_native = Matrix44D(previous_projection_matrix.GetTransposed());
            Matrix44D mViewInv;
            MatrixLookAtInverse(mViewInv, Matrix44D(cb_per_view_global.CV_ViewMatr.GetTransposed()));
            Matrix44D mProjInv;
            MatrixPerspectiveFovInverse(mProjInv, projection_matrix_native);
            Matrix44D mReprojection64 = mProjInv * mViewInv * Matrix44D(cb_per_view_global_actual_previous.CV_ViewMatr.GetTransposed()) * previous_projection_matrix_native;
            // These work (NDC adjustments) (anything else doesn't work, I've tried).
            mReprojection64 = mScaleBias2 * mReprojection64 * mScaleBias1;
            reprojection_matrix = mReprojection64.GetTransposed(); // Transpose it here so it's easier to read on the GPU (and consistent with the other matrices)
         }

         game_device_data.found_per_view_globals = true;
      }

      return true;
   }

   //TODOFT6: make these game agnostic? And possibly remove "UpdateGlobalCB"
   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      // No need to convert to native DX11 flags
      if (access == reshade::api::map_access::write_only || access == reshade::api::map_access::write_discard || access == reshade::api::map_access::read_write)
      {
         ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
         DeviceData& device_data = *device->get_private_data<DeviceData>();

         D3D11_BUFFER_DESC buffer_desc;
         buffer->GetDesc(&buffer_desc);

         // The "size" param of ReShade doesn't match with the size of the buffer/mapping.
         // There seems to only ever be one buffer type of this size, but it's not guaranteed (we might have found more, but it doesn't matter, they are discarded later)...
         // They seemingly all happen on the same thread.
         // Some how these are not marked as "D3D11_BIND_CONSTANT_BUFFER", probably because it copies them over to some other buffer later?
         if (buffer_desc.ByteWidth == CBPerViewGlobal_buffer_size)
         {
            device_data.cb_per_view_global_buffer = buffer;
            ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data);
            device_data.cb_per_view_global_buffer_map_data = *data;
#if DEVELOPMENT
            // These are the classic "features" of cbuffer 13 (the one we are looking for), in case any of these were different, it could possibly mean we are looking at the wrong buffer here.
            ASSERT_ONCE(buffer_desc.Usage == D3D11_USAGE_DYNAMIC && buffer_desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && buffer_desc.CPUAccessFlags == D3D11_CPU_ACCESS_WRITE && buffer_desc.MiscFlags == 0 && buffer_desc.StructureByteStride == 0);
#endif // DEVELOPMENT
         }
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      // We assume this buffer is always unmapped before destruction.
      bool is_global_cbuffer = device_data.cb_per_view_global_buffer != nullptr && device_data.cb_per_view_global_buffer == buffer;
      ASSERT_ONCE(!device_data.cb_per_view_global_buffer_map_data || is_global_cbuffer);
      if (is_global_cbuffer && device_data.cb_per_view_global_buffer_map_data != nullptr)
      {
         if (IsValidGlobalCB(device_data.cb_per_view_global_buffer_map_data))
         {
#if DEVELOPMENT && 0
            cb_per_view_globals.emplace_back(global_buffer_data);
            cb_per_view_globals_last_drawn_shader.emplace_back(last_drawn_shader); // The shader hash could we unspecified if we didn't replace the shader
#endif // DEVELOPMENT
#if 1
            if (game->UpdateGlobalCB(device_data.cb_per_view_global_buffer_map_data, device))
#else // TODO: delete
         // The whole buffer size is theoretically "CBPerViewGlobal_buffer_size" but we actually don't have the data for the excessive (padding) bytes,
         // they are never read by shaders on the GPU anyway.
         char global_buffer_data[CBPerViewGlobal_buffer_size];
         std::memcpy(&global_buffer_data[0], device_data.cb_per_view_global_buffer_map_data, CBPerViewGlobal_buffer_size);
         if (game->UpdateGlobalCB(&global_buffer_data[0], device))
#endif
         {
            // Write back the cbuffer data after we have fixed it up (we always do!)
            std::memcpy(device_data.cb_per_view_global_buffer_map_data, &cb_per_view_global, sizeof(CBPerViewGlobal));
#if DEVELOPMENT
            device_data.cb_per_view_global_buffers.emplace(buffer);
#endif // DEVELOPMENT
         }
         }
         device_data.cb_per_view_global_buffer_map_data = nullptr;
         device_data.cb_per_view_global_buffer = nullptr; // No need to keep this cached
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
      reshade::get_config_value(runtime, NAME, "PerspectiveCorrection", cb_luma_global_settings.GameSettings.LensDistortion);
      int HDR_textures_upgrade_requested_format_int = (HDR_textures_upgrade_requested_format == RE::ETEX_Format::eTF_R11G11B10F) ? 0 : 1;
      reshade::get_config_value(runtime, NAME, "HDRPostProcessQuality", HDR_textures_upgrade_requested_format_int);
      HDR_textures_upgrade_requested_format = HDR_textures_upgrade_requested_format_int == 0 ? RE::ETEX_Format::eTF_R11G11B10F : RE::ETEX_Format::eTF_R16G16B16A16F;
   }

   void OnDisplayModeChanged() override
   {
      GetShaderDefineData(AUTO_HDR_VIDEOS_HASH).editable = cb_luma_global_settings.DisplayMode == DisplayModeType::HDR;
   }

   void OnShaderDefinesChanged() override
   {
      // Automatically set "GAMUT_MAPPING_TYPE"
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).editable = !GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).IsValueDefault();
      if (!GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).editable)
      {
         // When these conditions are on, colors beyond BT.2020 are generated, so it's good to gamut map them
         bool expand_color_gamut = GetShaderDefineData(EXPAND_COLOR_GAMUT_HASH).editable_data.GetNumericalValue() != 0;
         bool hdr_tonemap = GetShaderDefineData(TONEMAP_TYPE_HASH).editable_data.GetNumericalValue() == 1;
         bool lut_extrapolation = GetShaderDefineData(ENABLE_LUT_EXTRAPOLATION_HASH).editable_data.GetNumericalValue() != 0;
         char gamut_mapping_type_value = (expand_color_gamut && hdr_tonemap && !lut_extrapolation) ? '3' : '0';
         GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetValue(gamut_mapping_type_value);
         GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue(gamut_mapping_type_value);
      }
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      if (ImGui::Checkbox("Tonemap UI Background", &tonemap_ui_background))
      {
         reshade::set_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("This can help to keep the UI readable when there's a bright background behind it.");
      }
      ImGui::SameLine();
      if (tonemap_ui_background != true)
      {
         ImGui::PushID("Tonemap UI Background");
         if (ImGui::SmallButton(ICON_FK_UNDO))
         {
            tonemap_ui_background = true;
            reshade::set_config_value(runtime, NAME, "TonemapUIBackground", tonemap_ui_background);
         }
         ImGui::PopID();
      }
      else
      {
         const auto& style = ImGui::GetStyle();
         ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
         size.x += style.FramePadding.x;
         size.y += style.FramePadding.y;
         ImGui::InvisibleButton("", ImVec2(size.x, size.y));
      }

      bool lens_distortion = cb_luma_global_settings.GameSettings.LensDistortion;
      if (ImGui::Checkbox("Perspective Correction", &lens_distortion))
      {
         cb_luma_global_settings.GameSettings.LensDistortion = lens_distortion;
         device_data.cb_luma_global_settings_dirty = true;
         reshade::set_config_value(runtime, NAME, "PerspectiveCorrection", lens_distortion);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Enables \"Perspective Correction\" lens distortion.\nThis is a specific type of lens distortion that is not meant to emulate camera lenses,\nbut is meant to make game's screen projection more natural, as if your display was a window to that place, seen directly through your eyes.\nFor example, round objects will appear round at any FOV even if they are at the edges of the screen.\nThe performance cost is low, though it slightly reduces the FOV and sharpness (DLSS is heavily suggested).\nYou can increase the FOV to your liking to counteract the loss of FOV (a vertical FOV around 57.5 (+2.5 degrees) is suggested for it at 16:9, and exponentially more in ultrawide).\nMake sure that the scene and weapons FOVs match for this to look good.\n\"g_reticleYPercentage\" can be changed in the \"game.cfg\" file to move the reticle more or loss towards the center of the screen.");
      }
      ImGui::SameLine();
      if (lens_distortion)
      {
         ImGui::PushID("Perspective Correction");
         if (ImGui::SmallButton(ICON_FK_UNDO))
         {
            bool lens_distortion = false;
            cb_luma_global_settings.GameSettings.LensDistortion = lens_distortion;
            device_data.cb_luma_global_settings_dirty = true;
            reshade::set_config_value(runtime, NAME, "PerspectiveCorrection", lens_distortion);
         }
         ImGui::PopID();
      }
      else
      {
         const auto& style = ImGui::GetStyle();
         ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
         size.x += style.FramePadding.x;
         size.y += style.FramePadding.y;
         ImGui::InvisibleButton("", ImVec2(size.x, size.y));
      }

#if DEVELOPMENT
      const char* hdr_formats[2] = {
          "R11G11B10F",
          "R16G16B16A16F",
      };
#else // !DEVELOPMENT
      const char* hdr_formats[2] = {
          "Medium (Vanilla)",
          "High (Luma)",
      };
#endif // DEVELOPMENT
      bool textures_upgrade_format_changed = false;
      bool textures_upgrade_format_pending_change = false;
      int HDR_textures_upgrade_requested_format_int = (HDR_textures_upgrade_requested_format == RE::ETEX_Format::eTF_R11G11B10F) ? 0 : 1;
      if (ImGui::SliderInt("HDR Post Process Quality", &HDR_textures_upgrade_requested_format_int, 0, 1, hdr_formats[(uint32_t)HDR_textures_upgrade_requested_format_int], ImGuiSliderFlags_NoInput))
      {
         HDR_textures_upgrade_requested_format = HDR_textures_upgrade_requested_format_int == 0 ? RE::ETEX_Format::eTF_R11G11B10F : RE::ETEX_Format::eTF_R16G16B16A16F;
         textures_upgrade_format_changed = true;
         reshade::set_config_value(runtime, NAME, "HDRPostProcessQuality", HDR_textures_upgrade_requested_format_int);
      }
      textures_upgrade_format_pending_change |= HDR_textures_upgrade_requested_format != HDR_textures_upgrade_confirmed_format;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("This controls the quality of Bloom, Motion Blur, Lens Optics (e.g. lens flare, sun glare, ...) textures (bit depth).\nLower it for better performance, at a nearly imperceptible quality cost.\nThis requires the game's resolution to be changed at least once to apply (or a reboot).");
      }
      if (textures_upgrade_format_changed)
      {
#if ENABLE_NATIVE_PLUGIN
         NativePlugin::SetTexturesFormat(RE::ETEX_Format::eTF_R16G16B16A16F, HDR_textures_upgrade_requested_format);
#endif // ENABLE_NATIVE_PLUGIN
      }
      ImGui::SameLine();
      if (textures_upgrade_format_pending_change)
      {
         ImGui::PushID("Texture Formats Change Warning");
         ImGui::BeginDisabled();
         ImGui::SmallButton(ICON_FK_WARNING);
         ImGui::EndDisabled();
         ImGui::PopID();

         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("A manual graphics settings change is required for the textures quality change to apply.\nSimply toggle the game's resolution in the graphics settings menu."); // Resetting the game base graphics settings also recreates textures sometimes but we can't be sure
         }
      }
      else
      {
         const auto& style = ImGui::GetStyle();
         ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
         size.x += style.FramePadding.x;
         size.y += style.FramePadding.y;
         ImGui::InvisibleButton("", ImVec2(size.x, size.y));
      }
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
#if ENABLE_SR
      ImGui::NewLine();
      ImGui::SliderFloat("SR Custom Pre-Exposure", &sr_custom_pre_exposure, 0.0, 10.0);
#endif // ENABLE_SR

      ImGui::NewLine();
      ImGui::SliderInt("Halton TAA Camera Jitter Phases", &force_taa_jitter_phases, 0, 64);
      if (ImGui::Checkbox("Disable TAA Camera Jitters", &disable_taa_jitters))
      {
         if (!disable_taa_jitters && force_taa_jitter_phases == 1)
         {
            force_taa_jitter_phases = 0;
         }
      }
      if (disable_taa_jitters)
      {
         force_taa_jitter_phases = 1; // Having 1 phase means there's no jitters (or well, they might not be centered in the pixel, but they are fixed over time)
      }
   }
#endif // DEVELOPMENT

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      std::string text;

      ImGui::NewLine();
      ImGui::Text("Camera Jitters: ", "");
      // In NCD space
      // Add padding to make it draw consistently even with a "-" in front of the numbers.
      text = (projection_jitters.x >= 0 ? " " : "") + std::to_string(projection_jitters.x) + " " + (projection_jitters.y >= 0 ? " " : "") + std::to_string(projection_jitters.y);
      ImGui::Text(text.c_str(), "");
      // In absolute space
      // These values should be between -1 and 1 (note that X might be flipped)
      text = (projection_jitters.x >= 0 ? " " : "") + std::to_string(projection_jitters.x * device_data.render_resolution.x) + " " + (projection_jitters.y >= 0 ? " " : "") + std::to_string(projection_jitters.y * device_data.render_resolution.y);
      ImGui::Text(text.c_str(), "");

      ImGui::NewLine();
      ImGui::Text("Texture Mip LOD Bias: ", "");
      text = std::to_string(device_data.texture_mip_lod_bias_offset);
      ImGui::Text(text.c_str(), "");

      ImGui::NewLine();
      ImGui::Text("Camera: ", "");
      //TODOFT3: figure out if this is meters or what
      text = "Scene Near: " + std::to_string(cb_per_view_global.CV_NearFarClipDist.x) + " Scene Far: " + std::to_string(cb_per_view_global.CV_NearFarClipDist.y);
      ImGui::Text(text.c_str(), "");
      float tanHalfFOVX = 1.f / projection_matrix.m00;
      float tanHalfFOVY = 1.f / projection_matrix.m11;
      float FOVX = atan(tanHalfFOVX) * 2.0 * 180 / M_PI;
      float FOVY = atan(tanHalfFOVY) * 2.0 * 180 / M_PI;
      text = "Scene: Hor FOV: " + std::to_string(FOVX) + " Vert FOV: " + std::to_string(FOVY);
      ImGui::Text(text.c_str(), "");
      tanHalfFOVX = 1.f / nearest_projection_matrix.m00;
      tanHalfFOVY = 1.f / nearest_projection_matrix.m11;
      FOVX = atan(tanHalfFOVX) * 2.0 * 180 / M_PI;
      FOVY = atan(tanHalfFOVY) * 2.0 * 180 / M_PI;
      text = "Weapon: Hor FOV: " + std::to_string(FOVX) + " Vert FOV: " + std::to_string(FOVY);
      ImGui::Text(text.c_str(), "");
   }
#endif // DEVELOPMENT || TEST

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Prey\" is developed by Pumbo and Ersh and is open source and free.\nIf you enjoy it, consider donating.", "");

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
      ImGui::SameLine();
      static const std::string donation_link_ersh = std::string("Buy Ersh a Coffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_ersh.c_str()))
      {
         system("start https://ko-fi.com/ershin");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
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
         "\nPumbo (Graphics)"
         "\nErsh (Reverse engineering)"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nDKUtil"
         "\nNvidia (DLSS)"
         "\nAMD (FSR)"
         "\nOklab"
         "\nFubaxiusz (Perfect Perspective)"
         "\nIntel (Xe)GTAO"
         "\nDarktable UCS"
         "\nAMD RCAS"
         "\nDICE (HDR tonemapper)"
         "\nCrytek (CryEngine)"
         "\nArkane (Prey)"

         "\n\nThanks:"
         "\nShortFuse (code and support)"
         "\nLilium (support)"
         "\nKoKlusz (testing)"
         "\nMusa (testing)"
         "\ncrosire (support)"
         "\nFreshCloth (support)"
         "\nRegevitamins (support)"
         "\nMartysMods (support)"
         "\nKaldaien (support)"
         "\nnd4spd (testing)"
         , "");
   }

   bool IsGamePaused(const DeviceData& device_data) const override
   {
      // This is a "perfect" way to check if gameplay is paused (sometimes it might still be rendering, but usually there would be no world mapped UI).
      bool paused = cb_per_view_global.CV_AnimGenParams.z == cb_per_view_global_previous.CV_AnimGenParams.z;
#if 1 // Disabled it has one frame delay when we unpause, and it's seemengly not necessary anymore (also, does it actually update every frame at high frame rates? We should then check shaders that made that assumption)
      paused = false;
#endif
      return paused;
   }

   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.sr_motion_vectors_rtv = nullptr;
   }

   float GetTonemapUIBackgroundAmount(const DeviceData& device_data) const override { return tonemap_ui_background ? tonemap_ui_background_amount : 0.f; }
};

//TODOFT3: add asserts for when we meet the shaders we are looking for
//TODOFT6: add a new RT to draw UI on top (pre-multiplied alpha everywhere), so we could compose it smartly, possibly in the final linearization pass. Or, add a new UI gamma setting for when in full screen menus and swap to gamma space on the spot. Then change "POST_PROCESS_SPACE_TYPE" default etc.
//TODOFT6: weapon FOV resets on map load and save load!

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Prey (2017) Luma mod", "https://www.nexusmods.com/prey2017/mods/149");
      Globals::VERSION = 6;

      // Registers 2, 4, 7, 8, 9, 10, 11 and 12 are 100% safe to be used for any post processing or late rendering passes.
      // Register 2 is never used in the whole Prey code. Register 4, 7 and 8 are also seemingly never actively used by Prey.
      // Register 3 seems to be used during post processing so it might not be safe.
      luma_settings_cbuffer_index = 2;
      luma_data_cbuffer_index = 8;
      luma_ui_cbuffer_index = 7;

      reprojection_matrix.SetIdentity();

      // Define the pixel shader of some important passes we can use to determine where we are within the rendering pipeline:
      
      // TiledShading TiledDeferredShading
      shader_hashes_TiledShadingTiledDeferredShading.compute_shaders = { std::stoul("1E676CD5", nullptr, 16), std::stoul("80FF9313", nullptr, 16), std::stoul("571D5EAE", nullptr, 16), std::stoul("6710AFD5", nullptr, 16), std::stoul("54147C78", nullptr, 16), std::stoul("BCD5A089", nullptr, 16), std::stoul("C2FC1948", nullptr, 16), std::stoul("E3EF3C20", nullptr, 16), std::stoul("F8633A07", nullptr, 16), std::stoul("7AB62E81", nullptr, 16) };
      // DeferredShading SSR_Raytrace 
      shader_hash_DeferredShadingSSRRaytrace = std::stoul("AED014D7", nullptr, 16);
      // DeferredShading - SSReflection_Comp
      shader_hash_DeferredShadingSSReflectionComp = std::stoul("F355426A", nullptr, 16);
      // PostEffects GaussBlurBilinear
      shader_hash_PostEffectsGaussBlurBilinear = std::stoul("8B135192", nullptr, 16);
      // PostEffects TextureToTextureResampled
      shader_hash_PostEffectsTextureToTextureResampled = std::stoul("B969DC27", nullptr, 16); // One of the many
      // MotionBlur MotionBlur
      shader_hashes_MotionBlur.pixel_shaders = { std::stoul("D0C2257A", nullptr, 16), std::stoul("76B51523", nullptr, 16), std::stoul("6DCC9E5D", nullptr, 16) };
      // HDRPostProcess HDRFinalScene (vanilla HDR->SDR tonemapping)
      shader_hashes_HDRPostProcessHDRFinalScene.pixel_shaders = { std::stoul("B5DC761A", nullptr, 16), std::stoul("17272B5B", nullptr, 16), std::stoul("F87B4963", nullptr, 16), std::stoul("81CE942F", nullptr, 16), std::stoul("83557B79", nullptr, 16), std::stoul("37ACE8EF", nullptr, 16), std::stoul("66FD11D0", nullptr, 16) };
      // Same as "shader_hashes_HDRPostProcessHDRFinalScene" but it includes ones with sunshafts only
      shader_hashes_HDRPostProcessHDRFinalScene_Sunshafts.pixel_shaders = { std::stoul("81CE942F", nullptr, 16), std::stoul("37ACE8EF", nullptr, 16), std::stoul("66FD11D0", nullptr, 16) };
      // PostAA PostAA
      // The "FXAA" and "SMAA 1TX" passes don't have any projection jitters (unless maybe "SMAA 1TX" could have them if we forced them through config), so we can't replace them with DLSS SR.
      // SMAA (without TX) is completely missing from here as it doesn't have a composition pass we could replace (well we could replace, "NeighborhoodBlendingSMAA" (hash "2E9A5D4C"), but we couldn't be certain that then the TAA pass would run too after).
      shader_hashes_PostAA.pixel_shaders.emplace(std::stoul("D8072D98", nullptr, 16)); // FXAA
      shader_hashes_PostAA.pixel_shaders.emplace(std::stoul("E9D92B11", nullptr, 16)); // SMAA 1TX
      shader_hashes_PostAA.pixel_shaders.emplace(std::stoul("BF813081", nullptr, 16)); // SMAA 2TX and TAA
      shader_hashes_PostAA_TAA.pixel_shaders.emplace(std::stoul("BF813081", nullptr, 16)); // SMAA 2TX and TAA
      // PostAA lendWeightSMAA + PostAA LumaEdgeDetectionSMAA
      shader_hashes_SMAA_EdgeDetection.pixel_shaders = { std::stoul("5636A813", nullptr, 16), std::stoul("47B723BD", nullptr, 16) };

      // PostAA PostAAComposites
      shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("83AE9250", nullptr, 16));
      shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("496492FE", nullptr, 16));
      shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("ED6287FE", nullptr, 16));
      shader_hashes_PostAAComposites.pixel_shaders.emplace(std::stoul("FAEE5EE9", nullptr, 16));
      shader_hash_PostAAUpscaleImage = std::stoul("C2F1D3F6", nullptr, 16); // Upscaling pixel shader (post TAA, only when in frames where DRS engage)
      shader_hashes_LensOptics.pixel_shaders = { std::stoul("4435D741", nullptr, 16), std::stoul("C54F3986", nullptr, 16), std::stoul("DAA20F29", nullptr, 16), std::stoul("047AB485", nullptr, 16), std::stoul("9D7A97B8", nullptr, 16), std::stoul("9B2630A0", nullptr, 16), std::stoul("51F2811A", nullptr, 16), std::stoul("9391298E", nullptr, 16), std::stoul("ED01E418", nullptr, 16), std::stoul("53529823", nullptr, 16), std::stoul("DDDE2220", nullptr, 16) };
      // DeferredShading DirOccPass
      shader_hashes_DirOccPass.pixel_shaders = { std::stoul("944B65F0", nullptr, 16), std::stoul("DB98D83F", nullptr, 16) };
      // ShadowBlur - SSDO Blur
      shader_hashes_SSDO_Blur.pixel_shaders.emplace(std::stoul("1023CD1B", nullptr, 16));
      //TODOFT: check all the Prey scaleform hashes for new unknown blend types, we need to set the cbuffers even for UI passes that render at the beginning of the frame, because they will draw in world UI (e.g. computers)
      //TODOFT: once we have collected 100% of the game shaders, update these hashes lists, and make global functions to convert hashes between string and int
      //TODOFT4: try to add lens distortion to psy-ops screen space effects (e.g. blue ring) so they aren't cropped with it?

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - Vanilla SDR\n1 - Luma HDR (Vanilla+)\n2 - Raw HDR (Untonemapped)\nThe HDR tonemapper works for SDR too\nThis games uses a filmic tonemapper, which slightly crushes blacks"},
         {"SUNSHAFTS_LOOK_TYPE", '2', true, false, "0 - Raw Vanilla\n1 - Vanilla+\n2 - Luma HDR (Suggested)\nThis influences both HDR and SDR, all options work in both"},
         {"ENABLE_LENS_OPTICS_HDR", '1', true, false, "Makes the lens effects (e.g. lens flare) slightly HDR", 1},
         {"AUTO_HDR_VIDEOS", '1', true, false, "(HDR only) Generates some HDR highlights from SDR videos, for consistency\nThis is pretty lightweight so it won't really affect the artistic intent", 1},
         {"EXPAND_COLOR_GAMUT", '1', true, false, "Makes the original tonemapper work in a wider color gamut (HDR BT.2020), resulting in more saturated colors.\nDisable for a more vanilla like experience", 1},
         {"ENABLE_LUT_EXTRAPOLATION", '1', true, false, "LUT Extrapolation should be the best looking and most accurate SDR to HDR LUT adaptation mode,\nbut you can always turn it off for the its simpler fallback", 1},
     #if DEVELOPMENT || TEST
         {"ENABLE_LINEAR_COLOR_GRADING_LUT", '1', true, false, "Whether (SDR) LUTs are stored in linear or gamma space"},
         {"FORCE_NEUTRAL_COLOR_GRADING_LUT_TYPE", '0', true, false, "Can force a neutral LUT in different ways (color grading is still applied)"},
         {"DRAW_LUT", '0', true, (DEVELOPMENT || TEST) ? false : true},
     #endif
         {"SSAO_TYPE", '1', true, false, "Screen Space Ambient Occlusion\n0 - Vanilla\n1 - Luma GTAO\nIn case GTAO is too performance intensive, lower the \"SSAO_QUALITY\" or go into the official game graphics settings and set \"Screen Space Directional Occlusion\" to half resolution\nDLSS is suggested to help with denoising AO"},
         {"SSAO_QUALITY", '1', true, false, "0 - Vanilla\n1 - High\n2 - Extreme (slow)"},
     #if DEVELOPMENT || TEST // For now we don't want to give users this customization, the default value should be good for most users and most cases
         {"SSAO_RADIUS", '1', true, false, "0 - Small\n1 - Vanilla/Standard (suggested)\n2 - Large\nSmaller radiuses can look more stable but don't do as much\nLarger radiuses can look more realistic, but also over darkening and bring out screen space limitations more often (e.g. stuff de-occluding around the edges when turning the camera)\nOnly applies to GTAO"},
     #endif
         {"ENABLE_SSAO_TEMPORAL", '1', true, false, "Disable if you don't use TAA to avoid seeing noise in Ambient Occlusion (though it won't have the same quality)\nYou can disable it for you use TAA too but it's not suggested", 1},
         {"BLOOM_QUALITY", '1', true, false, "0 - Vanilla\n1 - High"},
         {"MOTION_BLUR_QUALITY", '0', true, false, "0 - Vanilla (user graphics setting based)\n1 - Ultra"},
         {"SSR_QUALITY", '1', true, false, "Screen Space Reflections\n0 - Vanilla\n1 - High\n2 - Ultra\n3 - Extreme (slow)\nThis can be fairly expensive so lower it if you are having performance issues"},
     #if DEVELOPMENT || TEST
         {"FORCE_MOTION_VECTORS_JITTERED", force_motion_vectors_jittered ? '1' : '0', true, false, "Forces Motion Vectors generation to include the jitters from the previous frame too, as DLSS needs\nEnabling this forces the native TAA to work as when we have DLSS enabled, making it look a little bit better (less shimmery)", 1},
     #endif
         {"ENABLE_POST_PROCESS", '1', true, false, "Allows you to disable all Post Processing (at once)", 1},
         {"ENABLE_CAMERA_MOTION_BLUR", '0', true, false, "Camera Motion Blur can look pretty botched in Prey, and can mess with DLSS/TAA, it's turned off by default in Luma (in the config files)", 1},
         {"ENABLE_COLOR_GRADING_LUT", '1', true, false, "Allows you to disable Color Grading\nDon't disable it unless you know what you are doing", 1},
         {"POST_TAA_SHARPENING_TYPE", '2', true, false, "0 - None (disabled, soft)\n1 - Vanilla (basic sharpening)\n2 - RCAS (AMD improved sharpening, default preset)\n3 - RCAS (AMD improved sharpening, strong preset)"},
         {"ENABLE_VIGNETTE", '1', true, false, "Allows you to disable Vignette\nIt's not that prominent in Prey, it's only used in certain cases to convey gameplay information,\nso don't disable it unless you know what you are doing", 1},
     #if DEVELOPMENT || TEST // Disabled these final users because these require the "DEVELOPMENT" flag to be used and we don't want users to mess around with them (it's not what the mod wants to achieve)
         {"ENABLE_SHARPENING", '1', true, false, "Allows you to disable Sharpening globally\nDisabling it is not suggested, especially if you use TAA (you can use \"POST_TAA_SHARPENING_TYPE\" for that anyway)", 1},
         {"ENABLE_FILM_GRAIN", '1', true, false, "Allows you to disable Film Grain\nIt's not that prominent in Prey, it's only used in certain cases to convey gameplay information,\nso don't disable it unless you know what you are doing", 1},
     #endif
         {"CORRECT_CRT_INTERLACING_SIZE", '1', true, false, "Disable to keep the vanilla behaviour of CRT like emulated effects becoming near imperceptible at higher resolutions (which defeats their purpose)\nThese are occasionally used in Prey as a fullscreen screen overlay", 1},
         {"ALLOW_LENS_DISTORTION_BLACK_BORDERS", '1', true, false, "Disable to force lens distortion to crop all black borders (further increasing FOV is suggested if you turn this off)", 1},
         {"ENABLE_DITHERING", '0', true, false, "Temporal Dithering control\nIt doesn't seem to be needed in this game so Luma disabled it by default", 1},
         {"DITHERING_BIT_DEPTH", '9', true, false, "Dithering quantization (values between 7 and 9 should be best)"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

#if ENABLE_SR
      sr_game_tooltip = "Select \"SMAA 2TX\" or \"TAA\" in the game's AA settings for Super Resolution (DLSS/DLAA or FSR) to engage.\n";
#endif

      cb_luma_global_settings.GameSettings.LensDistortion = 0;

#if !ENABLE_NATIVE_PLUGIN && DEVELOPMENT
      // Test path to upgrade textures directly through classic Luma code, though this has major issues yet (later in rendering, some stuff is too dark and things flicker)
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
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

            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,

            reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
#endif
#if ENABLE_NATIVE_PLUGIN
      // Prey upgrades resources with native hooks, there's no incompatibilies left
      enable_upgraded_texture_resource_copy_redirection = false;
#endif

      enable_samplers_upgrade = true;
      samplers_upgrade_mode = 5; // Without bruteforcing the offset, many textures (e.g. decals) stay blurry in Prey

      game = new Prey();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
#if ENABLE_NATIVE_PLUGIN
      // Undo the memory patches (at least some of them)
      NativePlugin::Uninit();
#endif // ENABLE_NATIVE_PLUGIN

      reshade::unregister_event<reshade::addon_event::map_buffer_region>(Prey::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(Prey::OnUnmapBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
