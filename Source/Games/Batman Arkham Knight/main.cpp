
#define GAME_BATMAN_ARKHAM_KNIGHT 1

#include "../../../Shaders/Batman Arkham Knight/Includes/GameCBuffers.hlsl"
#include "..\..\Core\core.hpp"

// TODO: Fix this globaly? Define NOMINMAX before including windows.h.
#undef min
#undef max

namespace
{
    const ShaderHashesList shader_hashes_SMAAEdgeDetection = { .pixel_shaders = { 0x067FF80F } };
    const ShaderHashesList shader_hashes_SMAABlendingWeightCalculation = { .pixel_shaders = { 0x2F55F98E } };
    const ShaderHashesList shader_hashes_SMAANeighborhoodBlending = { .compute_shaders = { 0x42C0137E } };
    // Writes camera motion for every pixel of the velocity buffer, before the base pass draws object motion on top.
    // Replaced by "CameraVelocity_0x47811EB4.cs_5_0.hlsl", which reprojects with the matrix we provide.
    const ShaderHashesList shader_hashes_CameraVelocity = { .compute_shaders = { 0x47811EB4 } };

    // FViewInfo layout (Steam build, verified in the view copy constructor, the view init function and against the view cbuffer).
    constexpr size_t view_size_x_offset = 0x6C; // float
    constexpr size_t view_size_y_offset = 0x70; // float
    constexpr size_t view_projection_offset = 0x110; // FMatrix, row vectors (clip = pos * M)
    constexpr size_t view_secondary_projection_offset = 0x150; // FMatrix, second perspective projection built together with the main one
    constexpr size_t view_translated_view_projection_offset = 0x380; // FMatrix, translated world to clip (the view cbuffer one)
    constexpr size_t view_pre_view_translation_offset = 0x440; // float3, translated world = world + this (minus the view origin)
    constexpr size_t view_jitter_offset = 0x520; // float2, NDC offset ("UnicornJitter"), copied along with the view to the render thread

    // Written on the render thread, read on the game thread
    std::atomic<bool> g_jitter_enabled = false;
    std::atomic<uint32_t> g_render_width = 0;
    std::atomic<uint32_t> g_render_height = 0;
    // Game thread only
    uint32_t g_jitter_index = 0;
    // Render thread only
    uint32_t g_frame_index = 0;
    float2 g_frame_jitter_pixels = {};
    bool g_camera_velocity_rendered = false;
    bool g_camera_velocity_has_history = false;
    float4 g_reprojection_matrix[4] = {}; // Current jittered NDC + device depth to previous jittered clip space, for the camera velocity pass
    double g_last_translated_view_projection[4][4] = {};
    double g_last_pre_view_translation[3] = {};
    uint32_t g_last_camera_velocity_frame_index = 0;
    bool g_has_last_camera_velocity = false;

    using ViewJitterFunc = void(__fastcall*)(uint8_t* view);
    using CameraVelocityFunc = void(__fastcall*)(void* renderer, uint8_t* view);
    ViewJitterFunc g_original_view_jitter_func = nullptr;
    CameraVelocityFunc g_original_camera_velocity_func = nullptr;

    bool IsRenderResolutionView(const uint8_t* view)
    {
        const float size_x = *reinterpret_cast<const float*>(view + view_size_x_offset);
        const float size_y = *reinterpret_cast<const float*>(view + view_size_y_offset);
        return std::abs(size_x - float(g_render_width.load(std::memory_order_relaxed))) <= 0.5f && std::abs(size_y - float(g_render_height.load(std::memory_order_relaxed))) <= 0.5f;
    }

