#define GAME_FALLOUT4 1

#define ENABLE_NGX 1

#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

// TODO: Switch to Luma CBs.
struct alignas(16) F4_luma_cb_data
{
    float2 inv_renderer_resolution;
    float aspect_ratio;
    uint frame_index;
};

namespace
{
    const ShaderHashesList shader_hashes_TAA = { .pixel_shaders = { 0x61CC29E6 }};
    const ShaderHashesList shader_hashes_LinearizeAndDownsampleDepth = { .compute_shaders = { 0x1D1E3148 }};

    const ShaderHashesList shader_hashes_SSAOMain = { .compute_shaders = { 0x0307C239 }};
    const ShaderHashesList shader_hashes_SSAODenoiseX = { .compute_shaders = { 0xE151AD86 }};
    const ShaderHashesList shader_hashes_SSAODenoiseY = { .compute_shaders = { 0x7E8F370A }};

    // We only need jitters from it. They should be in [4] and [5].
    // 8 long Halton(2,3) sequence.
    void* g_cb_taa_mapped_data;
    float g_cb_taa_data[24];

    // XeGTAO
    constexpr size_t XE_GTAO_DEPTH_MIP_LEVELS = 5;
    constexpr UINT XE_GTAO_NUMTHREADS_X = 8;
    constexpr UINT XE_GTAO_NUMTHREADS_Y = 8;
    bool g_xegtao_enable = true;
    bool has_drawn_ssao;

    F4_luma_cb_data g_f4_luma_cb_data;

    void create_constant_buffer(ID3D11Device* device, UINT size, ID3D11Buffer** cb)
    {
	    D3D11_BUFFER_DESC desc = {};
	    desc.ByteWidth = size;
	    desc.Usage = D3D11_USAGE_DYNAMIC;
	    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	    ensure(device->CreateBuffer(&desc, nullptr, cb), >= 0);
    }

    void update_constant_buffer(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const void* data, size_t size)
    {
	    D3D11_MAPPED_SUBRESOURCE mapped_subresource;
	    ensure(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource), >= 0);
	    std::memcpy(mapped_subresource.pData, data, size);
	    ctx->Unmap(cb, 0);
    }
}

struct GameDeviceDataFallout4 final : GameDeviceData
{
    ComPtr<ID3D11Texture2D> tex_dlss_output;
    ComPtr<ID3D11Buffer> cb_taa;

    // XeGTAO
    ComPtr<ID3D11Resource> resource_ssao;
    std::array<ID3D11UnorderedAccessView*, XE_GTAO_DEPTH_MIP_LEVELS> uav_xe_gtao_prefilter_depths16x16 = {}; // FREE ME!
    ComPtr<ID3D11ShaderResourceView> srv_xe_gtao_prefilter_depths16x16;
    ComPtr<ID3D11UnorderedAccessView> uav_xe_gtao_main;
    ComPtr<ID3D11ShaderResourceView> srv_xe_gtao_main;
    ComPtr<ID3D11UnorderedAccessView> uav_xe_gtao_denoise_pass1;
    ComPtr<ID3D11ShaderResourceView> srv_xe_gtao_denoise_pass1;
    ComPtr<ID3D11UnorderedAccessView> uav_xe_gtao_denoise_pass2;
    ComPtr<ID3D11ShaderResourceView> srv_xe_gtao_denoise_pass2;

    ComPtr<ID3D11Buffer> f4_luma_cb;
};

class GameFallout4 final : public Game
{
public:
   
