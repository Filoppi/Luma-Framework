#define GAME_THUMPER 1

// TODO: delete after testing (it doesn't seem to be needed!)
#define ENABLE_SHADER_CLASS_INSTANCES 1

#define ENABLE_POST_DRAW_CALLBACK 1

#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

namespace
{
   ShaderHashesList pixel_shader_hashes_SubtractiveBackground;
   ShaderHashesList pixel_shader_hashes_DownscaleAndDarken;
}

struct GameDeviceDataThumper final : public GameDeviceData
{
   CustomPixelShaderPassData correct_subtractive_blends_data;
   uint correct_subtractive_blends_frame_count = 0; // Debug only
};

class Thumper final : public Game
{
public:
   static GameDeviceDataThumper& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataThumper*>(device_data.game);
   }
   static const GameDeviceDataThumper& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataThumper*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      // This game uses cbuffers in a weird way, so all 14 slots are used, and almost all of them are used across post processing,
      // chances are the game doesn't set them back after the beginning of the frame because they are all fixed (it seems like it does re-set them sometimes!).
      luma_settings_cbuffer_index = 2; // "TextConstants", only used in UI, which we don't replace
      luma_data_cbuffer_index = 7; // "Down4Constants", only used in the "Downscale and Darken" bloom preparation pass, we swap it live to support outs

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_VIGNETTE", '1', true, false, "Allows disabling the game's vignette effect", 1},
         {"DISABLE_DISTORTION_TYPE", '0', true, false, "The game applies a strong distortion filter, disable it if you like\n0 - Enabled\n1 - Disabled\n2 - Disabled + Stretched", 2},
         {"HDR_LOOK_TYPE", '1', true, false, "Makes the look more HDR, but less accurate to the source\n0-2, from most to least vanilla like", 2},
         {"VANILLA_LOOK_TYPE", '0', true, false, "If you prefer to have a look closer to the vanilla one, enable this (the HDR won't be as impactful)\n0-3, from least to most vanilla like", 3},
         {"BLACK_AND_WHITE", '0', true, false, "", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      // No gamma mismatch baked in the textures as the game never applied gamma, it was gamma from the beginning to the end.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      // Leave "UI_DRAW_TYPE" to default, there's barely any UI in this game it's not worth adding any special code or setting for it.
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataThumper;
   }

   // Add a saturate on materials and UI shaders
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader)
         return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      // All opaque materials have a sampler by this name
      const char str_to_find_1[] = "MaxOutputConstants"; // All materials seemengly have "MaxOutputConstants" and "gMaxOutputColor" in their code (though sometimes the second is within "VignetteConstants"), some UI shaders too
      const char str_to_find_2[] = "TextConstants"; // Some UI shaders have this
      const std::vector<std::byte> pattern_safety_check_1(reinterpret_cast<const std::byte*>(str_to_find_1), reinterpret_cast<const std::byte*>(str_to_find_1) + strlen(str_to_find_1));
      const std::vector<std::byte> pattern_safety_check_2(reinterpret_cast<const std::byte*>(str_to_find_2), reinterpret_cast<const std::byte*>(str_to_find_2) + strlen(str_to_find_2));
      bool pattern_found = !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check_1).empty() || !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check_2).empty();

      if (pattern_found)
      {
         std::vector<uint8_t> appended_patch;
         std::vector<const std::byte*> appended_patches_addresses;

         constexpr bool enable_unorm_emulation = true;
         if (enable_unorm_emulation)
         {
            std::vector<uint32_t> mov_sat_o0w_o0w = ShaderPatching::GetMovInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0, true);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()) + mov_sat_o0w_o0w.size() * sizeof(uint32_t));
            std::vector<uint32_t> max_o0xyz_o0xyz_0 = ShaderPatching::GetMaxInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()) + max_o0xyz_o0xyz_0.size() * sizeof(uint32_t));
         }

         size_t size_u32 = size / sizeof(uint32_t);
         const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
         size_t i = 0;
         size_t nested_branches = 0;
         while (i < size_u32)
         {
            uint32_t opcode_token = code_u32[i];
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);
            size_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i+1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself
            
            if (opcode_type == D3D10_SB_OPCODE_IF
               || opcode_type == D3D10_SB_OPCODE_LOOP
               || opcode_type == D3D10_SB_OPCODE_SWITCH)
            {
               nested_branches++;
            }
            else if (opcode_type == D3D10_SB_OPCODE_ENDIF
               || opcode_type == D3D10_SB_OPCODE_ENDLOOP
               || opcode_type == D3D10_SB_OPCODE_ENDSWITCH)
            {
               nested_branches--;
            }

            if (opcode_type == D3D10_SB_OPCODE_RET)
            {
               ASSERT_ONCE(instruction_size == 1);

               // Add the patch before every single return value!
               // Shift it by how much the data would have been shifted by prior patches we already added.
               size_t i_add = appended_patches_addresses.size() * appended_patch.size() / sizeof(uint32_t); // Patches should always be a multipler of DWORD
               appended_patches_addresses.emplace_back(reinterpret_cast<const std::byte*>(&code_u32[i + i_add]));

               // No need to continue, this is the last return (it's final given that it's not in a branch), Thumper has seemengly garbage data from the xbox compiler after this
               if (nested_branches == 0)
                  break;
            }

            i += instruction_size;
            if (instruction_size == 0)
               break;
         }

         // float3(0.3, 0.59, 0.11)
         const std::vector<std::byte> pattern_bt_601_luminance = {
            std::byte{0x02}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
            std::byte{0x9A}, std::byte{0x99}, std::byte{0x99}, std::byte{0x3E},
            std::byte{0x3D}, std::byte{0x0A}, std::byte{0x17}, std::byte{0x3F},
            std::byte{0xAE}, std::byte{0x47}, std::byte{0xE1}, std::byte{0x3D},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
         const std::vector<std::byte> pattern_bt_709_luminance = {
            std::byte{0x02}, std::byte{0x40}, std::byte{0x00}, std::byte{0x00},
            std::byte{0xD0}, std::byte{0xB3}, std::byte{0x59}, std::byte{0x3E},
            std::byte{0x59}, std::byte{0x17}, std::byte{0x37}, std::byte{0x3F},
            std::byte{0x98}, std::byte{0xDD}, std::byte{0x93}, std::byte{0x3D},
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
         std::vector<std::byte*> matches_bt_601_luminance = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(new_code.get()), size, pattern_bt_601_luminance);

         // Allocate new buffer and copy original shader code, then append the new code to fix UNORM to FLOAT texture upgrades
         // This emulates UNORM render target behaviour on FLOAT render targets (from texture upgrades), without limiting the rgb color range.
         // o0.rgb = max(o0.rgb, 0); // max is 0x34
         // o0.w = saturate(o0.w); // mov is 0x36
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

         std::memcpy(new_code.get(), code, size);

         // Fix usual wrong luminance calculations
         for (std::byte* match : matches_bt_601_luminance)
         {
            // Calculate offset of each match relative to original code
            size_t offset = match - code;
            std::memcpy(new_code.get() + offset, pattern_bt_709_luminance.data(), pattern_bt_709_luminance.size());
         }

         // Append patch at the end (should usually not happen)
         if (appended_patches_addresses.empty())
         {
            ASSERT_ONCE(false);

            std::memcpy(new_code.get() + size, appended_patch.data(), appended_patch.size());

            size += appended_patch.size();
         }
         else
         {
            size_t valid_size = size;

            std::unique_ptr<std::byte[]> scratch_buffer = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

            for (const auto appended_patches_address : appended_patches_addresses)
            {
               size_t insert_pos = appended_patches_address - code; // These are already shifted to account for the previously inserted patches

               // Copy from the address we'll insert the patch at, until the end, into a temporary buffer
               std::memcpy(scratch_buffer.get(), new_code.get() + insert_pos, valid_size - insert_pos);
               // Insert the patch
               std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
               // Fill back the previous data, shifted
               std::memcpy(new_code.get() + insert_pos + appended_patch.size(), scratch_buffer.get(), valid_size - insert_pos);

               valid_size += appended_patch.size();
            }

            size = valid_size;
         }
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      // Replace
      if (is_custom_pass && original_shader_hashes.Contains(pixel_shader_hashes_DownscaleAndDarken) && luma_data_cbuffer_index == 7)
      {
#if 1
         // Just say we already upgraded the cbuffers so they won't be set again by the parent function, we don't need them in this shader, we just need to clamp its result.
         updated_cbuffers = true;
#else // Previous design
         com_ptr<ID3D11Buffer> constant_buffers;
         native_device_context->PSGetConstantBuffers(luma_data_cbuffer_index, 1, &constant_buffers);
         native_device_context->PSSetConstantBuffers(4, 1, &constant_buffers); // Hardcoded in shader
#endif
         return DrawOrDispatchOverrideType::None;
      }

      if ((stages & reshade::api::shader_stage::pixel) == reshade::api::shader_stage::pixel)
      {
         com_ptr<ID3D11BlendState> blend_state;
         FLOAT blend_factor[4] = {1.f, 1.f, 1.f, 1.f};
         UINT blend_sample_mask;
         native_device_context->OMGetBlendState(&blend_state, blend_factor, &blend_sample_mask);
         if (blend_state)
         {
            D3D11_BLEND_DESC blend_desc;
            blend_state->GetDesc(&blend_desc);
            if (IsBlendInverted(blend_desc, 1))
            {
               ASSERT_ONCE(original_shader_hashes.Contains(pixel_shader_hashes_SubtractiveBackground)); // Make sure we didn't miss any, otherwise we should probably double check the results!

               // This game needs post draw callbacks to fix up subtractive blends with FLOAT render targets (ENABLE_POST_DRAW_CALLBACK)
               // Sure, using R11G11B10_FLOAT (unsigned) would have been easier to fix this, but the quality wouldn't be the same
               if (original_draw_dispatch_func && *original_draw_dispatch_func)
               {
                  (*original_draw_dispatch_func)();
               }

               com_ptr<ID3D11RenderTargetView> rtv;
               com_ptr<ID3D11DepthStencilView> dsv;
               native_device_context->OMGetRenderTargets(1, &rtv, &dsv);

               if (rtv.get() && test_index != 14)
               {
                  DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
                  draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

#if DEVELOPMENT || TEST // Make sure unexpected shaders types around bound (unlikely, given this also had a DX9 mode)
                  com_ptr<ID3D11HullShader> hs;
                  com_ptr<ID3D11DomainShader> ds;
                  com_ptr<ID3D11GeometryShader> gs;
                  native_device_context->HSGetShader(&hs, nullptr, nullptr);
                  native_device_context->DSGetShader(&ds, nullptr, nullptr);
                  native_device_context->GSGetShader(&gs, nullptr, nullptr);
                  ASSERT_ONCE(hs == nullptr && ds == nullptr && gs == nullptr);
#endif

                  // If we have more than one per frame, we should make a map of "correct_subtractive_blends_data" by source resource, and then clear it at the end of the frame if it wasn't used. Update: it's fine, there's two but they are both drawing to the same RT.
                  if (game_device_data.correct_subtractive_blends_frame_count != 0)
                  {
                     ASSERT_ONCE(rtv.get() == game_device_data.correct_subtractive_blends_data.original_rv);
                  }

                  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
                  rtv->GetDesc(&rtv_desc);
                  const bool ms = rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS;
                  ASSERT_ONCE(rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS || rtv_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D);

                  // Clip all negative values, like vanilla (I tried to clamp to the closest valid luminance instead, but it created weird colors)
                  DrawCustomPixelShaderPass(native_device, native_device_context, rtv.get(), device_data, ms ? Math::CompileTimeStringHash("Copy RGB Max 0 A Sat MS") : Math::CompileTimeStringHash("Copy RGB Max 0 A Sat"), game_device_data.correct_subtractive_blends_data);
                  game_device_data.correct_subtractive_blends_frame_count++;

                  draw_state_stack.Restore(native_device_context);

#if DEVELOPMENT
                  const std::shared_lock lock_trace(s_mutex_trace);
                  if (trace_running)
                  {
                     const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                     TraceDrawCallData trace_draw_call_data;
                     trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                     trace_draw_call_data.command_list = native_device_context;
                     trace_draw_call_data.custom_name = "Sanitize Subtractive Blends";
                     // Re-use the RTV data for simplicity
                     GetResourceInfo(rtv.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                     cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
                  }
#endif
               }

               return DrawOrDispatchOverrideType::Replaced;
            }
         }
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.correct_subtractive_blends_frame_count = 0;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Thumper Luma mod - about and credits section", ""); // TODO
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Thumper Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm, // Likely all that is needed
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
      };
      // Upgrade almost all RTs. This game does weird resizes resources before resizing/creating the swapchain, so it might break if you change the aspect ratio after boot (until you toggle MSAA to recreate some textures)!
      // The game also has a bug where in windowed mode, if the resolution matches the screen (or maybe even if not?), the swapchain is resized to a slightly higher resolution than the screen.
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolutionWidth;
      enable_indirect_texture_format_upgrades = true;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      // Prevents the game's fullscreen exclusive mode from being clunky, though it still is, and windowed in this game doesn't respect the target size, actually going above it
      force_borderless = true;

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("8081E6DD", nullptr, 16), "Clear to Black"); // Actually a tonemapper permutation (we can see that from the vertex shader output signature)

      forced_shader_names.emplace(std::stoul("29E880BE", nullptr, 16), "Bloom Blur");
      forced_shader_names.emplace(std::stoul("3D4F0EF0", nullptr, 16), "Bloom Blur");
      forced_shader_names.emplace(std::stoul("4E52C1E2", nullptr, 16), "Bloom Blur");
      forced_shader_names.emplace(std::stoul("9752C56E", nullptr, 16), "Bloom Blur");
      forced_shader_names.emplace(std::stoul("97581C03", nullptr, 16), "Bloom Blur");
      forced_shader_names.emplace(std::stoul("D35A01FD", nullptr, 16), "Bloom Blur");
#endif

      redirected_shader_hashes["Tonemap"] =
         {
            "0540C116",
            "05C8E302",
            "05E76366",
            "0894AEAC",
            "0A94FD50",
            "0AF7FC85",
            "0BA7CC5A",
            "0CF4E505",
            "0D88809E",
            "16F2F42F",
            "16F3B389",
            "1D69DFA7",
            "20AFCFD9",
            "2AA224D0",
            "2C11E81A",
            "33AF2ED3",
            "33AFEAA6",
            "3C2C0250",
            "4A0C32EA",
            "4A3022D9",
            "4CD8318D",
            "56CC1C12",
            "5C3E5427",
            "68FD089B",
            "6908F97F",
            "6EB6C27F",
            "74345370",
            "7798668A",
            "88950D2A",
            "8896B9E8",
            "8EF8C2FA",
            "92927AB3",
            "9301C9A5",
            "A12D8802",
            "A350A559",
            "AF5ADD5F",
            "AFBA65C3",
            "B0D713F8",
            "B17EFCA0",
            "B216143E",
            "B768EEE6",
            "BE256B16",
            "C991064E",
            "CADBBFBB",
            "CDDA9276",
            "CE766AA8",
            "DB91752E",
            "E054B1AB",
            "E5E90689",
            "E7D36C7A",
            "EAF1265E",
            "F487F974",
            "F591012D",
            "F735A18A",
            "F9C8DD5A",
            "8081E6DD",
         };

      pixel_shader_hashes_SubtractiveBackground.pixel_shaders = {Shader::Hash_StrToNum("0E76F7A1")}; // All the known shaders that use subtractive blending
      pixel_shader_hashes_DownscaleAndDarken.pixel_shaders = {Shader::Hash_StrToNum("AA2BFE7F")};
      
      game = new Thumper();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}