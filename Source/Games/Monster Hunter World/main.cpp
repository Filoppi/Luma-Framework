#define MONSTER_HUNTER_WORLD 1

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

struct alignas(16) CBViewProjection
{
    row_major float4x4 fViewProj;
    row_major float4x4 fView;
    row_major float4x4 fProj;
    row_major float4x4 fViewI;
    row_major float4x4 fProjI;
    row_major float4x4 fViewProjI;
    float3 fCameraPos;
    float padding1; // Added.
    float3 fCameraDir;
    float padding2; // Added.
    float3 fZToLinear;
    float fCameraNearClip;
    float fCameraFarClip;
    float fCameraTargetDist;
    float2 padding3; // Added.
    float4 fPassThrough;
    float3 fLODBasePos;
    float padding4; // Added.
    row_major float4x4 fViewProjPF;
    row_major float4x4 fViewProjIPF;
    row_major float4x4 fViewPF;
    row_major float4x4 fProjPF;
    row_major float4x4 fViewProjIViewProjPF;
    row_major float4x4 fNoJitterProj;
    row_major float4x4 fNoJitterViewProj;
    row_major float4x4 fNoJitterViewProjI;
    row_major float4x4 fNoJitterViewProjIViewProjPF;
    float2 fPassThroughCorrect;
    /*bool*/ uint bWideMonitor;
    float padding5;
};

struct alignas(16) Bloom_data
{
    float2 src_size;
    float2 inv_src_size;
    float2 axis;
    float sigma;
    float padding;
};

namespace
{
    const ShaderHashesList shader_hashes_TAA = { .compute_shaders = { 0xD84C4AF0 }};
    const ShaderHashesList shader_hashes_Copy = { .pixel_shaders = { 0xBCA17DC2 }};
    const ShaderHashesList shader_hashes_BloomThreshold = { .pixel_shaders = { 0xC1DD61BF }};
    const ShaderHashesList shader_hashes_Bloom = { .pixel_shaders = { 0xF45A211E, 0xEF5ED883, 0xBA76C0BA }};
    const ShaderHashesList shader_hashes_BloomAdd = { .pixel_shaders = { 0x77963D88 }};
    
    // Bloom
    bool g_enable_luma_bloom = true;
    int g_bloom_nmips;
    std::vector<float> g_bloom_sigmas;
    float g_bloom_intensity = 1.0;
    Bloom_data g_bloom_data;
    static std::vector<ID3D11RenderTargetView*> g_rtv_bloom_mips_y;
    static std::vector<ID3D11ShaderResourceView*> g_srv_bloom_mips_y;
    static std::vector<ID3D11RenderTargetView*> g_rtv_bloom_mips_x;
    static std::vector<ID3D11ShaderResourceView*> g_srv_bloom_mips_x;

    float g_jitter_x;
    float g_jitter_y;
    float g_prev_jitter_x;
    float g_prev_jitter_y;

    // The game is throwing EXCEPTION_BREAKPOINT which will crash the game if debugger isn't attached (even in release build!).
    LONG CALLBACK ExceptionBreakpointHandler(EXCEPTION_POINTERS* ep)
    {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
            ep->ContextRecord->Rip += 1;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    void CreateConstantBuffer(ID3D11Device* device, UINT size, ID3D11Buffer** cb)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = size;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ensure(device->CreateBuffer(&desc, nullptr, cb), >= 0);
    }

    void UpdateConstantBuffer(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const void* data, size_t size)
    {
        D3D11_MAPPED_SUBRESOURCE mapped_subresource;
        ensure(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource), >= 0);
        std::memcpy(mapped_subresource.pData, data, size);
        ctx->Unmap(cb, 0);
    }
}

struct GameDeviceDataMonsterHunterWorld final : GameDeviceData
{
};

class MonsterHunterWorld final : public Game
{
public:

