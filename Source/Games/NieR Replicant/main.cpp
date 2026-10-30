#define GAME_NIER_REPLICANT 1

#define ALLOW_SHADERS_DUMPING 0

#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#define LUMA_PATCH_BYTECODE_SYNC 1

// assertion triggers with OptiScaler without this, but OptiScaler doesn't work either way
#define ENABLE_GAME_PIPELINE_STATE_READBACK 1

#include "..\..\Core\core.hpp"

#include "ShaderPatches\ShaderPatches.h"

typedef __declspec(align(16)) Matrix44T<double> double4x4;

__declspec(align(16)) struct row_major_float3x4
{
   float m00, m01, m02, m03;
   float m10, m11, m12, m13;
   float m20, m21, m22, m23;

   template <typename F>
   Matrix44T<F> To4x4()
   {
      Matrix44T<F> m4x4;
      m4x4.m00 = m00;
      m4x4.m01 = m01;
      m4x4.m02 = m02;
      m4x4.m03 = m03;

      m4x4.m10 = m10;
      m4x4.m11 = m11;
      m4x4.m12 = m12;
      m4x4.m13 = m13;

      m4x4.m20 = m20;
      m4x4.m21 = m21;
      m4x4.m22 = m22;
      m4x4.m23 = m23;

      m4x4.m30 = 0.0f;
      m4x4.m31 = 0.0f;
      m4x4.m32 = 0.0f;
      m4x4.m33 = 1.0f;

      return m4x4;
   }
};

template <typename T, typename F>
Matrix44T<T> Convert4x4(const Matrix44T<F>& m_f)
{
   Matrix44T<T> m_t;
   m_t.m00 = m_f.m00;
   m_t.m01 = m_f.m01;
   m_t.m02 = m_f.m02;
   m_t.m03 = m_f.m03;

   m_t.m10 = m_f.m10;
   m_t.m11 = m_f.m11;
   m_t.m12 = m_f.m12;
   m_t.m13 = m_f.m13;

   m_t.m20 = m_f.m20;
   m_t.m21 = m_f.m21;
   m_t.m22 = m_f.m22;
   m_t.m23 = m_f.m23;

   m_t.m30 = m_f.m30;
   m_t.m31 = m_f.m31;
   m_t.m32 = m_f.m32;
   m_t.m33 = m_f.m33;

   return m_t;
}

struct SystemCbScene
{
   row_major_float3x4 gMtxView;
   float4x4 gMtxProj;
   float4x4 gMtxViewProj;
   row_major_float3x4 gMtxInvView;
   float4x4 gMtxInvProj;
   float4x4 gMtxInvViewProj;
   row_major_float3x4 gMtxPrevView;
   float4x4 gMtxPrevProj;
   float4x4 gMtxPrevViewProj;
   row_major_float3x4 gMtxPrevInvView;
   float4x4 gMtxPrevInvProj;
   float4x4 gMtxPrevInvViewProj;
   float2 gUVToView;
   float gNearClip;
   float gFarClip;
   float3 gCameraPos;
   float gCameraFov;
   float gTime;
   float gDeltaTime;
   float2 gScreenSize;
   float3 gFrustumCorner;
   float gExposureNormalizeFactor;
   float gLeafDiffuseAlgorithmMix;
   float gGiLightEvaluateScale;
   float2 gEyeLightFadeDistance;
   float3 gCameraDir;
   float gEyeLightScale;
   float gSkinDualLobeSpecularMix;
   float gEyeSpecularityForIBL;
   float gScleraRoughnessMix;
   float gScleraRoughness;
   float gScleraLightScale;
   float3 gIrisCausticsLightDir;
   float gIlluminanceEmissiveInfluence;
   float gEyeEmissiveCubeScale;
   float gIrisCausticsRatio;
   int gIrisCausticsOverride;
   float gIrisConcavityScaleParam;
   float gIrisConcavityPowerParam;
   uint2 pad1;
   float4 gEyeFixedLightOffsetDirection;
   float3 gCameraUp;
   uint pad2;
   float3 gCameraSide;
   float gEyeSpecularityScale;
};

struct BoneCacheElement
{
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11ShaderResourceView> buffer_srv;
    uint32_t time_to_live = 3;
};

namespace
{
   bool first_boot = true; // Automatic setting
   bool enable_hdr = false;
   bool next_enable_hdr = enable_hdr; // The value we serialize, that will be ignored until reboot
   float2 projection_jitters = {0, 0};
   ShaderHashesList shader_hashes_skinned;
   ShaderHashesList shader_hashes_sky_fog;
   ShaderHashesList shader_hashes_particle;
   ShaderHashesList shader_hashes_taa;
   ShaderHashesList shader_hashes_fxaa;
} // namespace

