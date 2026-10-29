#define GAME_SAINTS_ROW_THE_THIRD_REMASTERED 1

#define GEOMETRY_SHADER_SUPPORT 0
#define DISABLE_AUTO_DEBUGGER 1
// SMAA replaces the game's FXAA (see DrawSMAAInPlaceOfFXAA)
#define ENABLE_SMAA 1

#include "..\..\Core\core.hpp"
// Compiled into this project (see the .vcxproj), for "InstallCameraHooks"
#include "..\..\External\reshade\deps\minhook\include\MinHook.h"
// SMAA's area texture without the U-shape smoothing (see Luma_SMAA_impl.hlsl and "DrawSMAAInPlaceOfFXAA")
#include "SMAA_AreaTex_SharpU.h"

namespace
{
   // SRTTR.exe code and data the jitter patches and SR use. The Steam (PE TimeDateStamp 0x60EE85F4), GOG (0x608DDEF7) and Epic (0x617A5EF9) builds
   // have the same camera projection build function (see "CameraBuildDetour()") at different addresses, so it's found by signature and everything else
   // is an offset into it or the target of one of its RIP relative operands. "0x14..." addresses in comments are from the Steam build.
   struct GameAddresses
   {
      uint8_t* camera_build = nullptr;
      uint8_t* camera_counter_increment = nullptr; // inc [rcx+0x59C]
      uint8_t* jitter_pattern_8x = nullptr;
      uint8_t* jitter_index_mask_high_bytes[2] = {};
      int32_t* jitter_mode = nullptr;
      const int32_t* jitter_gate = nullptr;
      bool (*taa_gate)() = nullptr;
   };

   // The head of the Halton lookup that replaces the 8x jitter pattern code (see "InstallHaltonJitterPattern()"): mov eax, [rsi+0x59C];
   // and eax, 15; lea rdx, [rip+0x10]
   constexpr uint8_t halton_stub_prefix[] = {0x8B, 0x86, 0x9C, 0x05, 0x00, 0x00, 0x83, 0xE0, 0x0F, 0x48, 0x8D, 0x15, 0x10, 0x00, 0x00, 0x00};

   // All null for an unknown build; only scanned once
   const GameAddresses& GetGameAddresses()
   {
      static const GameAddresses addresses = []()
      {
         GameAddresses result;
         // mov rax, rsp; push rbx; push rbp; push rsi; push rdi; sub rsp, 0xA8; mov edx, [rip+?]; lea rdi, [rip+?];
         // movaps [rax-0x38], xmm6; mov rsi, rcx; movaps [rax-0x48], xmm7; movaps [rax-0x58], xmm8
         // clang-format off
         constexpr std::array<System::BytePattern, 43> camera_build_pattern = {{
            0x48, 0x8B, 0xC4, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x8B, 0x15, System::ANY, System::ANY, System::ANY, System::ANY, 0x48, 0x8D,
            0x3D, System::ANY, System::ANY, System::ANY, System::ANY, 0x0F, 0x29, 0x70, 0xC8, 0x48, 0x8B, 0xF1, 0x0F, 0x29, 0x78, 0xB8, 0x44, 0x0F, 0x29, 0x40, 0xA8
         }};
         // clang-format on
         const std::vector<std::byte*> matches = System::ScanModuleForPattern(camera_build_pattern);
         if (matches.size() != 1)
            return result;
         uint8_t* function = reinterpret_cast<uint8_t*>(matches[0]);
         // call taa_gate at +0x11B, cmp [rip+?], -1 (jitter_gate) at +0x128, mov ecx, [rip+?] (jitter_mode) at +0x146
         if (function[0x11B] != 0xE8 || function[0x128] != 0x83 || function[0x129] != 0x3D || function[0x12E] != 0xFF || function[0x146] != 0x8B || function[0x147] != 0x0D)
            return result;
         // The 8x pattern block: mov eax, [rsi+0x59C]; and eax, 0x80000007 ... ja (+0x17) to the shared scaling code, or the Halton lookup a
         // previous load of the addon in this process installed there
         constexpr uint8_t jitter_pattern_8x_start[] = {0x8B, 0x86, 0x9C, 0x05, 0x00, 0x00, 0x25, 0x07, 0x00, 0x00, 0x80};
         uint8_t* jitter_pattern_8x = function + 0x175;
         const bool jitter_pattern_8x_original = std::memcmp(jitter_pattern_8x, jitter_pattern_8x_start, sizeof(jitter_pattern_8x_start)) == 0 && jitter_pattern_8x[0x17] == 0x0F && jitter_pattern_8x[0x18] == 0x87;
         if (!jitter_pattern_8x_original && std::memcmp(jitter_pattern_8x, halton_stub_prefix, sizeof(halton_stub_prefix)) != 0)
            return result;
         // The 2x and 4x mask high bytes, 0x00 if a previous load of the addon already patched them
         uint8_t* jitter_index_mask_high_bytes[] = {function + 0x2C6, function + 0x252};
         for (const uint8_t* byte : jitter_index_mask_high_bytes)
         {
            if (*byte != 0x80 && *byte != 0x00)
               return result;
         }
         // Target of the rel32 at "offset", in an instruction that has "trailing_bytes" after it
         const auto rip_target = [function](size_t offset, size_t trailing_bytes)
         { return function + offset + sizeof(int32_t) + trailing_bytes + *reinterpret_cast<const int32_t*>(function + offset); };
         result.camera_build = function;
         result.camera_counter_increment = function - 0x2D0;
         result.jitter_pattern_8x = jitter_pattern_8x;
         std::copy(std::begin(jitter_index_mask_high_bytes), std::end(jitter_index_mask_high_bytes), result.jitter_index_mask_high_bytes);
         result.taa_gate = reinterpret_cast<bool (*)()>(rip_target(0x11C, 0));
         result.jitter_gate = reinterpret_cast<const int32_t*>(rip_target(0x12A, 1));
         result.jitter_mode = reinterpret_cast<int32_t*>(rip_target(0x148, 0));
         return result;
      }();
      return addresses;
   }

   // The size of the texture behind a view (0x0 without one)
   uint2 GetViewTextureSize(ID3D11View* view)
   {
      D3D11_TEXTURE2D_DESC desc = {};
      if (view)
      {
         com_ptr<ID3D11Resource> resource;
         view->GetResource(&resource);
         if (com_ptr<ID3D11Texture2D> texture; SUCCEEDED(resource->QueryInterface(&texture)))
         {
            texture->GetDesc(&desc);
         }
      }
      return uint2{desc.Width, desc.Height};
   }

   // Whether all the named Luma native shaders are compiled (they load asynchronously and reload live in development). Looked up with
   // "find": "operator[]" would insert on a miss, mutating a map Core's draw helpers read. Callers hold "s_mutex_shader_objects".
   template <typename ShaderMap, typename... Names>
   bool HasShaders(const ShaderMap& shaders, Names... names)
   {
      const auto has_shader = [&shaders](uint32_t name)
      {
         const auto it = shaders.find(name);
         return it != shaders.end() && it->second;
      };
      return (has_shader(names) && ...);
   }

   // Persisted setting widgets: the control, its ini save, a tooltip (also shown while disabled) and Core's reset button
   void DrawSettingSlider(const char* label, const char* key, float* value, float default_value, float min_value, float max_value, const char* tooltip)
   {
      ImGui::SliderFloat(label, value, min_value, max_value, "%.3f");
      // Saved once the edit ends, not on every drag step
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
         reshade::set_config_value(nullptr, NAME, key, *value);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("%s", tooltip);
      }
      DrawResetButton(*value, default_value, key);
   }

   // A bool, or a 0/1 float (a cbuffer field), shown as a checkbox. "forced": shown on and disabled while another feature does its job,
   // keeping the saved choice.
   template <typename T>
   void DrawSettingCheckbox(const char* label, const char* key, T* value, std::type_identity_t<T> default_value, const char* tooltip, bool forced = false)
   {
      bool shown = forced || float(*value) > 0.5f;
      ImGui::BeginDisabled(forced);
      if (ImGui::Checkbox(label, &shown))
      {
         *value = T(shown);
         reshade::set_config_value(nullptr, NAME, key, *value);
      }
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("%s", tooltip);
      }
      if (forced)
      {
         DrawResetButton(shown, shown, key); // Only its spacer, the saved choice can't be reset from here
      }
      else
      {
         DrawResetButton(*value, default_value, key);
      }
   }

   bool game_patches_applied = false;
   bool minhook_initialized = false; // By "InstallCameraHooks", for the uninitialization at unload

   // TAA jitter pattern (int, .data, static 1): 0 none, 1 D3D 2x MSAA, 2 4x, 3 8x (switch at 0x14089B886, indexed by the camera's +0x59C counter).
   // The counter starts near -2^24, and the pattern index is taken with a signed modulo ("and reg, 0x8000000N" + sign fixup), so the index is 0 or
   // negative and only entry 0 of each pattern is ever used, once per pattern length (every other frame for the vanilla 2x pattern).
   constexpr int32_t vanilla_jitter_mode = 1;
   constexpr int32_t halton_jitter_mode = 3; // SR, see "InstallHaltonJitterPattern()"

   // "Fix Native TAA Jitter": clearing the sign bit of the 2x and 4x masks (their high bytes, 0x14089BA06 and 0x14089B992) makes the index "counter & N",
   // so every offset of the pattern cycles. The game's TAA is tuned for its 2x pattern: the 8x Halton one of SR makes it flicker.
   bool g_fix_native_taa_jitter = true;

   // Cheap when the bytes already match, so it can run every frame
   void SetJitterIndexFix(bool enable)
   {
      const uint8_t mask_high_byte = (enable ? 0x00 : 0x80);
      for (uint8_t* byte : GetGameAddresses().jitter_index_mask_high_bytes)
      {
         if (byte && *byte != mask_high_byte)
         {
            System::PatchMemory(byte, &mask_high_byte, 1);
         }
      }
   }