    static GameDeviceDataFallout4& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataFallout4*)device_data.game;
    }

    void LoadConfigs() override
    {
        reshade::get_config_value(nullptr, NAME, "XeGTAOEnable", g_xegtao_enable);
    }

    void DrawImGuiSettings(DeviceData& device_data) override
    {
        ImGui::NewLine();

        if (ImGui::Checkbox("Enable XeGTAO", &g_xegtao_enable))
        {
            reshade::set_config_value(nullptr, NAME, "XeGTAOEnable", g_xegtao_enable);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("Replaces SSAO. SSAO has to be enabled in game's settings for it (XeGTAO) to work.");
        }
    }

    void OnLoad(std::filesystem::path& file_path, bool failed) override
    {
        if (!failed)
        {
            reshade::register_event<reshade::addon_event::map_buffer_region>(GameFallout4::OnMapBufferRegion);
            reshade::register_event<reshade::addon_event::unmap_buffer_region>(GameFallout4::OnUnmapBufferRegion);
        }
    }

    void OnInit(bool async) override
    {
        luma_settings_cbuffer_index = 8;
        luma_data_cbuffer_index = 7;

        std::vector<ShaderDefineData> game_shader_defines_data = {
            {"XE_GTAO_QUALITY", '2', true, false, "0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4}
        };
        shader_defines_data.append_range(game_shader_defines_data);

        // XeGTAO
        native_shaders_definitions.emplace(CompileTimeStringHash("F4 XeGTAO Prefilter Depths CS"), ShaderDefinition{ "Luma_F4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs" });
        native_shaders_definitions.emplace(CompileTimeStringHash("F4 XeGTAO Main CS"), ShaderDefinition{ "Luma_F4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs" });
        native_shaders_definitions.emplace(CompileTimeStringHash("F4 XeGTAO Denoise Pass 1 CS"), ShaderDefinition{ "Luma_F4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{ "XE_GTAO_FINAL_APPLY", "0" }}});
        native_shaders_definitions.emplace(CompileTimeStringHash("F4 XeGTAO Denoise Pass 2 CS"), ShaderDefinition{ "Luma_F4_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{ "XE_GTAO_FINAL_APPLY", "1" }}});
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataFallout4;
    }

    void OnDestroyDeviceData(DeviceData& device_data) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        ResetCOMArray(game_device_data.uav_xe_gtao_prefilter_depths16x16);
        Game::OnDestroyDeviceData(device_data);
    }

    void OnInitSwapchain(reshade::api::swapchain* swapchain) override
    {
        auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);

        game_device_data.tex_dlss_output.reset();
    }

    static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
    {
        auto& device_data = *device->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);

        auto buffer = (ID3D11Buffer*)resource.handle;
        if (buffer == game_device_data.cb_taa) {
            g_cb_taa_mapped_data = *data;
        }
    }

    static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
    {
        auto& device_data = *device->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);

        auto buffer = (ID3D11Buffer*)resource.handle;
        if (buffer == game_device_data.cb_taa) {
            std::memcpy(&g_cb_taa_data, g_cb_taa_mapped_data, sizeof(g_cb_taa_data));
        }
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        if (original_shader_hashes.Contains(shader_hashes_LinearizeAndDownsampleDepth))
        {
            if (g_xegtao_enable)
            {
                // The game is using these depths elsewhere so we can't just skip the original draw.
                (*original_draw_dispatch_func)();

                // XeGTAOPrefilterDepths16x16 pass
                //

                [[unlikely]] if (!game_device_data.f4_luma_cb)
                {
                    create_constant_buffer(native_device, sizeof(g_f4_luma_cb_data), game_device_data.f4_luma_cb.put());
                }

                g_f4_luma_cb_data.inv_renderer_resolution = float2(1.0f / device_data.render_resolution.x, 1.0f / device_data.render_resolution.y);
                g_f4_luma_cb_data.aspect_ratio = device_data.render_resolution.x / device_data.render_resolution.y;
                g_f4_luma_cb_data.frame_index = cb_luma_global_settings.FrameIndex;
                update_constant_buffer(native_device_context, game_device_data.f4_luma_cb.get(), &g_f4_luma_cb_data, sizeof(g_f4_luma_cb_data));

                // Create prefilter depths views.
                [[unlikely]] if (!game_device_data.uav_xe_gtao_prefilter_depths16x16[0])
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = XE_GTAO_DEPTH_MIP_LEVELS;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R32_FLOAT;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ComPtr<ID3D11Texture2D> tex;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);

                    // Create UAVs for each MIP.
                    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                    uav_desc.Format = tex_desc.Format;
                    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                    for (int i = 0; i < game_device_data.uav_xe_gtao_prefilter_depths16x16.size(); ++i) {
                       uav_desc.Texture2D.MipSlice = i;
                       ensure(native_device->CreateUnorderedAccessView(tex.get(), &uav_desc, &game_device_data.uav_xe_gtao_prefilter_depths16x16[i]), >= 0);
                    }

                    ensure(native_device->CreateShaderResourceView(tex.get(), nullptr, game_device_data.srv_xe_gtao_prefilter_depths16x16.put()), >= 0);
                }

                // Bindings.
                native_device_context->CSSetUnorderedAccessViews(0, game_device_data.uav_xe_gtao_prefilter_depths16x16.size(), game_device_data.uav_xe_gtao_prefilter_depths16x16.data(), nullptr);
                native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash("F4 XeGTAO Prefilter Depths CS")).get(), nullptr, 0);
                native_device_context->CSSetConstantBuffers(13, 1, &game_device_data.f4_luma_cb);
                auto smp_point = device_data.sampler_state_point.get();
                native_device_context->CSSetSamplers(0, 1, &smp_point);

                native_device_context->Dispatch((device_data.render_resolution.x + 16 - 1) / 16, (device_data.render_resolution.y + 16 - 1) / 16, 1);

                // Unbind UAVs.
                static constexpr std::array<ID3D11UnorderedAccessView*, XE_GTAO_DEPTH_MIP_LEVELS> uav_nulls_prefilter_depths_pass = {};
                native_device_context->CSSetUnorderedAccessViews(0, uav_nulls_prefilter_depths_pass.size(), uav_nulls_prefilter_depths_pass.data(), nullptr);

                //

                return DrawOrDispatchOverrideType::Replaced;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_SSAOMain))
        {
            has_drawn_ssao = true;

            if (g_xegtao_enable)
            {
                // XeGTAOMain pass
                //

                [[unlikely]] if (!game_device_data.f4_luma_cb)
                {
                    create_constant_buffer(native_device, sizeof(g_f4_luma_cb_data), game_device_data.f4_luma_cb.put());
                }

                g_f4_luma_cb_data.inv_renderer_resolution = float2(1.0f / device_data.render_resolution.x, 1.0f / device_data.render_resolution.y);
                g_f4_luma_cb_data.aspect_ratio = device_data.render_resolution.x / device_data.render_resolution.y;
                g_f4_luma_cb_data.frame_index = cb_luma_global_settings.FrameIndex;
                update_constant_buffer(native_device_context, game_device_data.f4_luma_cb.get(), &g_f4_luma_cb_data, sizeof(g_f4_luma_cb_data));

                // Create AO term and Edges views.
                [[unlikely]] if (!game_device_data.uav_xe_gtao_main)
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ComPtr<ID3D11Texture2D> tex;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
                    ensure(native_device->CreateUnorderedAccessView(tex.get(), nullptr, game_device_data.uav_xe_gtao_main.put()), >= 0);
                    ensure(native_device->CreateShaderResourceView(tex.get(), nullptr, game_device_data.srv_xe_gtao_main.put()), >= 0);
                }

                // Bindings.
                native_device_context->CSSetUnorderedAccessViews(0, 1, &game_device_data.uav_xe_gtao_main, nullptr);
                native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash("F4 XeGTAO Main CS")).get(), nullptr, 0);
                native_device_context->CSSetConstantBuffers(13, 1, &game_device_data.f4_luma_cb);
                auto smp_point = device_data.sampler_state_point.get();
                native_device_context->CSSetSamplers(0, 1, &smp_point);
                native_device_context->CSSetShaderResources(0, 1, &game_device_data.srv_xe_gtao_prefilter_depths16x16);

                native_device_context->Dispatch((device_data.render_resolution.x + XE_GTAO_NUMTHREADS_X - 1) / XE_GTAO_NUMTHREADS_X, (device_data.render_resolution.y + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);

                //

                // Doing 2 XeGTAODenoisePass passes correspond to "Denoising level: Medium" from the XeGTAO demo.

                // XeGTAODenoisePass1 pass
                //

                // Create AO term and Edges views.
                [[unlikely]] if (!game_device_data.uav_xe_gtao_denoise_pass1)
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ComPtr<ID3D11Texture2D> tex;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
                    ensure(native_device->CreateUnorderedAccessView(tex.get(), nullptr, game_device_data.uav_xe_gtao_denoise_pass1.put()), >= 0);
                    ensure(native_device->CreateShaderResourceView(tex.get(), nullptr, game_device_data.srv_xe_gtao_denoise_pass1.put()), >= 0);
                }

                // Bindings.
                native_device_context->CSSetUnorderedAccessViews(0, 1, &game_device_data.uav_xe_gtao_denoise_pass1, nullptr);
                native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash("F4 XeGTAO Denoise Pass 1 CS")).get(), nullptr, 0);
                native_device_context->CSSetShaderResources(0, 1, &game_device_data.srv_xe_gtao_main);

                native_device_context->Dispatch((device_data.render_resolution.x + XE_GTAO_NUMTHREADS_X * 2 - 1) / (XE_GTAO_NUMTHREADS_X * 2), (device_data.render_resolution.y + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y,1);

                //

                // XeGTAODenoisePass2 pass
                //

                // Create AO term and Edges views.
                [[unlikely]] if (!game_device_data.uav_xe_gtao_denoise_pass2)
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The original shader (the last SSAO pass) draws to rgba8_unorm, but only r is later used?
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ComPtr<ID3D11Texture2D> tex;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
                    ensure(native_device->CreateUnorderedAccessView(tex.get(), nullptr, game_device_data.uav_xe_gtao_denoise_pass2.put()), >= 0);
                    ensure(native_device->CreateShaderResourceView(tex.get(), nullptr, game_device_data.srv_xe_gtao_denoise_pass2.put()), >= 0);
                }

                // Bindings.
                native_device_context->CSSetUnorderedAccessViews(0, 1, &game_device_data.uav_xe_gtao_denoise_pass2, nullptr);
                native_device_context->CSSetShader(device_data.native_compute_shaders.at(CompileTimeStringHash("F4 XeGTAO Denoise Pass 2 CS")).get(), nullptr, 0);
                native_device_context->CSSetShaderResources(0, 1, &game_device_data.srv_xe_gtao_denoise_pass1);

                native_device_context->Dispatch((device_data.render_resolution.x + XE_GTAO_NUMTHREADS_X * 2 - 1) / (XE_GTAO_NUMTHREADS_X * 2), (device_data.render_resolution.y + XE_GTAO_NUMTHREADS_Y - 1) / XE_GTAO_NUMTHREADS_Y, 1);

                //

                return DrawOrDispatchOverrideType::Replaced;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_SSAODenoiseX))
        {
            if (has_drawn_ssao && g_xegtao_enable)
            {
                return DrawOrDispatchOverrideType::Skip;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_SSAODenoiseY))
        {
            if (has_drawn_ssao && g_xegtao_enable)
            {
                // We need to track the SSAO resource later.
                ComPtr<ID3D11UnorderedAccessView> uav;
                native_device_context->CSGetUnorderedAccessViews(0, 1, uav.put());
                uav->GetResource(game_device_data.resource_ssao.put());
            
                return DrawOrDispatchOverrideType::Skip;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_TAA))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // Get the TAA CB. We need to track it later on map/unmap.
                native_device_context->PSGetConstantBuffers(2, 1, game_device_data.cb_taa.put());

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
                settings_data.inverted_depth = false;
                settings_data.mvs_jittered = false;

                // MVs are in UV space so we need to scale them to screen space for DLSS.
                settings_data.mvs_x_scale = device_data.render_resolution.x;
                settings_data.mvs_y_scale = device_data.render_resolution.y;
                
                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = false;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                // Get SRVs.
                ComPtr<ID3D11ShaderResourceView> srv_scene;
                native_device_context->PSGetShaderResources(0, 1, srv_scene.put());
                ComPtr<ID3D11ShaderResourceView> srv_mvs;
                native_device_context->PSGetShaderResources(2, 1, srv_mvs.put());
                ComPtr<ID3D11ShaderResourceView> srv_depth;
                native_device_context->PSGetShaderResources(3, 1, srv_depth.put());

                // Get resources from SRVs.
                ComPtr<ID3D11Resource> resource_scene;
                srv_scene->GetResource(resource_scene.put());
                ComPtr<ID3D11Resource> resource_mvs;
                srv_mvs->GetResource(resource_mvs.put());
                ComPtr<ID3D11Resource> resource_depth;
                srv_depth->GetResource(resource_depth.put());

                // Get RTVs.
                std::array<ID3D11RenderTargetView*, 2> rtvs;
                native_device_context->OMGetRenderTargets(rtvs.size(), rtvs.data(), nullptr);

                // RTV1 should be the current frame and the backbuffer.
                ComPtr<ID3D11Resource> resource_output;
                rtvs[1]->GetResource(resource_output.put());

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
                draw_data.motion_vectors = resource_mvs.get();
                draw_data.depth_buffer = resource_depth.get();

                // Jitters are in UV offsets so we need to scale them to pixel offsets for DLSS.
                draw_data.jitter_x = g_cb_taa_data[4] * device_data.render_resolution.x * 1.0f;
                draw_data.jitter_y = g_cb_taa_data[5] * device_data.render_resolution.y * -1.0f;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                // Copy DLSS output to the original TAA's current frame and the backbuffer.
                native_device_context->CopyResource(resource_output.get(), game_device_data.tex_dlss_output.get());

                ResetCOMArray(rtvs);

                return DrawOrDispatchOverrideType::Replaced;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (has_drawn_ssao && g_xegtao_enable)
        {
            // This should be reliable.
            // Replace the original SSAO SRV with the XeGTAO SRV.
            // The original SSAO should be bound only onece as SRV9.
            // Confirmed PS permuatations: 0xEDF0538E, 0xC3B3F9E6, 0xBDFB307C
            ComPtr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(9, 1, srv.put());
            if (srv)
            {
                ComPtr<ID3D11Resource> resource;
                srv->GetResource(resource.put());
                if (resource == game_device_data.resource_ssao)
                {
                    native_device_context->PSSetShaderResources(9, 1, &game_device_data.srv_xe_gtao_denoise_pass2);
                }
            }
        }

        return DrawOrDispatchOverrideType::None;
    }

    void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        has_drawn_ssao = false;

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
        ImGui::Text("Fallout 4 Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Fallout 4 Luma mod");
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
        forced_shader_names.emplace(0x1D1E3148, "Linearize, downsample depth");
        forced_shader_names.emplace(0x18301D24, "Downsample linear depth");
        forced_shader_names.emplace(0x0307C239, "SSAO main");
        forced_shader_names.emplace(0xE151AD86, "SSAO denoise x");
        forced_shader_names.emplace(0x7E8F370A, "SSAO denoise y");
        forced_shader_names.emplace(0xEDF0538E, "Everything?");
        forced_shader_names.emplace(0xC3B3F9E6, "Everything?");
        forced_shader_names.emplace(0xBDFB307C, "Everything?");
        forced_shader_names.emplace(0x63EE533F, "Motion Blur");
        forced_shader_names.emplace(0x80802E60, "Tonemap");
        forced_shader_names.emplace(0x61CC29E6, "TAA");
        #endif

        game = new GameFallout4();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}