    static GameDeviceDataMonsterHunterWorld& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataMonsterHunterWorld*)device_data.game;
    }

    void OnLoad(std::filesystem::path& file_path, bool failed = false) override
    {
        // We must remove it later with `RemoveVectoredExceptionHandler`?
        AddVectoredExceptionHandler(1, ExceptionBreakpointHandler);

        reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdateBufferRegion);
    }

    void OnInit(bool async) override
    {
        // ### Update these (find the right values) ###
        // ### See the "GameCBuffers.hlsl" in the shader directory to expand settings ###
        luma_settings_cbuffer_index = 13;
        luma_data_cbuffer_index = -1;

        native_shaders_definitions.emplace("MHW Unpack MVs CS"_h, ShaderDefinition{ "Luma_MHW_UnpackMVs_CS", reshade::api::pipeline_subobject_type::compute_shader });
        
        // Bloom
        native_shaders_definitions.emplace("MHW Bloom VS"_h, ShaderDefinition{ "Luma_MHW_Bloom_impl", reshade::api::pipeline_subobject_type::vertex_shader, nullptr, "main_vs" });
        native_shaders_definitions.emplace("MHW Bloom Prefilter PS"_h, ShaderDefinition{ "Luma_MHW_Bloom_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "prefilter_ps" });
        native_shaders_definitions.emplace("MHW Bloom Downsample PS"_h, ShaderDefinition{ "Luma_MHW_Bloom_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "downsample_ps" });
        native_shaders_definitions.emplace("MHW Bloom Upsample PS"_h, ShaderDefinition{ "Luma_MHW_Bloom_impl", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "upsample_ps" });
      
        // Bloom
        g_bloom_nmips = 6;
        g_rtv_bloom_mips_y.resize(g_bloom_nmips);
        g_srv_bloom_mips_y.resize(g_bloom_nmips);
        g_rtv_bloom_mips_x.resize(g_bloom_nmips);
        g_srv_bloom_mips_x.resize(g_bloom_nmips);
        g_bloom_sigmas.resize(g_bloom_nmips);
        g_bloom_sigmas[0] = 1.5f;
        g_bloom_sigmas[1] = 2.0f;
        g_bloom_sigmas[2] = 2.0f;
        g_bloom_sigmas[3] = 2.0f;
        g_bloom_sigmas[4] = 1.0f;
        g_bloom_sigmas[5] = 1.0f;
    }

    void DrawImGuiSettings(DeviceData& device_data) override
    {
        // The game doesn't enable jitters with TAA (what do you need that for anyway).
        // Jitters can be enabled with mods, so let user know are jitters OK.
        if (device_data.sr_type != SR::Type::None && g_jitter_x != g_prev_jitter_x && g_jitter_y != g_prev_jitter_y)
        {
            ImGui::Text("TAA jitters status: OK.");
        }
        else
        {
            ImGui::Text("TAA jitters status: Invalid!");
        }

        ImGui::NewLine();

#if DEVELOPMENT
        if (ImGui::SliderInt("Luma Bloom nmips", &g_bloom_nmips, 1.0, 10.0))
        {
            ResetCOMArray(g_rtv_bloom_mips_y);
            ResetCOMArray(g_srv_bloom_mips_y);
            ResetCOMArray(g_rtv_bloom_mips_x);
            ResetCOMArray(g_srv_bloom_mips_x);

            g_rtv_bloom_mips_y.resize(g_bloom_nmips);
            g_srv_bloom_mips_y.resize(g_bloom_nmips);
            g_rtv_bloom_mips_x.resize(g_bloom_nmips);
            g_srv_bloom_mips_x.resize(g_bloom_nmips);
            g_bloom_sigmas.resize(g_bloom_nmips);
        }

        for (int i = 0; i < g_bloom_nmips; ++i)
        {
            const std::string name = "Luma Bloom Sigma" + std::to_string(i);
            ImGui::SliderFloat(name.c_str(), &g_bloom_sigmas[i], 0.0f, 15.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        }
#endif

        if (ImGui::Checkbox("Luma Bloom Enable", &g_enable_luma_bloom))
        {
            reshade::set_config_value(nullptr, NAME, "LumaBloomEnable", g_enable_luma_bloom);
        }

        if (ImGui::SliderFloat("Bloom Intensity", &g_bloom_intensity, 0.0f, 3.0f))
        {
            reshade::set_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
        }
    }

    void LoadConfigs() override
    {
        reshade::get_config_value(nullptr, NAME, "LumaBloomEnable", g_enable_luma_bloom);
        reshade::get_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataMonsterHunterWorld;
    }

    void OnInitSwapchain(reshade::api::swapchain* swapchain) override
    {
        auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
        auto& game_device_data = GetGameDeviceData(device_data);
        auto& managed_resources = game_device_data.managed_resources;

        managed_resources.unordered_access_views["mvs"_h].reset();

        ResetCOMArray(g_rtv_bloom_mips_y);
        ResetCOMArray(g_srv_bloom_mips_y);
        ResetCOMArray(g_rtv_bloom_mips_x);
        ResetCOMArray(g_srv_bloom_mips_x);
    }

    static bool OnUpdateBufferRegion(reshade::api::device* device, const void* data, reshade::api::resource resource, uint64_t offset, uint64_t size)
    {
        auto native_resource = (ID3D11Resource*)resource.handle;
        ComPtr<ID3D11Buffer> buffer;
        auto hr = native_resource->QueryInterface(buffer.put());
        if (SUCCEEDED(hr))
        {
            D3D11_BUFFER_DESC desc;
            buffer->GetDesc(&desc);

            // This alone should be reliable? Needs testing!
            if (desc.BindFlags == D3D11_BIND_CONSTANT_BUFFER && desc.ByteWidth == 1072)
            {
                g_prev_jitter_x = g_jitter_x;
                g_prev_jitter_y = g_jitter_y;
                g_jitter_x = ((CBViewProjection*)data)->fProj.m20;
                g_jitter_y = ((CBViewProjection*)data)->fProj.m21;
            }
        }
        return false;
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);
        auto& managed_resources = game_device_data.managed_resources;

        if (original_shader_hashes.Contains(shader_hashes_BloomThreshold))
        {
            if (g_enable_luma_bloom)
            {
                native_device_context->PSGetConstantBuffers(3, 1, managed_resources.buffers["CBLuminance"_h].put());
                native_device_context->PSGetShaderResources(1, 1, managed_resources.shader_resource_views["gLuminanceBufferSRV"_h].put());
                return DrawOrDispatchOverrideType::Skip;
            }
            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_Bloom))
        {
            if (g_enable_luma_bloom)
            {
                return DrawOrDispatchOverrideType::Skip;
            }
            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_BloomAdd))
        {
            if (g_enable_luma_bloom)
            {
                // Backup IA.
                D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
                native_device_context->IAGetPrimitiveTopology(&primitive_topology_original);

                // Backup VS.
                ComPtr<ID3D11VertexShader> vs_original;
                native_device_context->VSGetShader(vs_original.put(), nullptr, nullptr);

                // Backup PS.
                ComPtr<ID3D11PixelShader> ps_original;
                native_device_context->PSGetShader(ps_original.put(), nullptr, nullptr);
                ComPtr<ID3D11Buffer> cb_orginal;
                native_device_context->PSGetConstantBuffers(13, 1, cb_orginal.put());
                ComPtr<ID3D11SamplerState> ps_sampler_original;
                native_device_context->PSGetSamplers(0, 1, ps_sampler_original.put());
                ComPtr<ID3D11ShaderResourceView> ps_srv_original;
                native_device_context->PSGetShaderResources(0, 1, ps_srv_original.put());

                // Backup Viewports.
                UINT num_viewports;
                native_device_context->RSGetViewports(&num_viewports, nullptr);
                std::vector<D3D11_VIEWPORT> viewports_original(num_viewports);
                native_device_context->RSGetViewports(&num_viewports, viewports_original.data());

                // Backup Rasterizer.
                ComPtr<ID3D11RasterizerState> rasterizer_original;
                native_device_context->RSGetState(rasterizer_original.put());

                // Backup Blend.
                ComPtr<ID3D11BlendState> blend_original;
                FLOAT blend_factor_original[4];
                UINT sample_mask_original;
                native_device_context->OMGetBlendState(blend_original.put(), blend_factor_original, &sample_mask_original);

                // Get RTV (should be scene) and create SRV from it's resource.
                ComPtr<ID3D11RenderTargetView> rtv_scene;
                native_device_context->OMGetRenderTargets(1, rtv_scene.put(), nullptr);
                ComPtr<ID3D11Resource> resource_scene;
                rtv_scene->GetResource(resource_scene.put());
                ComPtr<ID3D11ShaderResourceView> srv_scene;
                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MipLevels = 1;
                ensure(native_device->CreateShaderResourceView(resource_scene.get(), &srv_desc, srv_scene.put()), >= 0);

                // Create MIPs and views.
                //

                const UINT y_mip0_width = (uint32_t)device_data.render_resolution.x >> 1;
                const UINT y_mip0_height = (uint32_t)device_data.render_resolution.y >> 1;

                // Create Y MIPs and views.
                [[unlikely]] if (!g_rtv_bloom_mips_y[0])
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = y_mip0_width;
                    tex_desc.Height = y_mip0_height;
                    tex_desc.MipLevels = g_bloom_nmips;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                    
                    ComPtr<ID3D11Texture2D> tex;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
                    
                    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                    rtv_desc.Format = tex_desc.Format;
                    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    
                    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                    srv_desc.Format = tex_desc.Format;
                    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                    srv_desc.Texture2D.MipLevels = 1;
                    
                    for (int i = 0; i < g_bloom_nmips; ++i)
                    {
                        rtv_desc.Texture2D.MipSlice = i;
                        ensure(native_device->CreateRenderTargetView(tex.get(), &rtv_desc, &g_rtv_bloom_mips_y[i]), >= 0);
                        srv_desc.Texture2D.MostDetailedMip = i;
                        ensure(native_device->CreateShaderResourceView(tex.get(), &srv_desc, &g_srv_bloom_mips_y[i]), >= 0);
                    }
                }

                const UINT x_mip0_width = (uint32_t)device_data.render_resolution.x >> 1;
                const UINT x_mip0_height = (uint32_t)device_data.render_resolution.y;

                // Create X MIPs and views.
                [[unlikely]] if (!g_rtv_bloom_mips_x[0])
                {
                    // Create X MIP0 and views.
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = x_mip0_width;
                    tex_desc.Height = x_mip0_height;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                    ComPtr<ID3D11Texture2D> tex;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
                    ensure(native_device->CreateRenderTargetView(tex.get(), nullptr, &g_rtv_bloom_mips_x[0]), >= 0);
                    ensure(native_device->CreateShaderResourceView(tex.get(), nullptr, &g_srv_bloom_mips_x[0]), >= 0);

                    // Create rest of X MIPs and views.
                    for (UINT i = 1; i < g_bloom_nmips; ++i)
                    {
                        tex_desc.Width = std::max(1u, x_mip0_width >> i);
                        tex_desc.Height = std::max(1u, x_mip0_height >> i);
                        ensure(native_device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
                        ensure(native_device->CreateRenderTargetView(tex.get(), nullptr, &g_rtv_bloom_mips_x[i]), >= 0);
                        ensure(native_device->CreateShaderResourceView(tex.get(), nullptr, &g_srv_bloom_mips_x[i]), >= 0);
                    }
                }

                //

                // Create CB.
                [[unlikely]] if (!managed_resources.buffers["cb_bloom"_h])
                {
                    CreateConstantBuffer(native_device, sizeof(g_bloom_data), managed_resources.buffers["cb_bloom"_h].put());
                }

                // Prefilter + downsample pass
                //

                D3D11_VIEWPORT viewport_x = {};
                viewport_x.Width = x_mip0_width;
                viewport_x.Height = x_mip0_height;

                // Update CB.
                g_bloom_data.src_size = float2(device_data.render_resolution.x, device_data.render_resolution.y);
                g_bloom_data.inv_src_size = float2(1.0f / g_bloom_data.src_size.x, 1.0f / g_bloom_data.src_size.y);
                g_bloom_data.axis = float2(1.0f, 0.0f);
                g_bloom_data.sigma = g_bloom_sigmas[0];
                UpdateConstantBuffer(native_device_context, managed_resources.buffers["cb_bloom"_h].get(), &g_bloom_data, sizeof(g_bloom_data));

                // Bindings.
                native_device_context->OMSetRenderTargets(1, &g_rtv_bloom_mips_x[0], nullptr);
                native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                native_device_context->VSSetShader(device_data.native_vertex_shaders.at("MHW Bloom VS"_h).get(), nullptr, 0);
                native_device_context->PSSetShader(device_data.native_pixel_shaders.at("MHW Bloom Downsample PS"_h).get(), nullptr, 0);
                native_device_context->PSSetConstantBuffers(13, 1, &managed_resources.buffers["cb_bloom"_h]);
                native_device_context->PSSetConstantBuffers(12, 1, &managed_resources.buffers["CBLuminance"_h]);
                native_device_context->PSSetShaderResources(0, 1, &srv_scene);
                native_device_context->PSSetShaderResources(1, 1, &managed_resources.shader_resource_views["gLuminanceBufferSRV"_h]);
                native_device_context->RSSetViewports(1, &viewport_x);
                native_device_context->OMSetBlendState(nullptr, nullptr, UINT_MAX);

                // Draw X pass.
                native_device_context->Draw(3, 0);

                std::vector<D3D11_VIEWPORT> viewports_y(g_bloom_nmips);
                viewports_y[0].Width = y_mip0_width;
                viewports_y[0].Height = y_mip0_height;

                // Update CB.
                g_bloom_data.src_size = float2(x_mip0_width, x_mip0_height);
                g_bloom_data.inv_src_size = float2(1.0f / g_bloom_data.src_size.x, 1.0f / g_bloom_data.src_size.y);
                g_bloom_data.axis = float2(0.0f, 1.0f);
                UpdateConstantBuffer(native_device_context, managed_resources.buffers["cb_bloom"_h].get(), &g_bloom_data, sizeof(g_bloom_data));

                // Bindings.
                native_device_context->OMSetRenderTargets(1, &g_rtv_bloom_mips_y[0], nullptr);
                native_device_context->PSSetShader(device_data.native_pixel_shaders.at("MHW Bloom Prefilter PS"_h).get(), nullptr, 0);
                native_device_context->PSSetShaderResources(0, 1, &g_srv_bloom_mips_x[0]);
                native_device_context->RSSetViewports(1, &viewports_y[0]);

                // Draw Y pass.
                native_device_context->Draw(3, 0);

                //

                // Downsample passes
                //

                // Bindings.
                native_device_context->PSSetShader(device_data.native_pixel_shaders.at("MHW Bloom Downsample PS"_h).get(), nullptr, 0);

                // Render downsample passes.
                for (UINT i = 1; i < g_bloom_nmips; ++i)
                {
                    viewport_x.Width = std::max(1u, x_mip0_width >> i);
                    viewport_x.Height = std::max(1u, x_mip0_height >> i);

                    // Update CB.
                    g_bloom_data.src_size = float2(viewports_y[i - 1].Width, viewports_y[i - 1].Height);
                    g_bloom_data.axis = float2(1.0f, 0.0f);
                    g_bloom_data.inv_src_size = float2(1.0f / g_bloom_data.src_size.x, 1.0f / g_bloom_data.src_size.y);
                    g_bloom_data.sigma = g_bloom_sigmas[i];
                    UpdateConstantBuffer(native_device_context, managed_resources.buffers["cb_bloom"_h].get(), &g_bloom_data, sizeof(g_bloom_data));

                    // Bindings.
                    native_device_context->OMSetRenderTargets(1, &g_rtv_bloom_mips_x[i], nullptr);
                    native_device_context->PSSetShaderResources(0, 1, &g_srv_bloom_mips_y[i - 1]);
                    native_device_context->RSSetViewports(1, &viewport_x);

                    // Draw X pass.
                    native_device_context->Draw(3, 0);

                    viewports_y[i].Width = std::max(1u, y_mip0_width >> i);
                    viewports_y[i].Height = std::max(1u, y_mip0_height >> i);

                    // Update CB.
                    g_bloom_data.src_size = float2(viewport_x.Width, viewport_x.Height);
                    g_bloom_data.axis = float2(0.0f, 1.0f);
                    g_bloom_data.inv_src_size = float2(1.0f / g_bloom_data.src_size.x, 1.0f / g_bloom_data.src_size.y);
                    UpdateConstantBuffer(native_device_context, managed_resources.buffers["cb_bloom"_h].get(), &g_bloom_data, sizeof(g_bloom_data));

                    // Bindings.
                    native_device_context->OMSetRenderTargets(1, &g_rtv_bloom_mips_y[i], nullptr);
                    native_device_context->PSSetShaderResources(0, 1, &g_srv_bloom_mips_x[i]);
                    native_device_context->RSSetViewports(1, &viewports_y[i]);

                    // Draw Y pass.
                    native_device_context->Draw(3, 0);
                }

                //

                // Upsample passes
                //

                // Create blend.
                [[unlikely]] if (!managed_resources.blends["bloom"_h])
                {
                    CD3D11_BLEND_DESC desc(D3D11_DEFAULT);
                    desc.RenderTarget[0].BlendEnable = TRUE;
                    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
                    desc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
                    ensure(native_device->CreateBlendState(&desc, managed_resources.blends["bloom"_h].put()), >= 0);
                }

                // Bindings.
                native_device_context->PSSetShader(device_data.native_pixel_shaders.at("MHW Bloom Upsample PS"_h).get(), nullptr, 0);

                for (int i = g_bloom_nmips - 1; i > 0; --i)
                {
                    // If both dst and src are D3D11_BLEND_BLEND_FACTOR,
                    // factor of 0.5 will be enegrgy preserving.
                    static constexpr FLOAT blend_factor[] = { 0.5f, 0.5f, 0.5f, 0.5f };

                    // Update CB.
                    g_bloom_data.src_size = float2(viewports_y[i].Width, viewports_y[i].Height);
                    g_bloom_data.inv_src_size = float2(1.0f / g_bloom_data.src_size.x, 1.0f / g_bloom_data.src_size.y);
                    UpdateConstantBuffer(native_device_context, managed_resources.buffers["cb_bloom"_h].get(), &g_bloom_data, sizeof(g_bloom_data));

                    // Bindings.
                    native_device_context->OMSetRenderTargets(1, &g_rtv_bloom_mips_y[i - 1], nullptr);
                    native_device_context->PSSetShaderResources(0, 1, &g_srv_bloom_mips_y[i]);
                    native_device_context->RSSetViewports(1, &viewports_y[i - 1]);
                    native_device_context->OMSetBlendState(managed_resources.blends["bloom"_h].get(), blend_factor, UINT_MAX);

                    native_device_context->Draw(3, 0);
                }

                //

                // BloomAdd pass
                //

                // Create blend.
                [[unlikely]] if (!managed_resources.blends["bloom_add"_h])
                {
                    CD3D11_BLEND_DESC desc(D3D11_DEFAULT);
                    desc.RenderTarget[0].BlendEnable = TRUE;
                    desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
                    desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
                    ensure(native_device->CreateBlendState(&desc, managed_resources.blends["bloom_add"_h].put()), >= 0);
                }

                D3D11_VIEWPORT viewport_add = {};
                viewport_add.Width = device_data.render_resolution.x;
                viewport_add.Height = device_data.render_resolution.y;

                FLOAT blend_factor[] = { g_bloom_intensity, g_bloom_intensity, g_bloom_intensity, 0.0f };

                // Bindings
                native_device_context->OMSetRenderTargets(1, &rtv_scene, nullptr);
                native_device_context->PSSetShaderResources(0, 1, &g_srv_bloom_mips_y[0]);
                native_device_context->RSSetViewports(1, &viewport_add);
                native_device_context->OMSetBlendState(managed_resources.blends["bloom_add"_h].get(), blend_factor, UINT_MAX);

                native_device_context->Draw(3, 0);

                //

                // Restore.
                native_device_context->IASetPrimitiveTopology(primitive_topology_original);
                native_device_context->VSSetShader(vs_original.get(), nullptr, 0);
                native_device_context->PSSetShader(ps_original.get(), nullptr, 0);
                native_device_context->PSSetConstantBuffers(13, 1, &cb_orginal);
                native_device_context->PSSetSamplers(0, 1, &ps_sampler_original);
                native_device_context->PSSetShaderResources(0, 1, &ps_srv_original);
                native_device_context->RSSetViewports(viewports_original.size(), viewports_original.data());
                native_device_context->RSSetState(rasterizer_original.get());
                native_device_context->OMSetBlendState(blend_original.get(), blend_factor_original, sample_mask_original);
                
                return DrawOrDispatchOverrideType::Replaced;
            }
            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_Copy))
        {
            // The shader is used to copy various resources.
            // Expecting depth to be the last before the TAA should be reliable? Needs testing!
            ComPtr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, srv.put());
            if (srv)
            {
                srv->GetResource(managed_resources.resources["depth"_h].put());
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_TAA))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // DLSS requires an immediate context for execution!
                ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

                // Get UAV and it's resource.
                ComPtr<ID3D11UnorderedAccessView> uav_output;
                native_device_context->CSGetUnorderedAccessViews(0, 1, uav_output.put());
                ComPtr<ID3D11Resource> resource_output;
                uav_output->GetResource(resource_output.put());

                // UnpackMVs pass
                //

                // Create UAV.
                [[unlikely]] if (!managed_resources.unordered_access_views["mvs"_h])
                {
                    D3D11_TEXTURE2D_DESC tex_desc = {};
                    tex_desc.Width = device_data.render_resolution.x;
                    tex_desc.Height = device_data.render_resolution.y;
                    tex_desc.MipLevels = 1;
                    tex_desc.ArraySize = 1;
                    tex_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
                    tex_desc.SampleDesc.Count = 1;
                    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                    ensure(native_device->CreateTexture2D(&tex_desc, nullptr, managed_resources.textures_2d["mvs"_h].put()), >= 0);
                    ensure(native_device->CreateUnorderedAccessView(managed_resources.textures_2d["mvs"_h].get(), nullptr, managed_resources.unordered_access_views["mvs"_h].put()), >= 0);
                }

                // Bindings.
                native_device_context->CSSetUnorderedAccessViews(0, 1, &managed_resources.unordered_access_views["mvs"_h], nullptr);
                native_device_context->CSSetShader(device_data.native_compute_shaders.at("MHW Unpack MVs CS"_h).get(), nullptr, 0);

                native_device_context->Dispatch((device_data.render_resolution.x + 8 - 1) / 8, (device_data.render_resolution.y + 8 - 1) / 8, 1);

                //

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
                settings_data.inverted_depth = true;
                settings_data.mvs_jittered = false;

                // MVs are in NDC space so we need to scale them to screen space for DLSS.
                settings_data.mvs_x_scale = device_data.render_resolution.x * -0.5;
                settings_data.mvs_y_scale = device_data.render_resolution.y * 0.5;

                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = true;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                ComPtr<ID3D11ShaderResourceView> srv_scene;
                native_device_context->CSGetShaderResources(0, 1, srv_scene.put());
                ComPtr<ID3D11Resource> resorce_scene;
                srv_scene->GetResource(resorce_scene.put());

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resorce_scene.get();
                draw_data.output_color = resource_output.get();
                draw_data.motion_vectors = managed_resources.textures_2d["mvs"_h].get();
                draw_data.depth_buffer = managed_resources.resources["depth"_h].get();

                // Jitters are in UV offsets so we need to scale them to pixel offsets for DLSS.
                draw_data.jitter_x = g_jitter_x * device_data.render_resolution.x * -1.0f;
                draw_data.jitter_y = g_jitter_y * device_data.render_resolution.y * 1.0f;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

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
        ImGui::Text("Monster Hunter World Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Monster Hunter World Luma mod");
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

              reshade::api::format::r11g11b10_float,
        };
        // ### Check these if textures are not upgraded ###
        texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;

        enable_samplers_upgrade = true;
        
        // TODO: Remove this later!
        Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

        #if DEVELOPMENT
        forced_shader_names.emplace(0xD84C4AF0, "TAA");
        forced_shader_names.emplace(0xBCA17DC2, "Copy");
        #endif

        game = new MonsterHunterWorld();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}