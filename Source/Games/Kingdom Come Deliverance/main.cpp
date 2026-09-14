
#define GAME_KINGDOM_COME_DELIVERANCE 1

#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

namespace
{
    const ShaderHashesList shader_hashes_TAA = { .pixel_shaders = { 0x505343B8 } };
    const ShaderHashesList shader_hashes_Lightning = { .compute_shaders = { 0x0181192D } };
    float g_jitter_x;
    float g_jitter_y;
    bool g_has_drawn_lightning;

    bool is_jitter(float x)
    {
        // Base jitters (x, y) in range [-0.5, 0.5], for "r_AntialiasingTAAPattern 4" (the game setting).
        // ( 0.0625, -0.1875), (-0.0625,  0.1875),
        // ( 0.3125,  0.0625), (-0.1875, -0.3125),
        // (-0.3125,  0.3125), (-0.4375, -0.0625),
        // ( 0.1875,  0.4375), ( 0.4375, -0.4375)
        //
        // We only need to check for x jitters in range [-1, 1], y jitters will match.
        static constexpr std::array x_jitters = { 0.0625f * 2.0f, 0.3125f * 2.0f, -0.3125f * 2.0f, 0.1875f * 2.0f, -0.0625f * 2.0f, -0.1875f * 2.0f, -0.4375f * 2.0f, 0.4375f * 2.0f }; 
        
        constexpr float eps = 1e-5f;
        for (int i = 0; i < x_jitters.size(); ++i)
        {
            if (std::abs(x - x_jitters[i]) < eps)
            {
                return true;
            }
        }
        return false;
    }
}

struct GameDeviceDataKingdomComeDeliverance final : GameDeviceData
{
    ComPtr<ID3D11Texture2D> tex_dlss_output;
    ComPtr<ID3D11Texture2D> tex_mvs;
    ComPtr<ID3D11RenderTargetView> rtv_mvs;
    std::unordered_map<uintptr_t, void*> mapped_cbs;
};

class KingdomComeDeliverance final : public Game
{
public:
   
