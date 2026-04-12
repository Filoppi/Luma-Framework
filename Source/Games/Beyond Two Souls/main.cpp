// ### Rename this ###
#define GAME_TEMPLATE 1

#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

class GameTemplate final : public Game
{
public:
   void OnInit(bool async) override
   {
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
   }

   // Add a saturate on opaque/transparent materials
   // Without this, some materials output alphas beyond 0-1, messing up the results if the render target is FLOAT (as opposed to the original UNORM)
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader) return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      // All shaders that write on the scene color buffer have these ops at the beginning
      // dcl_output o0.xyzw
      // dcl_output o1.xyzw
      // dcl_output o2.xyzw
      // dcl_output o3.xyzw
      uint8_t found_dcl_outputs = 0;

      size_t size_u32 = size / sizeof(uint32_t);
      const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
      size_t i = 0;
      bool found_first_dcl = false;
      while (i < size_u32)
      {
         uint32_t opcode_token = code_u32[i];
         D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);

         // Stop scanning the DCL opcodes, they are all declared at the beginning
         if (ShaderPatching::opcodes_dcl.contains(opcode_type))
            found_first_dcl = true;
         else if (found_first_dcl) // Extra safety in case there were instructions before DCL ones
            break;
         
         size_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i + 1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself

         if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
         {
            assert(instruction_size == 2);
            uint32_t operand_token = code_u32[i + 1];
            bool four_channels = true;
            uint32_t test_operand_token =
                ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Y : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Z : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_W : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
                ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
                ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
                ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(found_dcl_outputs, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
            if (operand_token == test_operand_token)
            {
               found_dcl_outputs++;
            }
         }

         i += instruction_size;
         if (instruction_size == 0)
            break;
      }

      bool pattern_found = found_dcl_outputs == 4;

      if (pattern_found)
      {
         std::vector<uint8_t> appended_patch;

         constexpr bool enable_unorm_emulation = true;
         if (enable_unorm_emulation)
         {
            std::vector<uint32_t> mov_sat_o0w_o0w = ShaderPatching::GetMovInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0, true);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()) + mov_sat_o0w_o0w.size() * sizeof(uint32_t));
            std::vector<uint32_t> max_o0xyz_o0xyz_0 = ShaderPatching::GetMaxInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()) + max_o0xyz_o0xyz_0.size() * sizeof(uint32_t));
         }

         // Allocate new buffer and copy original shader code, then append the new code to fix UNORM to FLOAT texture upgrades
         // This emulates UNORM render target behaviour on FLOAT render targets (from texture upgrades), without limiting the rgb color range.
         // o0.rgb = max(o0.rgb, 0); // max is 0x34
         // o0.w = saturate(o0.w); // mov is 0x36
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size());

         // Pattern to search for: 3E 00 00 01 (the last byte is the size, and the minimum is 1 (the unit is 4 bytes), given it also counts for the opcode and it's own size byte
         const std::vector<std::byte> return_pattern = { std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01} };

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
         // Append patch at the end
         else
         {
            std::memcpy(new_code.get(), code, size);
            std::memcpy(new_code.get() + size, appended_patch.data(), appended_patch.size());
         }

         size += appended_patch.size();
      }

      return new_code;
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Beyond: Two Souls\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Beyond: Two Souls - Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      //enable_indirect_texture_format_upgrades = true; // TODO: try without these or anyway only upgrade the tonemapper stuff?
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
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
      // Without aspect ratio patches, the game will be fixed at 16:9
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio;
      texture_format_upgrades_2d_custom_aspect_ratios.emplace(2.35f);
      prevent_fullscreen_state = true;

#if DEVELOPMENT
      forced_shader_names.emplace(std::stoul("CCFE68B2", nullptr, 16), "Swapchain Black Bars");
      forced_shader_names.emplace(std::stoul("D80718F8", nullptr, 16), "Swapchain Copy");
      forced_shader_names.emplace(std::stoul("A525E946", nullptr, 16), "UI");
#endif

      game = new GameTemplate();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}