#if ENABLE_SR
   // The 8x pattern code (only reached in jitter mode 3, from 0x14089B8B5 up to the 4x one at 0x14089B987) is replaced with a Halton (2, 3) table lookup,
   // indexed by the camera counter (& 15, which is fine for its negative values), then jumps to the shared code that scales the offset by 0.125 / resolution.
   // Offsets are in 1/16 pixels like the game's patterns (x right, y up). SR reads the resulting projection jitter back from the camera.
   constexpr size_t jitter_pattern_8x_size = 0xD2;
   bool halton_jitter_mode_applied = false; // The game's current state: the mode is process wide, not per device

   // Installed once, while the game still uses the 2x pattern, as the other camera thread might run the 8x one
   void InstallHaltonJitterPattern()
   {
      uint8_t* block = GetGameAddresses().jitter_pattern_8x;
      // Already installed by a previous load of the addon in this process: the "ja" operand read below is table data by now
      if (!block || std::memcmp(block, halton_stub_prefix, sizeof(halton_stub_prefix)) == 0)
         return;
      std::array<uint8_t, jitter_pattern_8x_size> bytes;
      std::memcpy(bytes.data(), block, jitter_pattern_8x_size); // The rest of the block stays original
      // clang-format off
      constexpr uint8_t code[] = {
         0x8B, 0x86, 0x9C, 0x05, 0x00, 0x00, // mov eax, [rsi+0x59C]
         0x83, 0xE0, 0x0F,                   // and eax, 15
         0x48, 0x8D, 0x15, 0x10, 0x00, 0x00, 0x00, // lea rdx, [rip+0x10] (the table, right after this code)
         0xF3, 0x0F, 0x10, 0x0C, 0xC2,       // movss xmm1, [rdx+rax*8] (x)
         0xF3, 0x0F, 0x10, 0x74, 0xC2, 0x04, // movss xmm6, [rdx+rax*8+4] (y)
         0xE9, 0x00, 0x00, 0x00, 0x00,       // jmp to the block's "ja" target (0x14089BA3B), set below
      };
      // clang-format on
      static_assert(std::equal(std::begin(halton_stub_prefix), std::end(halton_stub_prefix), std::begin(code)));
      std::memcpy(bytes.data(), code, sizeof(code));
      const uint8_t* ja_target = block + 0x1D + *reinterpret_cast<const int32_t*>(block + 0x19);
      const int32_t jmp_offset = static_cast<int32_t>(ja_target - (block + sizeof(code)));
      std::memcpy(bytes.data() + sizeof(code) - sizeof(jmp_offset), &jmp_offset, sizeof(jmp_offset));
      for (unsigned int i = 0; i < 16; i++)
      {
         const unsigned int phase = i % SR::GetDefaultJitterPhases();
         const float offset[2] = {SR::HaltonSequence(phase, 2) * 16.f, SR::HaltonSequence(phase, 3) * -16.f};
         std::memcpy(bytes.data() + sizeof(code) + i * sizeof(offset), offset, sizeof(offset));
      }
      System::PatchMemory(block, bytes.data(), jitter_pattern_8x_size);
   }
#endif

   // Camera projection build (camera in rcx), called many times per frame on the thread that also issues the frame's draws.
   // It writes the projection at camera+0xE0 (row major): row 1 y scale (cot(fov_y/2)) at +0xF4, row 2 jitter (NDC) at +0x100/+0x104.
   // Near and far are at +0x530/+0x534. The jitter pattern index is the camera's +0x59C counter.
   // Only jitters if TAA passes the gate (0x140908A90) and [0x1411DCF40] == -1.
   using CameraBuildFunc = uint64_t (*)(uint8_t* camera, void* rdx, void* r8, void* r9);
   CameraBuildFunc camera_build_original = nullptr;

   // The values of the last camera built on this thread, copied right after the build so the camera can't be read after being freed
   struct CameraData
   {
      float jitter_x = 0.f; // NDC
      float jitter_y = 0.f; // NDC
      float projection_y_scale = 0.f;
      float near_plane = 0.f;
      float far_plane = 0.f;
      bool valid = false;
   };
   thread_local CameraData last_built_camera;

#if DEVELOPMENT
   // "Log Camera Builds": traces the camera builds and counter increments
   std::atomic<int> camera_log_calls_left = 0;
   using CameraCounterIncrementFunc = void (*)(uint8_t* camera);
   CameraCounterIncrementFunc camera_counter_increment_original = nullptr;

   void CameraCounterIncrementDetour(uint8_t* camera)
   {
      if (camera_log_calls_left > 0)
      {
         char line[128];
         std::snprintf(line, sizeof(line), "[SRTTR JIT] increment tid=%lu camera=%p counter=%d", GetCurrentThreadId(), camera, *reinterpret_cast<const int32_t*>(camera + 0x59C));
         reshade::log::message(reshade::log::level::info, line);
      }
      camera_counter_increment_original(camera);
   }
#endif

   uint64_t CameraBuildDetour(uint8_t* camera, void* rdx, void* r8, void* r9)
   {
#if DEVELOPMENT
      const bool log = camera_log_calls_left > 0 && camera_log_calls_left.fetch_sub(1) > 0;
      const int32_t counter = log ? *reinterpret_cast<const int32_t*>(camera + 0x59C) : 0;
      const bool taa_gate = log && GetGameAddresses().taa_gate();
      const int32_t jitter_gate = log ? *GetGameAddresses().jitter_gate : 0;
#endif
      const uint64_t result = camera_build_original(camera, rdx, r8, r9);
      last_built_camera.jitter_x = *reinterpret_cast<const float*>(camera + 0x100);
      last_built_camera.jitter_y = *reinterpret_cast<const float*>(camera + 0x104);
      last_built_camera.projection_y_scale = *reinterpret_cast<const float*>(camera + 0xF4);
      last_built_camera.near_plane = *reinterpret_cast<const float*>(camera + 0x530);
      last_built_camera.far_plane = *reinterpret_cast<const float*>(camera + 0x534);
      last_built_camera.valid = true;
#if DEVELOPMENT
      if (log)
      {
         const float* row_2 = reinterpret_cast<const float*>(camera + 0x100);
         char line[256];
         std::snprintf(line, sizeof(line), "[SRTTR JIT] build tid=%lu camera=%p counter=%d taa_gate=%d jitter_gate=%d row2=%.9g,%.9g,%.9g,%.9g", GetCurrentThreadId(), camera, counter, int(taa_gate), jitter_gate, row_2[0], row_2[1], row_2[2], row_2[3]);
         reshade::log::message(reshade::log::level::info, line);
      }
#endif
      return result;
   }

   void InstallCameraHooks()
   {
      uint8_t* camera_build = GetGameAddresses().camera_build;
      if (!camera_build || MH_Initialize() != MH_OK)
         return;
      minhook_initialized = true;
      if (MH_CreateHook(camera_build, reinterpret_cast<void*>(&CameraBuildDetour), reinterpret_cast<void**>(&camera_build_original)) == MH_OK)
      {
         MH_EnableHook(camera_build);
      }
#if DEVELOPMENT
      uint8_t* camera_counter_increment = GetGameAddresses().camera_counter_increment;
      constexpr uint8_t camera_counter_increment_prologue[] = {0xFF, 0x81, 0x9C, 0x05, 0x00, 0x00}; // inc dword ptr [rcx+0x59C]
      if (std::memcmp(camera_counter_increment, camera_counter_increment_prologue, sizeof(camera_counter_increment_prologue)) == 0 && MH_CreateHook(camera_counter_increment, reinterpret_cast<void*>(&CameraCounterIncrementDetour), reinterpret_cast<void**>(&camera_counter_increment_original)) == MH_OK)
      {
         MH_EnableHook(camera_counter_increment);
      }
#endif
   }

   // taa CS perms (RGB, YCoCg, and their exposure normalized versions): t0 color, t1 depth, t3 G-buffer motion vectors, u0 color output, cb10 TAA_PARAMS
   constexpr uint32_t taa_hashes[] = {0xAB470526, 0x629C161F, 0x8E309763, 0x7D190953};

   // hdr_filter compose perms, the last draw of the frame (scene and GUI layer onto the swapchain). Every HDR_DISPLAY one is replaced by the SDR perm's math,
   // so the game's HDR setting does not change the output. The first ones compose the scene, the GUI only ones run in menus (the SDR GUI only one,
   // 0xA283B6FB, stays vanilla).
   constexpr uint32_t compose_hashes[] = {0xFCCD77CD, 0xADB2056B, 0xEB9D7036, 0x50DC2D70, 0x7083C926, 0xCE7FF710, 0xA11A22A3, 0x681958CA, 0x3D126636};
   constexpr uint32_t compose_gui_hashes[] = {0x79D0B6FF, 0x8DFF00F4, 0x3EA6C5A9, 0x1AD38FF6, 0xDEEDDD60, 0x281056F7, 0x818B5759, 0xC165ACD1};

   // hdr_filter tonemap CS perms (LUT and no LUT, with and without luminance output), run in every frame with a scene
   constexpr uint32_t tonemap_hashes[] = {0x941A9154, 0x835784B0, 0xAB466B4A, 0xFEDD50B7};

   constexpr uint32_t sr_inputs_shader_hash = CompileTimeStringHash("SR Inputs");

   // pbr_fxaa, the game's only FXAA pass (in-game Anti-Aliasing = FXAA), replaced by SMAA: t0 = the tonemap output (gamma), RTV = a separate
   // fp16 texture that compose reads
   constexpr uint32_t fxaa_pixel_shader_hash = 0xD928AE8D;
   // ambient, the pass that applies the SSAO: its t0 is the scene depth (R24), which is no longer bound at the FXAA draw, so it's kept for
   // SMAA's predication
   constexpr uint32_t ambient_hash = 0xFD45DCA7;
   // rl_prim_2d_bink_s_01, the Bink video: fullscreen movies draw it straight into the swapchain (the frame's only draw), where its
   // replacement adds AutoHDR. Menu backgrounds draw it into the RGBA8 GUI layer, which can't hold it, so they stay vanilla.
   constexpr uint32_t video_hash = 0xE85564EB;
   constexpr uint32_t smaa_predication_shader_hash = CompileTimeStringHash("SRTTR SMAA Predication CS");
   bool g_smaa_enable = true;
#if DEVELOPMENT
   bool g_smaa_predication = true;
   int g_smaa_debug_view = 0; // 0 off, 1 edges, 2 predication
#else
   constexpr bool g_smaa_predication = true;