    static GameDeviceDataKingdomComeDeliverance& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataKingdomComeDeliverance*)device_data.game;
    }

    void OnLoad(std::filesystem::path& file_path, bool failed) override
    {
       if (!failed)
       {
          reshade::register_event<reshade::addon_event::map_buffer_region>(KingdomComeDeliverance::OnMapBufferRegion);
          reshade::register_event<reshade::addon_event::unmap_buffer_region>(KingdomComeDeliverance::OnUnmapBufferRegion);
       }
    }

    void OnInit(bool async) override
    {
        // ### Update these (find the right values) ###
        // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
        luma_settings_cbuffer_index = 13;
        luma_data_cbuffer_index = -1;
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataKingdomComeDeliverance;
    }

    static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
    {
        auto& device_data = *device->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);

        auto buffer = (ID3D11Buffer*)resource.handle;
        D3D11_BUFFER_DESC desc;
        buffer->GetDesc(&desc);

        if ((desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && desc.ByteWidth >= 256)
        {
           game_device_data.mapped_cbs[resource.handle] = *data;
        }
    }

    static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
    {
        auto& device_data = *device->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);

        if (!g_has_drawn_lightning && game_device_data.mapped_cbs.contains(resource.handle))
        {
           auto data = (float4*)game_device_data.mapped_cbs[resource.handle];

           // The CB we are after, from Lightning_0x0181192D_CS.
           //
           // cbuffer PER_BATCH : register(b0)
           // {
           //   float4 GiSettings : packoffset(c0);
           //   float4 TPLParams : packoffset(c1);
           //   float4 FrustumTL : packoffset(c2);
           //   float4 FrustumBL : packoffset(c3);
           //   float4 WorldViewPos : packoffset(c4);
           //   float4 PS_NearFarClipDist : packoffset(c5);
           //   float4 ProjParams : packoffset(c6); // ProjMatrix._m00, ProjMatrix._m11, ProjMatrix._m20, ProjMatrix._m21
           //   float4 ForwGiIntegrationMode : packoffset(c7);
           //   float4 SunDir : packoffset(c8);
           //   float4 PS_ScreenSize : packoffset(c9);
           //   float4 SSDOParams : packoffset(c10);
           //   float4 FrustumTR : packoffset(c11);
           //   float4 ScreenSize : packoffset(c12); // w, h, 1/w, 1/h
           //   float4 g_vVisAreasParams[64] : packoffset(c13);
           // }
           //

           if (is_jitter(data[6].z * device_data.render_resolution.x))
           {
              g_jitter_x = data[6].z;
              g_jitter_y = data[6].w;
           }
           game_device_data.mapped_cbs.erase(resource.handle);
        }
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        if (original_shader_hashes.Contains(shader_hashes_Lightning))
        {
            // TODO: Remove this after we are sure that we can reliably catch this CB on Map/Unmap.
            #if 0
            if (device_data.sr_type != SR::Type::None)
            {
                // Get CB0 and its description.
                ComPtr<ID3D11Buffer> cb;
                native_device_context->CSGetConstantBuffers(0, 1, cb.put());
                D3D11_BUFFER_DESC desc;
                cb->GetDesc(&desc);

                // Create staging buffer.
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                ComPtr<ID3D11Buffer> buffer_staging;
                native_device->CreateBuffer(&desc, nullptr, buffer_staging.put());

                native_device_context->CopyResource(buffer_staging.get(), cb.get());

                // Map for CPU read.
                D3D11_MAPPED_SUBRESOURCE mapped;
                ensure(native_device_context->Map(buffer_staging.get(), 0, D3D11_MAP_READ, 0, &mapped), >= 0);

                // cbuffer PER_BATCH : register(b0)
                // {
                //   float4 GiSettings : packoffset(c0);
                //   float4 TPLParams : packoffset(c1);
                //   float4 FrustumTL : packoffset(c2);
                //   float4 FrustumBL : packoffset(c3);
                //   float4 WorldViewPos : packoffset(c4);
                //   float4 PS_NearFarClipDist : packoffset(c5);
                //   float4 ProjParams : packoffset(c6); // ProjMatrix._m00, ProjMatrix._m11, ProjMatrix._m20, ProjMatrix._m21
                //   float4 ForwGiIntegrationMode : packoffset(c7);
                //   float4 SunDir : packoffset(c8);
                //   float4 PS_ScreenSize : packoffset(c9);
                //   float4 SSDOParams : packoffset(c10);
                //   float4 FrustumTR : packoffset(c11);
                //   float4 ScreenSize : packoffset(c12); // w, h, 1/w, 1/h
                //   float4 g_vVisAreasParams[64] : packoffset(c13);
                // }
                auto data = (float4*)mapped.pData;
                g_jitter_x = data[6].z;
                g_jitter_y = data[6].w;

                native_device_context->Unmap(buffer_staging.get(), 0);
            }
            #endif

            g_has_drawn_lightning = true;
            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_TAA))
        {
            if (device_data.sr_type != SR::Type::None)
            {   
                // Get RTV and it's resource.
                ComPtr<ID3D11RenderTargetView> rtv;
                native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
                ComPtr<ID3D11Resource> resource_output;
                rtv->GetResource(resource_output.put());

                // MVs pass
                //

                // Create MVs texture and RTV.
                [[unlikely]] if (!game_device_data.rtv_mvs)
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, game_device_data.tex_mvs.put()), >= 0);
                    ensure(native_device->CreateRenderTargetView(game_device_data.tex_mvs.get(), nullptr, game_device_data.rtv_mvs.put()), >= 0);
                }

                // Bindings.
                native_device_context->OMSetRenderTargets(1, &game_device_data.rtv_mvs, nullptr);

                (*original_draw_dispatch_func)();

                //

                // DLSS pass
                //

                // DLSS requires an immediate context for execution!
                ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

                auto* sr_instance_data = device_data.GetSRInstanceData();
                ASSERT_ONCE(sr_instance_data);

                SR::SettingsData settings_data;
                settings_data.output_width = device_data.output_resolution.x;
                settings_data.output_height = device_data.output_resolution.y;
                settings_data.render_width = device_data.render_resolution.x;
                settings_data.render_height = device_data.render_resolution.y;
                settings_data.dynamic_resolution = false;
                settings_data.hdr = false;
                settings_data.inverted_depth = true;
                settings_data.mvs_jittered = false;

                // MVs are in UV space so we need to scale them to screen space for DLSS.
                settings_data.mvs_x_scale = device_data.render_resolution.x;
                settings_data.mvs_y_scale = device_data.render_resolution.y;

                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = false;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                // Get SRVs and their resources.
                ComPtr<ID3D11ShaderResourceView> srv_scene;
                native_device_context->PSGetShaderResources(4, 1, srv_scene.put());
                ComPtr<ID3D11Resource> resource_scene;
                srv_scene->GetResource(resource_scene.put());
                ComPtr<ID3D11ShaderResourceView> srv_depth;
                native_device_context->PSGetShaderResources(16, 1, srv_depth.put());
                ComPtr<ID3D11Resource> resource_depth;
                srv_depth->GetResource(resource_depth.put());

                // Create the output resource for DLSS.
                [[unlikely]] if (!game_device_data.tex_dlss_output)
                {
                    ensure(resource_output->QueryInterface(game_device_data.tex_dlss_output.put()), >= 0);
                    D3D11_TEXTURE2D_DESC tex_desc;
                    game_device_data.tex_dlss_output->GetDesc(&tex_desc);
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, game_device_data.tex_dlss_output.put()), >= 0);
                }

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resource_scene.get();
                draw_data.output_color = game_device_data.tex_dlss_output.get();
                draw_data.motion_vectors = game_device_data.tex_mvs.get();
                draw_data.depth_buffer = resource_depth.get();

                // Jitters are in UV offsets so we need to scale them to pixel offsets for DLSS.
                draw_data.jitter_x = g_jitter_x * device_data.render_resolution.x * -0.5;
                draw_data.jitter_y = g_jitter_y * device_data.render_resolution.y * 0.5;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                // Copy DLSS output to the original TAA's current frame and the backbuffer.
                native_device_context->CopyResource(resource_output.get(), game_device_data.tex_dlss_output.get());

                //

                return DrawOrDispatchOverrideType::Replaced;
            }
            return DrawOrDispatchOverrideType::None;
        }

        return DrawOrDispatchOverrideType::None;
    }

    void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        g_has_drawn_lightning = false;

        if (!custom_texture_mip_lod_bias_offset)
        {
            std::shared_lock shared_lock_samplers(s_mutex_samplers);
            if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
            {
               device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
            }
            else
            {
               device_data.texture_mip_lod_bias_offset = 0.0f;
            }
        }
    }

    void PrintImGuiAbout() override
    {
        ImGui::Text("Kingdom Come Deliverance Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Kingdom Come Deliverance Luma mod");
        Globals::VERSION = 1;

        swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
        swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
        texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
        // ### Check which of these are needed and remove the rest ###
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

            reshade::api::format::r11g11b10_float,
        };
        // ### Check these if textures are not upgraded ###
        texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

        enable_samplers_upgrade = true;
        
        // TODO: Remove this later!
        Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

        game = new KingdomComeDeliverance();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}