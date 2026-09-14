#define THE_EVIL_WITHIN_2 1

#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

struct alignas(16) fblock
{
    float4 renderpositiontoviewtexture;
    float4 lightprepassinverseparams;
    float4 prevviewposition;
    float4 prevviewprojectionmatrixrcx;
    float4 prevviewprojectionmatrixrcy;
    float4 prevviewprojectionmatrixrcw;
    float4 projectionmatrixz;
    float4 blurstep;
    float4 motionblurparms;
    float4 texsize;
    float4 tsaaparm;
};

namespace
{
    const ShaderHashesList shader_hashes_TAA = { .pixel_shaders = { 0xB57DD4D6 }};

    float g_jitter_x;
    float g_jitter_y;
}

struct GameDeviceDataTheEvilWithin2 final : GameDeviceData
{
};

class TheEvilWithin2 final : public Game
{
public:

    static GameDeviceDataTheEvilWithin2& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataTheEvilWithin2*)device_data.game;
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

        native_shaders_definitions.emplace("TEW2 MVs PS"_h, ShaderDefinition{ "Luma_TEW2_MVs_PS", reshade::api::pipeline_subobject_type::pixel_shader });
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataTheEvilWithin2;
    }

    void OnInitSwapchain(reshade::api::swapchain* swapchain) override
    {
        auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);
        auto& managed_resources = game_device_data.managed_resources;

        managed_resources.unordered_access_views["mvs"_h].reset();
        managed_resources.textures_2d["dlss_output"_h].reset();
    }

    static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
    {
        // This should be reliable.
        if (size >= sizeof(fblock)) {
            g_jitter_x = ((fblock*)data)->tsaaparm.y;
            g_jitter_y = ((fblock*)data)->tsaaparm.z;
        }

        return false;
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);
        auto& managed_resources = game_device_data.managed_resources;

        if (original_shader_hashes.Contains(shader_hashes_TAA))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // DLSS requires an immediate context for execution!
                ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

                // Get SRVs.
                std::array<ID3D11ShaderResourceView*, 3> srvs;
                native_device_context->PSGetShaderResources(0, srvs.size(), srvs.data());

                // Get SRV0 resource, depth.
                ComPtr<ID3D11Resource> resource_depth;
                srvs[0]->GetResource(resource_depth.put());

                // Get SRV2 resource, scene.
                ComPtr<ID3D11Resource> resource_scene;
                srvs[2]->GetResource(resource_scene.put());

                // Get RTVs.
                std::array<ID3D11RenderTargetView*, 3> rtvs;
                native_device_context->OMGetRenderTargets(rtvs.size(), rtvs.data(), nullptr);

                // Get RTV1 resource, TAA out.
                ComPtr<ID3D11Resource> resource_rt;
                rtvs[1]->GetResource(resource_rt.put());

                // MVs pass
                //

                // Create RTV.
                [[unlikely]] if (!managed_resources.render_target_views["mvs"_h]) {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, managed_resources.textures_2d["mvs"_h].put()), >= 0);
                    ensure(native_device->CreateRenderTargetView(managed_resources.textures_2d["mvs"_h].get(), nullptr, managed_resources.render_target_views["mvs"_h].put()), >= 0);
                }

                // Bindings.
                const std::array mvs_pass_rtvs = { managed_resources.render_target_views["mvs"_h].get(), rtvs[2] };
                native_device_context->OMSetRenderTargets(mvs_pass_rtvs.size(), mvs_pass_rtvs.data(), nullptr);
                native_device_context->PSSetShader(device_data.native_pixel_shaders.at("TEW2 MVs PS"_h).get(), nullptr, 0);

                (*original_draw_dispatch_func)();

                //

                // Create texture.
                // The original RT resource doesn't have D3D11_BIND_UNORDERED_ACCESS bind flag, needed for DLSS.
                [[unlikely]] if (!managed_resources.textures_2d["dlss_output"_h]) {
                    // Get original RT texture description.
                    ensure(resource_rt->QueryInterface(managed_resources.textures_2d["dlss_output"_h].put()), >= 0);
                    D3D11_TEXTURE2D_DESC tex_desc;
                    managed_resources.textures_2d["dlss_output"_h]->GetDesc(&tex_desc);

                    // Create DLSS output.
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, managed_resources.textures_2d["dlss_output"_h].put()), >= 0);
                }

                // DLSS pass
                //

                auto* sr_instance_data = device_data.GetSRInstanceData();
                ASSERT_ONCE(sr_instance_data);

                SR::SettingsData settings_data;
                settings_data.output_width = device_data.output_resolution.x;
                settings_data.output_height = device_data.output_resolution.y;
                settings_data.render_width = device_data.render_resolution.x;
                settings_data.render_height = device_data.render_resolution.y;
                settings_data.dynamic_resolution = false;
                settings_data.hdr = true;
                settings_data.inverted_depth = false;
                settings_data.mvs_jittered = false;

                // MVs are in UV space so we need to scale them to screen space for DLSS.
                settings_data.mvs_x_scale = device_data.render_resolution.x * -1.0f;
                settings_data.mvs_y_scale = device_data.render_resolution.y * -1.0f;

                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = true;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resource_scene.get();
                draw_data.output_color = managed_resources.textures_2d["dlss_output"_h].get();
                draw_data.motion_vectors = managed_resources.textures_2d["mvs"_h].get();
                draw_data.depth_buffer = resource_depth.get();

                // Jitters.
                draw_data.jitter_x = g_jitter_x * -1.0f;
                draw_data.jitter_y = g_jitter_y * 1.0f;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                //

                // Copy DLSS output to the original output.
                native_device_context->CopyResource(resource_rt.get(), managed_resources.textures_2d["dlss_output"_h].get());

                ResetCOMArray(rtvs);
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
        ImGui::Text("The Evil Within 2 Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "The Evil Within 2 Luma mod");
        Globals::VERSION = 1;

        swapchain_format_upgrade_type  = TextureFormatUpgradesType::None; // FIXME: AllowedEnabled causes black screen and other artifacts if ReShade overlay is on.
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

              reshade::api::format::r11g11b10_float,
        };
        // ### Check these if textures are not upgraded ###
        texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

        enable_samplers_upgrade = true;
        
        // TODO: Remove this later!
        Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

        #if DEVELOPMENT
        forced_shader_names.emplace(0xB57DD4D6, "TAA");
        #endif

        game = new TheEvilWithin2();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}