#endif

   // ssao_miniengine (MiniEngine SSAO), every quality level's passes: prepare 1/2, interleaved and full-res renders, and the blur-upsample perms.
   // Only the ambient reads their final output (its t2), which XeGTAO writes instead, so they are skipped. Prepare 1 runs first at any level
   // but Off, which only runs a linearize (0xCB8306F1) and clears the SSAO to 1 once.
   constexpr uint32_t ssao_prepare_1_hash = 0x2F4B251B;
   constexpr uint32_t ssao_chain_hashes[] = {ssao_prepare_1_hash, 0xAC38984B, 0x1DEB634A, 0xDE09F597, 0x23EF0DBA, 0x7378361E, 0x07B179C3, 0x7281BC27};
   constexpr UINT ambient_params_cb_slot = 10; // AMBIENT_PARAMS, rebound PS -> CS for XeGTAO
   // XeGTAO passes (Luma_SRTTR_XeGTAO.hlsl); the two denoisers differ only by XE_GTAO_FINAL_APPLY
   constexpr uint32_t gtao_prefilter_shader_hash = CompileTimeStringHash("SRTTR XeGTAO Prefilter Depths CS");
   constexpr uint32_t gtao_main_shader_hash = CompileTimeStringHash("SRTTR XeGTAO Main Pass CS");
   constexpr uint32_t gtao_denoise_1_shader_hash = CompileTimeStringHash("SRTTR XeGTAO Denoise Pass 1 CS");
   constexpr uint32_t gtao_denoise_2_shader_hash = CompileTimeStringHash("SRTTR XeGTAO Denoise Pass 2 CS");
   bool g_gtao_enable = true;
   // SMAA's, XeGTAO's and the SR inputs' scratch goes after this many presents without them (~5 s at 120 fps): the in-game Anti-Aliasing runs
   // either SMAA or SR, the in-game SSAO can be off, and menus and loading screens run none. Recreated on their next run.
   constexpr uint32_t idle_release_frames = 600;
   // Calibration knobs, development builds tune them (not persisted)
#if DEVELOPMENT
   float g_gtao_final_value_power = 1.4f; // With EFFECT_RADIUS 0.4 (see Luma_SRTTR_XeGTAO.hlsl for how it compares to the vanilla SSAO)
   float g_gtao_radius_override = 0.f;    // > 0 overrides the shader's EFFECT_RADIUS (metres)
   int g_gtao_debug_view = 0;             // 0 off, 1 depth gradient, 2 normals, 3 AO x8, 4 edges
#else
   constexpr float g_gtao_final_value_power = 1.4f;
   constexpr float g_gtao_radius_override = 0.f;
   constexpr int g_gtao_debug_view = 0;
#endif

   // The four XeGTAO passes are compiled. The caller holds s_mutex_shader_objects.
   bool HasXeGTAOShaders(const DeviceData& device_data)
   {
      return HasShaders(device_data.native_compute_shaders, gtao_prefilter_shader_hash, gtao_main_shader_hash, gtao_denoise_1_shader_hash, gtao_denoise_2_shader_hash);
   }

   // A 2D texture with a UAV and, if asked, an SRV (and the texture itself). The outputs are only written when everything was created.
   // com_ptr overloads "&" (it yields the raw out pointer), so callers pass the members with std::addressof.
   bool CreateTextureWithViews(ID3D11Device* native_device, const D3D11_TEXTURE2D_DESC& desc, com_ptr<ID3D11UnorderedAccessView>* uav, com_ptr<ID3D11ShaderResourceView>* srv = nullptr, com_ptr<ID3D11Texture2D>* texture = nullptr)
   {
      com_ptr<ID3D11Texture2D> new_texture;
      com_ptr<ID3D11UnorderedAccessView> new_uav;
      com_ptr<ID3D11ShaderResourceView> new_srv;
      if (FAILED(native_device->CreateTexture2D(&desc, nullptr, &new_texture)) || FAILED(native_device->CreateUnorderedAccessView(new_texture.get(), nullptr, &new_uav)) || (srv && FAILED(native_device->CreateShaderResourceView(new_texture.get(), nullptr, &new_srv))))
         return false;
      *uav = new_uav;
      if (srv)
      {
         *srv = new_srv;
      }
      if (texture)
      {
         *texture = new_texture;
      }
      return true;
   }

#if ENABLE_SR
   // Jitter sign conventions and the motion vectors jitter flag, tunable in development builds. The game's MVs have no jitter: with a static
   // camera, both the TAA reprojection (cb10 matReprojection) and the object MVs are ~0 while the jitter moves the image by up to ~0.9 pixels.
#if DEVELOPMENT
   bool sr_flip_jitter_x = false;
   bool sr_flip_jitter_y = false;
   bool sr_mvs_jittered = false;
#else
   constexpr bool sr_flip_jitter_x = false;
   constexpr bool sr_flip_jitter_y = false;
   constexpr bool sr_mvs_jittered = false;
#endif
#endif
} // namespace

#if DEVELOPMENT
namespace
{
#if ENABLE_SR
   // SR per-step timings, accumulated since the last log. CPU = submission time on the render thread. GPU = execution time, from timestamp
   // queries read back frames later without flushing; only the inputs CS and the SR draw record GPU work, the other steps only change states.
   enum SRStep : size_t
   {
      SRStepPrepare, // gates, bindings, inputs (re)creation
      SRStepUpdateSettings,
      SRStepStateCache,
      SRStepInputsCS,
      SRStepDraw,
      SRStepStateRestore,
      SRStepCount
   };
   constexpr const char* sr_step_names[SRStepCount + 1] = {"Prepare", "UpdateSettings", "State cache", "Inputs CS", "SR Draw", "State restore", "Total"};
   constexpr size_t sr_gpu_step_count = SRStepDraw - SRStepStateCache; // Inputs CS, SR Draw
   std::atomic<bool> sr_timings_log_requested = false;

   struct SRTimings
   {
      struct Stat
      {
         double sum = 0.0;
         double min = (std::numeric_limits<double>::max)();
         double max = 0.0;
         void Add(double ms)
         {
            sum += ms;
            min = (std::min)(min, ms);
            max = (std::max)(max, ms);
         }
      };
      Stat cpu[SRStepCount + 1]; // + total
      Stat gpu[sr_gpu_step_count + 1];
      uint32_t cpu_samples = 0;
      uint32_t gpu_samples = 0;
      uint32_t gpu_dropped = 0; // no free query slot: the GPU is too many frames behind
      uint32_t gpu_invalid = 0; // disjoint (e.g. a GPU clock change) or failed

      struct Queries
      {
         com_ptr<ID3D11Query> disjoint;
         com_ptr<ID3D11Query> timestamps[sr_gpu_step_count + 1];
         bool pending = false;
      };
      Queries queries[8];
      bool queries_failed = false;
   };

   // Lives through one SR call, which only counts once it reaches the last step
   class SRTimingScope
   {
      using Clock = std::chrono::steady_clock;