struct GameDeviceDataNierReplicant final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   bool has_drawn_upscaling = false;

   ComPtr<ID3D11Texture2D> depth_texture;

   ComPtr<ID3D11Texture2D> motion_vectors;
   ComPtr<ID3D11RenderTargetView> motion_vectors_rtv;
   ComPtr<ID3D11ShaderResourceView> motion_vectors_srv;
   ComPtr<ID3D11UnorderedAccessView> motion_vectors_uav;

   ComPtr<ID3D11Texture2D> scaled_motion_vectors;
   ComPtr<ID3D11UnorderedAccessView> scaled_motion_vectors_uav;

   ComPtr<ID3D11Texture2D> resolve_texture;

   ComPtr<ID3D11Buffer> system_cb_scene;
   SystemCbScene* mapped_system_cb_scene = nullptr;
   SystemCbScene system_cb_scene_data;
   float4x4 reprojection_matrix;
#endif // ENABLE_SR
   bool has_updated_resolutions = false;
   bool has_cleared_motion_vector_texture = false;
   bool has_updated_particle_cb = false;

   ComPtr<ID3D11Buffer> cbuffer_motion_vector;

   ComPtr<ID3D11Buffer> partial_update_constant_buffer_16;
   ComPtr<ID3D11Buffer> partial_update_constant_buffer_64;

   // make sure the object isn't deleted between queueing and commiting the cache
   std::vector<ComPtr<ID3D11ShaderResourceView>> pending_bone_cache_com_ptrs;
   std::unordered_set<ID3D11ShaderResourceView*> pending_bone_cache;
   std::unordered_map<ID3D11ShaderResourceView*, BoneCacheElement> bone_cache;

   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> fog_patched_pixel_shaders;
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> motion_vector_pixel_shaders;
};

class NierReplicant final : public Game
{
   static GameDeviceDataNierReplicant& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataNierReplicant*>(device_data.game);
   }

   static bool SrActive(const DeviceData& device_data)
   {
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   }

