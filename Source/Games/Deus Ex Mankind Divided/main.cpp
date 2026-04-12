#define GAME_DEUS_EX_MANKIND_DIVIDED 1

#define CHECK_GRAPHICS_API_COMPATIBILITY 1

// Any message box breaks the input of the game forever
#define DISABLE_AUTO_DEBUGGER 1

#define DISABLE_FOCUS_LOSS_SUPPRESSION 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

struct alignas(16) cbSharedPerViewData
{
    float4x4    mProjection;
    float4x4    mProjectionInverse;
    float4x4    mViewToViewport;
    float4x4    mWorldToView;
    float4x4    mViewToWorld;
    float4    vViewRemap;
    float4    vViewDepthRemap;
    float4    vEyeVectorUL;
    float4    vEyeVectorLR;
    float4    vViewSpaceUpVector;
    float4    vCheckerModeParams;
    float4    vViewportSize;
    float4    vEngineTime;
    float4    vPrecipitations;
    float4    vClipPlane;
    float4    vExtraParams;
    float4    vScatteringParams;
    float4    vStereoscopic3DCorrectionParams;
    float4    vCHSParams;
};

namespace
{
    const ShaderHashesList shader_hashes_TAA = { .compute_shaders = { 0x84EF14ED } };
    const ShaderHashesList shader_hashes_LastKnownLocation { .pixel_shaders = { 0xA6A0E453, 0xE30A40E6 } };

    void* g_cbSharedPerViewData_mapped_data;
    uintptr_t g_cbSharedPerViewData_handle;
    cbSharedPerViewData g_cbSharedPerViewData_data;

    bool g_disable_last_known_location;
}

struct GameDeviceDataDeusExMankindDivided final : GameDeviceData
{
};

class GameDeusExMankindDivided final : public Game
{
public:

