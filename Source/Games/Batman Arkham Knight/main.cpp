
#define GAME_BATMAN_ARKHAM_KNIGHT 1

#define ENABLE_NGX 1

// TODO: Do this dynamically.
// Offsets for patching "BuildProjectionMatrix" function are different.
#define GAME_VERSION 1 // 1 - Steam, 2 - GOG

#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

namespace
{
    const ShaderHashesList shader_hashes_SMAAEdgeDetection = { .pixel_shaders = { 0x067FF80F } };
    const ShaderHashesList shader_hashes_SMAABlendingWeightCalculation = { .pixel_shaders = { 0x2F55F98E } };
    const ShaderHashesList shader_hashes_SMAANeighborhoodBlending = { .compute_shaders = { 0x42C0137E } };

    float2 g_jitters;

    void PatchBuildProjectionMatrixFunc()
    {
        const size_t stolen_len = 12;
        HMODULE engine_module = nullptr;
        while (!engine_module)
        {
            engine_module = GetModuleHandleA("BatmanAK.exe");
            Sleep(100);
        }
        auto base_addr = (uintptr_t)engine_module;

        #if GAME_VERSION == 1 // Steam
        uintptr_t patch_addr = base_addr + 0x104FF;
        #else // GAME_VERSION == 2 (GOG)
        uintptr_t patch_addr = base_addr + 0x104BF;
        #endif
        
        uintptr_t ret_addr = patch_addr + stolen_len;
        uintptr_t mem = (uintptr_t)VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!mem)
        {
            return;
        }
        uint8_t stolen_bytes[stolen_len];
        memcpy(&stolen_bytes[0], (void*)patch_addr, stolen_len);

        // jmp to shellcode / jmp back
        uint8_t jmp[12] = {
            0x48, 0xB8, // mov rax, imm64 
            0, 0, 0, 0, 0, 0, 0, 0, // 8-byte address
            0xFF, 0xE0 // jmp rax
        };

        memcpy(&jmp[2], &ret_addr, sizeof(uintptr_t));
        const size_t actual_size = stolen_len + 13 + sizeof(jmp);

        uint8_t shellcode[13] = {
            0x48, 0xB8, // mov rax, imm64 
            0, 0, 0, 0, 0, 0, 0, 0, // 8-byte address
            0x48, 0x8B, 0x00 //mov rax, [rax]
        };

        uintptr_t data_addr1 = reinterpret_cast<uintptr_t>(&g_jitters);
        memcpy(&shellcode[2], &data_addr1, sizeof(data_addr1));
        size_t offset = 0;
        uint8_t actual_shell[actual_size];
        memcpy(&actual_shell[offset], &shellcode, sizeof(shellcode)); offset += sizeof(shellcode);
        memcpy(&actual_shell[offset], &stolen_bytes, sizeof(stolen_bytes)); offset += sizeof(stolen_bytes);
        memcpy(&actual_shell[offset], &jmp, sizeof(jmp)); offset += sizeof(jmp);
        memcpy((void*)mem, &actual_shell, sizeof(actual_shell));

        // PATCH
        DWORD old_protect;
        VirtualProtect((void*)patch_addr, stolen_len, PAGE_EXECUTE_READWRITE, &old_protect);

        memcpy(&jmp[2], &mem, sizeof(uintptr_t));
        memset((void*)patch_addr, 0x90, stolen_len); // NOP original bytes
        memcpy((void*)patch_addr, jmp, sizeof(jmp)); // Write the jump

        VirtualProtect((void*)patch_addr, stolen_len, old_protect, &old_protect);
    }

    // TODO: Replace this with precalculated values.
    float Halton(int index, int base)
    {
        const int b = base;
        int i = index;

        float f = 1.0f;
        float r = 0.0f;
 
        while (i > 0)
        {
            f /= (float)b;
            r = r + f * (float)(i % b);
            i /= b;
        }
 
        return r;
    }

    void JitterUpdate(bool enabled, float renderer_resolution_x, float renderer_resolution_y)
    {
        if (enabled)
        {
            static uint32_t frame;
            ++frame;
            auto index = frame % 8;
            g_jitters.x = (Halton(index + 1, 2) - 0.5f) / renderer_resolution_x;
            g_jitters.y = (Halton(index + 1, 3) - 0.5f) / renderer_resolution_y;
        }
        else
        {
            g_jitters.x = 0.0f;
            g_jitters.y = 0.0f;
        }
    }
}

struct GameDeviceDataBatmanArkhamKnight final : GameDeviceData
{
   ComPtr<ID3D11Resource> resource_depth;
   ComPtr<ID3D11Resource> resource_mvs;
};

class BatmanArkhamKnight final : public Game
{
public:

    static GameDeviceDataBatmanArkhamKnight& GetGameDeviceData(DeviceData& device_data)
    {
        return *(GameDeviceDataBatmanArkhamKnight*)device_data.game;
    }

    void OnLoad(std::filesystem::path& file_path, bool failed) override
    {
        if (!failed)
        {
            reshade::register_event<reshade::addon_event::clear_render_target_view>(BatmanArkhamKnight::OnClearRenderTargetView);
        }
    }

    void OnInit(bool async) override
    {
        PatchBuildProjectionMatrixFunc();

        std::vector<ShaderDefineData> game_shader_defines_data = {
            { "DISABLE_LENS_FLARE_AND_LENS_DIRT", '0', true, false, "Disables lens flare and lens dirt effects.", 1 }
        };

        shader_defines_data.append_range(game_shader_defines_data);

        luma_settings_cbuffer_index = 13;
        luma_data_cbuffer_index = 12;
    }

