#define GAME_GREEDFALL 1

#define ENABLE_NGX 1

#include "..\..\Core\core.hpp"

namespace
{
   ShaderHashesList shader_hashes_TAA;
}

thread_local void* mapped_cbuffer_data = nullptr;

struct GameDeviceDataGreedFall final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   std::atomic<bool> has_drawn_upscaling = false;

   com_ptr<ID3D11Texture2D> scaled_motion_vectors;
   com_ptr<ID3D11UnorderedAccessView> scaled_motion_vectors_uav;
   com_ptr<ID3D11Texture2D> exposure;
   com_ptr<ID3D11UnorderedAccessView> exposure_uav;
   com_ptr<ID3D11Texture2D> resolve_texture;
   com_ptr<ID3D11ShaderResourceView> resolve_texture_srv;
   com_ptr<ID3D11Texture2D> merged_texture;
   com_ptr<ID3D11UnorderedAccessView> merged_texture_uav;

   float2 jitter = {0, 0};
   uint2 size = {0, 0};
#endif // ENABLE_SR
};

class GreedFall final : public Game
{
   static GameDeviceDataGreedFall& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataGreedFall*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Inputs"), ShaderDefinition{"Luma_PrepareInputs", reshade::api::pipeline_subobject_type::compute_shader});

