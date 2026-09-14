#define MIDDLE_EARTH_SHADOW_OF_WAR 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

namespace
{
    const ShaderHashesList shader_hashes_linearize_depth_and_generate_mvs = { .compute_shaders = { 0x0E095BB1 }};
    const ShaderHashesList shader_hashes_TAA = { .pixel_shaders = { 0x8D06D556 }};

    float g_jitter_x;
    float g_jitter_y;
}

struct GameDeviceDataMiddleEarthShadowOfWar final : GameDeviceData
{
};

class MiddleEarthShadowOfWar final : public Game
{
public:

    static GameDeviceDataMiddleEarthShadowOfWar& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataMiddleEarthShadowOfWar*)device_data.game;
    }

    void OnLoad(std::filesystem::path& file_path, bool failed = false) override
    {
        reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
    }

    void OnInit(bool async) override
    {
        // ### Update these (find the right values) ###
        // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
        luma_settings_cbuffer_index = 13;
        luma_data_cbuffer_index = 12;
    }

    static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
    {
        auto native_resource = (ID3D11Resource*)resource.handle;
        ComPtr<ID3D11Buffer> buffer;
        auto hr = native_resource->QueryInterface(buffer.put());
        if (SUCCEEDED(hr)) {
            D3D11_BUFFER_DESC desc;
            buffer->GetDesc(&desc);

            // This alone should be reliable? Needs testing!
            if (desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && desc.ByteWidth == 544) {
                // cb0[31].xy in PS TAA 0x8D06D556.
                g_jitter_x = ((float4*)data)[31].x;
                g_jitter_y = ((float4*)data)[31].y;
            }
        }
        return false;
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);
        auto& managed_resources = game_device_data.managed_resources;

        if (original_shader_hashes.Contains(shader_hashes_linearize_depth_and_generate_mvs))
        {
            ComPtr<ID3D11ShaderResourceView> srv;
            native_device_context->CSGetShaderResources(1, 1, srv.put());
            srv->GetResource(managed_resources.resources["depth"_h].put());
            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_TAA))
        {
            if (device_data.sr_type != SR::Type::None)
            {
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
                settings_data.hdr = true;
                settings_data.inverted_depth = true;
                settings_data.mvs_jittered = false;

                // MVs are in UV space so we need to scale them to screen space for DLSS.
                settings_data.mvs_x_scale = -device_data.render_resolution.x;
                settings_data.mvs_y_scale = -device_data.render_resolution.y;

                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = true;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                std::array<ID3D11ShaderResourceView*, 2> srvs;
                native_device_context->PSGetShaderResources(0, srvs.size(), srvs.data());

                ComPtr<ID3D11Resource> resource_mvs;
                srvs[0]->GetResource(resource_mvs.put());
                ComPtr<ID3D11Resource> resource_scene;
                srvs[1]->GetResource(resource_scene.put());

                ComPtr<ID3D11RenderTargetView> rtv;
                native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
                ComPtr<ID3D11Resource> resource_rt;
                rtv->GetResource(resource_rt.put());

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resource_scene.get();
                draw_data.output_color = resource_rt.get();
                draw_data.motion_vectors = resource_mvs.get();
                draw_data.depth_buffer = managed_resources.resources["depth"_h].get();

                // Jitters are in range [-1, 1].
                draw_data.jitter_x = g_jitter_x * -0.5f;
                draw_data.jitter_y = g_jitter_y * 0.5f;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                ResetCOMArray(srvs);

                return DrawOrDispatchOverrideType::Replaced;
            }
            return DrawOrDispatchOverrideType::None;
        }

        return DrawOrDispatchOverrideType::None;
    }

    void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

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
        ImGui::Text("Middle-earth: Shadow of War Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Middle-earth: Shadow of War Luma mod");
        Globals::VERSION = 1;

        swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
        swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
        texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
        // ### Check which of these are needed and remove the rest ###
        texture_upgrade_formats = {
            //reshade::api::format::r8g8b8a8_unorm,
            //reshade::api::format::r8g8b8a8_unorm_srgb,
            //reshade::api::format::r8g8b8a8_typeless,
            //reshade::api::format::r8g8b8x8_unorm,
            //reshade::api::format::r8g8b8x8_unorm_srgb,
            //reshade::api::format::b8g8r8a8_unorm,
            //reshade::api::format::b8g8r8a8_unorm_srgb,
            //reshade::api::format::b8g8r8a8_typeless,
            //reshade::api::format::b8g8r8x8_unorm,
            //reshade::api::format::b8g8r8x8_unorm_srgb,
            //reshade::api::format::b8g8r8x8_typeless,
            //
            //reshade::api::format::r11g11b10_float,
        };
        // ### Check these if textures are not upgraded ###
        texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

        enable_samplers_upgrade = true;
        
        // TODO: Remove this later!
        Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

        game = new MiddleEarthShadowOfWar();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}