   public:
      SRTimingScope(SRTimings* timings, ID3D11Device* native_device, ID3D11DeviceContext* native_device_context)
          : timings(timings), native_device(native_device), native_device_context(native_device_context)
      {
         marks[0] = Clock::now();
         if (native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
            return;
         for (auto& queries : timings->queries)
         {
            if (!queries.pending)
               continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint = {};
            UINT64 ticks[sr_gpu_step_count + 1] = {};
            HRESULT hr = native_device_context->GetData(queries.disjoint.get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            for (size_t i = 0; i < ARRAYSIZE(ticks) && hr == S_OK; i++)
            {
               hr = native_device_context->GetData(queries.timestamps[i].get(), &ticks[i], sizeof(ticks[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            }
            if (hr == S_FALSE)
               continue;
            queries.pending = false;
            if (FAILED(hr) || disjoint.Disjoint || disjoint.Frequency == 0 || !std::is_sorted(std::begin(ticks), std::end(ticks)))
            {
               timings->gpu_invalid++;
               continue;
            }
            const double ms_per_tick = 1000.0 / double(disjoint.Frequency);
            for (size_t i = 0; i < sr_gpu_step_count; i++)
            {
               timings->gpu[i].Add(double(ticks[i + 1] - ticks[i]) * ms_per_tick);
            }
            timings->gpu[sr_gpu_step_count].Add(double(ticks[sr_gpu_step_count] - ticks[0]) * ms_per_tick);
            timings->gpu_samples++;
         }
         if (sr_timings_log_requested.exchange(false))
         {
            const auto log_stats = [](const std::string& prefix, const SRTimings::Stat* stats, size_t first_name, size_t count, uint32_t samples)
            {
               for (size_t i = 0; i < count; i++)
               {
                  const size_t name = (i + 1 == count ? SRStepCount : (first_name + i)); // The last is the total
                  reshade::log::message(reshade::log::level::info, std::format("{} {:<14} avg {:.3f} min {:.3f} max {:.3f} ms", prefix, sr_step_names[name], stats[i].sum / samples, stats[i].min, stats[i].max).c_str());
               }
            };
            reshade::log::message(reshade::log::level::info, std::format("[SRTTR SR] timings since the last log: CPU submission over {} frames, GPU execution over {} frames ({} dropped, {} invalid)", timings->cpu_samples, timings->gpu_samples, timings->gpu_dropped, timings->gpu_invalid).c_str());
            if (timings->cpu_samples)
            {
               log_stats("[SRTTR SR] CPU", timings->cpu, 0, SRStepCount + 1, timings->cpu_samples);
            }
            if (timings->gpu_samples)
            {
               log_stats("[SRTTR SR] GPU", timings->gpu, SRStepInputsCS, sr_gpu_step_count + 1, timings->gpu_samples);
            }
            for (auto& stat : timings->cpu)
            {
               stat = {};
            }
            for (auto& stat : timings->gpu)
            {
               stat = {};
            }
            timings->cpu_samples = timings->gpu_samples = timings->gpu_dropped = timings->gpu_invalid = 0;
         }
      }
      SRTimingScope(const SRTimingScope&) = delete;
      SRTimingScope& operator=(const SRTimingScope&) = delete;

      // Call at the end of each step. The GPU steps are bracketed by timestamps from the end of the state cache to the end of the SR draw.
      void Mark(SRStep step)
      {
         marks[step + 1] = Clock::now();
         if (step < SRStepStateCache || step > SRStepDraw)
            return;
         if (step == SRStepStateCache)
         {
            queries = BeginQueries();
         }
         if (!queries)
            return;
         native_device_context->End(queries->timestamps[step - SRStepStateCache].get());
         if (step == SRStepDraw)
         {
            native_device_context->End(queries->disjoint.get());
         }
      }

      ~SRTimingScope()
      {
         if (marks[SRStepCount] == Clock::time_point{})
            return;
         const auto ms = [](Clock::duration duration)
         { return std::chrono::duration<double, std::milli>(duration).count(); };
         for (size_t i = 0; i < SRStepCount; i++)
         {
            timings->cpu[i].Add(ms(marks[i + 1] - marks[i]));
         }
         timings->cpu[SRStepCount].Add(ms(marks[SRStepCount] - marks[0]));
         timings->cpu_samples++;
      }

   private:
      SRTimings::Queries* BeginQueries()
      {
         if (timings->queries_failed)
            return nullptr;
         for (auto& free_queries : timings->queries)
         {
            if (free_queries.pending)
               continue;
            if (!free_queries.disjoint)
            {
               D3D11_QUERY_DESC desc = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
               bool created = SUCCEEDED(native_device->CreateQuery(&desc, &free_queries.disjoint));
               desc.Query = D3D11_QUERY_TIMESTAMP;
               for (auto& timestamp : free_queries.timestamps)
               {
                  created = created && SUCCEEDED(native_device->CreateQuery(&desc, &timestamp));
               }
               if (!created)
               {
                  timings->queries_failed = true;
                  return nullptr;
               }
            }
            free_queries.pending = true;
            native_device_context->Begin(free_queries.disjoint.get());
            return &free_queries;
         }
         timings->gpu_dropped++;
         return nullptr;
      }

      SRTimings* timings;
      ID3D11Device* native_device;
      ID3D11DeviceContext* native_device_context;
      SRTimings::Queries* queries = nullptr;
      Clock::time_point marks[SRStepCount + 1] = {};
   };
#define SR_TIMING_MARK(step) sr_timing.Mark(step)
#endif
} // namespace
#endif
#if ENABLE_SR && !DEVELOPMENT
#define SR_TIMING_MARK(step)
#endif

struct SaintsRowTheThirdRemasteredGameDeviceData final : public GameDeviceData
{
   // SMAA scratch at FXAA's size: the predication edge-ness (null if its creation failed)
   struct SMAAScratch
   {
      com_ptr<ID3D11UnorderedAccessView> predication_uav;
      com_ptr<ID3D11ShaderResourceView> predication_srv;
      UINT width = 0;
      UINT height = 0;
      uint32_t last_frame = 0; // FrameIndex of SMAA's last run, for "idle_release_frames"
   } smaa_scratch;
   // This frame's scene depth, from the ambient pass (reset every present)
   com_ptr<ID3D11ShaderResourceView> smaa_depth_srv;

   // XeGTAO scratch at the SSAO texture's size
   struct GTAOScratch
   {
      com_ptr<ID3D11UnorderedAccessView> depth_mip_uavs[5]; // R32F view space depth pyramid
      com_ptr<ID3D11ShaderResourceView> depth_mips_srv;
      com_ptr<ID3D11UnorderedAccessView> working_uavs[2]; // R8G8_UNORM AO + edges ping-pong
      com_ptr<ID3D11ShaderResourceView> working_srvs[2];
      UINT width = 0;
      UINT height = 0;
      uint32_t last_frame = 0; // FrameIndex of XeGTAO's last run, for "idle_release_frames"
   } gtao_scratch;
   // A UAV on the game's SSAO texture (it holds the resource, so its address can't be reused)
   com_ptr<ID3D11UnorderedAccessView> gtao_ssao_uav;
   bool ssao_chain_ran_this_frame = false; // the game's SSAO is on (any level but Off)
   bool gtao_tried_this_frame = false;
   bool gtao_succeeded = false;         // on the last try: the vanilla chain is skipped while true
   bool temporal_aa_this_frame = false; // the game's TAA pass ran (vanilla, DLAA or FSR 3)

#if ENABLE_SR
   // SR inputs converted from the game's TAA ones
   com_ptr<ID3D11Texture2D> sr_motion_vectors;
   com_ptr<ID3D11UnorderedAccessView> sr_motion_vectors_uav;
   com_ptr<ID3D11Texture2D> sr_depth;
   com_ptr<ID3D11UnorderedAccessView> sr_depth_uav;
   uint32_t sr_inputs_last_frame = 0; // FrameIndex of the inputs' last use, for "idle_release_frames"
   // The last two TAA outputs SR drew into (so a game alternating two isn't "new" every frame), see "OUT-4" in "DrawSRInPlaceOfTAA"
   com_ptr<ID3D11Resource> sr_outputs[2];
#if DEVELOPMENT
   SRTimings sr_timings;
#endif
#endif
};

class SaintsRowTheThirdRemastered final : public Game
{
   static SaintsRowTheThirdRemasteredGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<SaintsRowTheThirdRemasteredGameDeviceData*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (reference)\n1 - HDR: native grade + reconstructed luminance + DICE display map", 1},
         {"XE_GTAO_QUALITY", '3', true, false, "XeGTAO quality (slice count)\n0 - Low\n1 - Medium\n2 - High\n3 - Very High\n4 - Ultra", 4},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

      // The tonemap CS writes display encoded (gamma 2.2) values into a float texture, compose copies them to the swapchain through a UNORM view
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      // The tonemap scales the scene by game / UI paper white so the GUI layer blended over it in gamma space by compose lands at UI paper white
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');

      // The game binds cbuffers 0-4 and 6-10
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      default_luma_global_game_settings.RCASSharpness = 0.f;
      default_luma_global_game_settings.Exposure = 1.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.ColorGradingIntensity = 1.f;
      default_luma_global_game_settings.VignetteIntensity = 1.f;
      default_luma_global_game_settings.FilmGrainIntensity = 1.f;
      default_luma_global_game_settings.Dithering = 1.f;
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f;
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f;
      default_luma_global_game_settings.HideGameplayUI = 0.f;
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;

      native_shaders_definitions.emplace(smaa_predication_shader_hash, ShaderDefinition{"Luma_SRTTR_SMAAPredication", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(gtao_prefilter_shader_hash, ShaderDefinition{"Luma_SRTTR_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "prefilter_depths16x16_cs"});
      native_shaders_definitions.emplace(gtao_main_shader_hash, ShaderDefinition{"Luma_SRTTR_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "main_pass_cs"});
      native_shaders_definitions.emplace(gtao_denoise_1_shader_hash, ShaderDefinition{"Luma_SRTTR_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "0"}}});
      native_shaders_definitions.emplace(gtao_denoise_2_shader_hash, ShaderDefinition{"Luma_SRTTR_XeGTAO", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "denoise_pass_cs", {{"XE_GTAO_FINAL_APPLY", "1"}}});
#if ENABLE_SR
      native_shaders_definitions.emplace(sr_inputs_shader_hash, ShaderDefinition{"Luma_SRTTR_SRInputs", reshade::api::pipeline_subobject_type::compute_shader});
      sr_game_tooltip = "Requires \"Anti-Aliasing\" set to \"TAA\" in the game's display settings.\n";
#endif
   }

   // SMAA in place of the FXAA draw: it reads FXAA's input (t0) and writes FXAA's render target. Returns false, and FXAA draws, when an input,
   // a shader or the scratch is missing.
   bool DrawSMAAInPlaceOfFXAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, bool* updated_cbuffers)
   {
      com_ptr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
      com_ptr<ID3D11ShaderResourceView> color_srv;
      native_device_context->PSGetShaderResources(0, 1, &color_srv);
      if (!rtv || !color_srv)
         return false;
      // SMAA's metrics are the swapchain's (the Luma settings), so its input and target must match it
      const uint2 size = GetViewTextureSize(color_srv.get());
      const uint2 swapchain_size = {uint32_t(device_data.output_resolution.x), uint32_t(device_data.output_resolution.y)};
      if (size != GetViewTextureSize(rtv.get()) || size != swapchain_size || size.x == 0 || size.y == 0)
         return false;

      // Held through SMAA so a shader reload cannot release them mid-use; "DrawSMAA" looks its shaders up with "at"
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      if (!HasShaders(device_data.native_vertex_shaders, "SMAA Edge Detection VS"_h, "SMAA Blending Weight Calculation VS"_h, "SMAA Neighborhood Blending VS"_h) || !HasShaders(device_data.native_pixel_shaders, "SMAA Edge Detection PS"_h, "SMAA Blending Weight Calculation PS"_h, "SMAA Neighborhood Blending PS"_h))
         return false;

      auto& game_device_data = GetGameDeviceData(device_data);
      auto& scratch = game_device_data.smaa_scratch;
      if (scratch.width != size.x || scratch.height != size.y)
      {
         scratch = {};
         const D3D11_TEXTURE2D_DESC desc = {.Width = size.x, .Height = size.y, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R16_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
         // Without it SMAA simply runs unpredicated
         CreateTextureWithViews(native_device, desc, std::addressof(scratch.predication_uav), std::addressof(scratch.predication_srv));
         scratch.width = size.x;
         scratch.height = size.y;
      }

      // Predication depth: the ambient pass' R24 scene depth at FXAA's size. Anything else falls back to plain ULTRA.
      const bool predication_available = g_smaa_predication && scratch.predication_uav && HasShaders(device_data.native_compute_shaders, smaa_predication_shader_hash);
      com_ptr<ID3D11ShaderResourceView> depth_srv = (predication_available ? game_device_data.smaa_depth_srv : nullptr);
      if (depth_srv)
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv_desc;
         depth_srv->GetDesc(&depth_srv_desc);
         if (depth_srv_desc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS || depth_srv_desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D || GetViewTextureSize(depth_srv.get()) != size)
         {
            depth_srv = nullptr;
         }
      }
      ID3D11ShaderResourceView* const predication_srv = (depth_srv ? scratch.predication_srv.get() : nullptr);

      if (depth_srv)
      {
         DrawStateStack<DrawStateStackType::Compute> compute_state;
         compute_state.Cache(native_device_context, device_data.uav_max_count);
         // Unbind the render targets: were the depth still bound as the DSV, D3D11 would silently null its SRV in the predication pass
         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);
         native_device_context->OMSetRenderTargets(0, nullptr, nullptr);

         ID3D11UnorderedAccessView* const predication_uav = scratch.predication_uav.get();
         ID3D11ShaderResourceView* const raw_depth_srv = depth_srv.get();
         native_device_context->CSSetUnorderedAccessViews(0, 1, &predication_uav, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &raw_depth_srv);
         native_device_context->CSSetShader(device_data.native_compute_shaders.at(smaa_predication_shader_hash).get(), nullptr, 0);
         native_device_context->Dispatch((size.x + 7) / 8, (size.y + 7) / 8, 1);

         native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, reinterpret_cast<ID3D11RenderTargetView* const*>(&rtvs[0]), dsv.get());
         compute_state.Restore(native_device_context);
      }

      // The SMAA shaders read the target size from the Luma settings and the predication scale from the Luma data, in both stages
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
      SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::vertex | reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, 0, 0, (depth_srv ? 2.f : 1.f));
      *updated_cbuffers = true;
      // Core creates its area texture at the first draw; ours goes in first, without the U-shape smoothing (see "SMAA_AreaTex_SharpU.h")
      if (auto& area_tex_srv = device_data.managed_resources.shader_resource_views["smaa_area_tex"_h]; !area_tex_srv)
      {
         std::vector<unsigned char> area_tex(std::begin(areaTexBytes), std::end(areaTexBytes));
         for (const auto& [offset, value] : areaTexSharpUPatch)
         {
            area_tex[offset] = value;
         }
         const D3D11_TEXTURE2D_DESC area_tex_desc = {.Width = AREATEX_WIDTH, .Height = AREATEX_HEIGHT, .MipLevels = 1, .ArraySize = 1, .Format = DXGI_FORMAT_R8G8_UNORM, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_IMMUTABLE, .BindFlags = D3D11_BIND_SHADER_RESOURCE};
         const D3D11_SUBRESOURCE_DATA area_tex_data = {.pSysMem = area_tex.data(), .SysMemPitch = AREATEX_PITCH};
         if (com_ptr<ID3D11Texture2D> texture; SUCCEEDED(native_device->CreateTexture2D(&area_tex_desc, &area_tex_data, &texture)))
         {
            native_device->CreateShaderResourceView(texture.get(), nullptr, area_tex_srv.put());
         }
      }
      // The neighborhood blend reads the gamma input too and filters it in linear light itself (SMAA_NEIGHBORHOOD_GAMMA_IN_LINEAR)
      DrawSMAA(native_device, native_device_context, device_data, rtv.get(), color_srv.get(), color_srv.get(), predication_srv);
      scratch.last_frame = cb_luma_global_settings.FrameIndex;

#if DEVELOPMENT
      ID3D11ShaderResourceView* const edges_srv = device_data.managed_resources.shader_resource_views["smaa_edge_detection"_h].get();
      // Calibration aid: SMAA's edges (red = horizontal, green = vertical) or the predication edge-ness (red) replace the frame
      ID3D11ShaderResourceView* const debug_srv = (g_smaa_debug_view == 1 ? edges_srv : (g_smaa_debug_view == 2 ? predication_srv : nullptr));
      if (debug_srv && HasShaders(device_data.native_vertex_shaders, "Copy VS"_h) && HasShaders(device_data.native_pixel_shaders, "Copy PS"_h))
      {
         DrawStateStack<DrawStateStackType::FullGraphics> debug_state;
         debug_state.Cache(native_device_context, device_data.uav_max_count);
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr, device_data.native_vertex_shaders.at("Copy VS"_h).get(), device_data.native_pixel_shaders.at("Copy PS"_h).get(), debug_srv, rtv.get(), size.x, size.y, false);
         debug_state.Restore(native_device_context);
      }
#endif
      return true;
   }

   // XeGTAO right before the ambient draw, on its inputs (t0 depth, t1 view space normals, cb10 AMBIENT_PARAMS for this frame's projection):
   // prefilter, main pass and two denoisers, the last one writing the ambient's t2 (the game's SSAO texture) through a UAV. Returns false,
   // and the SSAO keeps its previous content, when an input, a shader or the scratch is missing.
   bool RunXeGTAO(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, bool* updated_cbuffers)
   {
      // Held through the dispatches so a shader reload cannot release them mid-use
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      const auto& shaders = device_data.native_compute_shaders;
      if (!HasXeGTAOShaders(device_data))
         return false;

      com_ptr<ID3D11DeviceContext1> native_device_context1;
      if (FAILED(native_device_context->QueryInterface(&native_device_context1)))
         return false;
      com_ptr<ID3D11ShaderResourceView> srvs[3]; // depth, normals, SSAO
      native_device_context->PSGetShaderResources(0, 3, &srvs[0]);
      com_ptr<ID3D11Buffer> ambient_params;
      UINT ambient_params_first = 0, ambient_params_count = 0;
      native_device_context1->PSGetConstantBuffers1(ambient_params_cb_slot, 1, &ambient_params, &ambient_params_first, &ambient_params_count);
      if (!srvs[0] || !srvs[1] || !srvs[2] || !ambient_params)
         return false;
      D3D11_SHADER_RESOURCE_VIEW_DESC depth_srv_desc;
      srvs[0]->GetDesc(&depth_srv_desc);
      com_ptr<ID3D11Resource> ssao_texture;
      srvs[2]->GetResource(&ssao_texture);
      const uint2 depth_size = GetViewTextureSize(srvs[0].get());
      const uint2 normals_size = GetViewTextureSize(srvs[1].get());
      const uint2 ssao_size = GetViewTextureSize(srvs[2].get());
      const UINT width = ssao_size.x;
      const UINT height = ssao_size.y;
      if (depth_srv_desc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS || width == 0 || height == 0 || depth_size != ssao_size || normals_size != ssao_size)
         return false;

      auto& game_device_data = GetGameDeviceData(device_data);
      auto& scratch = game_device_data.gtao_scratch;
      if (scratch.width != width || scratch.height != height)
      {
         scratch = {};
         // MipLevels = XE_GTAO_DEPTH_MIP_LEVELS
         D3D11_TEXTURE2D_DESC desc = {.Width = width, .Height = height, .MipLevels = UINT(ARRAYSIZE(scratch.depth_mip_uavs)), .ArraySize = 1, .Format = DXGI_FORMAT_R32_FLOAT, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
         com_ptr<ID3D11Texture2D> depth_mips_texture;
         bool ok = SUCCEEDED(native_device->CreateTexture2D(&desc, nullptr, &depth_mips_texture)) && SUCCEEDED(native_device->CreateShaderResourceView(depth_mips_texture.get(), nullptr, &scratch.depth_mips_srv));
         D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
         uav_desc.Format = desc.Format;
         uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
         for (UINT mip = 0; ok && mip < desc.MipLevels; mip++)
         {
            uav_desc.Texture2D.MipSlice = mip;
            ok = SUCCEEDED(native_device->CreateUnorderedAccessView(depth_mips_texture.get(), &uav_desc, &scratch.depth_mip_uavs[mip]));
         }
         desc.MipLevels = 1;
         desc.Format = DXGI_FORMAT_R8G8_UNORM;
         for (int i = 0; ok && i < 2; i++)
         {
            ok = CreateTextureWithViews(native_device, desc, std::addressof(scratch.working_uavs[i]), std::addressof(scratch.working_srvs[i]));
         }
         if (!ok)
         {
            scratch = {};
            return false;
         }
         scratch.width = width;
         scratch.height = height;
      }
      scratch.last_frame = cb_luma_global_settings.FrameIndex; // In use, even if this run fails below
      // The vanilla final upsample writes the SSAO texture as a UAV, so it has the bind flag
      com_ptr<ID3D11Resource> uav_texture;
      if (game_device_data.gtao_ssao_uav)
      {
         game_device_data.gtao_ssao_uav->GetResource(&uav_texture);
      }
      if (uav_texture != ssao_texture)
      {
         game_device_data.gtao_ssao_uav = nullptr;
         D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
         uav_desc.Format = DXGI_FORMAT_R8_UNORM;
         uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
         if (FAILED(native_device->CreateUnorderedAccessView(ssao_texture.get(), &uav_desc, &game_device_data.gtao_ssao_uav)))
            return false;
      }

      {
         DrawStateStack<DrawStateStackType::Compute> compute_state;
         compute_state.Cache(native_device_context, device_data.uav_max_count);
         ID3D11Buffer* const ambient_params_cb = ambient_params.get();
         native_device_context1->CSSetConstantBuffers1(ambient_params_cb_slot, 1, &ambient_params_cb, &ambient_params_first, &ambient_params_count);
         // The noise pattern cycles only when a temporal AA accumulated last frame (the ambient runs before this frame's TAA), otherwise it would boil
         const uint32_t noise_index = (device_data.taa_detected ? (cb_luma_global_settings.FrameIndex % 64) : 0);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData, noise_index, uint32_t(g_gtao_debug_view), g_gtao_final_value_power, g_gtao_radius_override);
         *updated_cbuffers = true;
         ID3D11SamplerState* const point_sampler = device_data.sampler_state_point.get();
         native_device_context->CSSetSamplers(0, 1, &point_sampler);

         // Each pass binds its destination UAV before its source SRVs: D3D11 otherwise nulls an SRV that still aliases the previous pass's bound UAV
         ID3D11ShaderResourceView* const null_srvs[2] = {};
         ID3D11UnorderedAccessView* const null_uavs[ARRAYSIZE(scratch.depth_mip_uavs)] = {};
         const auto pass = [&](uint32_t shader_name_hash, UINT uav_count, ID3D11UnorderedAccessView* const* uavs, ID3D11ShaderResourceView* const(&pass_srvs)[2], UINT groups_x, UINT groups_y)
         {
            native_device_context->CSSetShaderResources(0, 2, null_srvs);
            native_device_context->CSSetUnorderedAccessViews(0, uav_count, uavs, nullptr);
            native_device_context->CSSetShaderResources(0, 2, pass_srvs);
            native_device_context->CSSetShader(shaders.at(shader_name_hash).get(), nullptr, 0);
            native_device_context->Dispatch(groups_x, groups_y, 1);
            native_device_context->CSSetUnorderedAccessViews(0, uav_count, null_uavs, nullptr);
         };
         ID3D11UnorderedAccessView* const mip_uavs[] = {scratch.depth_mip_uavs[0].get(), scratch.depth_mip_uavs[1].get(), scratch.depth_mip_uavs[2].get(), scratch.depth_mip_uavs[3].get(), scratch.depth_mip_uavs[4].get()};
         ID3D11UnorderedAccessView* const working_uavs[] = {scratch.working_uavs[0].get(), scratch.working_uavs[1].get()};
         ID3D11UnorderedAccessView* const ssao_uav = game_device_data.gtao_ssao_uav.get();
         pass(gtao_prefilter_shader_hash, ARRAYSIZE(mip_uavs), mip_uavs, {srvs[0].get(), nullptr}, (width + 15) / 16, (height + 15) / 16);
         pass(gtao_main_shader_hash, 1, &working_uavs[0], {scratch.depth_mips_srv.get(), srvs[1].get()}, (width + 7) / 8, (height + 7) / 8);
         pass(gtao_denoise_1_shader_hash, 1, &working_uavs[1], {scratch.working_srvs[0].get(), nullptr}, (width + 15) / 16, (height + 7) / 8);
         pass(gtao_denoise_2_shader_hash, 1, &ssao_uav, {scratch.working_srvs[1].get(), nullptr}, (width + 15) / 16, (height + 7) / 8);
         compute_state.Restore(native_device_context);
      }

      // Binding the SSAO as a UAV unbound it from the ambient's t2
      ID3D11ShaderResourceView* const ambient_srvs[] = {srvs[0].get(), srvs[1].get(), srvs[2].get()};
      native_device_context->PSSetShaderResources(0, ARRAYSIZE(ambient_srvs), ambient_srvs);
      return true;
   }

#if ENABLE_SR
   // DLAA / FSR 3 in place of the game's TAA dispatch, on its inputs (t0 color, t1 depth, t3 motion vectors, u0 output) converted to what SR takes.
   // Returns None, and the game's TAA runs, when SR is off or an input, the shader or the scratch is missing.
   DrawOrDispatchOverrideType DrawSRInPlaceOfTAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data)
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      // The jitter comes from the camera built on this thread (the render thread), and SR needs the game's 8x jitter pattern to be active
      const CameraData camera = last_built_camera;
      const int32_t* jitter_mode = GetGameAddresses().jitter_mode;
      // A skipped frame resets SR's history at the next one, see "OnPresent"
      if (device_data.sr_type == SR::Type::None || device_data.sr_suppressed || native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE || !camera.valid || !jitter_mode || *jitter_mode != halton_jitter_mode)
         return DrawOrDispatchOverrideType::None;
#if DEVELOPMENT
      SRTimingScope sr_timing(&game_device_data.sr_timings, native_device, native_device_context);
#endif
      // Held through the dispatch so a shader reload cannot release it mid-use
      const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
      if (!HasShaders(device_data.native_compute_shaders, sr_inputs_shader_hash))
         return DrawOrDispatchOverrideType::None;

      com_ptr<ID3D11ShaderResourceView> srvs[4]; // t0 color, t1 depth, t3 motion vectors
      native_device_context->CSGetShaderResources(0, ARRAYSIZE(srvs), &srvs[0]);
      com_ptr<ID3D11UnorderedAccessView> output_uav;
      native_device_context->CSGetUnorderedAccessViews(0, 1, &output_uav);
      if (!srvs[0] || !srvs[1] || !srvs[3] || !output_uav)
         return DrawOrDispatchOverrideType::None;
      com_ptr<ID3D11Resource> source_color;
      srvs[0]->GetResource(&source_color);
      com_ptr<ID3D11Resource> source_depth;
      srvs[1]->GetResource(&source_depth);
      com_ptr<ID3D11Resource> output_color;
      output_uav->GetResource(&output_color);
      const uint2 output_size = GetViewTextureSize(output_uav.get()); // 0x0 unless a 2D texture

      auto* sr_instance_data = device_data.GetSRInstanceData();
      if (!sr_instance_data || output_size.x == 0 || output_size.y == 0 || output_size.x < sr_instance_data->min_resolution || output_size.y < sr_instance_data->min_resolution)
         return DrawOrDispatchOverrideType::None;

      // (Re)create the converted inputs at the TAA resolution. DLSS reads the game's D24S8 depth as is; FSR has no 24 bit depth format, so it
      // gets an R32_FLOAT copy.
      const bool sr_inputs_changed = !game_device_data.sr_motion_vectors || !AreResourcesEqual(game_device_data.sr_motion_vectors.get(), output_color.get(), false);
      if (sr_inputs_changed)
      {
         CleanExtraSRResources(device_data);
      }
      const bool sr_depth_copy = device_data.sr_type == SR::Type::FSR;
      if (!sr_depth_copy)
      {
         game_device_data.sr_depth = nullptr;
         game_device_data.sr_depth_uav = nullptr;
      }
      const auto create_input = [&](DXGI_FORMAT format, com_ptr<ID3D11UnorderedAccessView>* uav, com_ptr<ID3D11Texture2D>* texture)
      {
         const D3D11_TEXTURE2D_DESC desc = {.Width = output_size.x, .Height = output_size.y, .MipLevels = 1, .ArraySize = 1, .Format = format, .SampleDesc = {.Count = 1}, .Usage = D3D11_USAGE_DEFAULT, .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS};
         return CreateTextureWithViews(native_device, desc, uav, nullptr, texture);
      };
      bool created = true;
      if (!game_device_data.sr_motion_vectors)
      {
         // fp16 keeps the object MVs' precision (RT3 is R16G16_UNORM: 1/257 pixel steps) up to 8 pixels of motion, and is finer below
         created = create_input(DXGI_FORMAT_R16G16_FLOAT, std::addressof(game_device_data.sr_motion_vectors_uav), std::addressof(game_device_data.sr_motion_vectors));
      }
      if (created && sr_depth_copy && !game_device_data.sr_depth)
      {
         created = create_input(DXGI_FORMAT_R32_FLOAT, std::addressof(game_device_data.sr_depth_uav), std::addressof(game_device_data.sr_depth));
      }
      ASSERT_ONCE(created);
      if (!created)
      {
         CleanExtraSRResources(device_data);
         return DrawOrDispatchOverrideType::None;
      }
      game_device_data.sr_inputs_last_frame = cb_luma_global_settings.FrameIndex;
      const bool new_output = output_color != game_device_data.sr_outputs[0] && output_color != game_device_data.sr_outputs[1];

      SR_TIMING_MARK(SRStepPrepare);

      SR::SettingsData settings_data;
      settings_data.output_width = output_size.x;
      settings_data.output_height = output_size.y;
      settings_data.render_width = output_size.x;
      settings_data.render_height = output_size.y;
      settings_data.hdr = true; // TAA runs on the scene linear HDR color, before tonemapping
      settings_data.mvs_jittered = sr_mvs_jittered;
      // FSR's auto exposure clips highlights
      settings_data.auto_exposure = device_data.sr_type != SR::Type::FSR;
      settings_data.render_preset = dlss_render_preset;
      sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);
      SR_TIMING_MARK(SRStepUpdateSettings);

      DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
      DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
      draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
      compute_state_stack.Cache(native_device_context, device_data.uav_max_count);
      SR_TIMING_MARK(SRStepStateCache);

      // Convert the motion vectors and depth (null without the copy: its writes are dropped), the game's cb10, t1 and t3 are still bound
      ID3D11UnorderedAccessView* const sr_inputs_uavs[] = {game_device_data.sr_motion_vectors_uav.get(), game_device_data.sr_depth_uav.get()};
      native_device_context->CSSetUnorderedAccessViews(0, ARRAYSIZE(sr_inputs_uavs), sr_inputs_uavs, nullptr);
      native_device_context->CSSetShader(device_data.native_compute_shaders.at(sr_inputs_shader_hash).get(), nullptr, 0);
      native_device_context->Dispatch((output_size.x + 7) / 8, (output_size.y + 7) / 8, 1);
      ID3D11UnorderedAccessView* const null_uavs[ARRAYSIZE(sr_inputs_uavs)] = {};
      native_device_context->CSSetUnorderedAccessViews(0, ARRAYSIZE(null_uavs), null_uavs, nullptr);
      SR_TIMING_MARK(SRStepInputsCS);

      SR::SuperResolutionImpl::DrawData draw_data;
      draw_data.source_color = source_color.get();
      draw_data.output_color = output_color.get();
      draw_data.motion_vectors = game_device_data.sr_motion_vectors.get();
      draw_data.depth_buffer = (sr_depth_copy ? game_device_data.sr_depth.get() : source_depth.get());
      draw_data.render_width = output_size.x;
      draw_data.render_height = output_size.y;
      // Projection jitter (NDC, y up) to the sample offset in pixels (y down)
      draw_data.jitter_x = camera.jitter_x * output_size.x * (sr_flip_jitter_x ? 0.5f : -0.5f);
      draw_data.jitter_y = camera.jitter_y * output_size.y * (sr_flip_jitter_y ? -0.5f : 0.5f);
      draw_data.vert_fov = (camera.projection_y_scale > 0.f ? (2.f * std::atan(1.f / camera.projection_y_scale)) : (60.f * float(M_PI) / 180.f)); // FSR fails without it
      if (camera.near_plane > 0.f && camera.far_plane > camera.near_plane)
      {
         draw_data.near_plane = camera.near_plane;
         draw_data.far_plane = camera.far_plane;
      }
      draw_data.reset = device_data.force_reset_sr || sr_inputs_changed;
      device_data.force_reset_sr = false;

      const bool sr_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
      SR_TIMING_MARK(SRStepDraw);
      draw_state_stack.Restore(native_device_context);
      compute_state_stack.Restore(native_device_context);
      SR_TIMING_MARK(SRStepStateRestore);
      if (!sr_succeeded)
         return DrawOrDispatchOverrideType::None;
      if (new_output)
      {
         game_device_data.sr_outputs[1] = std::exchange(game_device_data.sr_outputs[0], output_color);
         // OUT-4 (DLSS Best Practices): DLSS writes nothing into a new output until its feature is created again after a draw.
         // Settings changed once here force that at the next frame's "UpdateSettings".
         if (device_data.sr_type == SR::Type::DLSS)
         {
            SR::SettingsData throwaway_settings_data = settings_data;
            throwaway_settings_data.mvs_jittered = !throwaway_settings_data.mvs_jittered;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, throwaway_settings_data);
         }
      }
      device_data.has_drawn_sr = true;
      return DrawOrDispatchOverrideType::Replaced; // The game's TAA history isn't updated, it's only read again if SR is turned off
   }
#endif

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new SaintsRowTheThirdRemasteredGameDeviceData;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      if ((stages & reshade::api::shader_stage::compute) != reshade::api::shader_stage::compute)
      {
         const auto is_pixel_shader = [&](uint32_t hash)
         { return original_shader_hashes.Contains(hash, reshade::api::shader_stage::pixel); };
         if (is_pixel_shader(ambient_hash))
         {
            // Once per frame, and only when the game's SSAO is on
            if (g_gtao_enable && game_device_data.ssao_chain_ran_this_frame && !std::exchange(game_device_data.gtao_tried_this_frame, true))
            {
               game_device_data.gtao_succeeded = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE && RunXeGTAO(native_device, native_device_context, cmd_list_data, device_data, &updated_cbuffers);
            }
            game_device_data.smaa_depth_srv = nullptr;
            if (g_smaa_enable)
            {
               native_device_context->PSGetShaderResources(0, 1, &game_device_data.smaa_depth_srv);
            }
         }
         else if (is_pixel_shader(video_hash))
         {
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            if (rtv)
            {
               rtv->GetDesc(&rtv_desc);
            }
            // Core only binds the Luma settings itself when the game leaves both cbuffers to it. The flag tells the shader its target
            // (the swapchain) keeps AutoHDR highlights above 1.
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData, (rtv_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1 : 0));
            updated_cbuffers = true;
         }
         else if (g_smaa_enable && is_pixel_shader(fxaa_pixel_shader_hash) && DrawSMAAInPlaceOfFXAA(native_device, native_device_context, cmd_list_data, device_data, &updated_cbuffers))
         {
            return DrawOrDispatchOverrideType::Replaced;
         }
         return DrawOrDispatchOverrideType::None;
      }

      const auto is_compute_shader = [&](uint32_t hash)
      { return original_shader_hashes.Contains(hash, reshade::api::shader_stage::compute); };
      // XeGTAO writes the chain's only output at the ambient draw. While it works, the chain is skipped.
      if (is_compute_shader(ssao_prepare_1_hash))
      {
         game_device_data.ssao_chain_ran_this_frame = true;
      }
      if (g_gtao_enable && game_device_data.gtao_succeeded && std::any_of(std::begin(ssao_chain_hashes), std::end(ssao_chain_hashes), is_compute_shader))
      {
         // Only while XeGTAO can still run at the ambient: a shader reload would otherwise leave the previous frame's AO
         const std::shared_lock lock_shader_objects(s_mutex_shader_objects);
         if (HasXeGTAOShaders(device_data))
            return DrawOrDispatchOverrideType::Skip;
      }
      // The tonemap runs in every frame with a scene, whatever the anti-aliasing setting
      if (std::any_of(std::begin(tonemap_hashes), std::end(tonemap_hashes), is_compute_shader))
      {
         device_data.has_drawn_main_post_processing = true;
         return DrawOrDispatchOverrideType::None;
      }
      if (std::none_of(std::begin(taa_hashes), std::end(taa_hashes), is_compute_shader))
         return DrawOrDispatchOverrideType::None;

      game_device_data.temporal_aa_this_frame = true; // OnPresent turns it into device_data.taa_detected

#if ENABLE_SR
      return DrawSRInPlaceOfTAA(native_device, native_device_context, device_data);
#else
      return DrawOrDispatchOverrideType::None;
#endif
   }

#if ENABLE_SR
   // Core swaps the game's samplers for their biased variants only as they're bound, so after a change of
   // "texture_mip_lod_bias_offset" a sampler that stays bound keeps the previous bias until the game rebinds it. Swaps every stage's
   // bound samplers for the current bias's variants. Expects "s_mutex_samplers" locked exclusively.
   static void RebindUpgradedSamplers(ID3D11DeviceContext* native_device_context, DeviceData* device_data)
   {
      using GetSamplers = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11SamplerState**);
      using SetSamplers = void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT, UINT, ID3D11SamplerState* const*);
      constexpr std::pair<GetSamplers, SetSamplers> stages[] = {
         {&ID3D11DeviceContext::VSGetSamplers, &ID3D11DeviceContext::VSSetSamplers},
         {&ID3D11DeviceContext::HSGetSamplers, &ID3D11DeviceContext::HSSetSamplers},
         {&ID3D11DeviceContext::DSGetSamplers, &ID3D11DeviceContext::DSSetSamplers},
         {&ID3D11DeviceContext::GSGetSamplers, &ID3D11DeviceContext::GSSetSamplers},
         {&ID3D11DeviceContext::PSGetSamplers, &ID3D11DeviceContext::PSSetSamplers},
         {&ID3D11DeviceContext::CSGetSamplers, &ID3D11DeviceContext::CSSetSamplers},
      };
      auto& custom_samplers = device_data->custom_sampler_by_original_sampler;
      const float bias = device_data->texture_mip_lod_bias_offset;
      for (const auto& [get_samplers, set_samplers] : stages)
      {
         ID3D11SamplerState* samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
         (native_device_context->*get_samplers)(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
         bool swapped = false;
         for (ID3D11SamplerState*& sampler : samplers)
         {
            if (!sampler)
               continue;
            // The game's sampler: the bound one, or the one whose variant is bound
            auto original = custom_samplers.find(reinterpret_cast<uint64_t>(sampler));
            if (original == custom_samplers.end())
            {
               original = std::find_if(custom_samplers.begin(), custom_samplers.end(), [sampler](const auto& entry)
                  { return std::any_of(entry.second.begin(), entry.second.end(), [sampler](const auto& variant)
                       { return variant.second.get() == sampler; }); });
               if (original == custom_samplers.end())
                  continue;
            }
            auto& [original_handle, variants] = *original;
            auto variant = variants.find(bias);
            if (variant == variants.end())
            {
               D3D11_SAMPLER_DESC desc;
               reinterpret_cast<ID3D11SamplerState*>(original_handle)->GetDesc(&desc);
               variant = variants.emplace(bias, CreateCustomSampler(*device_data, device_data->native_device, desc)).first;
            }
            // No variant: a sampler Core doesn't upgrade, the game's own
            ID3D11SamplerState* const replacement = (variant->second ? variant->second.get() : reinterpret_cast<ID3D11SamplerState*>(original_handle));
            if (replacement != sampler)
            {
               sampler->Release();
               sampler = replacement;
               sampler->AddRef();
               swapped = true;
            }
         }
         if (swapped)
         {
            (native_device_context->*set_samplers)(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, samplers);
         }
         for (ID3D11SamplerState* sampler : samplers)
         {
            if (sampler)
            {
               sampler->Release();
            }
         }
      }
   }
#endif

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      // Patched at the first present rather than at load time, once SteamStub has unpacked the game code
      if (!game_patches_applied)
      {
         game_patches_applied = true;
#if ENABLE_SR
         // SR takes its jitter from the patched game code (see "GetGameAddresses")
         if (!GetGameAddresses().camera_build)
         {
            sr_game_tooltip = "Unsupported game executable version: Super Resolution can't engage.\n";
         }
         InstallHaltonJitterPattern();
#endif
         InstallCameraHooks();
      }
      SetJitterIndexFix(g_fix_native_taa_jitter);

      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.smaa_depth_srv = nullptr;
      // Scratch of a feature turned off or idle goes back, on the render thread between frames, Core's SMAA intermediates included
      if (game_device_data.smaa_scratch.width != 0 && cb_luma_global_settings.FrameIndex - game_device_data.smaa_scratch.last_frame > idle_release_frames)
      {
         game_device_data.smaa_scratch = {};
         ReleaseSMAAIntermediates(device_data);
      }
      if (game_device_data.gtao_scratch.width != 0 && (!g_gtao_enable || cb_luma_global_settings.FrameIndex - game_device_data.gtao_scratch.last_frame > idle_release_frames))
      {
         game_device_data.gtao_scratch = {};
         game_device_data.gtao_ssao_uav = nullptr;
      }
      game_device_data.ssao_chain_ran_this_frame = false;
      game_device_data.gtao_tried_this_frame = false;
      device_data.taa_detected = std::exchange(game_device_data.temporal_aa_this_frame, false);
      device_data.has_drawn_main_post_processing = false; // Set by the tonemap
#if ENABLE_SR
      // SR resolves more detail than the game's TAA, so sharpen texture sampling while it draws (-1 at native resolution).
      // The offset is added to the game's own sampler bias, which is unknown, so the game's TAA keeps it unchanged.
      if (enable_samplers_upgrade && !custom_texture_mip_lod_bias_offset)
      {
         const float bias = (device_data.has_drawn_sr ? SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y) : 0.f);
         const std::unique_lock lock_samplers(s_mutex_samplers);
         if (device_data.texture_mip_lod_bias_offset != bias)
         {
            device_data.texture_mip_lod_bias_offset = bias;
            if (!ignore_upgraded_samplers)
            {
               com_ptr<ID3D11DeviceContext> native_device_context;
               native_device->GetImmediateContext(&native_device_context);
               RebindUpgradedSamplers(native_device_context.get(), &device_data);
            }
         }
      }
      // Any frame SR didn't draw (off, skipped, failed, no TAA dispatch) restarts its history at the next one
      device_data.force_reset_sr = !device_data.has_drawn_sr;
      device_data.has_drawn_sr = false;
      const bool sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
      if (game_device_data.sr_motion_vectors && cb_luma_global_settings.FrameIndex - game_device_data.sr_inputs_last_frame > idle_release_frames)
      {
         CleanExtraSRResources(device_data);
      }
      // Only switched on changes, so the development combo stays usable
      if (int32_t* jitter_mode = GetGameAddresses().jitter_mode; jitter_mode && sr_active != halton_jitter_mode_applied)
      {
         halton_jitter_mode_applied = sr_active;
         *jitter_mode = (sr_active ? halton_jitter_mode : vanilla_jitter_mode);
      }