    void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
    {
        device_data.game = new GameDeviceDataBatmanArkhamKnight;
    }

    static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
    {
        // Get RTV resource and try to query it to texture2d.
        auto native_rtv = (ID3D11RenderTargetView*)rtv.handle;
        ComPtr<ID3D11Resource> resource;
        native_rtv->GetResource(resource.put());
        ComPtr<ID3D11Texture2D> tex;
        auto hr = resource->QueryInterface(tex.put());

        if (SUCCEEDED(hr))
        {
            D3D11_TEXTURE2D_DESC tex_desc;
            tex->GetDesc(&tex_desc);

            // Motion vectors should be only ones with DXGI_FORMAT_R16G16_FLOAT format,
            // at least here.
            if (tex_desc.Format == DXGI_FORMAT_R16G16_FLOAT)
            {
                auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
                auto& game_device_data = GetGameDeviceData(device_data);
            
                game_device_data.resource_mvs = resource;
            }
        }

        return false;
    }

    DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        if (original_shader_hashes.Contains(shader_hashes_SMAAEdgeDetection))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // SRV1 should be the depth.
                ComPtr<ID3D11ShaderResourceView> srv_depth;
                native_device_context->PSGetShaderResources(1, 1, srv_depth.put());
                srv_depth->GetResource(game_device_data.resource_depth.put());

                return DrawOrDispatchOverrideType::Skip;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_SMAABlendingWeightCalculation))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                return DrawOrDispatchOverrideType::Skip;
            }

            return DrawOrDispatchOverrideType::None;
        }

        if (original_shader_hashes.Contains(shader_hashes_SMAANeighborhoodBlending))
        {
            if (device_data.sr_type != SR::Type::None)
            {
                // DLSS requires an immediate context for execution!
                ASSERT_ONCE(native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE);

                auto* sr_instance_data = device_data.GetSRInstanceData();
                ASSERT_ONCE(sr_instance_data);

                // UAV0 should be the anti aliased out.
                ComPtr<ID3D11UnorderedAccessView> uav_out;
                native_device_context->CSGetUnorderedAccessViews(0, 1, uav_out.put());
                ComPtr<ID3D11Resource> resource_out;
                uav_out->GetResource(resource_out.put());

                // SRV0 should be the scene in linear color space.
                ComPtr<ID3D11ShaderResourceView> srv_scene;
                native_device_context->CSGetShaderResources(0, 1, srv_scene.put());
                ComPtr<ID3D11Resource> resource_scene;
                srv_scene->GetResource(resource_scene.put());

                // We don't need to set/call this every frame?
                SR::SettingsData settings_data;
                settings_data.output_width = device_data.output_resolution.x;
                settings_data.output_height = device_data.output_resolution.y;
                settings_data.render_width = device_data.render_resolution.x;
                settings_data.render_height = device_data.render_resolution.y;
                settings_data.dynamic_resolution = false;
                settings_data.hdr = true;
                settings_data.mvs_jittered = true;

                // MVs are in UV space so we need to scale them to screen space for DLSS.
                settings_data.mvs_x_scale = device_data.render_resolution.x;
                settings_data.mvs_y_scale = device_data.render_resolution.y;
                
                settings_data.render_preset = dlss_render_preset;
                settings_data.auto_exposure = true;

                sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                SR::SuperResolutionImpl::DrawData draw_data;
                draw_data.source_color = resource_scene.get();
                draw_data.output_color = resource_out.get();
                draw_data.motion_vectors = game_device_data.resource_mvs.get();
                draw_data.depth_buffer = game_device_data.resource_depth.get();

                // Jitters are in UV offsets so we need to scale them to pixel offsets for DLSS.
                draw_data.jitter_x = g_jitters.x * device_data.render_resolution.x;
                draw_data.jitter_y = g_jitters.y * device_data.render_resolution.y * -1.0f;

                draw_data.render_width = device_data.render_resolution.x;
                draw_data.render_height = device_data.render_resolution.y;

                sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);

                return DrawOrDispatchOverrideType::Replaced;
            }

            return DrawOrDispatchOverrideType::None;
        }
        
        return DrawOrDispatchOverrideType::None;
    }

    void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
    {
        auto& game_device_data = GetGameDeviceData(device_data);

        JitterUpdate(device_data.sr_type != SR::Type::None, device_data.render_resolution.x, device_data.render_resolution.y);
    }

    void PrintImGuiAbout() override
    {
        ImGui::Text("Batman Arkham Knight Luma mod - about and credits section", "");
    }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        Globals::SetGlobals(PROJECT_NAME, "Batman Arkham Knight Luma mod");
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

        #if DEVELOPMENT
        forced_shader_names.emplace(0x978BFB09, "Tonemap");
        forced_shader_names.emplace(0xFE8D7315, "Lightning");
        forced_shader_names.emplace(0x816CB5D7, "Lightning");
        forced_shader_names.emplace(0x067FF80F, "SMAAEdgeDetection");
        forced_shader_names.emplace(0x2F55F98E, "SMAABlendingWeightCalculation");
        forced_shader_names.emplace(0x42C0137E, "SMAANeighborhoodBlending");
        #endif

        game = new BatmanArkhamKnight();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}