    static GameDeviceDataDeusExMankindDivided& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataDeusExMankindDivided*)device_data.game;
    }

    void OnLoad(std::filesystem::path& file_path, bool failed) override
    {
        if (!failed)
        {
            reshade::register_event<reshade::addon_event::map_buffer_region>(GameDeusExMankindDivided::OnMapBufferRegion);
            reshade::register_event<reshade::addon_event::unmap_buffer_region>(GameDeusExMankindDivided::OnUnmapBufferRegion);
        }
    }

    void OnInit(bool async) override
    {
        // ### Update these (find the right values) ###
        // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
        // FIXME: The game is using all CB slots!
        luma_settings_cbuffer_index = 8;
        luma_data_cbuffer_index = -1;

        native_shaders_definitions.emplace(CompileTimeStringHash("DeusExMD Post DLSS CS"), ShaderDefinition("Luma_DeusExMD_Post_DLSS_CS", reshade::api::pipeline_subobject_type::compute_shader));
    }

    void LoadConfigs() override
    {
        reshade::get_config_value(nullptr, NAME, "DisableLastKnownLocation", g_disable_last_known_location);
    }

    void DrawImGuiSettings(DeviceData& device_data) override
    {
        ImGui::NewLine();

        if (ImGui::Checkbox("Disable last known location", &g_disable_last_known_location))
        {
            reshade::set_config_value(nullptr, NAME, "DisableLastKnownLocation", g_disable_last_known_location);
        }
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataDeusExMankindDivided;
    }

    static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
    {
        auto buffer = (ID3D11Buffer*)resource.handle;
        D3D11_BUFFER_DESC desc;
        buffer->GetDesc(&desc);
        if (desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && desc.ByteWidth == 544)
        {
            g_cbSharedPerViewData_handle = resource.handle;
            g_cbSharedPerViewData_mapped_data = *data;
        }
    }

    static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
    {
        // Not thread safe, but it looks like it's fine.
        if (g_cbSharedPerViewData_handle == resource.handle)
        {
            auto data = (float*)g_cbSharedPerViewData_mapped_data;

            // This should be reliable.
            if (data[8] && data[9] && data[8] == data[129])
            {
                std::memcpy(&g_cbSharedPerViewData_data, data, sizeof(g_cbSharedPerViewData_data));
                g_cbSharedPerViewData_handle = 0;
            }
        }
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        if (original_shader_hashes.Contains(shader_hashes_LastKnownLocation))
        {
            if (g_disable_last_known_location)
            {
                return DrawOrDispatchOverrideType::Replaced;
            }
            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_TAA))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // DLSS pass
                //

                // DLSS requires an immediate context for execution!
                ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

                auto* sr_instance_data = device_data.GetSRInstanceData();
                ASSERT_ONCE(sr_instance_data);

                // We don't need to set/call this every frame?
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
                // Also for DLSS we need to flip the sign for both x and y.
                settings_data.mvs_x_scale = -device_data.render_resolution.x;
                settings_data.mvs_y_scale = -device_data.render_resolution.y;
                
                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = true;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                // The game renders all in sRGB (or mixed), should we linearize for DLSS?

                // Get SRVs and their resources.
                std::array<ID3D11ShaderResourceView*, 3> srvs = {};
                native_device_context->CSGetShaderResources(0, srvs.size(), srvs.data());
                ComPtr<ID3D11Resource> resource_depth;
                srvs[0]->GetResource(resource_depth.put());
                ComPtr<ID3D11Resource> resource_scene;
                srvs[1]->GetResource(resource_scene.put());
                ComPtr<ID3D11Resource> resource_mvs;
                srvs[2]->GetResource(resource_mvs.put());

                // Get UAV and it's resource.
                ComPtr<ID3D11UnorderedAccessView> uav;
                native_device_context->CSGetUnorderedAccessViews(0, 1, uav.put());
                ComPtr<ID3D11Resource> resource_output;
                uav->GetResource(resource_output.put());

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resource_scene.get();
                draw_data.output_color = resource_output.get();
                draw_data.motion_vectors = resource_mvs.get();
                draw_data.depth_buffer = resource_depth.get();

                // Jitters are in UV offsets so we need to scale them to pixel offsets for DLSS.
                draw_data.jitter_x = g_cbSharedPerViewData_data.mProjection.m20 * device_data.render_resolution.x * -0.5f;
                draw_data.jitter_y = g_cbSharedPerViewData_data.mProjection.m21 * device_data.render_resolution.y * 0.5f;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;
                draw_data.vert_fov = std::atan(1.0f / g_cbSharedPerViewData_data.mProjection.m11) * 2.0f;
                draw_data.near_plane = g_cbSharedPerViewData_data.mProjection.m32 / (1.0f - g_cbSharedPerViewData_data.mProjection.m22);
                draw_data.far_plane = g_cbSharedPerViewData_data.mProjection.m32 / g_cbSharedPerViewData_data.mProjection.m22;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                //

                // PostDLSS pass
                //

                // Bindings.
                native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
                native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("DeusExMD Post DLSS CS")].get(), nullptr, 0);

                native_device_context->Dispatch((device_data.output_resolution.x + 8 - 1) / 8, (device_data.output_resolution.x + 8 - 1) / 8, 1);

                //

                return DrawOrDispatchOverrideType::Replaced;
            }

            return DrawOrDispatchOverrideType::None;
        }

        return DrawOrDispatchOverrideType::None;
    }

    void PrintImGuiAbout() override
    {
        ImGui::Text("Deus Ex: Mankind Divided Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Deus Ex: Mankind Divided Luma mod");
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

        // TODO: Remove this later!
        Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

        #if DEVELOPMENT
        forced_shader_names.emplace(0xDA65F8ED, "Sharpen");
        forced_shader_names.emplace(0x84EF14ED, "TAA");
        forced_shader_names.emplace(0xA6A0E453, "LastKnownLocation");
        forced_shader_names.emplace(0xE30A40E6, "LastKnownLocation");
        #endif

        game = new GameDeusExMankindDivided();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}