#endif
   }

#if ENABLE_SR
   void CleanExtraSRResources(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.sr_motion_vectors = nullptr;
      game_device_data.sr_motion_vectors_uav = nullptr;
      game_device_data.sr_depth = nullptr;
      game_device_data.sr_depth_uav = nullptr;
      for (auto& output : game_device_data.sr_outputs)
      {
         output = nullptr;
      }
   }
#endif

   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "XeGTAOEnable", g_gtao_enable);
      reshade::get_config_value(nullptr, NAME, "FixNativeTAAJitter", g_fix_native_taa_jitter);
      auto& settings = cb_luma_global_settings.GameSettings;
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", settings.RCASSharpness);
      reshade::get_config_value(nullptr, NAME, "Exposure", settings.Exposure);
      reshade::get_config_value(nullptr, NAME, "Contrast", settings.Contrast);
      reshade::get_config_value(nullptr, NAME, "Saturation", settings.Saturation);
      reshade::get_config_value(nullptr, NAME, "ColorGradingIntensity", settings.ColorGradingIntensity);
      reshade::get_config_value(nullptr, NAME, "VignetteIntensity", settings.VignetteIntensity);
      reshade::get_config_value(nullptr, NAME, "FilmGrainIntensity", settings.FilmGrainIntensity);
      reshade::get_config_value(nullptr, NAME, "Dithering", settings.Dithering);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", settings.VideoAutoHDREnable);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", settings.VideoAutoHDRBoost);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      auto& settings = cb_luma_global_settings.GameSettings;
      const auto& defaults = default_luma_global_game_settings;

      ImGui::SeparatorText("Anti-Aliasing");
      DrawSettingCheckbox("SMAA Enable", "SMAAEnable", &g_smaa_enable, true, "Replaces the game's FXAA with SMAA (only active when in-game Anti-Aliasing is set to FXAA).");
      // RCAS runs after any anti-aliasing, in place of the game's disabled sharpen
      DrawSettingSlider("RCAS Sharpness", "RCASSharpness", &settings.RCASSharpness, defaults.RCASSharpness, 0.f, 1.f, "Sharpening applied on top of anti-aliasing (0 = off). Replaces the game's Sharpen setting.");

      ImGui::SeparatorText("Grade");
      DrawSettingSlider("Exposure", "Exposure", &settings.Exposure, defaults.Exposure, 0.f, 2.f, "Overall image brightness (1 = vanilla).");
      DrawSettingSlider("Contrast", "Contrast", &settings.Contrast, defaults.Contrast, 0.f, 2.f, "Overall image contrast, HDR only (1 = vanilla).");
      DrawSettingSlider("Saturation", "Saturation", &settings.Saturation, defaults.Saturation, 0.f, 2.f, "Color saturation, HDR only (1 = vanilla).");
      DrawSettingSlider("Color Grading Intensity", "ColorGradingIntensity", &settings.ColorGradingIntensity, defaults.ColorGradingIntensity, 0.f, 1.f, "Strength of the game's own color grading (1 = vanilla, 0 = neutral).");

      ImGui::SeparatorText("Ambient Occlusion");
      DrawSettingCheckbox("XeGTAO Enable", "XeGTAOEnable", &g_gtao_enable, true, "Replaces the game's SSAO with XeGTAO (cleaner, more accurate ambient occlusion; requires Ambient Occlusion enabled in the game's display settings).");

      ImGui::SeparatorText("Effects");
      DrawSettingSlider("Vignette Intensity", "VignetteIntensity", &settings.VignetteIntensity, defaults.VignetteIntensity, 0.f, 1.f, "Scales the game's vignette darkening (1 = vanilla, 0 = none).");
      DrawSettingSlider("Film Grain Intensity", "FilmGrainIntensity", &settings.FilmGrainIntensity, defaults.FilmGrainIntensity, 0.f, 1.f, "Scales the game's film grain (1 = vanilla, 0 = off).");
      DrawSettingCheckbox("Video AutoHDR", "VideoAutoHDREnable", &settings.VideoAutoHDREnable, defaults.VideoAutoHDREnable, "Adds HDR highlights to pre-rendered videos (HDR only).");
      ImGui::BeginDisabled(settings.VideoAutoHDREnable <= 0.5f);
      DrawSettingSlider("Video HDR Boost", "VideoAutoHDRBoost", &settings.VideoAutoHDRBoost, defaults.VideoAutoHDRBoost, 0.f, 1.f, "Video highlight strength (0 = off).");
      ImGui::EndDisabled();
      DrawSettingCheckbox("Dithering", "Dithering", &settings.Dithering, defaults.Dithering, "Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      // Session only, so a restart never comes back without a HUD
      bool hide_gameplay_ui = settings.HideGameplayUI > 0.5f;
      if (ImGui::Checkbox("Hide Gameplay UI", &hide_gameplay_ui))
      {
         settings.HideGameplayUI = (hide_gameplay_ui ? 1.f : 0.f);
      }
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Disables the in-game UI.");
      }

      ImGui::SeparatorText("Fixes");