    // Row vectors: out = a * b
    void Multiply(const double a[4][4], const double b[4][4], double out[4][4])
    {
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c] + a[r][3] * b[3][c];
            }
        }
    }

    // Gauss-Jordan elimination with partial pivoting
    bool Invert(const double in[4][4], double out[4][4])
    {
        double m[4][8];
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                m[r][c] = in[r][c];
                m[r][c + 4] = r == c ? 1.0 : 0.0;
            }
        }
        for (int c = 0; c < 4; c++)
        {
            int pivot = c;
            for (int r = c + 1; r < 4; r++)
            {
                if (std::abs(m[r][c]) > std::abs(m[pivot][c]))
                {
                    pivot = r;
                }
            }
            if (m[pivot][c] == 0.0)
            {
                return false;
            }
            if (pivot != c)
            {
                for (int k = 0; k < 8; k++)
                {
                    std::swap(m[c][k], m[pivot][k]);
                }
            }
            const double inv_pivot = 1.0 / m[c][c];
            for (int k = 0; k < 8; k++)
            {
                m[c][k] *= inv_pivot;
            }
            for (int r = 0; r < 4; r++)
            {
                if (r != c && m[r][c] != 0.0)
                {
                    const double factor = m[r][c];
                    for (int k = 0; k < 8; k++)
                    {
                        m[r][k] -= factor * m[c][k];
                    }
                }
            }
        }
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                out[r][c] = m[r][c + 4];
            }
        }
        return true;
    }

    std::vector<System::BytePattern> ParsePattern(std::string_view text)
    {
        std::vector<System::BytePattern> pattern;
        for (size_t i = 0; i < text.size(); i += 3)
        {
            pattern.push_back(text[i] == '?' ? System::ANY : System::BytePattern(unsigned(std::stoul(std::string(text.substr(i, 2)), nullptr, 16))));
        }
        return pattern;
    }

    // Returns the start of the only match of the pattern in the executable, or null if there are none or several
    std::byte* FindUniquePattern(const char* name, std::string_view pattern)
    {
        const std::vector<std::byte*> matches = System::ScanModuleForPattern(ParsePattern(pattern));
        if (matches.size() != 1)
        {
            reshade::log::message(reshade::log::level::warning, (std::string("Batman Arkham Knight Luma: pattern \"") + name + "\" matched " + std::to_string(matches.size()) + " times, patch skipped").c_str());
            return nullptr;
        }
        return matches[0];
    }

    void NopBytes(std::byte* address, size_t size)
    {
        const std::vector<uint8_t> nops(size, 0x90);
        System::PatchMemory(address, nops.data(), size);
    }

    // Redirects the "call rel32" at "call_site" to "target" through an absolute jump stub allocated within reach,
    // returning the original callee (or null on failure, in which case nothing was changed).
    void* RedirectCall(std::byte* call_site, void* target)
    {
        if (call_site[0] != std::byte{ 0xE8 })
        {
            return nullptr;
        }
        int32_t original_rel;
        std::memcpy(&original_rel, call_site + 1, sizeof(original_rel));
        void* original_callee = call_site + 5 + original_rel;

        uint8_t* stub = static_cast<uint8_t*>(System::VirtualAllocNear(call_site, 14));
        if (!stub)
        {
            return nullptr;
        }
        const int64_t stub_rel = reinterpret_cast<int64_t>(stub) - reinterpret_cast<int64_t>(call_site + 5);
        if (stub_rel < INT32_MIN || stub_rel > INT32_MAX)
        {
            VirtualFree(stub, 0, MEM_RELEASE);
            return nullptr;
        }
        // jmp qword ptr [rip+0] followed by the absolute target
        const uint8_t jmp_abs[6] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        std::memcpy(stub, jmp_abs, sizeof(jmp_abs));
        std::memcpy(stub + sizeof(jmp_abs), &target, sizeof(target));
        FlushInstructionCache(GetCurrentProcess(), stub, 14);

        const int32_t new_rel = static_cast<int32_t>(stub_rel);
        System::PatchMemory(call_site + 1, &new_rel, sizeof(new_rel));
        return original_callee;
    }

    // Game thread: the engine resets the view jitter (it's only used by its disabled checkerboard upscaler) right before
    // deriving all the view matrices from the projection, so we offset the projection here (only for perspective views that render at our resolution, so not shadows or scene captures).
    // The NDC offset is also stored in the view's own jitter field, which is copied along with the view to the render thread.
    // This runs twice per frame for the main view: once for the game thread view, and once for the render thread view constructed from it (with the already jittered projection).
    void __fastcall HookedViewJitter(uint8_t* view)
    {
        g_original_view_jitter_func(view);

        if (!g_jitter_enabled.load(std::memory_order_relaxed) || !IsRenderResolutionView(view))
        {
            return;
        }
        // M[2][3] is 1 for perspective projections (w = view z), so M[2][0..1] offsets x/y by a constant amount after the perspective divide
        float* projection = reinterpret_cast<float*>(view + view_projection_offset);
        if (projection[11] != 1.0f)
        {
            return;
        }

        float* view_jitter = reinterpret_cast<float*>(view + view_jitter_offset);
        // The game's perspective projections have exactly 0 there, and Halton never gives 0 on both axes, so a non zero offset is our own jitter (from the view this one was constructed from)
        if (projection[8] != 0.0f || projection[9] != 0.0f)
        {
            view_jitter[0] = projection[8];
            view_jitter[1] = projection[9];
            return;
        }

        const float size_x = *reinterpret_cast<const float*>(view + view_size_x_offset);
        const float size_y = *reinterpret_cast<const float*>(view + view_size_y_offset);
        const unsigned int index = g_jitter_index++ % SR::GetDefaultJitterPhases();
        const float jitter_ndc_x = SR::HaltonSequence(index, 2) * 2.0f / size_x;
        const float jitter_ndc_y = -SR::HaltonSequence(index, 3) * 2.0f / size_y;

        projection[8] = jitter_ndc_x;
        projection[9] = jitter_ndc_y;
        float* secondary_projection = reinterpret_cast<float*>(view + view_secondary_projection_offset);
        if (secondary_projection[11] == 1.0f)
        {
            secondary_projection[8] += jitter_ndc_x;
            secondary_projection[9] += jitter_ndc_y;
        }
        view_jitter[0] = jitter_ndc_x;
        view_jitter[1] = jitter_ndc_y;
    }

    // Render thread, main view: builds the camera velocity reprojection matrix (current jittered NDC + depth -> previous jittered clip space).
    // The engine's previous view projection is exactly last frame's translated view projection, relative to last frame's view origin,
    // so this moves current translated world positions by how much the origin moved since. Built in double precision, as positions are relative to the camera.
    void UpdateReprojectionMatrix(const uint8_t* view)
    {
        const float* translated_view_projection_f = reinterpret_cast<const float*>(view + view_translated_view_projection_offset);
        const float* pre_view_translation_f = reinterpret_cast<const float*>(view + view_pre_view_translation_offset);
        double translated_view_projection[4][4];
        for (int i = 0; i < 16; i++)
        {
            translated_view_projection[i / 4][i % 4] = translated_view_projection_f[i];
        }
        const double pre_view_translation[3] = { pre_view_translation_f[0], pre_view_translation_f[1], pre_view_translation_f[2] };

        double inverse_translated_view_projection[4][4];
        g_camera_velocity_has_history = g_has_last_camera_velocity && g_last_camera_velocity_frame_index + 1 == g_frame_index && Invert(translated_view_projection, inverse_translated_view_projection);
        if (g_camera_velocity_has_history)
        {
            double translation[4][4] = { { 1.0, 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0, 0.0 }, { 0.0, 0.0, 0.0, 1.0 } };
            for (int i = 0; i < 3; i++)
            {
                translation[3][i] = g_last_pre_view_translation[i] - pre_view_translation[i];
            }
            double to_previous_translated_world[4][4];
            double reprojection[4][4];
            Multiply(inverse_translated_view_projection, translation, to_previous_translated_world);
            Multiply(to_previous_translated_world, g_last_translated_view_projection, reprojection);
            for (int r = 0; r < 4; r++)
            {
                g_reprojection_matrix[r] = float4{ float(reprojection[r][0]), float(reprojection[r][1]), float(reprojection[r][2]), float(reprojection[r][3]) };
            }
        }
        else
        {
            // No motion (DLSS history is reset this frame)
            g_reprojection_matrix[0] = float4{ 1.0f, 0.0f, 0.0f, 0.0f };
            g_reprojection_matrix[1] = float4{ 0.0f, 1.0f, 0.0f, 0.0f };
            g_reprojection_matrix[2] = float4{ 0.0f, 0.0f, 1.0f, 0.0f };
            g_reprojection_matrix[3] = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
        }

        std::memcpy(g_last_translated_view_projection, translated_view_projection, sizeof(g_last_translated_view_projection));
        std::memcpy(g_last_pre_view_translation, pre_view_translation, sizeof(g_last_pre_view_translation));
        g_last_camera_velocity_frame_index = g_frame_index;
        g_has_last_camera_velocity = true;
    }

    // Render thread: runs once per frame for the main view (we force it), before the base pass and SMAA.
    // The matrix needs to be ready before the pass (our replaced shader reads it from the Luma instance data).
    void __fastcall HookedCameraVelocity(void* renderer, uint8_t* view)
    {
        const bool is_main_view = IsRenderResolutionView(view);
        if (is_main_view)
        {
            UpdateReprojectionMatrix(view);
        }

        g_original_camera_velocity_func(renderer, view);

        if (is_main_view)
        {
            const float size_x = *reinterpret_cast<const float*>(view + view_size_x_offset);
            const float size_y = *reinterpret_cast<const float*>(view + view_size_y_offset);
            const float* view_jitter = reinterpret_cast<const float*>(view + view_jitter_offset);
            g_frame_jitter_pixels.x = view_jitter[0] * size_x * 0.5f;
            g_frame_jitter_pixels.y = view_jitter[1] * size_y * -0.5f;
            g_camera_velocity_rendered = true;
        }
    }

    // The engine only computes velocities (camera and objects) when motion blur is enabled, and skips the camera velocity pass when the camera barely moved.
    // These patches make it always compute them, while the motion blur effect itself still follows the user setting.
    void PatchVelocityAndJitter()
    {
        // Motion blur post process proxy creation: don't require the motion blur setting for "requires velocities" (je -> nop)
        if (std::byte* match = FindUniquePattern("MotionBlurRequiresVelocities", "39 3D ?? ?? ?? ?? 74 1E 39 3D ?? ?? ?? ?? 75 16 39 3D ?? ?? ?? ?? 75 0E B8 01 00 00 00"))
        {
            NopBytes(match + 6, 2);
        }
        // Velocity stage (camera velocity pass) motion blur setting check (je rel32 -> nop)
        if (std::byte* match = FindUniquePattern("VelocityStageGate", "83 3D ?? ?? ?? ?? 00 49 8B D9 48 8B F2 0F 84 ?? ?? ?? ?? F7 41 28 00 04 00 00"))
        {
            NopBytes(match + 13, 6);
        }
        // Velocity stage end motion blur setting check (je -> nop)
        if (std::byte* match = FindUniquePattern("VelocityStageEndGate", "83 3D ?? ?? ?? ?? 00 49 8B F1 48 8B EA 48 8B F9 74 ?? F7 41 28 00 04 00 00"))
        {
            NopBytes(match + 16, 2);
        }
        // Base pass object velocity motion blur setting check (je -> nop)
        if (std::byte* match = FindUniquePattern("ObjectVelocityGate", "83 3D ?? ?? ?? ?? 00 74 ?? F7 43 28 00 04 00 00"))
        {
            NopBytes(match + 7, 2);
        }
        // Camera velocity pass: skip the "camera didn't move enough" early out (jne -> nop) and hook the pass itself
        if (std::byte* match = FindUniquePattern("CameraVelocityPass", "E8 ?? ?? ?? ?? 85 C0 75 ?? 48 8B D3 48 8B CE E8 ?? ?? ?? ?? E9"))
        {
            NopBytes(match + 7, 2);
            g_original_camera_velocity_func = reinterpret_cast<CameraVelocityFunc>(RedirectCall(match + 15, reinterpret_cast<void*>(&HookedCameraVelocity)));
        }
        // View init: hook the view jitter setup, to jitter the projection before all the other view matrices are derived from it
        if (std::byte* match = FindUniquePattern("ViewJitter", "E8 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 45 33 E4 89 87 38 05 00 00"))
        {
            g_original_view_jitter_func = reinterpret_cast<ViewJitterFunc>(RedirectCall(match, reinterpret_cast<void*>(&HookedViewJitter)));
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
        PatchVelocityAndJitter();

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

        if (original_shader_hashes.Contains(shader_hashes_CameraVelocity))
        {
            // UAV0 is the velocity buffer (the base pass then draws moving objects on top of it).
            // It's not necessarily cleared anymore when this pass runs, so grab it here too.
            ComPtr<ID3D11UnorderedAccessView> uav_mvs;
            native_device_context->CSGetUnorderedAccessViews(0, 1, uav_mvs.put());
            if (uav_mvs.get())
            {
                uav_mvs->GetResource(game_device_data.resource_mvs.put());
            }
            return DrawOrDispatchOverrideType::None;
        }

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

                // MVs are "previous UV - current UV" (camera velocity matches thanks to the replaced shader), DLSS wants pixels.
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

                // Jitter the frame was rendered with (pixels), read back from its view on the render thread.
                draw_data.jitter_x = g_frame_jitter_pixels.x;
                draw_data.jitter_y = g_frame_jitter_pixels.y;
                // The camera velocity pass is skipped by the engine on camera cuts and similar, and the velocities need last frame's camera to be valid.
                draw_data.reset = !g_camera_velocity_rendered || !g_camera_velocity_has_history;

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
        g_jitter_enabled.store(device_data.sr_type != SR::Type::None && !device_data.sr_suppressed, std::memory_order_relaxed);
        g_render_width.store(device_data.render_resolution.x, std::memory_order_relaxed);
        g_render_height.store(device_data.render_resolution.y, std::memory_order_relaxed);
        g_camera_velocity_rendered = false;
        g_frame_index++;

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

    void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
    {
        std::memcpy(data.GameData.ReprojectionMatrix, g_reprojection_matrix, sizeof(g_reprojection_matrix));
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

        enable_samplers_upgrade = true;

        #if DEVELOPMENT
        forced_shader_names.emplace(0x978BFB09, "Tonemap");
        forced_shader_names.emplace(0xFE8D7315, "Lightning");
        forced_shader_names.emplace(0x816CB5D7, "Lightning");
        forced_shader_names.emplace(0x067FF80F, "SMAAEdgeDetection");
        forced_shader_names.emplace(0x2F55F98E, "SMAABlendingWeightCalculation");
        forced_shader_names.emplace(0x42C0137E, "SMAANeighborhoodBlending");
        #endif

        // TODO: Remove this later!
        Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;

        game = new BatmanArkhamKnight();
    }

    CoreMain(hModule, ul_reason_for_call, lpReserved);

    return TRUE;
}