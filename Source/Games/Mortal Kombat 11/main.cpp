
#define GAME_MORTAL_KOMBAT_11 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

struct alignas(16) ViewConstants
{
    float4x4 _PRIVATE_ViewProjectionMatrix;
    float4x4 _PRIVATE_InvViewProjectionMatrix;
    float4x4 _PRIVATE_PreviousViewProjectionMatrix;
    float4x4 _PRIVATE_ClipToPrevClipMatrix;
    float4x4 _PRIVATE_ProjectionMatrix;
    float4x4 _PRIVATE_WorldToViewMatrix;
    float4x4 _PRIVATE_ViewToWorldMatrix;
    float4x4 _PRIVATE_InvProjectionMatrix;
    float4x4 _PRIVATE_PrevProjectionMatrix;
    float4x4 _PRIVATE_SceneToWorldMatrix;
    float4x4 _PRIVATE_ScreenToWorldMatrix;
    float4 _PRIVATE_ViewOrigin;
    float4 _PRIVATE_ScreenPositionScaleBias;
    float4 _PRIVATE_InvScreenPositionScaleBias;
    float4 _PRIVATE_TilePositionScaleBias;
    float4 _PRIVATE_WindowDimensions;
    float4 _PRIVATE_ScreenUVMinMax;
    float4 _PRIVATE_MinZ_MaxZRatio;
    float4 _PRIVATE_InvFocalLength;
    float4 _PRIVATE_DebugDirectIndirectEmissiveOverrides;
    float4 _PRIVATE_DebugDiffuseSpecularOverrides;
    float4 _PRIVATE_ExposureScales;
    
    struct
    {
      float4 Color;
      float4 DistanceDensity;
    } _PRIVATE_FogBandData[8];
    
    float3 _PRIVATE_ViewDirection;
    float _PRIVATE_AdaptiveTessellationFactor;
    uint _PRIVATE_ActiveDitherFrame;
    uint _PRIVATE_bUseHigherQualityGBufferEncoding;
    float _PRIVATE_ProjectionScaleX;
    float _PRIVATE_ProjectionScaleY;
    float3 _PRIVATE_VolumetricFogRange;
    /* bool */ uint _PRIVATE_VolumetricFogEnabled;
    float2 _PRIVATE_LevelDesatAndFadeControls;
    float _PRIVATE_AmbientIntensity;
    float _PRIVATE_AmbientAlpha;
    /* bool */ uint _PRIVATE_bDynamicPlanarReflectionsEnabled;
    /* bool */ uint _PRIVATE_bDynamicScreenSpaceReflectionsEnabled;
    int _PRIVATE_TotalEnvironmentMapVolumeCount;
    float _PRIVATE_SpecularMipFactor;
    float2 _PRIVATE_DOFFocusRange;
    float _PRIVATE_EnvironmentIBLContributionIntensity;
    uint _PRIVATE_dummy1;
    float _PRIVATE_DynamicResolutionScaleRatio;
    float _PRIVATE_InvDynamicResolutionScaleRatio;
    uint _PRIVATE_EnableTemporalDithering;
    uint _PRIVATE_const0_1;
    uint _PRIVATE_const0_2;
    uint _PRIVATE_const0_3;
    uint _PRIVATE_const0_4;
    uint _PRIVATE_const1_1;
    uint _PRIVATE_const1_2;
    uint _PRIVATE_const1_3;
    uint _PRIVATE_const1_4;
};

namespace
{
    const ShaderHashesList shader_hashes_TAA = { .compute_shaders = { 0xF529F5BE }};
    const ShaderHashesList shader_hashes_PostTAASharpen = { .compute_shaders = { 0xABAF5929 }};

    float g_jitter_x;
    float g_jitter_y;
}

struct GameDeviceDataMortalKombat11 final : GameDeviceData
{
};

class MortalKombat11 final : public Game
{
public:

    static GameDeviceDataMortalKombat11& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataMortalKombat11*)device_data.game;
    }

    void OnLoad(std::filesystem::path& file_path, bool failed) override
    {
        if (!failed)
        {
            reshade::register_event<reshade::addon_event::create_resource>(MortalKombat11::OnCreateResource);
        }
    }

    void OnInit(bool async) override
    {
       // ### Update these (find the right values) ###
       // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
       luma_settings_cbuffer_index = 13;
       luma_data_cbuffer_index = -1;
    }

    static bool OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state)
    {
        // ViewConstants CB is immutable so we have to catch it on creation.
        // The game will be recreating it at least onece per frame.
        if ((desc.usage & reshade::api::resource_usage::constant_buffer) != 0 && (desc.flags & reshade::api::resource_flags::immutable) != 0 && desc.buffer.size == 1280) {
            auto data = (ViewConstants*)initial_data->data;

            // This should be reliable.
            if (data->_PRIVATE_ProjectionMatrix.m00 && !data->_PRIVATE_ProjectionMatrix.m01 && !data->_PRIVATE_ProjectionMatrix.m02 && !data->_PRIVATE_ProjectionMatrix.m03 && !data->_PRIVATE_ProjectionMatrix.m10 && data->_PRIVATE_ProjectionMatrix.m11 && data->_PRIVATE_ProjectionMatrix.m23 == 1.0f) {
                g_jitter_x = data->_PRIVATE_ProjectionMatrix.m20;
                g_jitter_y = data->_PRIVATE_ProjectionMatrix.m21;
            }
        }

        return false;
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

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
                // Also for DLSS we need to flip the sign for both x and y.
                settings_data.mvs_x_scale = -device_data.render_resolution.x;
                settings_data.mvs_y_scale = -device_data.render_resolution.y;
                
                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = true;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                // Get depth resource.
                ComPtr<ID3D11ShaderResourceView> srv_stencil;
                native_device_context->CSGetShaderResources(0, 1, srv_stencil.put());
                ComPtr<ID3D11Resource> resource_depth;
                srv_stencil->GetResource(resource_depth.put());

                // Get scene resource.
                ComPtr<ID3D11ShaderResourceView> srv_scene;
                native_device_context->CSGetShaderResources(1, 1, srv_scene.put());
                ComPtr<ID3D11Resource> resource_scene;
                srv_scene->GetResource(resource_scene.put());

                // Get MVs resource.
                ComPtr<ID3D11ShaderResourceView> srv_mvs;
                native_device_context->CSGetShaderResources(3, 1, srv_mvs.put());
                ComPtr<ID3D11Resource> resource_mvs;
                srv_mvs->GetResource(resource_mvs.put());

                // Get TAA resource.
                ComPtr<ID3D11Resource> resource_taa;
                ComPtr<ID3D11UnorderedAccessView> uav_taa;
                native_device_context->CSGetUnorderedAccessViews(1, 1, uav_taa.put());
                uav_taa->GetResource(resource_taa.put());

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resource_scene.get();
                draw_data.output_color = resource_taa.get();
                draw_data.motion_vectors = resource_mvs.get();
                draw_data.depth_buffer = resource_depth.get();

                // We need to scale jitters to pixel offsets for DLSS.
                // This should be correct. It's hard to tell cause all jitter offset configurations look ok.
                // TODO: The game uses only 4 jitters, upgrade to Halton?
                draw_data.jitter_x = g_jitter_x * device_data.render_resolution.x * -1.0;
                draw_data.jitter_y = g_jitter_y * device_data.render_resolution.y * 1.0;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                return DrawOrDispatchOverrideType::Replaced;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_PostTAASharpen))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // Get SRV resource.
                ComPtr<ID3D11ShaderResourceView> srv;
                native_device_context->CSGetShaderResources(0, 1, srv.put());
                ComPtr<ID3D11Resource> resource_srv;
                srv->GetResource(resource_srv.put());

                // Get UAV resource.
                ComPtr<ID3D11UnorderedAccessView> uav;
                native_device_context->CSGetUnorderedAccessViews(0, 1, uav.put());
                ComPtr<ID3D11Resource> resource_uav;
                uav->GetResource(resource_uav.put());

                // Just copy the scene and skip the original draw.
                native_device_context->CopyResource(resource_uav.get(), resource_srv.get());
                return DrawOrDispatchOverrideType::Skip;
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
        ImGui::Text("Mortal Kombat 11 - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Mortal Kombat 11");
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

        #if DEVELOPMENT
        forced_shader_names.emplace(0xF529F5BE, "TAA");
        forced_shader_names.emplace(0xABAF5929, "PostTAASharpen");
        #endif

        game = new MortalKombat11();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}