#if ENABLE_SR
      // SR uses its own jitter, the fix is shown as always on
      const bool sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#else
      constexpr bool sr_active = false;
#endif
      DrawSettingCheckbox("Fix Native TAA Jitter", "FixNativeTAAJitter", &g_fix_native_taa_jitter, true, "The game's TAA only uses half of the detail it was designed to gather over frames. This fixes it, for a sharper, steadier image.\nDLAA and FSR 3 always use their own fix.", sr_active);
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("TAA Jitter");
      if (int32_t* jitter_mode = GetGameAddresses().jitter_mode)
      {
         ImGui::Combo("TAA Jitter Pattern", jitter_mode, "None\0"
                                                         "2x\0"
                                                         "4x\0"
                                                         "8x Halton (SR)\0");
         if (ImGui::IsItemHovered())
         {
            ImGui::SetTooltip("Game TAA jitter: D3D MSAA sample patterns, with 8x replaced by Halton. SR requires 8x. Reset when SR changes. Not saved.");
         }
      }
      else
      {
         ImGui::TextDisabled("Unsupported SRTTR.exe build: jitter controls unavailable");
      }
#if ENABLE_SR
      ImGui::SeparatorText("Super Resolution");
      ImGui::Checkbox("Flip Jitter X", &sr_flip_jitter_x);
      ImGui::SameLine();
      ImGui::Checkbox("Flip Jitter Y", &sr_flip_jitter_y);
      if (ImGui::Checkbox("MVs Jittered", &sr_mvs_jittered))
      {
         device_data.force_reset_sr = true;
      }
      ImGui::TextDisabled("Camera: jitter %.6f %.6f NDC, fov %.2f deg, near %.3f, far %.1f", last_built_camera.jitter_x, last_built_camera.jitter_y, (last_built_camera.projection_y_scale > 0.f ? (2.f * std::atan(1.f / last_built_camera.projection_y_scale) * 180.f / float(M_PI)) : 0.f), last_built_camera.near_plane, last_built_camera.far_plane);