      luma_settings_cbuffer_index = 13;
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::map_buffer_region>(GreedFall::OnMapBufferRegion);
         reshade::register_event<reshade::addon_event::unmap_buffer_region>(GreedFall::OnUnmapBufferRegion);
      }
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(
      ID3D11Device* native_device,
      ID3D11DeviceContext* native_device_context,
      CommandListData& cmd_list_data,
      DeviceData& device_data,
      reshade::api::shader_stage stages,
      const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes,
      bool is_custom_pass,
      bool& updated_cbuffers,
      std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (device_data.sr_type != SR::Type::None &&
          !device_data.sr_suppressed &&
          original_shader_hashes.Contains(shader_hashes_TAA))
      {
         com_ptr<ID3D11ShaderResourceView> vs_srv;
         native_device_context->VSGetShaderResources(0, 1, &vs_srv);

         com_ptr<ID3D11ShaderResourceView> ps_srvs[3];
         native_device_context->PSGetShaderResources(0, 3, &ps_srvs[0]);

         com_ptr<ID3D11Resource> color_resource;
         ps_srvs[0]->GetResource(&color_resource);
         com_ptr<ID3D11Texture2D> color_texture;
         color_resource->QueryInterface(&color_texture);

         D3D11_TEXTURE2D_DESC texture_desc;
         color_texture->GetDesc(&texture_desc);

         com_ptr<ID3D11Resource> depth_resource;
         ps_srvs[1]->GetResource(&depth_resource);

         uint32_t width = texture_desc.Width;
         uint32_t height = texture_desc.Height;

         cb_luma_global_settings.GameSettings.Resolution = {(float)width, (float)height};
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);

         if (width != game_device_data.size.x ||
             height != game_device_data.size.y)
         {
            {
               D3D11_TEXTURE2D_DESC motion_vector_desc;
               motion_vector_desc.Width = width;
               motion_vector_desc.Height = height;
               motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
               motion_vector_desc.ArraySize = 1;
               motion_vector_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
               motion_vector_desc.SampleDesc.Count = 1;
               motion_vector_desc.SampleDesc.Quality = 0;
               motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
               motion_vector_desc.CPUAccessFlags = 0;
               motion_vector_desc.MiscFlags = 0;
               motion_vector_desc.MipLevels = 1;

               native_device->CreateTexture2D(&motion_vector_desc,
                  nullptr,
                  &game_device_data.scaled_motion_vectors);
            }
            {
               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
               uav_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
               uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               uav_desc.Texture2D.MipSlice = 0;

               native_device->CreateUnorderedAccessView(game_device_data.scaled_motion_vectors.get(),
                  &uav_desc,
                  &game_device_data.scaled_motion_vectors_uav);
            }
            {
               D3D11_TEXTURE2D_DESC exposure_desc;
               exposure_desc.Width = 1;
               exposure_desc.Height = 1;
               exposure_desc.Usage = D3D11_USAGE_DEFAULT;
               exposure_desc.ArraySize = 1;
               exposure_desc.Format = DXGI_FORMAT_R16_FLOAT;
               exposure_desc.SampleDesc.Count = 1;
               exposure_desc.SampleDesc.Quality = 0;
               exposure_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
               exposure_desc.CPUAccessFlags = 0;
               exposure_desc.MiscFlags = 0;
               exposure_desc.MipLevels = 1;

               native_device->CreateTexture2D(&exposure_desc,
                  nullptr,
                  &game_device_data.exposure);
            }
            {
               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
               uav_desc.Format = DXGI_FORMAT_R16_FLOAT;
               uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               uav_desc.Texture2D.MipSlice = 0;

               native_device->CreateUnorderedAccessView(game_device_data.exposure.get(),
                  &uav_desc,
                  &game_device_data.exposure_uav);
            }

            {
               D3D11_TEXTURE2D_DESC resolve_desc;
               resolve_desc.Width = width;
               resolve_desc.Height = height;
               resolve_desc.Usage = D3D11_USAGE_DEFAULT;
               resolve_desc.ArraySize = 1;
               resolve_desc.Format = texture_desc.Format;
               resolve_desc.SampleDesc.Count = 1;
               resolve_desc.SampleDesc.Quality = 0;
               resolve_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
               resolve_desc.CPUAccessFlags = 0;
               resolve_desc.MiscFlags = 0;
               resolve_desc.MipLevels = 1;

               native_device->CreateTexture2D(&resolve_desc,
                  nullptr,
                  &game_device_data.resolve_texture);
            }
            {
               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
               srv_desc.Format = texture_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;

               native_device->CreateShaderResourceView(game_device_data.resolve_texture.get(),
                  &srv_desc,
                  &game_device_data.resolve_texture_srv);
            }
            {
               D3D11_TEXTURE2D_DESC merged_desc;
               merged_desc.Width = width;
               merged_desc.Height = height;
               merged_desc.Usage = D3D11_USAGE_DEFAULT;
               merged_desc.ArraySize = 1;
               merged_desc.Format = texture_desc.Format;
               merged_desc.SampleDesc.Count = 1;
               merged_desc.SampleDesc.Quality = 0;
               merged_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
               merged_desc.CPUAccessFlags = 0;
               merged_desc.MiscFlags = 0;
               merged_desc.MipLevels = 1;

               native_device->CreateTexture2D(&merged_desc,
                  nullptr,
                  &game_device_data.merged_texture);
            }
            {
               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
               uav_desc.Format = texture_desc.Format;
               uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               uav_desc.Texture2D.MipSlice = 0;

               native_device->CreateUnorderedAccessView(game_device_data.merged_texture.get(),
                  &uav_desc,
                  &game_device_data.merged_texture_uav);
            }

            game_device_data.size = {width, height};
         }

         {
            ID3D11ShaderResourceView* srvs[] = {ps_srvs[2].get(), vs_srv.get()};
            ID3D11UnorderedAccessView* uavs[] = {game_device_data.scaled_motion_vectors_uav.get(), game_device_data.exposure_uav.get()};
            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Prepare Inputs")].get(), 0, 0);
            native_device_context->CSSetShaderResources(0, 2, srvs);
            native_device_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
            native_device_context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
         }

         auto* sr_instance_data = device_data.GetSRInstanceData();
         {
            SR::SettingsData settings_data;
            settings_data.output_width = width;
            settings_data.output_height = height;
            settings_data.render_width = width;
            settings_data.render_height = height;
            settings_data.dynamic_resolution = false;
            settings_data.hdr = true;
            settings_data.inverted_depth = true;
            settings_data.mvs_jittered = false;
            settings_data.render_preset = dlss_render_preset;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
         }
         {
            SR::SuperResolutionImpl::DrawData draw_data;
            draw_data.source_color = color_resource.get();
            draw_data.output_color = game_device_data.resolve_texture.get();
            draw_data.motion_vectors = game_device_data.scaled_motion_vectors.get();
            draw_data.depth_buffer = depth_resource.get();
            draw_data.exposure = game_device_data.exposure.get();
            draw_data.pre_exposure = 0.0f;
            draw_data.jitter_x = -game_device_data.jitter.x * cb_luma_global_settings.GameSettings.Resolution.x * 0.5f;
            draw_data.jitter_y = game_device_data.jitter.y * cb_luma_global_settings.GameSettings.Resolution.y * 0.5f;
            draw_data.vert_fov = 60.0f * (3.14159265f / 180.0f);
            draw_data.reset = device_data.force_reset_sr;

            sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
            device_data.has_drawn_sr = true;
            device_data.force_reset_sr = false;
            game_device_data.has_drawn_upscaling = true;
         }
         com_ptr<ID3D11RenderTargetView> render_target;
         native_device_context->OMGetRenderTargets(1, &render_target, nullptr);
         com_ptr<ID3D11Resource> render_target_resource;
         render_target->GetResource(&render_target_resource);
         native_device_context->CopyResource(render_target_resource.get(), game_device_data.resolve_texture.get());
         return DrawOrDispatchOverrideType::Replaced;
      }

      return DrawOrDispatchOverrideType::None;
   }

   static void OnMapBufferRegion(
      reshade::api::device* device,
      reshade::api::resource resource,
      uint64_t offset,
      uint64_t size,
      reshade::api::map_access access,
      void** data)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (access != reshade::api::map_access::write_discard)
      {
         return;
      }

      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      D3D11_BUFFER_DESC buffer_desc;
      buffer->GetDesc(&buffer_desc);
      if ((buffer_desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 ||
          buffer_desc.ByteWidth < 1616)
      {
         return;
      }

      mapped_cbuffer_data = *data;
   }

   static void OnUnmapBufferRegion(
      reshade::api::device* device,
      reshade::api::resource resource)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!mapped_cbuffer_data)
      {
         return;
      }

      float* float_data = (float*)mapped_cbuffer_data;

      if (float_data[16] != 0.0f &&
          float_data[17] == 0.0f &&
          float_data[18] == 0.0f &&
          float_data[19] == 0.0f &&
          float_data[20] == 0.0f &&
          float_data[21] != 0.0f &&
          float_data[22] == 0.0f &&
          float_data[23] == 0.0f &&
          float_data[28] == 0.0f &&
          float_data[29] == 0.0f &&
          float_data[31] == 0.0f)
      {
         game_device_data.jitter = {float_data[24], float_data[25]};
      }

      mapped_cbuffer_data = nullptr;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataGreedFall;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("GreedFall Luma mod - DLAA", "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "GreedFall");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      shader_hashes_TAA.pixel_shaders.emplace(std::stoul("E4371F05", nullptr, 16));

      swapchain_upgrade_type = SwapchainUpgradeType::None;
      force_disable_display_composition = true;

      game = new GreedFall();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(GreedFall::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(GreedFall::OnUnmapBufferRegion);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