public:
   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');

      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Motion Vector"), ShaderDefinition{"Luma_PrepareMotionVector", reshade::api::pipeline_subobject_type::compute_shader});

      reshade::register_event<reshade::addon_event::create_pipeline>(NierReplicant::OnCreatePipeline);
      reshade::register_event<reshade::addon_event::map_buffer_region>(NierReplicant::OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::unmap_buffer_region>(NierReplicant::OnUnmapBufferRegion);
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      if (!enable_hdr)
      {
         cb_luma_global_settings.DisplayMode = DisplayModeType::SDR;
         cb_luma_global_settings.ScenePeakWhite = srgb_white_level;
         cb_luma_global_settings.ScenePaperWhite = srgb_white_level;
         cb_luma_global_settings.UIPaperWhite = srgb_white_level;
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         bd.ByteWidth = 16;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.partial_update_constant_buffer_16.put());
      }
      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         bd.ByteWidth = 64;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.partial_update_constant_buffer_64.put());
      }

      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 64;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.cbuffer_motion_vector.put());
      }
   }

   std::unique_ptr<std::byte[]> PatchShaderBytecodeSync(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type == reshade::api::pipeline_subobject_type::vertex_shader)
      {
          if (System::ScanMemoryForPattern(shader_object, shader_object_size, (std::byte*)"SystemBuffPrevBones", 19, true).size() != 0)
          {
              shader_hashes_skinned.vertex_shaders.emplace((uint32_t)shader_hash);
          }
         if (System::ScanMemoryForPattern(shader_object, shader_object_size, (std::byte*)"cbChangesEveryFrame", 16, true).size() != 0)
         {
            shader_hashes_particle.vertex_shaders.emplace((uint32_t)shader_hash);
            return nullptr;
         }
         if (System::ScanMemoryForPattern(shader_object, shader_object_size, (std::byte*)"gMtxPrevViewProj", 16, true).size() == 0)
         {
            return nullptr;
         }
         return PatchConstantVertexShader(code, size, shader_object, shader_object_size);
      }
      return nullptr;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if ((stages & reshade::api::shader_stage::vertex) == 0 &&
          (stages & reshade::api::shader_stage::pixel) == 0)
      {
         return DrawOrDispatchOverrideType::None;
      }
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!game_device_data.has_updated_resolutions && max(last_draw_dispatch_data.index_count, last_draw_dispatch_data.vertex_count) > 3)
      {
         D3D11_VIEWPORT viewport;
         uint32_t num_viewports = 1;
         native_device_context->RSGetViewports(&num_viewports, &viewport);

         ComPtr<ID3D11RenderTargetView> render_target_view;
         native_device_context->OMGetRenderTargets(1, render_target_view.put(), nullptr);
         ComPtr<ID3D11Resource> rtv_resource;
         if (render_target_view)
         {
             render_target_view->GetResource(rtv_resource.put());
             ComPtr<ID3D11Texture2D> rtv_texture;
             rtv_resource->QueryInterface(rtv_texture.put());

             D3D11_TEXTURE2D_DESC rtv_desc;
             rtv_texture->GetDesc(&rtv_desc);
             device_data.output_resolution = { (float)rtv_desc.Width, (float)rtv_desc.Height };
         }


         device_data.render_resolution = {viewport.Width, viewport.Height};
         cb_luma_global_settings.RenderSize = device_data.render_resolution;
         cb_luma_global_settings.RenderInvSize = float2(1.f / cb_luma_global_settings.RenderSize.x, 1.f / cb_luma_global_settings.RenderSize.y);
         device_data.cb_luma_global_settings_dirty = true;
         if (!custom_texture_mip_lod_bias_offset)
         {
            std::shared_lock shared_lock_samplers(s_mutex_samplers);
            if (SrActive(device_data) &&
                device_data.render_resolution.y > 0.0f &&
                device_data.output_resolution.y > 0.0f)
            {
               device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
            }
            else
            {
               device_data.texture_mip_lod_bias_offset = 0.f;
            }
         }

         if (SrActive(device_data))
         {
            double4x4 mtxProj = Convert4x4<double>(game_device_data.system_cb_scene_data.gMtxProj);
            mtxProj.m02 -= 2.0f * projection_jitters.x / (float)device_data.render_resolution.x;
            mtxProj.m12 += 2.0f * projection_jitters.y / (float)device_data.render_resolution.y;
            double4x4 mtxViewProj = (mtxProj * game_device_data.system_cb_scene_data.gMtxView.To4x4<double>());
            double4x4 mtxInvProj = mtxProj.GetInverted();
            double4x4 mtxInvViewProj = game_device_data.system_cb_scene_data.gMtxInvView.To4x4<double>() * mtxInvProj;

            double4x4 mtxPrevProj = Convert4x4<double>(game_device_data.system_cb_scene_data.gMtxPrevProj);
            mtxPrevProj.m02 -= 2.0f * projection_jitters.x / (float)device_data.render_resolution.x;
            mtxPrevProj.m12 += 2.0f * projection_jitters.y / (float)device_data.render_resolution.y;
            double4x4 mtxPrevViewProj = (mtxPrevProj * game_device_data.system_cb_scene_data.gMtxPrevView.To4x4<double>());
            double4x4 mtxPrevInvProj = mtxPrevProj.GetInverted();
            double4x4 mtxPrevInvViewProj = game_device_data.system_cb_scene_data.gMtxPrevInvView.To4x4<double>() * mtxPrevInvProj;

            game_device_data.system_cb_scene_data.gMtxProj = Convert4x4<float>(mtxProj);
            game_device_data.system_cb_scene_data.gMtxViewProj = Convert4x4<float>(mtxViewProj);
            game_device_data.system_cb_scene_data.gMtxInvProj = Convert4x4<float>(mtxInvProj);
            game_device_data.system_cb_scene_data.gMtxInvViewProj = Convert4x4<float>(mtxInvViewProj);

            game_device_data.system_cb_scene_data.gMtxPrevProj = Convert4x4<float>(mtxPrevProj);
            game_device_data.system_cb_scene_data.gMtxPrevViewProj = Convert4x4<float>(mtxPrevViewProj);
            game_device_data.system_cb_scene_data.gMtxPrevInvProj = Convert4x4<float>(mtxPrevInvProj);
            game_device_data.system_cb_scene_data.gMtxPrevInvViewProj = Convert4x4<float>(mtxPrevInvViewProj);

            game_device_data.reprojection_matrix = Convert4x4<float>(mtxPrevViewProj * mtxInvViewProj);

            if (game_device_data.system_cb_scene)
            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.system_cb_scene.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               memcpy(mapped_cbuffer.pData, &game_device_data.system_cb_scene_data, sizeof(game_device_data.system_cb_scene_data));
               native_device_context->Unmap(game_device_data.system_cb_scene.get(), 0);
            }
         }

         game_device_data.has_updated_resolutions = true;
      }

      if (SrActive(device_data))
      {
         auto pixel_shader_it = game_device_data.motion_vector_pixel_shaders.find(original_shader_hashes.pixel_shaders.front());
         if (pixel_shader_it != game_device_data.motion_vector_pixel_shaders.cend())
         {
            ComPtr<ID3D11DepthStencilView> depth_stencil_view;
            ComPtr<ID3D11RenderTargetView> render_target_views[6];
            {
               ID3D11RenderTargetView* render_target_views_raw[6];
               native_device_context->OMGetRenderTargets(6, &render_target_views_raw[0], depth_stencil_view.put());
               for (uint32_t i = 0; i < 6; ++i)
               {
                  render_target_views[i].attach(render_target_views_raw[i]);
               }
            }

            if (render_target_views[5].get() != game_device_data.motion_vectors_rtv.get())
            {
               ID3D11RenderTargetView* updated_render_target_views[] = {render_target_views[0].get(),
                  render_target_views[1].get(),
                  render_target_views[2].get(),
                  render_target_views[3].get(),
                  render_target_views[4].get(),
                  game_device_data.motion_vectors_rtv.get()};
               native_device_context->OMSetRenderTargets(6, updated_render_target_views, depth_stencil_view.get());
            }

            if (!game_device_data.has_cleared_motion_vector_texture)
            {
               ComPtr<ID3D11Resource> depth_resource;
               depth_stencil_view->GetResource(depth_resource.put());
               depth_resource->QueryInterface(game_device_data.depth_texture.put());

               ComPtr<ID3D11Resource> rtv_resource;
               render_target_views[0]->GetResource(rtv_resource.put());

               ComPtr<ID3D11Texture2D> rtv_texture;
               rtv_resource->QueryInterface(rtv_texture.put());

               D3D11_TEXTURE2D_DESC rtv_desc;
               rtv_texture->GetDesc(&rtv_desc);

               bool needs_recreate = true;

               if (game_device_data.motion_vectors)
               {
                  D3D11_TEXTURE2D_DESC motion_vectors_desc;
                  game_device_data.motion_vectors->GetDesc(&motion_vectors_desc);
                  if (motion_vectors_desc.Width == rtv_desc.Width &&
                      motion_vectors_desc.Height == rtv_desc.Height)
                  {
                     needs_recreate = false;
                  }
               }
               if (needs_recreate)
               {
                  {
                     D3D11_TEXTURE2D_DESC motion_vector_desc = {};
                     motion_vector_desc.Width = rtv_desc.Width;
                     motion_vector_desc.Height = rtv_desc.Height;
                     motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
                     motion_vector_desc.ArraySize = 1;
                     motion_vector_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                     motion_vector_desc.SampleDesc.Count = 1;
                     motion_vector_desc.SampleDesc.Quality = 0;
                     motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
                     motion_vector_desc.CPUAccessFlags = 0;
                     motion_vector_desc.MiscFlags = 0;
                     motion_vector_desc.MipLevels = 1;

                     native_device->CreateTexture2D(&motion_vector_desc,
                        nullptr,
                        game_device_data.motion_vectors.put());
                  }
                  {
                     D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                     rtv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                     rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                     rtv_desc.Texture2D.MipSlice = 0;

                     native_device->CreateRenderTargetView(game_device_data.motion_vectors.get(),
                        &rtv_desc,
                        game_device_data.motion_vectors_rtv.put());
                  }
                  {
                     D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                     srv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                     srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                     srv_desc.Texture2D.MostDetailedMip = 0;
                     srv_desc.Texture2D.MipLevels = 1;

                     native_device->CreateShaderResourceView(game_device_data.motion_vectors.get(),
                        &srv_desc,
                        game_device_data.motion_vectors_srv.put());
                  }
                  {
                     D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                     uav_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                     uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                     uav_desc.Texture2D.MipSlice = 0;

                     native_device->CreateUnorderedAccessView(game_device_data.motion_vectors.get(),
                        &uav_desc,
                        game_device_data.motion_vectors_uav.put());
                  }

                  {
                     D3D11_TEXTURE2D_DESC motion_vector_desc = {};
                     motion_vector_desc.Width = rtv_desc.Width;
                     motion_vector_desc.Height = rtv_desc.Height;
                     motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
                     motion_vector_desc.ArraySize = 1;
                     motion_vector_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                     motion_vector_desc.SampleDesc.Count = 1;
                     motion_vector_desc.SampleDesc.Quality = 0;
                     motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                     motion_vector_desc.CPUAccessFlags = 0;
                     motion_vector_desc.MiscFlags = 0;
                     motion_vector_desc.MipLevels = 1;

                     native_device->CreateTexture2D(&motion_vector_desc,
                        nullptr,
                        game_device_data.scaled_motion_vectors.put());
                  }
                  {
                     D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                     uav_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                     uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                     uav_desc.Texture2D.MipSlice = 0;

                     native_device->CreateUnorderedAccessView(game_device_data.scaled_motion_vectors.get(),
                        &uav_desc,
                        game_device_data.scaled_motion_vectors_uav.put());
                  }
               }

               float motion_clear[] = {-1000.0f, -1000.0f, 0.0f, 0.0f};
               native_device_context->ClearUnorderedAccessViewFloat(game_device_data.motion_vectors_uav.get(), motion_clear);

               game_device_data.has_cleared_motion_vector_texture = true;
            }

            native_device_context->PSSetShader(pixel_shader_it->second.get(), nullptr, 0);

            if (shader_hashes_skinned.Contains(original_shader_hashes))
            {
                ComPtr<ID3D11ShaderResourceView> srvs[2];
                {
                    ID3D11ShaderResourceView* srvs_raw[2];
                    native_device_context->VSGetShaderResources(0, 2, &srvs_raw[0]);
                    for (uint32_t i = 0; i < 2; ++i)
                    {
                        srvs[i].attach(srvs_raw[i]);
                    }
                }

                auto bone_cache_it = game_device_data.bone_cache.find(srvs[1].get());
                if (bone_cache_it != game_device_data.bone_cache.end())
                {
                    native_device_context->VSSetShaderResources(1, 1, bone_cache_it->second.buffer_srv.get_addressof());
                }

                if (game_device_data.pending_bone_cache.insert(srvs[0].get()).second)
                {
                    game_device_data.pending_bone_cache_com_ptrs.push_back(srvs[0]);
                }
            }
         }
         else if (shader_hashes_particle.Contains(original_shader_hashes))
         {
            if (!game_device_data.has_updated_particle_cb)
            {
               ComPtr<ID3D11Buffer> constant_buffer_changes_every_frame;
               native_device_context->VSGetConstantBuffers(0, 1, constant_buffer_changes_every_frame.put());

               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.partial_update_constant_buffer_64.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               memcpy(mapped_cbuffer.pData, &game_device_data.system_cb_scene_data.gMtxViewProj, sizeof(game_device_data.system_cb_scene_data.gMtxViewProj));
               native_device_context->Unmap(game_device_data.partial_update_constant_buffer_64.get(), 0);

               native_device_context->CopySubresourceRegion(constant_buffer_changes_every_frame.get(), 0, 0, 0, 0, game_device_data.partial_update_constant_buffer_64.get(), 0, nullptr);
               game_device_data.has_updated_particle_cb = true;
            }
         }
         else if (shader_hashes_taa.Contains(original_shader_hashes) && game_device_data.depth_texture)
         {
             for (auto buffer_srv: game_device_data.pending_bone_cache)
             {
                 ComPtr<ID3D11Resource> bone_buffer_resource;
                 buffer_srv->GetResource(bone_buffer_resource.put());
                 ComPtr<ID3D11Buffer> bone_buffer;
                 bone_buffer_resource->QueryInterface(bone_buffer.put());

                 auto storage_it = game_device_data.bone_cache.find(buffer_srv);
                 if (storage_it == game_device_data.bone_cache.end())
                 {
                     D3D11_BUFFER_DESC buffer_desc;
                     bone_buffer->GetDesc(&buffer_desc);

                     ComPtr<ID3D11Buffer> cache_buffer;
                     native_device->CreateBuffer(&buffer_desc, nullptr, cache_buffer.put());

                     D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                     buffer_srv->GetDesc(&srv_desc);

                     ComPtr<ID3D11ShaderResourceView> cache_srv;
                     native_device->CreateShaderResourceView(cache_buffer.get(), &srv_desc, cache_srv.put());

                     game_device_data.bone_cache[buffer_srv] = { cache_buffer, cache_srv, 3 };

                     storage_it = game_device_data.bone_cache.find(buffer_srv);
                 }

                 native_device_context->CopyResource(storage_it->second.buffer.get(), bone_buffer.get());
                 storage_it->second.time_to_live = 3;
             }

             game_device_data.pending_bone_cache.clear();
             game_device_data.pending_bone_cache_com_ptrs.clear();

             for (auto it = game_device_data.bone_cache.begin(); it != game_device_data.bone_cache.end();)
             {
                 if (it->second.time_to_live == 0)
                 {
                     it = game_device_data.bone_cache.erase(it);
                 }
                 else
                 {
                     it->second.time_to_live--;
                     it++;
                 }
             }

            auto* sr_instance_data = device_data.GetSRInstanceData();

            device_data.cb_luma_global_settings_dirty = true;
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::all_compute, LumaConstantBufferType::LumaSettings);

            {
               ComPtr<ID3D11ShaderResourceView> depth_texture_srv;
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  native_device->CreateShaderResourceView(game_device_data.depth_texture.get(),
                     &srv_desc,
                     depth_texture_srv.put());
               }

               {
                  D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
                  native_device_context->Map(game_device_data.cbuffer_motion_vector.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
                  memcpy(mapped_cbuffer.pData, &game_device_data.reprojection_matrix, sizeof(game_device_data.reprojection_matrix));
                  native_device_context->Unmap(game_device_data.cbuffer_motion_vector.get(), 0);
               }

               ID3D11Buffer* cbs[] = {game_device_data.cbuffer_motion_vector.get()};
               ID3D11ShaderResourceView* srvs[] = {game_device_data.motion_vectors_srv.get(), depth_texture_srv.get()};
               ID3D11UnorderedAccessView* uavs[] = {game_device_data.scaled_motion_vectors_uav.get()};

               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Prepare Motion Vector")].get(), 0, 0);
               native_device_context->CSSetConstantBuffers(0, 1, cbs);
               native_device_context->CSSetShaderResources(0, 2, srvs);
               native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
               native_device_context->Dispatch((device_data.render_resolution.x + 7) / 8, (device_data.render_resolution.y + 7) / 8, 1);
            }

            {
               D3D11_TEXTURE2D_DESC desc = {};
               desc.Width = device_data.output_resolution.x;
               desc.Height = device_data.output_resolution.y;
               desc.Usage = D3D11_USAGE_DEFAULT;
               desc.ArraySize = 1;
               desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
               desc.SampleDesc.Count = 1;
               desc.SampleDesc.Quality = 0;
               desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
               desc.CPUAccessFlags = 0;
               desc.MiscFlags = 0;
               desc.MipLevels = 1;

               native_device->CreateTexture2D(&desc,
                  nullptr,
                  game_device_data.resolve_texture.put());
            }

            ComPtr<ID3D11ShaderResourceView> src_srv;
            native_device_context->PSGetShaderResources(0, 1, src_srv.put());

            if (!src_srv)
            {
               return DrawOrDispatchOverrideType::None;
            }

            ComPtr<ID3D11Resource> src_resource;
            src_srv->GetResource(src_resource.put());

            {
               SR::SettingsData settings_data;
               settings_data.output_width = device_data.output_resolution.x;
               settings_data.output_height = device_data.output_resolution.y;
               settings_data.render_width = (device_data.output_resolution.x / 4) * 3;
               settings_data.render_height = (device_data.output_resolution.y / 4) * 3;
               settings_data.dynamic_resolution = true;
               settings_data.hdr = true;
               settings_data.inverted_depth = false;
               settings_data.mvs_jittered = false;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
            }
            {
               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = src_resource.get();
               draw_data.output_color = game_device_data.resolve_texture.get();
               draw_data.motion_vectors = game_device_data.scaled_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth_texture.get();
               draw_data.render_width = device_data.render_resolution.x;
               draw_data.render_height = device_data.render_resolution.y;
               draw_data.pre_exposure = 0.0f;
               draw_data.jitter_x = projection_jitters.x;
               draw_data.jitter_y = projection_jitters.y;
               draw_data.vert_fov = game_device_data.system_cb_scene_data.gCameraFov;
               draw_data.near_plane = game_device_data.system_cb_scene_data.gNearClip;
               draw_data.far_plane = game_device_data.system_cb_scene_data.gFarClip;
               draw_data.time_delta = game_device_data.system_cb_scene_data.gDeltaTime;
               draw_data.reset = device_data.force_reset_sr;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
               game_device_data.has_drawn_upscaling = dlss_succeeded;
               device_data.has_drawn_sr = dlss_succeeded;
            }

            ComPtr<ID3D11RenderTargetView> dst_rtv;
            native_device_context->OMGetRenderTargets(1, dst_rtv.put(), nullptr);
            if (dst_rtv)
            {
               ComPtr<ID3D11Resource> dst_resource;
               dst_rtv->GetResource(dst_resource.put());

               native_device_context->CopySubresourceRegion(dst_resource.get(), 0, 0, 0, 0, game_device_data.resolve_texture.get(), 0, nullptr);

               return DrawOrDispatchOverrideType::Replaced;
            }
         }
         else if (shader_hashes_fxaa.Contains(original_shader_hashes))
         {
            ComPtr<ID3D11ShaderResourceView> src_srv;
            native_device_context->PSGetShaderResources(0, 1, src_srv.put());
            ComPtr<ID3D11Resource> src_resource;
            if (src_srv)
            {
               src_srv->GetResource(src_resource.put());
            }

            ComPtr<ID3D11RenderTargetView> dst_rtv;
            native_device_context->OMGetRenderTargets(1, dst_rtv.put(), nullptr);
            ComPtr<ID3D11Resource> dst_resource;
            if (dst_rtv)
            {
               dst_rtv->GetResource(dst_resource.put());
            }

            if (src_resource && dst_resource)
            {
               native_device_context->CopySubresourceRegion(dst_resource.get(), 0, 0, 0, 0, src_resource.get(), 0, nullptr);
            }
         }
      }
      else
      {
         auto pixel_shader_it = game_device_data.fog_patched_pixel_shaders.find(original_shader_hashes.pixel_shaders.front());
         if (pixel_shader_it != game_device_data.fog_patched_pixel_shaders.cend())
         {
            native_device_context->PSSetShader(pixel_shader_it->second.get(), nullptr, 0);
         }
      }
      if (shader_hashes_sky_fog.Contains(original_shader_hashes))
      {
         // skytex_fog_reso isn't actually used by these shaders it's just easier to update it here
         // for the transparent materials that use it since the cb is always using the same register
         ComPtr<ID3D11Buffer> sky_texture_fog_constant_buffer;
         native_device_context->PSGetConstantBuffers(2, 1, sky_texture_fog_constant_buffer.put());

         float4 skytex_fog_reso;
         skytex_fog_reso.x = device_data.output_resolution.x / 4.0f;
         skytex_fog_reso.y = device_data.output_resolution.y / 4.0f;
         skytex_fog_reso.z = 1.0f / skytex_fog_reso.x;
         skytex_fog_reso.w = 1.0f / skytex_fog_reso.y;

         D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
         native_device_context->Map(game_device_data.partial_update_constant_buffer_16.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
         memcpy(mapped_cbuffer.pData, &skytex_fog_reso, sizeof(skytex_fog_reso));
         native_device_context->Unmap(game_device_data.partial_update_constant_buffer_16.get(), 0);

         native_device_context->CopySubresourceRegion(sky_texture_fog_constant_buffer.get(), 0, 16, 0, 0, game_device_data.partial_update_constant_buffer_16.get(), 0, nullptr);
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataNierReplicant;
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (SrActive(device_data) && game_device_data.has_updated_resolutions)
      {
         data.GameData.ProjectionJitters.x = projection_jitters.x / (float)device_data.render_resolution.x;
         data.GameData.ProjectionJitters.y = projection_jitters.y / (float)device_data.render_resolution.y;
      }
      else
      {
         data.GameData.ProjectionJitters.x = 0.0f;
         data.GameData.ProjectionJitters.y = 0.0f;
      }
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      device_data.force_reset_sr = !game_device_data.has_drawn_upscaling;
      game_device_data.has_drawn_upscaling = false;
      game_device_data.has_updated_resolutions = false;
      game_device_data.has_cleared_motion_vector_texture = false;
      game_device_data.has_updated_particle_cb = false;

      game_device_data.system_cb_scene.reset();
      game_device_data.depth_texture.reset();

      // Update TAA jitters:
      int phases = SR::GetDefaultJitterPhases();
      if (device_data.sr_type != SR::Type::None)
      {
         auto* sr_instance_data = device_data.GetSRInstanceData();
         phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
      }
      int temporal_frame = cb_luma_global_settings.FrameIndex % phases;
      projection_jitters.x = SR::HaltonSequence(temporal_frame, 2);
      projection_jitters.y = SR::HaltonSequence(temporal_frame, 3);
   }

   static bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout layout, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            if (subobject.type == reshade::api::pipeline_subobject_type::pixel_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               if (System::ScanMemoryForPattern((const std::byte*)original_shader_desc->code, original_shader_desc->code_size, (std::byte*)"PREV_POSITION_CS", 16, true).size() == 0)
               {
                  continue;
               }
               uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
               std::vector<std::byte> shader_code((const std::byte*)original_shader_desc->code, ((const std::byte*)original_shader_desc->code) + original_shader_desc->code_size);

               if (PatchPixelShaderFogScaling(shader_code))
               {
                  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
                  ID3D11PixelShader* shader;
                  native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &shader);

                  game_device_data.fog_patched_pixel_shaders[hash] = shader;
               }
               if (PatchPixelShaderMotionVectors(shader_code))
               {
                  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
                  ID3D11PixelShader* shader;
                  native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &shader);

                  game_device_data.motion_vector_pixel_shaders[hash] = shader;
               }
            }
         }
      }
      return false;
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      if (access != reshade::api::map_access::write_discard)
      {
         return;
      }
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);

      ComPtr<ID3D11Buffer> native_buffer;
      native_resource->QueryInterface(native_buffer.put());
      if (!native_buffer)
      {
         return;
      }
      D3D11_BUFFER_DESC bd;
      native_buffer->GetDesc(&bd);
      if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0 &&
          bd.ByteWidth == 912)
      {
         auto& device_data = *device->get_private_data<DeviceData>();
         auto& game_device_data = GetGameDeviceData(device_data);
         game_device_data.mapped_system_cb_scene = (SystemCbScene*)*data;
         game_device_data.system_cb_scene = native_buffer;
      }
   }

   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      if (game_device_data.mapped_system_cb_scene)
      {
         // only copy data can't update the matrices until we know viewport size
         game_device_data.system_cb_scene_data = *game_device_data.mapped_system_cb_scene;
         game_device_data.mapped_system_cb_scene = nullptr;
      }
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::Checkbox("Enable HDR", &next_enable_hdr))
      {
         reshade::set_config_value(runtime, NAME, "EnableHDR", next_enable_hdr);
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("NieR Replicant™ ver.1.22474487139... Luma mod - about and credits section", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "NieR Replicant™ ver.1.22474487139... Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      enable_samplers_upgrade = true;

      shader_hashes_sky_fog.pixel_shaders.emplace(0x64A0A452);
      shader_hashes_sky_fog.pixel_shaders.emplace(0x253152FD);
      shader_hashes_sky_fog.pixel_shaders.emplace(0xCAA93520);

      shader_hashes_taa.pixel_shaders.emplace(0xAF46DA8B);
      shader_hashes_taa.pixel_shaders.emplace(0xB2EA4C80);
      shader_hashes_taa.pixel_shaders.emplace(0x7B9E33BA);
      shader_hashes_taa.pixel_shaders.emplace(0x3DD2E82C);

      shader_hashes_fxaa.pixel_shaders.emplace(0xFD188ABA);
      shader_hashes_fxaa.pixel_shaders.emplace(0x744152FF);
      shader_hashes_fxaa.pixel_shaders.emplace(0x4BA20BF0);
      shader_hashes_fxaa.pixel_shaders.emplace(0x89D61155);

      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
         reshade::api::format::r10g10b10a2_unorm};

      redirected_shader_hashes["Tonemap"] =
         {
            "029A4C03",
            "0701FA1A",
            "0AF8FBCC",
            "12F91938",
            "134D5127",
            "1B8367C6",
            "1CD01365",
            "1D46FC37",
            "2685FF45",
            "2A516D34",
            "31FF4D91",
            "325E18C9",
            "34A816BA",
            "39D658BC",
            "3E142416",
            "44285E78",
            "45BBA609",
            "58F52206",
            "59302561",
            "5A0EA592",
            "5B18AA0C",
            "5DE08981",
            "5E926B65",
            "656E3714",
            "675FFB89",
            "6DF41D20",
            "7C9826D7",
            "7EEEE3BF",
            "80EF63B9",
            "85778F80",
            "88A1CCBA",
            "8BE0264D",
            "8F609210",
            "90EE1419",
            "91993DC4",
            "939D4667",
            "9422AA33",
            "96F7E635",
            "9C292250",
            "A1A5BEF7",
            "A59F6D98",
            "A6B8160E",
            "ADDE23B0",
            "B104F0E0",
            "B42C78C9",
            "B59F3693",
            "BBC9C6BB",
            "BD39BA78",
            "BECCA52E",
            "BFF7D818",
            "C93A88D1",
            "CD7AA2E4",
            "D270A735",
            "D4B5F3B6",
            "D74BFC47",
            "D9ACC9FF",
            "E6563C2D",
            "F10BD1EB",
            "F24DAEA3",
            "F5C3DAD8",
            "FC031FC2",
            "FE6C4D15"};

      redirected_shader_hashes["ColorGrading"] =
         {
            "02D70327",
            "151DE180",
            "1620C24C",
            "444114F8",
            "45E46B73",
            "6140A5AC",
            "690C605C",
            "70A11A47",
            "7DA8E920",
            "906B6848",
            "9716339C",
            "CE45E40C",
            "D207899C",
            "E45D3F6F",
            "E59C431D",
            "EF8E0BE4"};

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      swapchain_upgrade_type = SwapchainUpgradeType::None;

      game = new NierReplicant();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::create_pipeline>(NierReplicant::OnCreatePipeline);
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(NierReplicant::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(NierReplicant::OnUnmapBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      reshade::get_config_value(nullptr, NAME, "FirstBoot", first_boot);
      if (first_boot)
      {
         reshade::set_config_value(nullptr, NAME, "FirstBoot", false);

         // Automatically enable HDR in the mod if it's supported on the primary display on first boot
         bool hdr_supported_display;
         bool hdr_enabled_display;
         Display::IsHDRSupportedAndEnabled(0, hdr_supported_display, hdr_enabled_display);
         enable_hdr = hdr_supported_display;

         reshade::set_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      else
      {
         reshade::get_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      next_enable_hdr = enable_hdr;

      if (enable_hdr)
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      }
      else
      {
         swapchain_upgrade_type = SwapchainUpgradeType::None;
         force_disable_display_composition = true;
      }
   }

   return TRUE;
}