#endif

      ImGui::SeparatorText("SMAA");
      ImGui::Checkbox("SMAA Predication", &g_smaa_predication);
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Finds edges by geometry (plane deviation of the scene depth) as well as by colour, so texture detail stays sharp while silhouettes are antialiased. Not saved.");
      }
      ImGui::Combo("SMAA Predication Debug View", &g_smaa_debug_view, "Off\0Edges\0Predication\0");
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Replaces the frame with SMAA's edges (red = horizontal, green = vertical) or the predication edge-ness (red).\nToggle SMAA Predication to compare: texture detail should lose edges, silhouettes keep them.");
      }

      ImGui::SeparatorText("XeGTAO");
      ImGui::BeginDisabled(!g_gtao_enable);
      ImGui::SliderFloat("GTAO Final Value Power", &g_gtao_final_value_power, 0.3f, 4.5f, "%.2f");
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Midtone darkness of the occlusion (1.4 = default, calibrated to the vanilla SSAO on depth normals: darker with the G-buffer ones; 2.2 = Intel default). Not saved.");
      }
      ImGui::SliderFloat("GTAO Radius Override", &g_gtao_radius_override, 0.f, 5.f, "%.3f");
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Radius in metres at 8 m view depth, scaled with depth (0 = the shader's EFFECT_RADIUS). Not saved.");
      }
      ImGui::Combo("GTAO Debug View", &g_gtao_debug_view, "Off\0Depth gradient\0Normals\0AO x8\0Edges\0");
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Written to the game's SSAO texture, so it shows through the ambient light. Not saved.");
      }
      ImGui::EndDisabled();

      ImGui::SeparatorText("Diagnostics");
      if (ImGui::Button("Log Camera Builds"))
      {
         camera_log_calls_left = 200;
      }
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Writes the next 200 camera projection builds (counter, TAA gates, jitter row) and counter increments to ReShade.log.");
      }
