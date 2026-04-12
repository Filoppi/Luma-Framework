#define GAME_LEGO_CITY_UNDERCOVER 1

#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

// TODO: delete?
struct GameDeviceDataLegoCityUndercover final : public GameDeviceData
{
};

class LegoCityUndercover final : public Game
{
   static GameDeviceDataLegoCityUndercover& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataLegoCityUndercover*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0'); // Rendering was linear, post processing in gamma space
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // The game actually never directly applied gamma, it bundled it in a filmic tonemapper formula
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('0');

      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataLegoCityUndercover;
   }

   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader)
         return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      // Fixes UI generating nans, they have one of these two names in them
      const char str_to_find_1[] = "g_MaterialPS_CB"; // All materials seems to have this. UI shaders are mostly "normal" materials given that they are rendered both in the world and in the UI
      const char str_to_find_2[] = "g_DX11AlphaTestPS_CB"; // Some UI shaders have this only
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
         uint8_t found_dcl_outputs = 0;
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

            if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
            {
               // This isn't a UI shader, they only have 1 render target (final color), while other materials have 3 (gbuffers)
               if (found_dcl_outputs > 0)
               {
                  return nullptr;
               }
               found_dcl_outputs++;
            }

            i += instruction_size;
            if (instruction_size == 0)
               break;

#if 0 // New version of the code
            if (opcode_type == D3D10_SB_OPCODE_RET)
            {
               ASSERT_ONCE(instruction_size == 1);

               ASSERT_ONCE(appended_patches_addresses.empty()); // To make sure this is handled properly

               // Add the patch before every single return value!
               // Shift it by how much the data would have been shifted by prior patches we already added.
               size_t i_add = appended_patches_addresses.size() * appended_patch.size() / sizeof(uint32_t); // Patches should always be a multipler of DWORD
               appended_patches_addresses.emplace_back(reinterpret_cast<const std::byte*>(&code_u32[i + i_add]));

               // No need to continue, this is the last return (it's final given that it's not in a branch), Thumper has seemengly garbage data from the xbox compiler after this
               if (nested_branches == 0)
                  break;
            }
#endif
         }

#if 1 // Old version of the code // TODO: no idea why this is required over the new code, the new one crashes or hangs on boot? There's probably some logic bug somewhere
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size());

         // Pattern to search for: 3E 00 00 01 (the last byte is the size, and the minimum is 1 (the unit is 4 bytes), given it also counts for the opcode and it's own size byte
         const std::vector<std::byte> return_pattern = {std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};

         // Append before the ret instruction if there's one at the end (there might not be?)
         // Our patch shouldn't pre-include a ret value (though it'd probably work anyway)!
         // Note that we could also just remove the return instruction and the shader would compile fine anyway? Unless the shader had any branches (if we added one, we should force add return!)
         if (!appended_patch.empty() && code[size - return_pattern.size()] == return_pattern[0] && code[size - return_pattern.size() + 1] == return_pattern[1] && code[size - return_pattern.size() + 2] == return_pattern[2] && code[size - return_pattern.size() + 3] == return_pattern[3])
         {
            size_t insert_pos = size - return_pattern.size();
            // Copy everything before pattern
            std::memcpy(new_code.get(), code, insert_pos);
            // Insert the patch
            std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
            // Copy the rest (including the return instruction)
            std::memcpy(new_code.get() + insert_pos + appended_patch.size(), code + insert_pos, size - insert_pos);
         }

         size += appended_patch.size();
#else
         // Allocate new buffer and copy original shader code, then append the new code to fix UNORM to FLOAT texture upgrades
         // This emulates UNORM render target behaviour on FLOAT render targets (from texture upgrades), without limiting the rgb color range.
         // o0.rgb = max(o0.rgb, 0); // max is 0x34
         // o0.w = saturate(o0.w); // mov is 0x36
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

         // Append patch at the end (should usually not happen)
         if (appended_patches_addresses.empty())
         {
            if (false) // Avoid changing the size in this game, it causes crashes? // TODO: it shouldn't though
            {
               std::memcpy(new_code.get() + size, appended_patch.data(), appended_patch.size());
               size += appended_patch.size();
            }
         }
         else
         {
            std::memcpy(new_code.get(), code, size);

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
#endif
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Lego City Undercover\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nPumbo"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "");
   }
};

// TODO: improve shadow quality in this game, they are very blocky (boosting up the shadow map texture resolution would work probably)
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Lego City Undercover Luma mod");
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
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

            reshade::api::format::r10g10b10a2_unorm,
            reshade::api::format::r10g10b10a2_typeless,

            reshade::api::format::r11g11b10_float,
      };
#if DEVELOPMENT // Seemingly not needed in this game but makes development easier
      enable_indirect_texture_format_upgrades = true;
#endif
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      redirected_shader_hashes["Tonemap"] =
         {
            "0E0294C0",
            "1A64CEA7",
            "6E2E91CE",
            "48F00D4F",
            "64A0A446",
            "94DFB540",
            "635EB656",
            "721BE532",
            "BA76349B",
            "BC560C2C",
            "C8EB534B",
            "CCCCD97F",
            "F72115F0",
         };

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("65D6A60E", nullptr, 16), "Clear");
      forced_shader_names.emplace(std::stoul("C76A5C7C", nullptr, 16), "DoF");
      forced_shader_names.emplace(std::stoul("41627D7A", nullptr, 16), "Downscale Type B"); // Uses Depth?
      forced_shader_names.emplace(std::stoul("37068C24", nullptr, 16), "Downscale");
      forced_shader_names.emplace(std::stoul("0B815FB3", nullptr, 16), "Blur Type B");
      forced_shader_names.emplace(std::stoul("BBCA06B9", nullptr, 16), "Blur");
      forced_shader_names.emplace(std::stoul("CEA28077", nullptr, 16), "Merge Bloom Mips");
      forced_shader_names.emplace(std::stoul("46988DD4", nullptr, 16), "Copy/Upscale");
      forced_shader_names.emplace(std::stoul("2ED6B88D", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("2862B6C1", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("7C024E6E", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("285FF21D", nullptr, 16), "FXAA");
      forced_shader_names.emplace(std::stoul("88CBAE06", nullptr, 16), "Edge AA");
      forced_shader_names.emplace(std::stoul("22F2EE3C", nullptr, 16), "Downscale ?");
      forced_shader_names.emplace(std::stoul("76DB7FD2", nullptr, 16), "Downscale ?");
#endif

      game = new LegoCityUndercover();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}