#if ENABLE_SR
      if (ImGui::Button("Log SR Timings"))
      {
         sr_timings_log_requested = true;
      }
      if (ImGui::IsItemHovered())
      {
         ImGui::SetTooltip("Writes the average, min and max time of every DLAA / FSR 3 step since the last log to ReShade.log, on the next SR frame.\nCPU = submission time on the render thread. GPU = execution time of the inputs conversion CS and the SR draw.");
      }
#endif
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Saints Row: The Third Remastered\" is developed by DristoforColumb and is open source and free.\n"
         "It adds HDR, replaces the game's TAA with DLAA or FSR 3 native anti-aliasing, its FXAA with SMAA and its SSAO with XeGTAO, plus 16x anisotropic filtering and a fix for the game's TAA jitter.\n"
         "Set Anti-Aliasing in the game's display settings to TAA for DLAA and FSR 3, or to FXAA for SMAA, and enable Ambient Occlusion for XeGTAO.\n"
         "Do NOT run another HDR mod (e.g. RenoDX) alongside it.\n"
         "Thanks to the Luma team and contributors.\n"
         "If you enjoy it, consider donating.");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link = std::string("Buy DristoforColumb a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link.c_str()))
      {
         ShellExecuteA(nullptr, "open", "https://ko-fi.com/dristoforcolumb", nullptr, nullptr, SW_SHOWNORMAL);
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
         static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
         ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);
      }

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nDristoforColumb"
                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nSMAA (Iryoku)"
                  "\nXeGTAO (Intel)"
                  "\nAMD FidelityFX (RCAS + FSR 3)"
                  "\nNVIDIA NGX (DLSS)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Saints Row: The Third Remastered Luma mod", "", 1);

      // Scene, UI and video are composed into the swapchain by the last draw of the frame
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      // The scene, TAA and tonemap outputs are r11g11b10_float at swapchain resolution: fp16 removes banding in HDR highlights.
      // The GUI layer (with Bink video) and the TAA history stay RGBA8; the GUI layer's UNORM clamp is what bounds the Bink YCbCr conversion.
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {reshade::api::format::r11g11b10_float};
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution;

      // Anisotropic samplers get 16x AF and the SR mip bias offset (added to the game's bias)
      enable_samplers_upgrade = true;
      samplers_upgrade_mode = 4;

      for (const uint32_t hash : compose_hashes)
      {
         redirected_shader_hashes["Compose"].insert(std::format("{:08X}", hash));
      }
      for (const uint32_t hash : compose_gui_hashes)
      {
         redirected_shader_hashes["ComposeGUI"].insert(std::format("{:08X}", hash));
      }

      game = new SaintsRowTheThirdRemastered();
   }
   // The camera hook detour lives in this module: remove it before it unloads. On an unload (not the process exit) the game's jitter goes back
   // to vanilla too, as a later load starts from the 2x pattern (the Halton code stays, it's only reached in mode 3).
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      if (lpReserved == nullptr && game_patches_applied)
      {
         if (int32_t* jitter_mode = GetGameAddresses().jitter_mode)
         {
            *jitter_mode = vanilla_jitter_mode;
         }
         SetJitterIndexFix(false);
      }
      if (minhook_initialized)
      {
         MH_Uninitialize();
      }
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
